/* The exported API (API.md): handles, settings, the speaking calls and raw synthesis.
 *
 * A job is one utterance, or one run of prose_render_frames. prose_speak queues its jobs for the handle's audio
 * thread, which plays them on the sound device; the blocking calls run theirs on the caller's thread. Either way the
 * job runs the engine 10 ms at a time and hands the audio and the events, in order, to a sink: the sound device, a
 * WAV file, the program's buffer, or nothing (prose_text_to_phoneme). Each event is delivered just before the audio
 * that holds it. The firmware reports a phoneme when it has been played, so while phoneme events are wanted the
 * audio after the last reported phoneme waits for the next report. */
#include "prose_int.h"

#include "prose_wave.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHUNK 100                       /* samples per engine call: one frame */
#define MAX_UTTERANCE (600u * 10000u)   /* 10 minutes */
#define MAX_TEXT 1000000                /* characters per call */
#define DEVICE_AHEAD 1500               /* samples queued on the sound device ahead of what it plays */

static int valid(prose_h h) { return h && h->magic == PROSE_MAGIC; }

/* ---- callbacks ---- */

static int in_own_callback(prose_h h)
{
	sys_lock(&h->lock);
	int r = h->cb_depth > 0 && sys_thread_equal(h->cb_thread, sys_thread_self());
	sys_unlock(&h->lock);
	return r;
}

static void cb_enter(prose_h h)
{
	sys_lock(&h->lock);
	h->cb_thread = sys_thread_self();
	h->cb_depth++;
	sys_unlock(&h->lock);
}

static void cb_leave(prose_h h)
{
	sys_lock(&h->lock);
	h->cb_depth--;
	sys_unlock(&h->lock);
}

static void callbacks(prose_h h, prose_callbacks *cb, void **user)
{
	sys_lock(&h->lock);
	*cb = h->cb;
	*user = h->user;
	sys_unlock(&h->lock);
}

static void fire_event(prose_h h, const prose_event *e)
{
	prose_callbacks cb;
	void *user;
	callbacks(h, &cb, &user);
	cb_enter(h);
	if (e->type == EV_INDEX && cb.on_index)
		cb.on_index(h, e->n, e->pos, user);
	else if (e->type == EV_PHONEME && cb.on_phoneme)
		cb.on_phoneme(h, e->ph, e->ms, e->pos, user);
	else if (e->type == EV_PARAMS && cb.on_params)
		cb.on_params(h, e->p, e->pos, user);
	cb_leave(h);
}

static void fire_done(prose_h h, int last_index, uint32_t total)
{
	prose_callbacks cb;
	void *user;
	callbacks(h, &cb, &user);
	if (!cb.on_done)
		return;
	cb_enter(h);
	cb.on_done(h, last_index, total, user);
	cb_leave(h);
}

/* ---- jobs ---- */

/* true when the job queued (or started) in generation `gen` must stop */
static int stopped(prose_h h, unsigned gen)
{
	sys_lock(&h->lock);
	int r = h->stop_gen != gen || h->closing;
	sys_unlock(&h->lock);
	return r;
}

/* waits while the handle is paused; returns 1 if the job must stop */
static int wait_paused(prose_h h, unsigned gen)
{
	sys_lock(&h->lock);
	while (h->paused && h->stop_gen == gen && !h->closing)
		sys_cond_wait(&h->cond, &h->lock);
	int r = h->stop_gen != gen || h->closing;
	sys_unlock(&h->lock);
	return r;
}

/* the settings and markers queued since the last text, then the text */
static char *take_pending(prose_h h, const char *text)
{
	size_t n = strlen(text);
	sys_lock(&h->lock);
	char *s = malloc(h->pending_len + n + 1);
	if (s) {
		if (h->pending_len)
			memcpy(s, h->pending, h->pending_len);
		memcpy(s + h->pending_len, text, n + 1);
		h->pending_len = 0;
	}
	sys_unlock(&h->lock);
	return s;
}

static void pend(prose_h h, const char *fmt, int v)
{
	char esc[16];
	int n = snprintf(esc, sizeof esc, fmt, v);
	sys_lock(&h->lock);
	if (h->pending_len + (size_t)n + 1 > h->pending_cap) {
		size_t cap = h->pending_cap ? h->pending_cap * 2 : 64;
		while (cap < h->pending_len + (size_t)n + 1)
			cap *= 2;
		char *p = realloc(h->pending, cap);
		if (!p) {
			sys_unlock(&h->lock);
			return;
		}
		h->pending = p;
		h->pending_cap = cap;
	}
	memcpy(h->pending + h->pending_len, esc, (size_t)n + 1);
	h->pending_len += (size_t)n;
	sys_unlock(&h->lock);
}

static void reset_handle_state(prose_h h); /* under h->lock */

/* a blocking call starts: 0, or BUSY / REENTRANT */
static int begin_blocking(prose_h h, unsigned *gen)
{
	if (in_own_callback(h))
		return PROSE_ERR_REENTRANT;
	sys_lock(&h->lock);
	if (h->job_active || h->jobs || h->closing) {
		sys_unlock(&h->lock);
		return PROSE_ERR_BUSY;
	}
	h->job_active = 1;
	h->blocking = 1;
	*gen = h->stop_gen;
	sys_unlock(&h->lock);
	return 0;
}

/* a job has ended (either kind); a prose_reset from one of its callbacks happens now */
static void end_job(prose_h h)
{
	sys_lock(&h->lock);
	int reset = h->reset_pending;
	h->reset_pending = 0;
	sys_unlock(&h->lock);
	if (reset)
		engine_reboot(h);
	sys_lock(&h->lock);
	h->job_active = 0;
	h->blocking = 0;
	sys_cond_broadcast(&h->cond);
	sys_unlock(&h->lock);
}

/* ---- sinks: where a job's audio and events go ---- */

typedef struct sink sink;
struct sink {
	int (*audio)(sink *s, prose_h h, const int16_t *pcm, size_t n, uint32_t pos); /* nonzero: stop */
	void (*event)(sink *s, prose_h h, const prose_event *e);
	unsigned gen;
	int error; /* a sink's own error: PROSE_ERR_FILE_IO, PROSE_ERR_AUDIO_WRITE */
};

static void event_now(sink *s, prose_h h, const prose_event *e)
{
	(void)s;
	fire_event(h, e);
}

/* audio waiting for its events (a phoneme's length is known when the next one starts) */
typedef struct {
	int16_t *buf;
	size_t len, cap;
	uint32_t done; /* position of buf[0]: everything before it has gone to the sink */
} staging;

static int stage_add(staging *st, const int16_t *pcm, size_t n)
{
	if (st->len + n > st->cap) {
		size_t cap = st->cap ? st->cap * 2 : 4096;
		while (cap < st->len + n)
			cap *= 2;
		int16_t *p = realloc(st->buf, cap * sizeof *p);
		if (!p)
			return PROSE_ERR_MEMORY;
		st->buf = p;
		st->cap = cap;
	}
	memcpy(st->buf + st->len, pcm, n * sizeof *pcm);
	st->len += n;
	return 0;
}

/* Hand audio and events to the sink in order: each event just before the audio from its position on. With phoneme
 * events, audio after the last reported phoneme waits, since the next report goes back to where its phoneme started,
 * unless this is the end. Returns 1 if the sink stopped. */
static int deliver(prose_h h, sink *sk, staging *st, int final)
{
	for (;;) {
		prose_event *e = h->ev_head < h->ev_len ? &h->ev[h->ev_head] : NULL;
		uint32_t limit = st->done + (uint32_t)st->len;
		if (e && e->pos < limit)
			limit = e->pos;
		if (h->want_phoneme && !final && h->ph_end < limit)
			limit = h->ph_end;
		if (limit > st->done) {
			size_t n = limit - st->done;
			int stop = sk->audio && sk->audio(sk, h, st->buf, n, st->done); /* it has the audio even so */
			memmove(st->buf, st->buf + n, (st->len - n) * sizeof *st->buf);
			st->len -= n;
			st->done = limit;
			if (stop)
				return 1;
		}
		if (!e)
			return 0;
		if (e->pos > st->done && !final)
			return 0;
		sk->event(sk, h, e);
		h->ev_head++;
		if (h->ev_head == h->ev_len) {
			h->ev_head = h->ev_len = 0;
		} else if (h->ev_head >= 256) {
			memmove(h->ev, h->ev + h->ev_head, (h->ev_len - h->ev_head) * sizeof *h->ev);
			h->ev_len -= h->ev_head;
			h->ev_head = 0;
		}
	}
}

/* One utterance through the engine into the sink. Returns 0 or an error; *total = samples delivered, *last = the
 * last index marker (from the firmware's ESC[n x). */
static int run_utterance(prose_h h, sink *sk, const char *text, int capture, uint32_t *total, int *last)
{
	prose_callbacks cb;
	void *user;
	callbacks(h, &cb, &user);
	int err = engine_start(h, text, !capture && cb.on_params, !capture && cb.on_phoneme, capture);
	*total = 0;
	*last = 0;
	if (err)
		return err;
	staging st = {NULL, 0, 0, 0};
	int16_t chunk[CHUNK];
	int stop = 0;
	for (;;) {
		if (wait_paused(h, sk->gen)) {
			stop = 1;
			break;
		}
		if ((err = engine_run(h, chunk, CHUNK)) != 0)
			break;
		if ((err = stage_add(&st, chunk, CHUNK)) != 0) {
			stop = 1;
			break;
		}
		if (deliver(h, sk, &st, 0)) {
			stop = 1;
			break;
		}
		if (engine_finished(h) || engine_position(h) >= MAX_UTTERANCE)
			break;
	}
	if (stop)
		engine_abort(h);
	else if (!err && deliver(h, sk, &st, 1))
		engine_abort(h);
	*total = st.done;
	*last = h->last_index;
	engine_end(h);
	free(st.buf);
	return err ? err : sk->error;
}

/* ---- prose_speak: the audio thread and the sound device ---- */

typedef struct {
	sink base;
	sys_audio *dev;
	uint32_t start;   /* the device's played count when the job's first sample was written */
	prose_event *ev;  /* events waiting for the device to reach them */
	size_t ev_len, ev_cap, ev_head;
	int paused;
} device_sink;

static void device_fire_due(device_sink *d, prose_h h)
{
	uint32_t played = sys_audio_played(d->dev) - d->start;
	while (d->ev_head < d->ev_len && d->ev[d->ev_head].pos <= played)
		fire_event(h, &d->ev[d->ev_head++]);
	if (d->ev_head == d->ev_len)
		d->ev_head = d->ev_len = 0;
}

static void device_event(sink *s, prose_h h, const prose_event *e)
{
	device_sink *d = (device_sink *)s;
	if (!d->dev) {
		fire_event(h, e);
		return;
	}
	if (d->ev_len == d->ev_cap) {
		size_t cap = d->ev_cap ? d->ev_cap * 2 : 64;
		prose_event *p = realloc(d->ev, cap * sizeof *p);
		if (!p) {
			fire_event(h, e);
			return;
		}
		d->ev = p;
		d->ev_cap = cap;
	}
	d->ev[d->ev_len++] = *e;
}

/* pause the device with the handle; returns 1 if the job must stop */
static int device_wait(device_sink *d, prose_h h)
{
	sys_lock(&h->lock);
	int paused = h->paused, stop = h->stop_gen != d->base.gen || h->closing;
	sys_unlock(&h->lock);
	if (stop)
		return 1;
	if (paused != d->paused) {
		sys_audio_pause(d->dev, paused);
		d->paused = paused;
	}
	if (paused) {
		sys_lock(&h->lock);
		if (h->paused && h->stop_gen == d->base.gen && !h->closing)
			sys_cond_timed_wait(&h->cond, &h->lock, 50);
		sys_unlock(&h->lock);
	}
	return 0;
}

static int device_audio(sink *s, prose_h h, const int16_t *pcm, size_t n, uint32_t pos)
{
	device_sink *d = (device_sink *)s;
	(void)pos;
	if (!d->dev)
		return 0;
	for (;;) {
		if (device_wait(d, h))
			return 1;
		device_fire_due(d, h);
		if (!d->paused && sys_audio_queued(d->dev) < DEVICE_AHEAD)
			break;
		if (!d->paused)
			sys_sleep_ms(5);
	}
	if (sys_audio_write(d->dev, pcm, n)) {
		s->error = PROSE_ERR_AUDIO_WRITE;
		return 1;
	}
	return 0;
}

/* after the job: wait for the device to play it all, firing the events on the way */
static void device_finish(device_sink *d, prose_h h, uint32_t total, int stop)
{
	if (!d->dev)
		return;
	if (!stop) {
		/* at most the audio still to play and 1 s, not counting pauses */
		uint32_t played = sys_audio_played(d->dev) - d->start;
		long budget = (played < total ? (long)(total - played) / 10 : 0) + 1000;
		while (budget > 0) {
			if (device_wait(d, h)) {
				stop = 1;
				break;
			}
			device_fire_due(d, h);
			if (sys_audio_played(d->dev) - d->start >= total && d->ev_head == d->ev_len)
				break;
			if (!d->paused) {
				sys_sleep_ms(10);
				budget -= 10;
			}
		}
		while (d->ev_head < d->ev_len) /* the device is behind: the rest now */
			fire_event(h, &d->ev[d->ev_head++]);
	}
	if (stop)
		sys_audio_drop(d->dev);
	d->ev_head = d->ev_len = 0;
}

static void audio_thread(void *arg)
{
	prose_h h = arg;
	device_sink d;
	memset(&d, 0, sizeof d);
	d.base.audio = device_audio;
	d.base.event = device_event;
	sys_lock(&h->lock);
	for (;;) {
		while ((!h->jobs || h->job_active) && !h->closing) { /* job_active here: prose_reset holds the handle */
			if (!h->jobs && h->dev) { /* nothing more to say: give the device back */
				sys_audio *dev = h->dev;
				h->dev = NULL;
				sys_unlock(&h->lock);
				sys_audio_close(dev);
				sys_lock(&h->lock);
				continue;
			}
			sys_cond_wait(&h->cond, &h->lock);
		}
		if (h->closing)
			break;
		job *j = h->jobs;
		h->jobs = j->next;
		if (!h->jobs)
			h->jobs_tail = &h->jobs;
		if (j->gen != h->stop_gen) {
			free(j->text);
			free(j);
			continue;
		}
		h->job_active = 1;
		h->blocking = 0;
		d.dev = h->dev;
		sys_unlock(&h->lock);

		uint32_t total;
		int last;
		d.base.gen = j->gen;
		d.base.error = 0;
		d.paused = 0;
		d.start = d.dev ? sys_audio_played(d.dev) : 0;
		int err = run_utterance(h, &d.base, j->text, 0, &total, &last);
		device_finish(&d, h, total, err != 0 || stopped(h, j->gen));
		if (d.paused)
			sys_audio_pause(d.dev, 0);
		fire_done(h, last, total);
		free(j->text);
		free(j);
		end_job(h);
		sys_lock(&h->lock);
		if (err == PROSE_ERR_AUDIO_WRITE && h->dev) { /* the device failed: open a new one for the next text */
			sys_audio *dev = h->dev;
			h->dev = NULL;
			sys_unlock(&h->lock);
			sys_audio_drop(dev);
			sys_audio_close(dev);
			sys_lock(&h->lock);
		}
	}
	sys_audio *dev = h->dev;
	h->dev = NULL;
	sys_unlock(&h->lock);
	if (dev) {
		sys_audio_drop(dev);
		sys_audio_close(dev);
	}
	free(d.ev);
}

/* ---- handles and information ---- */

static void reset_handle_state(prose_h h)
{
	h->pending_len = 0;
	h->paused = 0;
	h->use_custom = 0;
	h->custom_gen++;
	params_defaults(h->version, h->raw);
}

PROSE_API int PROSE_CALL prose_open(prose_h *out, int version)
{
	if (!out)
		return PROSE_ERR_ARG;
	*out = NULL;
	if (version != PROSE_V341 && version != PROSE_V11)
		return PROSE_ERR_ARG;
	sys_lock(sys_global_mutex());
	int err = engine_init();
	sys_unlock(sys_global_mutex());
	if (err)
		return err;
	prose_h h = calloc(1, sizeof *h);
	if (!h)
		return PROSE_ERR_MEMORY;
	h->magic = PROSE_MAGIC;
	h->version = version;
	h->nparams = version == PROSE_V11 ? 18 : 22;
	h->jobs_tail = &h->jobs;
	sys_mutex_init(&h->lock);
	sys_cond_init(&h->cond);
	params_defaults(version, h->raw);
	frame_builder_init(&h->raw_fb, engine_rom(), 0);
	if ((err = engine_open(h)) != 0) {
		engine_close(h);
		sys_cond_destroy(&h->cond);
		sys_mutex_destroy(&h->lock);
		free(h);
		return err;
	}
	*out = h;
	return 0;
}

PROSE_API void PROSE_CALL prose_close(prose_h h)
{
	if (!valid(h) || in_own_callback(h)) /* from its own callback it cannot wait for itself */
		return;
	sys_lock(&h->lock);
	h->closing = 1;
	h->stop_gen++;
	while (h->jobs) {
		job *j = h->jobs;
		h->jobs = j->next;
		free(j->text);
		free(j);
	}
	sys_cond_broadcast(&h->cond);
	sys_unlock(&h->lock);
	if (h->thread_started)
		sys_thread_join(h->thread);
	sys_lock(&h->lock);
	while (h->job_active) /* a blocking call on another thread */
		sys_cond_wait(&h->cond, &h->lock);
	sys_unlock(&h->lock);
	engine_close(h);
	prose_synth_destroy(h->raw_synth);
	free(h->pending);
	sys_cond_destroy(&h->cond);
	sys_mutex_destroy(&h->lock);
	h->magic = 0;
	free(h);
}

PROSE_API int PROSE_CALL prose_get_version(prose_h h) { return valid(h) ? h->version : PROSE_ERR_HANDLE; }

PROSE_API void PROSE_CALL prose_set_callbacks(prose_h h, const prose_callbacks *cb, void *user)
{
	if (!valid(h))
		return;
	sys_lock(&h->lock);
	if (cb)
		h->cb = *cb;
	else
		memset(&h->cb, 0, sizeof h->cb);
	h->user = user;
	sys_unlock(&h->lock);
}

PROSE_API const char *PROSE_CALL prose_error_string(int code)
{
	switch (code) {
	case PROSE_OK: return "success";
	case PROSE_ERR_HANDLE: return "invalid handle";
	case PROSE_ERR_ARG: return "invalid argument";
	case PROSE_ERR_BUSY: return "the handle is busy with another job";
	case PROSE_ERR_REENTRANT: return "blocking call from inside the handle's own callback";
	case PROSE_ERR_MEMORY: return "out of memory";
	case PROSE_ERR_TEXT_TOO_LONG: return "text too long";
	case PROSE_ERR_FILE_OPEN: return "cannot open or create the file";
	case PROSE_ERR_FILE_IO: return "file read or write failed";
	case PROSE_ERR_WAV_FORMAT: return "WAV file is not 16-bit mono PCM";
	case PROSE_ERR_WAV_RATE: return "WAV file is not 10000 Hz";
	case PROSE_ERR_WAV_LENGTH: return "WAV file is empty or longer than 1 second";
	case PROSE_ERR_WAV_SILENT: return "WAV file is silent";
	case PROSE_ERR_AUDIO_OPEN: return "cannot open the sound device";
	case PROSE_ERR_AUDIO_WRITE: return "the sound device failed";
	case PROSE_ERR_FIRMWARE: return "the firmware hit a fatal error and was restarted";
	}
	return "unknown error";
}

PROSE_API int PROSE_CALL prose_last_firmware_error(prose_h h)
{
	if (!valid(h))
		return PROSE_ERR_HANDLE;
	sys_lock(&h->lock);
	int e = h->fw_error;
	sys_unlock(&h->lock);
	return e;
}

/* ---- settings ---- */

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

PROSE_API void PROSE_CALL prose_set_voice(prose_h h, int n)
{
	if (valid(h) && h->version == PROSE_V341)
		pend(h, "\x1B[%dV", clamp(n, 0, 2));
}

PROSE_API void PROSE_CALL prose_set_rate(prose_h h, int wpm)
{
	if (valid(h))
		pend(h, "\x1B[%dr", clamp(wpm, 50, 250));
}

PROSE_API void PROSE_CALL prose_set_pitch(prose_h h, int n)
{
	if (valid(h))
		pend(h, "\x1B[%dp", n <= 0 ? 0 : clamp(n, 50, 200));
}

PROSE_API void PROSE_CALL prose_set_volume(prose_h h, int n)
{
	if (valid(h))
		pend(h, "\x1B[%da", 15 - clamp(n, 0, 15)); /* ESC[a: larger is quieter */
}

PROSE_API void PROSE_CALL prose_set_word_mode(prose_h h, int on)
{
	if (valid(h))
		pend(h, "\x1B[%dP", on ? 0 : 1); /* 0 = word mode, 1 = prosody */
}

PROSE_API void PROSE_CALL prose_set_fast_read(prose_h h, int n)
{
	if (valid(h) && h->version == PROSE_V341)
		pend(h, "\x1B[%df", clamp(n, 0, 9));
}

PROSE_API void PROSE_CALL prose_set_speak_punctuation(prose_h h, int on)
{
	if (valid(h) && h->version == PROSE_V341)
		pend(h, on ? "\x1B[%dN" : "\x1B[%dF", 2); /* mode flag 2 */
}

/* ---- speaking ---- */

static int check_text(const char *text)
{
	if (!text)
		return PROSE_ERR_ARG;
	return strlen(text) > MAX_TEXT ? PROSE_ERR_TEXT_TOO_LONG : 0;
}

PROSE_API int PROSE_CALL prose_speak(prose_h h, const char *text)
{
	if (!valid(h))
		return PROSE_ERR_HANDLE;
	int err = check_text(text);
	if (err)
		return err;
	sys_lock(&h->lock);
	int busy = (h->job_active && h->blocking) || h->closing, need_dev = !h->dev;
	sys_unlock(&h->lock);
	if (busy)
		return PROSE_ERR_BUSY;
	sys_audio *dev = NULL;
	if (need_dev && !(dev = sys_audio_open())) /* opened here, so that a missing device is reported here */
		return PROSE_ERR_AUDIO_OPEN;
	job *j = calloc(1, sizeof *j);
	if (!j) {
		sys_audio_close(dev);
		return PROSE_ERR_MEMORY;
	}
	sys_lock(&h->lock);
	if ((h->job_active && h->blocking) || h->closing)
		err = PROSE_ERR_BUSY;
	else if (!h->thread_started && sys_thread_start(&h->thread, audio_thread, h) != 0)
		err = PROSE_ERR_MEMORY;
	else
		h->thread_started = 1;
	sys_unlock(&h->lock);
	if (!err && !(j->text = take_pending(h, text)))
		err = PROSE_ERR_MEMORY;
	if (err) {
		free(j);
		sys_audio_close(dev);
		return err;
	}
	/* the device and the job go in together: the audio thread gives the device back only when it has no jobs */
	sys_lock(&h->lock);
	while (!h->dev && !dev) { /* the thread gave it back meanwhile */
		sys_unlock(&h->lock);
		if (!(dev = sys_audio_open())) {
			free(j->text);
			free(j);
			return PROSE_ERR_AUDIO_OPEN;
		}
		sys_lock(&h->lock);
	}
	if (!h->dev) {
		h->dev = dev;
		dev = NULL;
	}
	j->gen = h->stop_gen;
	*h->jobs_tail = j;
	h->jobs_tail = &j->next;
	sys_cond_broadcast(&h->cond);
	sys_unlock(&h->lock);
	sys_audio_close(dev); /* one opened meanwhile by another call */
	return 0;
}

/* a blocking utterance into a sink, after begin_blocking; the caller fires on_done and ends the job */
static int speak_blocking(prose_h h, sink *sk, unsigned gen, const char *text, int capture, uint32_t *total,
                          int *last)
{
	char *t = take_pending(h, text);
	*total = 0;
	*last = 0;
	if (!t)
		return PROSE_ERR_MEMORY;
	sk->gen = gen;
	int err = run_utterance(h, sk, t, capture, total, last);
	free(t);
	return err;
}

typedef struct {
	sink base;
	FILE *f;
} wave_sink;

static int wave_audio(sink *s, prose_h h, const int16_t *pcm, size_t n, uint32_t pos)
{
	wave_sink *w = (wave_sink *)s;
	(void)h;
	(void)pos;
	if (wav_put_samples(w->f, pcm, n)) {
		s->error = PROSE_ERR_FILE_IO;
		return 1;
	}
	return 0;
}

PROSE_API int PROSE_CALL prose_speak_to_wave(prose_h h, const char *filename, const char *text)
{
	if (!valid(h))
		return PROSE_ERR_HANDLE;
	int err = check_text(text);
	if (err || !filename)
		return err ? err : PROSE_ERR_ARG;
	unsigned gen;
	if ((err = begin_blocking(h, &gen)) != 0)
		return err;
	wave_sink w = {{wave_audio, event_now, 0, 0}, fopen(filename, "wb")};
	if (!w.f) {
		end_job(h);
		return PROSE_ERR_FILE_OPEN;
	}
	uint8_t hdr[44];
	uint32_t total = 0;
	int last = 0;
	wav_header(hdr, 0);
	if (fwrite(hdr, 1, 44, w.f) != 44)
		err = PROSE_ERR_FILE_IO;
	else
		err = speak_blocking(h, &w.base, gen, text, 0, &total, &last);
	wav_header(hdr, total); /* the header with the length */
	if ((fseek(w.f, 0, SEEK_SET) != 0 || fwrite(hdr, 1, 44, w.f) != 44) && !err)
		err = PROSE_ERR_FILE_IO;
	if (fclose(w.f) != 0 && !err)
		err = PROSE_ERR_FILE_IO;
	fire_done(h, last, total);
	end_job(h);
	return err;
}

typedef struct {
	sink base;
	int16_t *buf, own[CHUNK];
	size_t size, fill;
	uint32_t pos; /* position of buf[0] */
	prose_audio_cb cb;
	void *user;
	int stopped; /* the callback returned nonzero */
} buffer_sink;

static int buffer_flush(buffer_sink *b, prose_h h)
{
	cb_enter(h);
	int r = b->cb(h, b->buf, b->fill, b->pos, b->user);
	cb_leave(h);
	b->pos += (uint32_t)b->fill;
	b->fill = 0;
	if (r)
		b->stopped = 1;
	return r;
}

static int buffer_audio(sink *s, prose_h h, const int16_t *pcm, size_t n, uint32_t pos)
{
	buffer_sink *b = (buffer_sink *)s;
	(void)pos;
	while (n) {
		size_t k = b->size - b->fill < n ? b->size - b->fill : n;
		memcpy(b->buf + b->fill, pcm, k * sizeof *pcm);
		b->fill += k;
		pcm += k;
		n -= k;
		if (b->fill == b->size && buffer_flush(b, h))
			return 1;
	}
	return 0;
}

PROSE_API int32_t PROSE_CALL prose_speak_to_buffer(prose_h h, const char *text, int16_t *buf, size_t buf_samples,
                                                   prose_audio_cb on_audio, void *user)
{
	if (!valid(h))
		return PROSE_ERR_HANDLE;
	int err = check_text(text);
	if (err || !on_audio)
		return err ? err : PROSE_ERR_ARG;
	buffer_sink b;
	memset(&b, 0, sizeof b);
	b.base.audio = buffer_audio;
	b.base.event = event_now;
	b.buf = buf && buf_samples ? buf : b.own;
	b.size = buf && buf_samples ? buf_samples : CHUNK;
	b.cb = on_audio;
	b.user = user;
	unsigned gen;
	uint32_t total = 0;
	int last = 0;
	if ((err = begin_blocking(h, &gen)) != 0)
		return err;
	err = speak_blocking(h, &b.base, gen, text, 0, &total, &last);
	if (b.fill && !b.stopped)
		buffer_flush(&b, h); /* the rest, in a partly filled buffer */
	fire_done(h, last, total);
	end_job(h);
	return err ? err : (int32_t)total;
}

static void no_event(sink *s, prose_h h, const prose_event *e)
{
	(void)s;
	(void)h;
	(void)e;
}

PROSE_API int PROSE_CALL prose_text_to_phoneme(prose_h h, const char *text, char *out, size_t out_size)
{
	if (!valid(h))
		return PROSE_ERR_HANDLE;
	int err = check_text(text);
	if (err || (!out && out_size))
		return err ? err : PROSE_ERR_ARG;
	sink s = {NULL, no_event, 0, 0};
	unsigned gen;
	uint32_t total;
	int last;
	if ((err = begin_blocking(h, &gen)) != 0)
		return err;
	err = speak_blocking(h, &s, gen, text, 1, &total, &last);
	/* the echo, with runs of spaces (pauses) as one space and none at the ends, and without the ] of the end flush */
	size_t n = 0, len = h->cap ? h->cap_len : 0;
	const char *c = h->cap ? h->cap : "";
	while (len && (c[len - 1] == ' ' || c[len - 1] == ']'))
		len--;
	for (size_t i = 0; !err && i < len; i++, c++) {
		if (*c == ' ' && (n == 0 || c[1] == ' '))
			continue;
		if (out && n + 1 < out_size)
			out[n] = *c;
		n++;
	}
	if (out_size)
		out[n < out_size ? n : out_size - 1] = 0;
	end_job(h);
	if (err)
		return err;
	return n > 0x7FFFFFFF ? PROSE_ERR_TEXT_TOO_LONG : (int)n;
}

PROSE_API void PROSE_CALL prose_index(prose_h h, int n)
{
	if (valid(h))
		pend(h, "\x1B[%di", clamp(n, 1, 255));
}

PROSE_API void PROSE_CALL prose_stop(prose_h h)
{
	if (!valid(h))
		return;
	sys_lock(&h->lock);
	h->stop_gen++;
	h->paused = 0;
	while (h->jobs) {
		job *j = h->jobs;
		h->jobs = j->next;
		free(j->text);
		free(j);
	}
	h->jobs_tail = &h->jobs;
	sys_cond_broadcast(&h->cond);
	sys_unlock(&h->lock);
}

PROSE_API void PROSE_CALL prose_pause(prose_h h)
{
	if (!valid(h))
		return;
	sys_lock(&h->lock);
	if (h->job_active || h->jobs)
		h->paused = 1;
	sys_unlock(&h->lock);
}

PROSE_API void PROSE_CALL prose_resume(prose_h h)
{
	if (!valid(h))
		return;
	sys_lock(&h->lock);
	h->paused = 0;
	sys_cond_broadcast(&h->cond);
	sys_unlock(&h->lock);
}

PROSE_API void PROSE_CALL prose_reset(prose_h h)
{
	if (!valid(h))
		return;
	prose_stop(h);
	sys_lock(&h->lock);
	reset_handle_state(h);
	if (h->cb_depth > 0 && sys_thread_equal(h->cb_thread, sys_thread_self())) {
		h->reset_pending = 1; /* from a callback: when its job ends */
		sys_unlock(&h->lock);
		return;
	}
	while (h->job_active)
		sys_cond_wait(&h->cond, &h->lock);
	h->job_active = 1; /* keep jobs out while the firmware restarts */
	h->blocking = 1;
	sys_unlock(&h->lock);
	engine_reboot(h);
	sys_lock(&h->lock);
	h->job_active = 0;
	h->blocking = 0;
	sys_cond_broadcast(&h->cond);
	sys_unlock(&h->lock);
}

/* ---- raw synthesis ---- */

PROSE_API void PROSE_CALL prose_set_frame_param(prose_h h, int p, int value)
{
	if (!valid(h) || p < 0 || p >= h->nparams)
		return;
	int v = clamp(value, 0, params_max(h->version, p));
	sys_lock(&h->lock);
	h->raw[p] = (uint8_t)v;
	sys_unlock(&h->lock);
}

/* the raw DSP model's host port: the boot block, then the frame built for this request */
static int raw_poll(void *user, size_t produced, uint16_t *word)
{
	prose_h h = user;
	if (h->raw_q_pos == h->raw_q_len) {
		h->raw_q_len = h->raw_q_pos = 0;
		if (!h->raw_booted) {
			memcpy(h->raw_q, engine_boot_block(), 9 * sizeof *h->raw_q);
			h->raw_q_len = 9;
			h->raw_booted = 1;
		} else {
			if (!h->raw_have_frame || produced == h->raw_last)
				return 0;
			h->raw_last = produced;
			h->raw_q_len = 40;
			h->raw_have_frame = 0;
			return raw_poll(user, produced, word);
		}
	}
	*word = h->raw_q[h->raw_q_pos++];
	return 1;
}

PROSE_API int PROSE_CALL prose_render_frames(prose_h h, int count, prose_frame_cb on_frame, void *user)
{
	if (!valid(h))
		return PROSE_ERR_HANDLE;
	if (count < 0 || !on_frame)
		return PROSE_ERR_ARG;
	unsigned gen;
	int err = begin_blocking(h, &gen);
	if (err)
		return err;
	if (!h->raw_synth) {
		h->raw_synth = prose_synth_create(engine_dsp_rom());
		if (!h->raw_synth) {
			end_job(h);
			return PROSE_ERR_MEMORY;
		}
		prose_synth_set_host(h->raw_synth, raw_poll, NULL, h);
		h->raw_booted = 0;
		h->raw_custom_applied = h->custom_gen - 1;
		prose_synth_reset(h->raw_synth);
	}
	int frames = 0;
	uint8_t p[PROSE_PARAM_MAX];
	int16_t pcm[CHUNK];
	for (; frames < count; frames++) {
		if (wait_paused(h, gen))
			break;
		prose_callbacks cb;
		void *cb_user;
		callbacks(h, &cb, &cb_user);
		sys_lock(&h->lock);
		memcpy(p, h->raw, sizeof p);
		if (h->raw_custom_applied != h->custom_gen) {
			prose_synth_set_custom_pulse(h->raw_synth, h->use_custom && h->has_custom ? h->custom : NULL);
			h->raw_custom_applied = h->custom_gen;
		}
		sys_unlock(&h->lock);
		uint16_t frame[40];
		params_build_frame(h, p, frame);
		memcpy(h->raw_q, frame, sizeof frame);
		h->raw_have_frame = 1;
		if (cb.on_params) {
			cb_enter(h);
			cb.on_params(h, p, (uint32_t)frames * CHUNK, cb_user);
			cb_leave(h);
		}
		prose_synth_continue(h->raw_synth, pcm, CHUNK);
		for (int i = 0; i < CHUNK; i++)
			pcm[i] = prose_dac_to_pcm((uint16_t)pcm[i]);
		cb_enter(h);
		int r = on_frame(h, frames, pcm, CHUNK, user);
		cb_leave(h);
		if (r) {
			frames++;
			break;
		}
	}
	fire_done(h, 0, (uint32_t)frames * CHUNK);
	end_job(h);
	return frames;
}

/* ---- custom glottal pulse ---- */

PROSE_API int PROSE_CALL prose_load_glottal_wave(prose_h h, const char *filename, int kind)
{
	int16_t period[PROSE_SYNTH_PULSE_LEN];
	if (!valid(h))
		return PROSE_ERR_HANDLE;
	if (!filename || (kind != PROSE_GLOTTAL_FLOW && kind != PROSE_GLOTTAL_DERIVATIVE))
		return PROSE_ERR_ARG;
	int err = glottal_load(filename, kind, period);
	if (err)
		return err;
	sys_lock(&h->lock);
	memcpy(h->custom, period, sizeof period);
	h->has_custom = 1;
	h->custom_gen++;
	sys_unlock(&h->lock);
	return 0;
}

PROSE_API void PROSE_CALL prose_use_custom_glottal(prose_h h, int on)
{
	if (!valid(h))
		return;
	sys_lock(&h->lock);
	h->use_custom = on != 0;
	h->custom_gen++;
	sys_unlock(&h->lock);
}

/* Threads, locks and the sound device for the DLL (see prose_sys.h). */
#include "prose_sys.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <mmsystem.h>

void sys_mutex_init(sys_mutex *m) { InitializeCriticalSection(m); }
void sys_mutex_destroy(sys_mutex *m) { DeleteCriticalSection(m); }
void sys_lock(sys_mutex *m) { EnterCriticalSection(m); }
void sys_unlock(sys_mutex *m) { LeaveCriticalSection(m); }
void sys_cond_init(sys_cond *c) { InitializeConditionVariable(c); }
void sys_cond_destroy(sys_cond *c) { (void)c; }
void sys_cond_wait(sys_cond *c, sys_mutex *m) { SleepConditionVariableCS(c, m, INFINITE); }
void sys_cond_timed_wait(sys_cond *c, sys_mutex *m, int ms) { SleepConditionVariableCS(c, m, (DWORD)ms); }
void sys_cond_broadcast(sys_cond *c) { WakeAllConditionVariable(c); }
sys_thread_id sys_thread_self(void) { return GetCurrentThreadId(); }
int sys_thread_equal(sys_thread_id a, sys_thread_id b) { return a == b; }
void sys_sleep_ms(int ms) { Sleep((DWORD)ms); }

typedef struct {
	void (*fn)(void *);
	void *arg;
} thread_start;

static DWORD WINAPI thread_main(LPVOID p)
{
	thread_start s = *(thread_start *)p;
	free(p);
	s.fn(s.arg);
	return 0;
}

int sys_thread_start(sys_thread *t, void (*fn)(void *), void *arg)
{
	thread_start *s = malloc(sizeof *s);
	if (!s)
		return -1;
	s->fn = fn;
	s->arg = arg;
	*t = CreateThread(NULL, 0, thread_main, s, 0, NULL);
	if (!*t) {
		free(s);
		return -1;
	}
	return 0;
}

void sys_thread_join(sys_thread t)
{
	WaitForSingleObject(t, INFINITE);
	CloseHandle(t);
}

static INIT_ONCE global_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION global_mutex;
static BOOL CALLBACK global_init(PINIT_ONCE once, PVOID param, PVOID *ctx)
{
	(void)once;
	(void)param;
	(void)ctx;
	InitializeCriticalSection(&global_mutex);
	return TRUE;
}
sys_mutex *sys_global_mutex(void)
{
	InitOnceExecuteOnce(&global_once, global_init, NULL, NULL);
	return &global_mutex;
}

/* winmm: every write is one queued buffer. The caller keeps little queued ahead (sys_audio_queued), so that stop and
 * pause act fast. */
typedef struct block {
	WAVEHDR hdr;
	struct block *next;
} block;

struct sys_audio {
	HWAVEOUT out;
	HANDLE done;
	block *queued, **tail;
	size_t queued_samples;
	uint32_t base; /* samples played before the last drop (waveOutReset sets the position back to 0) */
};

static void release_done(sys_audio *a)
{
	while (a->queued && (a->queued->hdr.dwFlags & WHDR_DONE)) {
		block *b = a->queued;
		waveOutUnprepareHeader(a->out, &b->hdr, sizeof b->hdr);
		a->queued_samples -= b->hdr.dwBufferLength / 2;
		a->queued = b->next;
		free(b);
	}
	if (!a->queued)
		a->tail = &a->queued;
}

sys_audio *sys_audio_open(void)
{
	sys_audio *a = calloc(1, sizeof *a);
	if (!a)
		return NULL;
	a->tail = &a->queued;
	WAVEFORMATEX fmt = {0};
	fmt.wFormatTag = WAVE_FORMAT_PCM;
	fmt.nChannels = 1;
	fmt.nSamplesPerSec = 10000;
	fmt.wBitsPerSample = 16;
	fmt.nBlockAlign = 2;
	fmt.nAvgBytesPerSec = 20000;
	a->done = CreateEventA(NULL, FALSE, FALSE, NULL);
	if (!a->done || waveOutOpen(&a->out, WAVE_MAPPER, &fmt, (DWORD_PTR)a->done, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
		if (a->done)
			CloseHandle(a->done);
		free(a);
		return NULL;
	}
	return a;
}

uint32_t sys_audio_queued(sys_audio *a)
{
	release_done(a);
	return (uint32_t)a->queued_samples;
}

int sys_audio_write(sys_audio *a, const int16_t *pcm, size_t count)
{
	release_done(a);
	block *b = calloc(1, sizeof *b + count * 2);
	if (!b)
		return -1;
	memcpy(b + 1, pcm, count * 2);
	b->hdr.lpData = (LPSTR)(b + 1);
	b->hdr.dwBufferLength = (DWORD)(count * 2);
	if (waveOutPrepareHeader(a->out, &b->hdr, sizeof b->hdr) != MMSYSERR_NOERROR) {
		free(b);
		return -1;
	}
	if (waveOutWrite(a->out, &b->hdr, sizeof b->hdr) != MMSYSERR_NOERROR) {
		waveOutUnprepareHeader(a->out, &b->hdr, sizeof b->hdr);
		free(b);
		return -1;
	}
	*a->tail = b;
	a->tail = &b->next;
	a->queued_samples += count;
	return 0;
}

uint32_t sys_audio_played(sys_audio *a)
{
	MMTIME t;
	t.wType = TIME_SAMPLES;
	if (waveOutGetPosition(a->out, &t, sizeof t) != MMSYSERR_NOERROR || t.wType != TIME_SAMPLES)
		return a->base;
	return a->base + t.u.sample;
}

void sys_audio_pause(sys_audio *a, int on)
{
	if (on)
		waveOutPause(a->out);
	else
		waveOutRestart(a->out);
}

void sys_audio_drop(sys_audio *a)
{
	uint32_t played = sys_audio_played(a);
	waveOutReset(a->out); /* marks every queued buffer done */
	release_done(a);
	a->base = played;
}

void sys_audio_close(sys_audio *a)
{
	if (!a)
		return;
	for (release_done(a); a->queued; release_done(a))
		WaitForSingleObject(a->done, 20);
	waveOutClose(a->out);
	CloseHandle(a->done);
	free(a);
}

#else /* POSIX */
#include <dlfcn.h>
#include <errno.h>
#include <time.h>

void sys_mutex_init(sys_mutex *m) { pthread_mutex_init(m, NULL); }
void sys_mutex_destroy(sys_mutex *m) { pthread_mutex_destroy(m); }
void sys_lock(sys_mutex *m) { pthread_mutex_lock(m); }
void sys_unlock(sys_mutex *m) { pthread_mutex_unlock(m); }
void sys_cond_init(sys_cond *c) { pthread_cond_init(c, NULL); }
void sys_cond_destroy(sys_cond *c) { pthread_cond_destroy(c); }
void sys_cond_wait(sys_cond *c, sys_mutex *m) { pthread_cond_wait(c, m); }
void sys_cond_broadcast(sys_cond *c) { pthread_cond_broadcast(c); }
sys_thread_id sys_thread_self(void) { return pthread_self(); }
int sys_thread_equal(sys_thread_id a, sys_thread_id b) { return pthread_equal(a, b); }

void sys_cond_timed_wait(sys_cond *c, sys_mutex *m, int ms)
{
	struct timespec t;
	clock_gettime(CLOCK_REALTIME, &t);
	t.tv_sec += ms / 1000;
	t.tv_nsec += (long)(ms % 1000) * 1000000L;
	if (t.tv_nsec >= 1000000000L) {
		t.tv_sec++;
		t.tv_nsec -= 1000000000L;
	}
	pthread_cond_timedwait(c, m, &t);
}

void sys_sleep_ms(int ms)
{
	struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000L};
	while (nanosleep(&t, &t) != 0 && errno == EINTR) {
	}
}

typedef struct {
	void (*fn)(void *);
	void *arg;
} thread_start;

static void *thread_main(void *p)
{
	thread_start s = *(thread_start *)p;
	free(p);
	s.fn(s.arg);
	return NULL;
}

int sys_thread_start(sys_thread *t, void (*fn)(void *), void *arg)
{
	thread_start *s = malloc(sizeof *s);
	if (!s)
		return -1;
	s->fn = fn;
	s->arg = arg;
	if (pthread_create(t, NULL, thread_main, s) != 0) {
		free(s);
		return -1;
	}
	return 0;
}

void sys_thread_join(sys_thread t) { pthread_join(t, NULL); }

static pthread_mutex_t global_mutex = PTHREAD_MUTEX_INITIALIZER;
sys_mutex *sys_global_mutex(void) { return &global_mutex; }

/* PulseAudio's simple API, or ALSA, whichever loads first; declared here so that no headers are needed. */
typedef struct {
	int format;
	uint32_t rate;
	uint8_t channels;
} pa_spec;
typedef struct {
	uint32_t maxlength, tlength, prebuf, minreq, fragsize;
} pa_attr;
static struct {
	void *(*open)(const char *, const char *, int, const char *, const char *, const pa_spec *, const void *,
	              const pa_attr *, int *);
	int (*write)(void *, const void *, size_t, int *);
	int (*drain)(void *, int *);
	int (*flush)(void *, int *);
	void (*free)(void *);
} pa;
static struct {
	int (*open)(void **, const char *, int, int);
	int (*set_params)(void *, int, int, unsigned, unsigned, int, unsigned);
	long (*writei)(void *, const void *, unsigned long);
	int (*recover)(void *, int, int);
	int (*drain)(void *);
	int (*drop)(void *);
	int (*prepare)(void *);
	int (*close)(void *);
} al;
static int backend; /* 0 not tried, 1 PulseAudio, 2 ALSA, -1 none */

static void *sym(void *lib, const char *name, int *ok)
{
	void *p = dlsym(lib, name);
	if (!p)
		*ok = 0;
	return p;
}

/* dlsym returns void *; the cast to a function pointer goes through memcpy to stay within ISO C */
#define LOAD(lib, dst, name, ok)                                                                                      \
	do {                                                                                                          \
		void *p_ = sym(lib, name, ok);                                                                        \
		memcpy(&(dst), &p_, sizeof(dst));                                                                     \
	} while (0)

static int load_backend(void)
{
	if (backend)
		return backend;
	const char *want = getenv("PROSE_AUDIO"); /* "pulse" or "alsa" to force one */
	void *lib;
	if ((!want || !strcmp(want, "pulse")) && (lib = dlopen("libpulse-simple.so.0", RTLD_NOW))) {
		int ok = 1;
		LOAD(lib, pa.open, "pa_simple_new", &ok);
		LOAD(lib, pa.write, "pa_simple_write", &ok);
		LOAD(lib, pa.drain, "pa_simple_drain", &ok);
		LOAD(lib, pa.flush, "pa_simple_flush", &ok);
		LOAD(lib, pa.free, "pa_simple_free", &ok);
		if (ok)
			return backend = 1;
	}
	if ((!want || !strcmp(want, "alsa")) && (lib = dlopen("libasound.so.2", RTLD_NOW))) {
		int ok = 1;
		LOAD(lib, al.open, "snd_pcm_open", &ok);
		LOAD(lib, al.set_params, "snd_pcm_set_params", &ok);
		LOAD(lib, al.writei, "snd_pcm_writei", &ok);
		LOAD(lib, al.recover, "snd_pcm_recover", &ok);
		LOAD(lib, al.drain, "snd_pcm_drain", &ok);
		LOAD(lib, al.drop, "snd_pcm_drop", &ok);
		LOAD(lib, al.prepare, "snd_pcm_prepare", &ok);
		LOAD(lib, al.close, "snd_pcm_close", &ok);
		if (ok)
			return backend = 2;
	}
	return backend = -1;
}

/* What the device has played is estimated by the clock: a play head that moves at 10 samples per ms and never passes
 * what was written. The backends' own latency figures are not used: PulseAudio's include the sink's latency, which
 * does not go to 0 when the stream has played out (seen on WSLg). */
struct sys_audio {
	int kind;
	void *dev;
	uint32_t written, head;
	struct timespec last; /* when head was last moved */
};

static void move_head(sys_audio *a)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	long long ms = (long long)(now.tv_sec - a->last.tv_sec) * 1000 + (now.tv_nsec - a->last.tv_nsec) / 1000000;
	if (ms <= 0)
		return;
	uint32_t left = a->written - a->head;
	a->head += ms * 10 < (long long)left ? (uint32_t)(ms * 10) : left;
	a->last = now;
}

sys_audio *sys_audio_open(void)
{
	static pthread_mutex_t load_lock = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutex_lock(&load_lock);
	int kind = load_backend();
	pthread_mutex_unlock(&load_lock);
	sys_audio *a = calloc(1, sizeof *a);
	if (!a)
		return NULL;
	a->kind = kind;
	if (kind == 1) {
		pa_spec spec = {3 /* PA_SAMPLE_S16LE */, 10000, 1};
		pa_attr attr = {(uint32_t)-1, 2 * 1000 /* 100 ms */, (uint32_t)-1, (uint32_t)-1, (uint32_t)-1};
		int err;
		a->dev = pa.open(NULL, "Prose", 1 /* PA_STREAM_PLAYBACK */, NULL, "speech", &spec, NULL, &attr, &err);
	} else if (kind == 2) {
		if (al.open(&a->dev, "default", 0 /* SND_PCM_STREAM_PLAYBACK */, 0) < 0)
			a->dev = NULL;
		/* S16_LE (2), RW_INTERLEAVED (3), 1 channel, 10 kHz (the device may convert), 100 ms latency */
		else if (al.set_params(a->dev, 2, 3, 1, 10000, 1, 100000) < 0) {
			al.close(a->dev);
			a->dev = NULL;
		}
	}
	if (!a->dev) {
		free(a);
		return NULL;
	}
	clock_gettime(CLOCK_MONOTONIC, &a->last);
	return a;
}

int sys_audio_write(sys_audio *a, const int16_t *pcm, size_t count)
{
	move_head(a); /* up to now, before the new samples can count */
	if (a->kind == 1) {
		int err;
		if (pa.write(a->dev, pcm, count * 2, &err) < 0)
			return -1;
	} else {
		size_t done = 0;
		while (done < count) {
			long n = al.writei(a->dev, pcm + done, (unsigned long)(count - done));
			if (n < 0 && al.recover(a->dev, (int)n, 1) < 0)
				return -1;
			if (n > 0)
				done += (size_t)n;
		}
	}
	a->written += (uint32_t)count;
	return 0;
}

uint32_t sys_audio_played(sys_audio *a)
{
	move_head(a);
	return a->head;
}

uint32_t sys_audio_queued(sys_audio *a) { return a->written - sys_audio_played(a); }

/* Neither API pauses a stream simply: the caller stops writing, and what is queued (up to 100 ms) plays out. */
void sys_audio_pause(sys_audio *a, int on)
{
	(void)a;
	(void)on;
}

void sys_audio_drop(sys_audio *a)
{
	if (a->kind == 1) {
		int err;
		pa.flush(a->dev, &err);
	} else {
		al.drop(a->dev);
		al.prepare(a->dev);
	}
	move_head(a);
	a->written = a->head;
}

void sys_audio_close(sys_audio *a)
{
	if (!a)
		return;
	if (a->kind == 1) {
		int err;
		pa.drain(a->dev, &err);
		pa.free(a->dev);
	} else {
		al.drain(a->dev);
		al.close(a->dev);
	}
	free(a);
}
#endif

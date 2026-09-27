/* The engine: runs a handle's copy of the firmware and its DSP model, one utterance at a time (see prose_int.h).
 *
 * The DSP model pulls: it asks for a frame every 10 ms, and each request runs the firmware's loop until it would idle,
 * then its frame interrupt, which sends the frame built at the previous request and builds the next (as
 * tests/pipeline_play.c does). The DLL plays the host: it sends the text to the firmware's serial input, pausing while
 * the firmware has sent XOFF, and reads the firmware's replies (index markers, the ESC[n x that ends an utterance).
 *
 * The text ends with a CR, not ESC[x: while an ESC[x is pending, the generator ends the utterance at the next index
 * marker it reaches (pg_run.c, END_REQUEST), so text after a marker would be lost. Instead the DLL flushes in two
 * steps, each once the text is in, the pipeline idle and the DSP without a frame for 3 requests:
 * 1. ESC[C: its phrase boundary (the ] symbol) releases a last phrase without final punctuation, which the firmware
 *    holds back waiting for more text; markers in it are reported as usual.
 * 2. ESC[x: everything has been spoken, so it ends nothing early; its reply ends the utterance. */
#include "prose_int.h"

#include "dsp_rom.h"
#include "prose_wave.h"
#include "v1_map.h"

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

static prose_rom *rom;
static uint16_t dsp_rom[512], boot[9];
static int ready;
static prose_h cur;       /* the handle whose firmware is running */
static prose_h loaded[2]; /* whose RAM is in the v3.4.1 / v1.1 image */
static jmp_buf fatal_jmp;
static int fatal_code, fatal_armed;

static const engine_driver *drv(prose_h h) { return h->version == PROSE_V11 ? &eng1_driver : &eng3_driver; }

const uint16_t *engine_boot_block(void) { return boot; }
const prose_rom *engine_rom(void) { return rom; }
const uint16_t *engine_dsp_rom(void) { return dsp_rom; }
prose_h engine_current(void) { return cur; }

int engine_init(void)
{
	if (ready)
		return 0;
	rom = malloc(sizeof *rom);
	if (!rom)
		return PROSE_ERR_MEMORY;
	prose_rom_builtin(rom);
	prose_dsp_builtin_data_rom(dsp_rom);
	for (int i = 0; i < 9; i++)
		boot[i] = (uint16_t)prose_ds_table(rom, 0x5644, i);
	eng3_driver.init(rom);
	eng1_driver.init(rom);
	ready = 1;
	return 0;
}

/* Take the engine for h: swap its RAM in (saving the handle that had it). */
static void activate(prose_h h)
{
	sys_lock(sys_global_mutex());
	int slot = h->version == PROSE_V11;
	if (loaded[slot] != h) {
		if (loaded[slot])
			drv(loaded[slot])->save(loaded[slot]);
		drv(h)->load(h);
		loaded[slot] = h;
	}
	cur = h;
}

static void deactivate(void)
{
	cur = NULL;
	sys_unlock(sys_global_mutex());
}

/* ---- the DSP's host port ---- */

static int poll(void *user, size_t produced, uint16_t *word)
{
	prose_h h = user;
	if (h->q_pos == h->q_len) {
		h->q_len = h->q_pos = 0;
		if (!h->booted) {
			drv(h)->boot_block(h, h->q);
			h->q_len = 9;
			h->booted = 1;
		} else {
			/* one interrupt per frame request (the DSP polls again once after a miss, without a new one) */
			if (h->finished || produced == h->last_request)
				return 0;
			h->last_request = h->req_produced = produced;
			drv(h)->request(h);
			if (!h->q_len)
				return 0;
		}
	}
	*word = h->q[h->q_pos++];
	return 1;
}

static int text_done(prose_h h) { return !h->text || (h->text_pos >= h->text_len && h->x_replies >= h->x_sent); }

static void timeout(void *user, size_t produced)
{
	prose_h h = user;
	(void)produced;
	h->misses++;
	if (!h->finished && h->misses >= 3 && text_done(h) && drv(h)->idle()) {
		const char *flush = NULL;
		if (h->flushed == 2) {
			h->finished = 1;
		} else if (h->flushed == 1) { /* the flush: see the top of the file */
			flush = "\x1B[x";
			h->capture = 0; /* the echo is complete */
			h->x_sent++;
		} else {
			flush = "\x1B[C";
		}
		if (flush) {
			h->flushed++;
			h->misses = 0;
			for (; *flush; flush++)
				drv(h)->send_char(*flush);
		}
	}
	/* after ESC[S or ESC[W the firmware may not answer an ESC[x that was sent: give up after 0.3 s of quiet */
	if (!h->finished && h->misses >= 30 && (!h->text || h->text_pos >= h->text_len) && drv(h)->idle())
		h->finished = 1;
}

void engine_frame(const uint16_t frame[40])
{
	prose_h h = cur;
	memcpy(h->q, frame, 40 * sizeof *frame);
	h->q_len = 40;
	h->q_pos = 0;
	h->misses = 0;
}

/* ---- the utterance's text ---- */

/* the end of the segment starting at `from`: just after the next ESC [ digits/; x, or len + 1 if there is none (the
 * rest of the text is one segment, and no reply follows it) */
static size_t segment_end(const char *t, size_t len, size_t from)
{
	for (size_t i = from; i + 2 < len; i++)
		if (t[i] == 0x1B && t[i + 1] == '[') {
			size_t j = i + 2;
			while (j < len && ((t[j] >= '0' && t[j] <= '9') || t[j] == ';'))
				j++;
			if (j < len && t[j] == 'x')
				return j + 1;
		}
	return len + 1;
}

/* The host: send text until the firmware sends XOFF. A segment after an ESC[x waits for its reply, since text that
 * arrives before it is discarded (REFERENCE §8.2). */
void engine_feed(void)
{
	prose_h h = cur;
	if (!h || !h->text)
		return;
	const engine_driver *d = drv(h);
	while (h->text_pos < h->text_len && !d->xoff()) {
		if (h->text_pos == h->seg_end) {
			if (h->x_replies < h->x_sent)
				return;
			h->seg_end = segment_end(h->text, h->text_len, h->text_pos);
		}
		d->send_char((unsigned char)h->text[h->text_pos++]);
		if (h->text_pos == h->seg_end)
			h->x_sent++;
	}
}

static void cap_add(prose_h h, int c)
{
	if (h->cap_len + 2 > h->cap_cap) {
		size_t n = h->cap_cap ? h->cap_cap * 2 : 256;
		char *p = realloc(h->cap, n);
		if (!p)
			return;
		h->cap = p;
		h->cap_cap = n;
	}
	h->cap[h->cap_len++] = (char)c;
	h->cap[h->cap_len] = 0;
}

/* ---- events ---- */

static uint32_t event_pos(prose_h h)
{
	/* the frame built at this request reaches the DSP at the next one, 100 samples later */
	size_t at = h->req_produced + 100;
	return at > h->utt_start ? (uint32_t)(at - h->utt_start) : 0;
}

static prose_event *event_add(prose_h h, int type)
{
	if (h->ev_len == h->ev_cap) {
		size_t n = h->ev_cap ? h->ev_cap * 2 : 64;
		prose_event *p = realloc(h->ev, n * sizeof *p);
		if (!p) {
			h->ev_failed = 1;
			return NULL;
		}
		h->ev = p;
		h->ev_cap = n;
	}
	prose_event *e = &h->ev[h->ev_len++];
	memset(e, 0, sizeof *e);
	e->type = type;
	e->pos = event_pos(h);
	return e;
}

void engine_params(const uint8_t *p, int count)
{
	prose_h h = cur;
	if (!h || !h->in_utt || !h->want_params)
		return;
	prose_event *e = event_add(h, EV_PARAMS);
	if (e)
		memcpy(e->p, p, (size_t)count);
}

/* The playback stage reports a segment once its last frame has been played, so the event goes back by the segment's
 * length to where it started, and into the queue before the events of its frames. deliver() holds the audio after
 * ph_end back until then. */
void engine_segment(int ch, int frames)
{
	prose_h h = cur;
	if (!h || !h->in_utt)
		return;
	if (h->capture && h->version == PROSE_V11) /* v1.1 has no phoneme echo */
		cap_add(h, ch);
	if (!h->want_phoneme)
		return;
	uint32_t end = event_pos(h), len = (uint32_t)frames * 100, start = end > len ? end - len : 0;
	if (start < h->ph_end) /* the audio before ph_end may have gone */
		start = h->ph_end;
	if (end < start)
		end = start;
	prose_event *e = event_add(h, EV_PHONEME);
	if (!e)
		return;
	e->ph = (char)ch;
	e->pos = start;
	e->ms = (int)((end - start) / 10);
	h->ph_end = end;
	size_t i = h->ev_len - 1;
	while (i > h->ev_head && h->ev[i - 1].pos > start)
		i--;
	if (i != h->ev_len - 1) {
		prose_event t = *e;
		memmove(&h->ev[i + 1], &h->ev[i], (h->ev_len - 1 - i) * sizeof *h->ev);
		h->ev[i] = t;
	}
}

/* A byte from the firmware to the host: ESC [ n ; ... letter replies, flow control, and the phoneme echo. */
void engine_tx(int c)
{
	prose_h h = cur;
	if (!h)
		return;
	c &= 0xFF;
	if (c == 0x1B) {
		h->tx_state = 1;
		return;
	}
	switch (h->tx_state) {
	case 0:
		if (c >= 0x20 && h->capture && h->version == PROSE_V341)
			cap_add(h, c);
		return; /* XON, XOFF, BEL and CR are for a serial host */
	case 1:
		h->tx_state = c == '[' ? 2 : 0;
		h->tx_val = 0;
		h->tx_first = -1;
		return;
	default:
		if (c >= '0' && c <= '9') {
			h->tx_val = h->tx_val * 10 + c - '0';
			return;
		}
		if (c == ';') {
			if (h->tx_first < 0)
				h->tx_first = h->tx_val;
			h->tx_val = 0;
			return;
		}
		h->tx_state = 0;
		int n = h->tx_first >= 0 ? h->tx_first : h->tx_val;
		if (c == 'i' && h->in_utt) {
			prose_event *e = event_add(h, EV_INDEX);
			if (e)
				e->n = n;
			h->last_index = n;
		} else if (c == 'x') {
			h->x_replies++;
		}
	}
}

/* ---- fatal errors ---- */

void engine_fatal(int code)
{
	fatal_code = code;
	if (fatal_armed)
		longjmp(fatal_jmp, 1);
	abort(); /* firmware code never runs outside engine_run, engine_open and engine_reboot */
}

/* power-up of h's firmware and a new start of its DSP model (h active) */
static void boot_firmware(prose_h h)
{
	drv(h)->power_up(h);
	h->booted = 0;
	h->q_len = h->q_pos = 0;
	h->finished = 1;
	h->echo_on = 0;
	h->last_request = (size_t)-1;
	prose_synth_reset(h->synth);
}

/* runs boot_firmware, retrying after a fatal error (none is known to happen at power-up) */
static int boot_guarded(prose_h h)
{
	for (int tries = 0; tries < 3; tries++) {
		fatal_armed = 1;
		if (!setjmp(fatal_jmp)) {
			boot_firmware(h);
			fatal_armed = 0;
			return 0;
		}
		fatal_armed = 0;
		h->fw_error = fatal_code;
	}
	return PROSE_ERR_FIRMWARE;
}

/* ---- the handle's engine ---- */

int engine_open(prose_h h)
{
	h->synth = prose_synth_create(dsp_rom);
	if (!h->synth)
		return PROSE_ERR_MEMORY;
	prose_synth_set_host(h->synth, poll, timeout, h);
	if (h->version == PROSE_V11) {
		v1_map *m = malloc(sizeof *m);
		if (!m)
			return PROSE_ERR_MEMORY;
		v1_map_init(m, rom);
		h->map = m;
	}
	h->ph_end = 0;
	activate(h);
	int r = boot_guarded(h);
	deactivate();
	return r;
}

void engine_close(prose_h h)
{
	sys_lock(sys_global_mutex());
	for (int i = 0; i < 2; i++)
		if (loaded[i] == h)
			loaded[i] = NULL;
	sys_unlock(sys_global_mutex());
	prose_synth_destroy(h->synth);
	free(h->map);
	free(h->text);
	free(h->cap);
	free(h->ev);
}

void engine_reboot(prose_h h)
{
	activate(h);
	boot_guarded(h);
	deactivate();
}

int engine_start(prose_h h, const char *text, int want_params, int want_phoneme, int capture)
{
	size_t n = strlen(text);
	char *t = malloc(n + 16);
	if (!t)
		return PROSE_ERR_MEMORY;
	size_t len = 0;
	if (capture && h->version == PROSE_V341 && !h->echo_on) { /* N-flag 16: the phoneme echo (REFERENCE §8.3) */
		memcpy(t, "\x1B[16N", 5);
		len = 5;
		h->echo_on = 1;
	}
	/* The serial line is 7-bit ASCII: bytes above 0x7F (UTF-8 and the like) become spaces, and so do control
	 * characters other than ESC, CR, LF and TAB, which would stop the output (XOFF) or restart the firmware (^R). */
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)text[i];
		t[len++] = (char)(c >= 0x80 || (c < 0x20 && c != 0x1B && c != '\r' && c != '\n' && c != '\t') ? ' ' : c);
	}
	/* end with a CR, or with the text's own ESC[x, which then needs no flush */
	int has_x = 0;
	for (size_t i = len; i >= 3 && !has_x; i--) {
		if (t[i - 1] != 'x')
			break;
		size_t j = i - 1;
		while (j > 0 && ((t[j - 1] >= '0' && t[j - 1] <= '9') || t[j - 1] == ';'))
			j--;
		has_x = j >= 2 && t[j - 1] == '[' && t[j - 2] == 0x1B;
	}
	if (!has_x)
		t[len++] = '\r';
	t[len] = 0;
	/* a text that ends a sentence (. ? !, before any spaces and escapes) holds nothing back: no ESC[C, whose phrase
	 * boundary would add a pause */
	size_t k = len;
	while (k > 0) {
		if (t[k - 1] == ' ' || t[k - 1] == '\r' || t[k - 1] == '\n' || t[k - 1] == '\t') {
			k--;
			continue;
		}
		size_t j = k - 1; /* the letter of an escape sequence? */
		while (j > 0 && ((t[j - 1] >= '0' && t[j - 1] <= '9') || t[j - 1] == ';'))
			j--;
		if (j >= 2 && t[j - 1] == '[' && t[j - 2] == 0x1B) {
			k = j - 2;
			continue;
		}
		break;
	}
	int sentence_end = k > 0 && (t[k - 1] == '.' || t[k - 1] == '?' || t[k - 1] == '!');
	h->flushed = has_x ? 2 : sentence_end ? 1 : 0;

	free(h->text);
	h->text = t;
	h->text_len = len;
	h->text_pos = h->seg_end = 0;
	h->x_sent = h->x_replies = h->last_index = 0;
	h->tx_state = 0;
	h->ev_head = h->ev_len = 0;
	h->ph_end = 0;
	h->ev_failed = 0;
	h->cap_len = 0;
	if (h->cap)
		h->cap[0] = 0;
	h->want_params = want_params;
	h->want_phoneme = want_phoneme;
	h->capture = capture;
	h->utt_start = h->req_produced = prose_synth_produced(h->synth);
	h->misses = 0;
	h->finished = 0;
	h->in_utt = 1;
	return 0;
}

/* the custom glottal pulse, when the program has changed it */
static void apply_custom(prose_h h)
{
	sys_lock(&h->lock);
	if (h->custom_applied != h->custom_gen) {
		prose_synth_set_custom_pulse(h->synth, h->use_custom && h->has_custom ? h->custom : NULL);
		h->custom_applied = h->custom_gen;
	}
	sys_unlock(&h->lock);
}

int engine_run(prose_h h, int16_t *pcm, size_t count)
{
	volatile int err = 0;
	activate(h);
	apply_custom(h);
	fatal_armed = 1;
	if (setjmp(fatal_jmp)) {
		/* the firmware's fatal error: restart it, as the board does, and drop the utterance */
		fatal_armed = 0;
		h->fw_error = fatal_code;
		boot_guarded(h);
		memset(pcm, 0, count * sizeof *pcm);
		h->in_utt = 0;
		err = PROSE_ERR_FIRMWARE;
	} else {
		prose_synth_continue(h->synth, pcm, count);
		fatal_armed = 0;
		for (size_t i = 0; i < count; i++)
			pcm[i] = prose_dac_to_pcm((uint16_t)pcm[i]);
	}
	deactivate();
	return err;
}

int engine_finished(prose_h h) { return h->finished; }

uint32_t engine_position(prose_h h) { return (uint32_t)(prose_synth_produced(h->synth) - h->utt_start); }

void engine_abort(prose_h h)
{
	int16_t scratch[100];
	activate(h);
	h->in_utt = 0;
	h->text_pos = h->text_len; /* nothing more is sent, and no flush */
	h->x_sent = h->x_replies;
	h->flushed = 2;
	for (const char *s = "\x1B[S"; *s; s++)
		drv(h)->send_char(*s);
	deactivate();
	/* run until the firmware has reset its pipeline and the DSP is quiet, at most 3 s */
	for (int i = 0; i < 300 && !h->finished; i++)
		if (engine_run(h, scratch, 100))
			break;
	h->ev_head = h->ev_len = 0;
	h->ph_end = 0;
}

void engine_end(prose_h h)
{
	h->in_utt = 0;
	h->finished = 1;
	free(h->text);
	h->text = NULL;
	h->ev_head = h->ev_len = 0;
	h->ph_end = 0;
}

/* The engine's driver for v3.4.1: the synthesis loop's scheduling between frame requests (as tests/pipeline_play.c
 * runs it, which matches the emulator frame for frame) and the hooks into the decompiled firmware. */
#include "prose_int.h"

#include "input.h"
#include "lexical.h"
#include "pg.h"
#include "prosody.h"
#include "textrules.h"

#include <string.h>

/* the synthesis loop's pending flags */
#define TEXT_PENDING 0xEE60
#define LEXICAL_PENDING 0xEE62
#define PROSODY_PENDING 0xEE64
#define PARAMGEN_PENDING 0xEE66
#define PARAMGEN_WAIT 0xEE68 /* paramgen_run returned 1: wait for ring room */
#define FRAMES_JUST_WRITTEN 0xDD9C

static const prose_rom *rom;

static void drain(void)
{
	while (rw(TX_RUNNING))
		if (tx_send() < 0)
			break;
}

static void on_tx(int c) { engine_tx(c); }

/* ^R and ESC[W restart the synthesis loop */
static void on_restart(int code)
{
	loop_entry(code);
	drain();
}

/* ESC[nL: the stub at 0000:0024 writes n to port 3402 and jumps to the reset vector: a cold boot */
static void on_int8(int n)
{
	wb(0xF302, n);
	power_up(rom);
	drain();
}

static int on_dsp_frame(const uint16_t *frame, int words)
{
	(void)words;
	engine_frame(frame);
	return 1;
}

static void on_frame_built(unsigned pos, int mark, int alt, const uint8_t t[22])
{
	(void)pos;
	(void)mark;
	(void)alt;
	engine_params(t, 22);
}

static void init(const prose_rom *r)
{
	rom = r;
	boot_load_rom(r);
	pg_load_rom(r);
	lexical_load_rom(r);
	pg_fatal_hook = engine_fatal;
	pg_host_send_hook = host_send;
	pg_segment_hook = engine_segment;
	input_restart_hook = on_restart;
	input_int8_hook = on_int8;
	host_tx_hook = on_tx;
	dsp_write_hook = on_dsp_frame;
	frame_trace_hook = on_frame_built;
}

static void power(prose_h h)
{
	(void)h;
	memset(pg_ds + DS_RAM_LO, 0, 0x3000); /* power-on RAM as in the emulator; the boot code fills 0-2BFF */
	dsp_status = 0x80;
	power_up(rom);
	drain();
}

static void save(prose_h h)
{
	pg_save_ram(h->ram);
	h->dsp_status = dsp_status;
}

static void load(prose_h h)
{
	pg_load_ram(h->ram);
	dsp_status = h->dsp_status;
}

static void send_char(int c) { host_rx_char(c, 0); }

static int xoff(void) { return rw(INPUT_XOFF) != 0; }

/* One stage of E39A7: run it if enough nodes are free. Returns 0 when the loop must idle instead. */
static int run_stage(int (*stage)(void), int pending, int next_pending, int min_free)
{
	if (rw(FREE_COUNT) <= min_free)
		return 0;
	if (stage())
		ww(next_pending, 1);
	else
		ww(pending, 0);
	return 1;
}

/* The passes of E39A7 until there is nothing to do. */
static void schedule_pass(void)
{
	engine_feed();
	for (int k = 0; k < 1024; k++) {
		drain();
		if (loop_check_stop()) { /* ESC[q, ESC[S, or an end of sentence reached playback: back to the start */
			drain();
			engine_feed();
			continue;
		}
		if (rw(PLAYBACK_STATE) != 2)
			ww(PLAYBACK_STATE, stage_playback_run());
		if (rw(PARAMGEN_WAIT) && param_ring_ctl(1, rw(FRAMES_JUST_WRITTEN)))
			ww(PARAMGEN_WAIT, 0);
		if (!rw(PARAMGEN_WAIT) && rw(PARAMGEN_PENDING)) {
			int r = paramgen_run();
			if (r == 0)
				ww(PARAMGEN_PENDING, 0);
			else if (r == 1)
				ww(PARAMGEN_WAIT, 1);
			ww(PLAYBACK_STATE, stage_playback_run());
			continue;
		}
		if (rw(PROSODY_PENDING)) {
			if (run_stage(prosody_run, PROSODY_PENDING, PARAMGEN_PENDING, 0x69))
				continue;
		} else if (rw(LEXICAL_PENDING)) {
			if (run_stage(stage_lexical_run, LEXICAL_PENDING, PROSODY_PENDING, 0x25))
				continue;
		} else if (rw(TEXT_PENDING)) {
			if (run_stage(stage_text_rules_run, TEXT_PENDING, LEXICAL_PENDING, 0x25))
				continue;
		} else if (!rw(INPUT_IDLE) && rw(FREE_COUNT) > 0x0F) {
			if (stage_input_run())
				ww(TEXT_PENDING, 1);
			else
				ww(INPUT_IDLE, 1);
			engine_feed();
			continue;
		}
		break;
	}
}

/* The idle routine (E3B9D): 1 if it would return now; otherwise the loop waits for the next frame request. */
static int idle_has_work(void)
{
	for (int spin = 0; spin < 2; spin++) {
		if (!rw(INPUT_IDLE) && rw(FREE_COUNT) > 0x0F)
			return 1;
		if (!rw(PARAMGEN_WAIT) && rw(PARAMGEN_PENDING))
			return 1;
		if (rw(STOP_REQUEST) || rw(QUIT_REQUEST))
			return 1;
		if (rw(PLAYBACK_STATE) == 0 && rw(SEGMENTS_PLAYED)) /* a played segment to free */
			return 1;
		if (rw(PLAYBACK_STATE) == 1 && output_ring_free() > 6) /* a marker can be sent now */
			return 1;
		if (rw(INPUT_READ) != rw(INPUT_WRITE)) /* wake the input stage */
			ww(INPUT_IDLE, 0);
		/* release the wait once the ring has room, restart the generator when it runs low */
		if (rw(PARAMGEN_WAIT) &&
		    s16(rw(RING_HIGH) + rw(FRAMES_JUST_WRITTEN) + rw(RING_SLACK) - rw(RING_READ)) < 0x80)
			ww(PARAMGEN_WAIT, 0);
		if (rw(RING_HOLD) == -1 && s16(rw(RING_LOW) - rw(RING_LAG) - rw(RING_READ)) <= 8) {
			ww(PARAMGEN_PENDING, 1);
			return 1;
		}
	}
	return 0;
}

/* E39A7 between two frame requests, then the frame interrupt */
static void request(prose_h h)
{
	(void)h;
	for (int round = 0; round < 64; round++) {
		schedule_pass();
		if (!idle_has_work())
			break;
	}
	dsp_status = 0xA0; /* USF0 and RQM */
	dsp_irq_service();
}

static int idle(void)
{
	return rw(INPUT_IDLE) && !rw(TEXT_PENDING) && !rw(LEXICAL_PENDING) && !rw(PROSODY_PENDING) &&
	       !rw(PARAMGEN_PENDING) && !rw(PARAMGEN_WAIT);
}

static void boot_block(prose_h h, uint16_t out[9])
{
	(void)h;
	memcpy(out, engine_boot_block(), 9 * sizeof *out);
}

const engine_driver eng3_driver = {init, power, save, load, send_char, xoff, request, idle, drain, boot_block};

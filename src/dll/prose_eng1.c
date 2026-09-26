/* The engine's driver for v1.1: synthesis_main's loop between frame requests (as tests/v1_pipeline_play.c runs it)
 * and the hooks into the decompiled firmware. v1.1's 37-word frames are mapped onto the v3.12 DSP (v1_map), since
 * v1.1's own DSP program is lost: an approximation of its sound (REFERENCE §16). */
#include "prose_int.h"

#include "v1.h"
#include "v1_map.h"

#include <string.h>

static void drain(void)
{
	while (rw(V1_TX_RUNNING))
		v1_uart_tx_isr();
}

static void on_tx(int c) { engine_tx(c); }

/* ^R and ESC[nT: loop_restart, then synthesis_main starts again */
static void on_restart(int code)
{
	(void)code;
	v1_loop_restart();
	v1_loop_entry();
	drain();
}

static void on_frame(const uint16_t *frame, int words)
{
	uint16_t out[40];
	(void)words;
	v1_map_frame(engine_current()->map, frame, out);
	engine_frame(out);
}

static void on_params(const uint8_t track[18]) { engine_params(track, 18); }

static void init(const prose_rom *r)
{
	(void)r;
	v1_load_rom();
	v1_fatal_hook = engine_fatal;
	v1_tx_hook = on_tx;
	v1_restart_hook = on_restart;
	v1_frame_hook = on_frame;
	v1_params_hook = on_params;
	v1_segment_hook = engine_segment;
}

static void power(prose_h h)
{
	(void)h;
	memset(v1_ds + V1_DS_RAM_LO, 0, 0x3000); /* power-on RAM as in the emulator */
	v1_dsp_status = 0x80;
	v1_power_up();
	drain();
}

static void save(prose_h h)
{
	v1_save_ram(h->ram);
	h->dsp_status = v1_dsp_status;
}

static void load(prose_h h)
{
	v1_load_ram(h->ram);
	v1_dsp_status = h->dsp_status;
}

static void send_char(int c) { v1_host_rx_char(c, 0); }

static int xoff(void) { return rw(V1_INPUT_XOFF) != 0; }

/* synthesis_main's loop between two frame requests, until it would idle; then the frame interrupt */
static void request(prose_h h)
{
	(void)h;
	for (int k = 0; k < 4096; k++) {
		engine_feed();
		drain();
		if (v1_loop_check_stop())
			drain();
		if (!v1_loop_pass() && !v1_idle_has_work())
			break;
	}
	drain();
	v1_dsp_status = 0xA0; /* USF0 and RQM */
	v1_dsp_irq_service();
}

static int idle(void)
{
	return rw(V1_INPUT_EMPTY) && !rw(V1_RUN_TEXTRULES) && !rw(V1_RUN_LEXICAL) && !rw(V1_RUN_PROSODY) &&
	       !rw(V1_RUN_PARAMGEN) && !rw(V1_TRACKS_FULL);
}

static void boot_block(prose_h h, uint16_t out[9]) { v1_map_boot_block(h->map, out); }

const engine_driver eng1_driver = {init, power, save, load, send_char, xoff, request, idle, drain, boot_block};

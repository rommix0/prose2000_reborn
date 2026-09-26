/*
 * State of the instruction-level C translation of the Prose uPD7720 program (tools/dsp_recompile.py).
 *
 * This is the reference model. It keeps every 7720 register, so it can be checked sample for sample against the
 * emulator (native/prose_dsp.cpp). The readable decompilation in prose_synth.c is then checked against it.
 */
#ifndef DSP_REC_H
#define DSP_REC_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
	uint8_t s1, s0, c, z, ov1, ov0;
} dsp_flag;

typedef struct dsp_rec {
	uint16_t pc;           /* resume point: 0 (reset) or 0x018 (top of the sample loop) */
	uint16_t stack[4];
	uint8_t sp;
	uint16_t rp, dp;
	int16_t k, l, m, n;    /* multiplier inputs and outputs: M = K*L >> 15, N = K*L << 1 */
	uint16_t a, b;
	dsp_flag fa, fb;
	uint16_t tr, trb, sr, dr, si, so, idb;
	uint16_t ram[128];
	uint16_t rom[512];     /* data ROM, ascending address order */

	/* Host side. host_poll returns 1 and fills host_word when the 8086 has written a word. host_timeout is called
	   when the frame request at 0x1A2 gives up waiting. */
	int (*host_poll)(struct dsp_rec *s);
	void (*host_timeout)(struct dsp_rec *s);
	uint16_t host_word;
	void *user;

	int16_t *out;          /* serial output words, as the emulator captures them */
	size_t out_n, out_cap;
} dsp_rec;

/* Runs until max_samples words have been output (then stops at the loop top, s->pc = 0x018). Returns 0, or a
   negative value if the program went somewhere the translation does not support. */
int dsp_rec_run(dsp_rec *s, long max_samples);

#endif

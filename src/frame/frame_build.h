/*
 * The 8086 frame builder: one frame of the 22 synthesis parameters -> the 40-word DSP frame.
 *
 * Decompiled from dsp_build_frame (Prose 2000 D89CB, 4001 D8710). REFERENCE.md sections 11.3 and 11.4 describe
 * the parameters and the frame words; src/dsp/prose_synth.c is the DSP side that consumes the frame.
 */
#ifndef FRAME_BUILD_H
#define FRAME_BUILD_H

#include "prose_rom.h"

#include <stdint.h>

typedef struct {
	const prose_rom *rom;
	uint16_t w[40];     /* the frame buffer DS:DBD2. Words not rewritten keep their last value. */
	uint16_t lfsr;      /* jitter/shimmer generator [DC26] */
	int16_t silent;     /* [DC22]: 1 while the last frame was silent */
	int16_t silent_run; /* [DC24]: silent frames in a row, capped at 2 */
	uint8_t latch;      /* board control latch shadow [DB82]; bit 1 is set during silence */
	uint16_t played;    /* [DD9A]: counts frames that carry a segment mark (used by the phoneme echo) */
	/* Working values the firmware keeps in DS; only dsp_build_frame (src/input) uses them, to leave the same
	   RAM. p: the 22 parameters as used, at DC46 (p17 and p21 changed). work[i]: DC28 + 2i (see WK_* in
	   frame_build.c); each is written only on the path that computes it. */
	int16_t p[22];
	int16_t work[15];
} frame_builder;

/* Sets up a builder: both resets below. */
void frame_builder_init(frame_builder *fb, const prose_rom *rom, uint8_t latch);

/* D8918: reload the frame buffer from the template at DS:5656 (and clear the send flags). */
void frame_builder_reset_frame(frame_builder *fb);

/* D89B8: LFSR = 0x55, silence counters cleared. The firmware runs both resets between utterances. */
void frame_builder_reset_source(frame_builder *fb);

/* Builds one frame from the 22 track bytes (p0-p21, raw codings of REFERENCE 11.4).
   mark: the frame's bit in the segment bitset DS:DD8A. alt: its bit in DS:EAE8, which switches F1-F3 to a 10 Hz
   coding. */
void frame_build(frame_builder *fb, const uint8_t track[22], int mark, int alt);

#endif

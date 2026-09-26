/*
 * The Prose 2000 formant synthesizer: a C decompilation of the uPD7720 program (DSP v3.12, 8/9/88).
 *
 * The 8086 sends one 40-word frame every 100 samples (10 ms at 10 kHz); REFERENCE.md sections 11.3 and 13 describe
 * the words. This model produces the same 16-bit serial words the DSP sends to its DAC, sample for sample.
 */
#ifndef PROSE_SYNTH_H
#define PROSE_SYNTH_H

#include <stddef.h>
#include <stdint.h>

typedef struct prose_synth prose_synth;

/* poll: return 1 and set *word when the host has a word ready; produced = samples output so far.
   timeout: called when a frame request is abandoned (the DSP then repeats the old frame, and after two misses
   goes silent). */
typedef int (*prose_synth_poll)(void *user, size_t produced, uint16_t *word);
typedef void (*prose_synth_timeout)(void *user, size_t produced);

prose_synth *prose_synth_create(const uint16_t rom[512]);
void prose_synth_destroy(prose_synth *s);
void prose_synth_set_host(prose_synth *s, prose_synth_poll poll, prose_synth_timeout timeout, void *user);
/* on: start from zeroed output registers, as the emulator does. The first two samples are then DAC code 0 (full
   negative scale, a click); by default they are the mid-scale bias the program outputs next. */
void prose_synth_set_raw_start(prose_synth *s, int on);
/* Runs from reset until `count` samples have been written to out; returns the number written. */
size_t prose_synth_run(prose_synth *s, int16_t *out, size_t count);

/* prose_synth_run in two steps, for output in pieces: the reset (it reads the setup words through poll), then any
   number of calls that each write the next `count` samples to out[0..count). */
void prose_synth_reset(prose_synth *s);
size_t prose_synth_continue(prose_synth *s, int16_t *out, size_t count);
size_t prose_synth_produced(const prose_synth *s); /* samples output since the reset */

/* Not in the DSP (for the DLL's custom glottal pulse): `period` is one glottal flow period of PROSE_SYNTH_PULSE_LEN
   samples, 0 (closed) to 0x7FFF (the peak). Each pitch period then plays it stretched to the period's length, scaled
   by the voicing amplitude, in place of the pulse table; everything after the flow is unchanged. NULL goes back to
   the table. The period is copied. */
#define PROSE_SYNTH_PULSE_LEN 256
void prose_synth_set_custom_pulse(prose_synth *s, const int16_t *period);

#endif

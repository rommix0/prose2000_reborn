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

#endif

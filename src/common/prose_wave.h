/*
 * Audio output for the tests and tools: plays 16-bit mono PCM through the Windows waveOut API (winmm).
 * On other systems the calls do nothing and prose_wave_open returns -1.
 *
 *   prose_wave_open(10000);
 *   prose_wave_write(pcm, n);   (any number of times; the data is copied and queued)
 *   prose_wave_close();         (waits until everything queued has played)
 */
#ifndef PROSE_WAVE_H
#define PROSE_WAVE_H

#include <stddef.h>
#include <stdint.h>

int prose_wave_open(unsigned rate);
int prose_wave_write(const int16_t *pcm, size_t count);
void prose_wave_close(void);

/* The DSP's serial output word -> PCM. The word goes out LSB first into a 12-bit offset-binary DAC (AM6012). The
 * signal rests at code 0x7F0 (silent frames, and the mean of speech), so that is PCM 0. The gain of 15 (not a 4-bit
 * shift, x16) keeps the whole 12-bit range in 16 bits: x16 about 0x7F0 would overflow at codes 0xFF0-0xFFF. */
int16_t prose_dac_to_pcm(uint16_t word);

/* Converts `count` DSP output words and plays them at 10 kHz, returning when done. */
int prose_wave_play_dsp(const int16_t *words, size_t count);

/* Writes 16-bit mono PCM as a WAV file. */
int prose_wav_save(const char *path, const int16_t *pcm, size_t count, unsigned rate);

#endif

#include "prose_wave.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int16_t prose_dac_to_pcm(uint16_t word)
{
	uint16_t r = 0;
	for (int bit = 0; bit < 16; bit++)
		r |= (uint16_t)(((word >> bit) & 1u) << (15 - bit));
	return (int16_t)((((r >> 1) & 0x0FFF) - 0x7F0) * 15);
}

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* mmsystem.h needs windows.h first */
#include <mmsystem.h>

/* Every write becomes one queued buffer; finished buffers are released on the next write or at close. */
typedef struct block {
	WAVEHDR hdr;
	struct block *next;
} block;

static HWAVEOUT out;
static HANDLE done_event;
static block *queued, **queued_tail = &queued;

static void release_done(int wait)
{
	while (queued) {
		while (!(queued->hdr.dwFlags & WHDR_DONE)) {
			if (!wait)
				return;
			WaitForSingleObject(done_event, 100);
		}
		block *b = queued;
		waveOutUnprepareHeader(out, &b->hdr, sizeof b->hdr);
		queued = b->next;
		free(b);
	}
	queued_tail = &queued;
}

int prose_wave_open(unsigned rate)
{
	WAVEFORMATEX fmt = {0};
	fmt.wFormatTag = WAVE_FORMAT_PCM;
	fmt.nChannels = 1;
	fmt.nSamplesPerSec = rate;
	fmt.wBitsPerSample = 16;
	fmt.nBlockAlign = 2;
	fmt.nAvgBytesPerSec = rate * 2;
	done_event = CreateEventA(NULL, FALSE, FALSE, NULL);
	if (!done_event)
		return -1;
	if (waveOutOpen(&out, WAVE_MAPPER, &fmt, (DWORD_PTR)done_event, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
		CloseHandle(done_event);
		out = NULL;
		return -1;
	}
	return 0;
}

int prose_wave_write(const int16_t *pcm, size_t count)
{
	if (!out || !count)
		return -1;
	release_done(0);
	block *b = calloc(1, sizeof *b + count * 2);
	if (!b)
		return -1;
	memcpy(b + 1, pcm, count * 2);
	b->hdr.lpData = (LPSTR)(b + 1);
	b->hdr.dwBufferLength = (DWORD)(count * 2);
	if (waveOutPrepareHeader(out, &b->hdr, sizeof b->hdr) != MMSYSERR_NOERROR) {
		free(b);
		return -1;
	}
	/* a buffer the device did not take never gets WHDR_DONE, so it must not stay queued (close would wait on it) */
	if (waveOutWrite(out, &b->hdr, sizeof b->hdr) != MMSYSERR_NOERROR) {
		waveOutUnprepareHeader(out, &b->hdr, sizeof b->hdr);
		free(b);
		return -1;
	}
	*queued_tail = b;
	queued_tail = &b->next;
	return 0;
}

void prose_wave_close(void)
{
	if (!out)
		return;
	release_done(1);
	waveOutClose(out);
	CloseHandle(done_event);
	out = NULL;
}
#else
int prose_wave_open(unsigned rate)
{
	(void)rate;
	return -1;
}
int prose_wave_write(const int16_t *pcm, size_t count)
{
	(void)pcm;
	(void)count;
	return -1;
}
void prose_wave_close(void)
{
}
#endif

int prose_wave_play_dsp(const int16_t *words, size_t count)
{
	int16_t *pcm = malloc(count * 2 + 2);
	if (!pcm)
		return -1;
	for (size_t i = 0; i < count; i++)
		pcm[i] = prose_dac_to_pcm((uint16_t)words[i]);
	int r = prose_wave_open(10000);
	if (r == 0) {
		r = prose_wave_write(pcm, count);
		prose_wave_close();
	}
	free(pcm);
	return r;
}

int prose_wav_save(const char *path, const int16_t *pcm, size_t count, unsigned rate)
{
	FILE *f = fopen(path, "wb");
	if (!f)
		return -1;
	uint32_t data = (uint32_t)(count * 2), v32;
	uint16_t v16;
	fwrite("RIFF", 1, 4, f);
	v32 = 36 + data;
	fwrite(&v32, 4, 1, f);
	fwrite("WAVEfmt ", 1, 8, f);
	v32 = 16;
	fwrite(&v32, 4, 1, f);
	v16 = 1; /* PCM */
	fwrite(&v16, 2, 1, f);
	fwrite(&v16, 2, 1, f); /* mono */
	fwrite(&rate, 4, 1, f);
	v32 = rate * 2;
	fwrite(&v32, 4, 1, f);
	v16 = 2;
	fwrite(&v16, 2, 1, f);
	v16 = 16;
	fwrite(&v16, 2, 1, f);
	fwrite("data", 1, 4, f);
	fwrite(&data, 4, 1, f);
	fwrite(pcm, 2, count, f);
	return fclose(f) == 0 ? 0 : -1;
}

/* Grabbing audio with prose_speak_to_buffer (API.md): 100 ms chunks through a callback, with an index marker as a
 * bookmark. The chunks are collected and saved as buffer.wav.
 *
 *   speak_buffer [v11] */
#include "prose.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* stands in for wherever the audio goes: a SAPI site, a file, the network */
typedef struct {
	int16_t *pcm;
	size_t len, cap;
	int chunks;
} my_output;

typedef struct {
	my_output *out;
	volatile int cancelled; /* set by another thread to abort */
} job;

static int on_audio(prose_h h, const int16_t *pcm, size_t count, uint32_t pos, void *user)
{
	job *j = user;
	my_output *o = j->out;
	(void)h;
	(void)pos;
	if (o->len + count > o->cap) {
		size_t cap = (o->cap + count) * 2;
		int16_t *p = realloc(o->pcm, cap * sizeof *p);
		if (!p)
			return 1; /* nonzero stops synthesis */
		o->pcm = p;
		o->cap = cap;
	}
	memcpy(o->pcm + o->len, pcm, count * sizeof *pcm); /* copy it out: the buffer is reused for the next chunk */
	o->len += count;
	o->chunks++;
	return j->cancelled;
}

static void on_index(prose_h h, int n, uint32_t pos, void *user)
{
	(void)h;
	(void)user;
	printf("bookmark %d at byte offset %lu\n", n, (unsigned long)pos * 2); /* SAPI: 2 bytes per sample */
}

int main(int argc, char **argv)
{
	int version = argc > 1 && !strcmp(argv[1], "v11") ? PROSE_V11 : PROSE_V341;
	prose_h h;
	if (prose_open(&h, version))
		return 1;
	int16_t buf[1000]; /* 100 ms chunks */
	my_output out = {NULL, 0, 0, 0};
	job j = {&out, 0};
	prose_callbacks cb = {0};
	cb.on_index = on_index;
	prose_set_callbacks(h, &cb, &j);
	int32_t total = prose_speak_to_buffer(h, "Hello \x1B[1i world.", buf, 1000, on_audio, &j);
	if (total < 0) {
		printf("prose_speak_to_buffer: %s\n", prose_error_string(total));
		return 1;
	}
	printf("%ld samples (%.2f s) in %d chunks\n", (long)total, total / 10000.0, out.chunks);
	int err = prose_save_wave("buffer.wav", out.pcm, out.len);
	if (err)
		printf("prose_save_wave: %s\n", prose_error_string(err));
	free(out.pcm);
	prose_close(h);
	return err ? 1 : 0;
}

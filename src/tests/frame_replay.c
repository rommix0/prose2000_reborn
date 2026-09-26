/*
 * Checks the C frame builder against a capture of dsp_build_frame calls from the emulator.
 *
 *   frame_replay CAPTURE.txt
 *
 * Each capture line is one call: "F dd82 dd86 eae4 lfsr silent silent_run mark alt t0..t21 | w0..w39 latch XX",
 * taken on entry, so its frame buffer is the previous build's result. A build happened when the next call's dd82
 * is one higher. The builder starts from its reset state and then evolves on its own. Between builds the firmware
 * may reset it (D8918, D89B8, at utterance boundaries); the test accepts a change of state only if it is such a
 * reset.
 */
#include "frame_build.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	unsigned dd82, lfsr, silent, silent_run, mark, alt, latch;
	uint8_t t[22];
	uint16_t w[40];
} call;

static int parse(const char *line, call *c)
{
	const char *p = line;
	unsigned dd86, eae4;
	int n;
	if (sscanf(p, "F %u %u %u %u %u %u %u %u%n", &c->dd82, &dd86, &eae4, &c->lfsr, &c->silent, &c->silent_run,
	           &c->mark, &c->alt, &n) != 8)
		return -1;
	p += n;
	for (int i = 0; i < 22; i++) {
		unsigned v;
		if (sscanf(p, " %u%n", &v, &n) != 1)
			return -1;
		c->t[i] = (uint8_t)v;
		p += n;
	}
	p = strchr(p, '|');
	if (!p)
		return -1;
	p++;
	for (int i = 0; i < 40; i++) {
		unsigned v;
		if (sscanf(p, " %x%n", &v, &n) != 1)
			return -1;
		c->w[i] = (uint16_t)v;
		p += n;
	}
	return sscanf(p, " latch %x", &c->latch) == 1 ? 0 : -1;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: frame_replay CAPTURE.txt\n");
		return 2;
	}
	static prose_rom rom;
	prose_rom_builtin(&rom);
	FILE *f = fopen(argv[1], "r");
	if (!f)
		return 2;
	size_t cap = 4096, n = 0;
	call *calls = malloc(cap * sizeof *calls);
	char line[2048];
	while (fgets(line, sizeof line, f)) {
		if (n == cap)
			calls = realloc(calls, (cap *= 2) * sizeof *calls);
		if (!parse(line, &calls[n]))
			n++;
	}
	fclose(f);

	frame_builder fb, reset;
	frame_builder_init(&fb, &rom, 0);
	reset = fb;
	size_t builds = 0, bad_frames = 0, bad_state = 0, resets = 0;
	for (size_t i = 0; i + 1 < n; i++) {
		const call *c = &calls[i], *next = &calls[i + 1];
		if (next->dd82 != ((c->dd82 + 1) & 0xFFFF))
			continue;
		fb.latch = (uint8_t)c->latch; /* other code also drives the latch */
		if (memcmp(fb.w, c->w, sizeof fb.w) || fb.lfsr != c->lfsr || (unsigned)fb.silent != c->silent ||
		    (unsigned)fb.silent_run != c->silent_run) {
			/* the only allowed change between builds is a reset */
			int is_reset = !memcmp(reset.w, c->w, sizeof fb.w) && c->lfsr == reset.lfsr && c->silent == 0 &&
			               c->silent_run == 0;
			if (is_reset) {
				resets++;
			} else {
				if (!bad_state)
					printf("state changed between builds before build %zu, and not by a reset\n", builds + 1);
				bad_state++;
			}
			memcpy(fb.w, c->w, sizeof fb.w);
			fb.lfsr = (uint16_t)c->lfsr;
			fb.silent = (int16_t)c->silent;
			fb.silent_run = (int16_t)c->silent_run;
		}
		frame_build(&fb, c->t, (int)c->mark, (int)c->alt);
		builds++;
		if (memcmp(fb.w, next->w, sizeof fb.w)) {
			if (!bad_frames) {
				printf("first mismatch at build %zu (dd82 %u):\n", builds, c->dd82);
				for (int k = 0; k < 40; k++)
					if (fb.w[k] != next->w[k])
						printf("  w%d got %04X want %04X\n", k, fb.w[k], next->w[k]);
			}
			bad_frames++;
			memcpy(fb.w, next->w, sizeof fb.w);
		}
		if (fb.lfsr != next->lfsr || (unsigned)fb.silent != next->silent ||
		    (unsigned)fb.silent_run != next->silent_run || ((fb.latch ^ next->latch) & 0x22)) {
			if (!bad_state)
				printf("first state mismatch at build %zu: lfsr %u/%u silent %d/%u run %d/%u latch %02X/%02X\n",
				       builds, fb.lfsr, next->lfsr, fb.silent, next->silent, fb.silent_run, next->silent_run,
				       fb.latch, next->latch);
			bad_state++;
			fb.lfsr = (uint16_t)next->lfsr;
			fb.silent = (int16_t)next->silent;
			fb.silent_run = (int16_t)next->silent_run;
			fb.latch = (uint8_t)next->latch;
		}
	}
	printf("%zu builds (%zu after a reset), %zu frames differ, %zu state mismatches\n", builds, resets, bad_frames,
	       bad_state);
	return bad_frames || bad_state || !builds;
}

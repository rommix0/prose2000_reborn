/*
 * End-to-end check of the decompiled back end: captured track bytes -> frame_build -> prose_synth -> samples,
 * compared with the emulator's audio for the same run.
 *
 *   chain_replay FRAMES.txt DSPCAPTURE [play]
 *
 * FRAMES.txt is a dsp_build_frame capture (see frame_replay.c) and DSPCAPTURE.log/.pcm a DSP capture of the same
 * run (see dsp_replay.c). Only the timing comes from the DSP log: which frame requests timed out, and when. Every
 * word the DSP receives is produced by the C code: the boot block from DS:5644 and each frame from frame_build.
 * play also plays the C output through the sound card (Windows).
 */
#include "dsp_rom.h"
#include "frame_build.h"
#include "prose_synth.h"
#include "prose_wave.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	unsigned dd82, lfsr, silent, silent_run, mark, alt;
	uint8_t t[22];
} build;

typedef struct {
	unsigned kind, stamp;
	uint16_t word;
} event;

static build *builds;
static size_t n_builds, next_build;
static event *events;
static size_t n_events, next_event;
static frame_builder fb;
static uint16_t queue[64];
static size_t q_len, q_pos;
static size_t mismatched_words;
static const prose_rom *rom_ptr;

static int load_builds(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return -1;
	char line[2048];
	size_t cap = 4096, n = 0;
	build *all = malloc(cap * sizeof *all);
	while (fgets(line, sizeof line, f)) {
		build b;
		unsigned dd86, eae4;
		int k, off;
		if (sscanf(line, "F %u %u %u %u %u %u %u %u%n", &b.dd82, &dd86, &eae4, &b.lfsr, &b.silent, &b.silent_run,
		           &b.mark, &b.alt, &off) != 8)
			continue;
		const char *p = line + off;
		for (int i = 0; i < 22; i++) {
			unsigned v;
			sscanf(p, " %u%n", &v, &k);
			b.t[i] = (uint8_t)v;
			p += k;
		}
		if (n == cap)
			all = realloc(all, (cap *= 2) * sizeof *all);
		all[n++] = b;
	}
	fclose(f);
	/* keep the calls that built a frame */
	builds = malloc(n * sizeof *builds);
	for (size_t i = 0; i + 1 < n; i++)
		if (all[i + 1].dd82 == ((all[i].dd82 + 1) & 0xFFFF))
			builds[n_builds++] = all[i];
	free(all);
	return 0;
}

static int load_events(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return -1;
	size_t cap = 1 << 16;
	events = malloc(cap * sizeof *events);
	unsigned k, st, w;
	while (fscanf(f, "%u %u %x", &k, &st, &w) == 3) {
		if (n_events == cap)
			events = realloc(events, (cap *= 2) * sizeof *events);
		events[n_events++] = (event){k, st, (uint16_t)w};
	}
	fclose(f);
	return 0;
}

/* The DSP asks for a word. The log says whether the 8086 had one at this sample; the value comes from C. */
static int poll(void *user, size_t produced, uint16_t *word)
{
	(void)user;
	if (next_event >= n_events || events[next_event].kind != 1 || events[next_event].stamp != produced)
		return 0;
	if (q_pos == q_len) {
		q_len = q_pos = 0;
		if (next_event == 0) {
			for (int i = 0; i < 9; i++) /* boot block: start word + 8 setup words */
				queue[q_len++] = (uint16_t)prose_ds_table(rom_ptr, 0x5644, i);
		} else {
			if (next_build >= n_builds) {
				fprintf(stderr, "ran out of captured frames\n");
				return 0;
			}
			const build *b = &builds[next_build++];
			/* utterance boundary: the firmware resets the builder (D8918, D89B8) */
			if (b->lfsr != fb.lfsr || b->silent != (unsigned)fb.silent || b->silent_run != (unsigned)fb.silent_run) {
				frame_builder_reset_frame(&fb);
				frame_builder_reset_source(&fb);
			}
			frame_build(&fb, b->t, (int)b->mark, (int)b->alt);
			memcpy(queue, fb.w, sizeof fb.w);
			q_len = 40;
		}
	}
	if (queue[q_pos] != events[next_event].word)
		mismatched_words++;
	*word = queue[q_pos++];
	next_event++;
	return 1;
}

static void timeout(void *user, size_t produced)
{
	(void)user;
	if (next_event < n_events && events[next_event].kind == 2 && events[next_event].stamp == produced)
		next_event++;
}

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: chain_replay FRAMES.txt DSPCAPTURE [play]\n");
		return 2;
	}
	static prose_rom rom;
	char path[1024];
	uint16_t dsp_rom[512];
	prose_rom_builtin(&rom);
	prose_dsp_builtin_data_rom(dsp_rom);
	rom_ptr = &rom;
	snprintf(path, sizeof path, "%s.log", argv[2]);
	if (load_builds(argv[1]) || load_events(path))
		return 2;
	snprintf(path, sizeof path, "%s.pcm", argv[2]);
	FILE *f = fopen(path, "rb");
	if (!f)
		return 2;
	fseek(f, 0, SEEK_END);
	size_t total = (size_t)ftell(f) / 2;
	fseek(f, 0, SEEK_SET);
	int16_t *want = malloc(total * 2), *got = calloc(total, 2);
	if (fread(want, 2, total, f) != total)
		return 2;
	fclose(f);

	frame_builder_init(&fb, &rom, 0);
	prose_synth *s = prose_synth_create(dsp_rom);
	prose_synth_set_host(s, poll, timeout, NULL);
	prose_synth_set_raw_start(s, 1); /* compare with the emulator from sample 0 */
	size_t produced = prose_synth_run(s, got, total);
	prose_synth_destroy(s);

	size_t diff = 0;
	for (size_t i = 0; i < total; i++)
		diff += got[i] != want[i];
	printf("%zu of %zu samples, %zu differ; %zu of %zu frames used, %zu host words differ from the 8086's\n", produced,
	       total, diff, next_build, n_builds, mismatched_words);
	if (argc > 3 && !strcmp(argv[3], "play") && prose_wave_play_dsp(got, produced))
		fprintf(stderr, "cannot open the audio output\n");
	return diff || mismatched_words;
}

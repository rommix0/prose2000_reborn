/*
 * Replays a capture from the emulator through a C model of the DSP and compares the output.
 *
 *   dsp_replay CAPTURE [synth|rec] [play]
 *
 * synth = the readable decompilation (prose_synth.c); rec = the generated instruction-level translation. play also
 * plays the C output through the sound card (Windows).
 *
 * CAPTURE.log holds one host event per line: "1 <sample> <word>" for each word the DSP program read from the 8086
 * and "2 <sample> 0" for each frame request that timed out, stamped with the number of samples output so far.
 * CAPTURE.pcm is the emulator's output (int16). Captures come from a harness built on native/ (not committed).
 */
#ifdef HAVE_DSP_REC
#include "../dsp/dsp_rec.h"
#endif
#include "../dsp/dsp_rom.h"
#include "../dsp/prose_synth.h"
#include "prose_wave.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	unsigned kind, stamp;
	uint16_t word;
} event;

static event *events;
static size_t n_events, next_event;
static int failed;

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

/* The next word is available when the capture read one at this same point in the sample stream. */
static int ready(size_t produced, uint16_t *word)
{
	if (next_event < n_events && events[next_event].kind == 1 && events[next_event].stamp == produced) {
		*word = events[next_event++].word;
		return 1;
	}
	return 0;
}

static void timed_out(size_t produced)
{
	if (next_event < n_events && events[next_event].kind == 2 && events[next_event].stamp == produced) {
		next_event++;
		return;
	}
	if (!failed)
		fprintf(stderr, "unexpected timeout at sample %zu (next event %zu)\n", produced, next_event);
	failed = 1;
}

#ifdef HAVE_DSP_REC
static int rec_poll(dsp_rec *s) { return ready(s->out_n, &s->host_word); }
static void rec_timeout(dsp_rec *s) { timed_out(s->out_n); }
#endif

static int synth_poll(void *user, size_t produced, uint16_t *word) { (void)user; return ready(produced, word); }
static void synth_timeout(void *user, size_t produced) { (void)user; timed_out(produced); }

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: dsp_replay CAPTURE [rec|synth] [play]\n");
		return 2;
	}
	const char *model = argc > 2 && strcmp(argv[2], "play") ? argv[2] : "synth";
	int play = !strcmp(argv[argc - 1], "play");
	char path[1024];
	uint16_t rom[512];
	prose_dsp_builtin_data_rom(rom);
	snprintf(path, sizeof path, "%s.log", argv[1]);
	if (load_events(path)) {
		fprintf(stderr, "cannot read %s\n", path);
		return 2;
	}
	snprintf(path, sizeof path, "%s.pcm", argv[1]);
	FILE *f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "cannot read %s\n", path);
		return 2;
	}
	fseek(f, 0, SEEK_END);
	size_t total = (size_t)ftell(f) / 2;
	fseek(f, 0, SEEK_SET);
	int16_t *want = malloc(total * 2), *got = calloc(total, 2);
	if (fread(want, 2, total, f) != total)
		return 2;
	fclose(f);

	size_t produced;
	if (!strcmp(model, "rec")) {
#ifndef HAVE_DSP_REC
		fprintf(stderr, "built without the instruction-level translation (configure with PROSE_ROM_DIR)\n");
		return 2;
#else
		dsp_rec *s = calloc(1, sizeof *s);
		memcpy(s->rom, rom, sizeof rom);
		s->host_poll = rec_poll;
		s->host_timeout = rec_timeout;
		s->out = got;
		int r = dsp_rec_run(s, (long)total);
		if (r)
			fprintf(stderr, "dsp_rec_run returned %d\n", r);
		produced = s->out_n < total ? s->out_n : total;
#endif
	} else {
		prose_synth *s = prose_synth_create(rom);
		prose_synth_set_host(s, synth_poll, synth_timeout, NULL);
		prose_synth_set_raw_start(s, 1); /* compare with the emulator from sample 0 */
		produced = prose_synth_run(s, got, total);
		prose_synth_destroy(s);
	}

	size_t diff = 0, first = (size_t)-1;
	for (size_t i = 0; i < total; i++)
		if (got[i] != want[i]) {
			if (first == (size_t)-1)
				first = i;
			diff++;
		}
	printf("%s: %zu of %zu samples produced, %zu differ", model, produced, total, diff);
	if (diff)
		printf(" (first at %zu: got %04X want %04X)", first, (uint16_t)got[first], (uint16_t)want[first]);
	printf(", %zu of %zu host events used\n", next_event, n_events);
	if (play && prose_wave_play_dsp(got, produced))
		fprintf(stderr, "cannot open the audio output\n");
	return diff || failed || next_event != n_events;
}

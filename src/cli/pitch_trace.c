/* pitch_trace: speak a text with the v3.4.1 firmware and write how each phoneme's F0 target was built, with the F0
 * track, as tab-separated lines on standard output (PITCH_SYSTEM.md; pitch_graphs/make_graphs.py plots them).
 *
 *   pitch_trace [-v VOICE] [-p PITCH] [-r WPM] [-W] TEXT
 *
 * -W first speaks a sentence that clears the flag left set by the power-up RAM test (DECL_LOW, PITCH_SYSTEM.md §4.6),
 * as a warmed-up board would have. \e in the text is ESC.
 *
 * Output lines:
 *   F  ms  f0  av                  one 10 ms frame: F0 in Hz (0 = unvoiced) and AV in dB
 *   P  ms  len  ph  stress  flags  features  target  line  contour  kind  first  pos  phrase_len  word  events
 *                                  one phoneme as it is played: start and length in ms, the Prose code, stress level
 *                                  (0-3), node flags, feature byte (plane 0), the F0 target and the phrase-line value
 *                                  in Hz, the contour type, phrase kind and "not first" byte, the position in the
 *                                  line, and the symbol before it ('&' '%' word start, punctuation, or '-'). events:
 *                                  name=value,... from f0_target's trace hook. Segments the generator made have '-'
 *                                  for the prosody fields.
 * The phoneme's F0 as played (after values given with phoneme input) is `target`; `events` ends with given=Hz when
 * a given value replaced the rule target. */
#include "prose.h"
#include "pg.h"
#include "prosody.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_EVENTS 24
#define PLAYBACK_CUR 0xC292 /* the playback stage's cursor while it reports a segment (stage_playback_run) */

static const char *const event_name[] = {
    "line",  "register",  "accent", "prenucleus",  "nucleus", "hat",    "dip",    "high",         "end",
    "end_skipped", "clamp", "zero", "monotone", "hold", "type12", "type17", "decl_cleared", "target",
};

typedef struct {
	int node, ch, target, line, contour, kind, first, pos, len, word, nev, ev[MAX_EVENTS][2];
} record;

static record *recs;
static int nrecs, cap, open_rec = -1;
static int by_node[0x10000]; /* record index + 1 of the latest target computed for a node */
static int pending[MAX_EVENTS][2], npending;
static int recording;

static void add_event(record *r, int ev, int value)
{
	if (r->nev < MAX_EVENTS) {
		r->ev[r->nev][0] = ev;
		r->ev[r->nev][1] = value;
		r->nev++;
	}
}

static void on_f0(int node, int event, int value)
{
	if (!recording)
		return;
	if (event == PR_F0_TYPE12 || event == PR_F0_TYPE17 || event == PR_F0_DECL_CLEARED) {
		/* at a word boundary: kept for the next phoneme */
		if (npending < MAX_EVENTS) {
			pending[npending][0] = event;
			pending[npending][1] = value;
			npending++;
		}
		return;
	}
	if (open_rec < 0) {
		if (nrecs == cap) {
			cap = cap ? 2 * cap : 256;
			recs = realloc(recs, (size_t)cap * sizeof *recs);
			if (!recs)
				exit(1);
		}
		record *r = &recs[nrecs];
		memset(r, 0, sizeof *r);
		r->node = node;
		r->ch = node_char(node);
		r->line = -1;
		int prev = rw(PREV_NODE); /* context_load's symbol before this one */
		r->word = prev ? node_char(prev) : '-';
		if (!(feature(r->word, 0) & 8) && !(feature(r->word, 0x80) & 0x40))
			r->word = '-';
		for (int i = 0; i < npending; i++)
			add_event(r, pending[i][0], pending[i][1]);
		npending = 0;
		open_rec = nrecs++;
	}
	record *r = &recs[open_rec];
	if (event == PR_F0_LINE)
		r->line = value;
	if (event != PR_F0_TARGET) {
		add_event(r, event, value);
		return;
	}
	r->target = value;
	r->contour = rw(CONTOUR);
	r->kind = rw(PR_PROSODY) & 0xFF;
	r->first = (rw(PR_PROSODY) >> 8) & 0xFF;
	r->pos = rw(F0_POS);
	r->len = rw(PHRASE_LEN);
	by_node[node & 0xFFFF] = open_rec + 1;
	open_rec = -1;
}

/* segments reported by playback, waiting for their on_phoneme timing */
typedef struct {
	int ch, rec, stress, flags, f0;
} played;
static played queue[4096];
static int q_head, q_tail;
static void (*engine_segment_hook)(int ch);

static void on_segment(int ch)
{
	if (recording && q_tail - q_head < (int)(sizeof queue / sizeof queue[0])) {
		int n = rw(PLAYBACK_CUR), idx = by_node[n & 0xFFFF] - 1;
		played *p = &queue[q_tail++ % (int)(sizeof queue / sizeof queue[0])];
		p->ch = ch;
		p->rec = idx >= 0 && recs[idx].ch == ch ? idx : -1;
		by_node[n & 0xFFFF] = 0;
		p->stress = node_stress(n);
		p->flags = rw(n + N_FLAGS) & 0xFFFF;
		p->f0 = rb(n + N_F0) * 2;
	}
	engine_segment_hook(ch);
}

static void PROSE_CALL on_phoneme(prose_h h, char ph, int ms, uint32_t pos, void *user)
{
	(void)h, (void)user;
	while (q_head < q_tail) {
		played *p = &queue[q_head++ % (int)(sizeof queue / sizeof queue[0])];
		if (p->ch != (unsigned char)ph)
			continue;
		printf("P\t%u\t%d\t%c\t%d\t%d\t%d", pos / 10, ms, ph == ' ' ? '_' : ph, p->stress, p->flags,
		       feature(p->ch, 0));
		if (p->rec < 0) {
			printf("\t%d\t-\t-\t-\t-\t-\t-\t-\t\n", p->f0);
			return;
		}
		const record *r = &recs[p->rec];
		printf("\t%d\t", p->f0);
		if (r->line >= 0)
			printf("%d", r->line);
		else
			putchar('-');
		printf("\t%d\t%d\t%d\t%d\t%d\t%c\t", r->contour, r->kind, r->first, r->pos, r->len, r->word);
		for (int i = 0; i < r->nev; i++)
			printf("%s%s=%d", i ? "," : "", event_name[r->ev[i][0]], r->ev[i][1]);
		if (p->f0 != r->target)
			printf("%sgiven=%d", r->nev ? "," : "", p->f0);
		putchar('\n');
		return;
	}
}

static void PROSE_CALL on_params(prose_h h, const uint8_t *p, uint32_t pos, void *user)
{
	(void)h, (void)user;
	printf("F\t%u\t%u\t%u\n", pos / 10, p[PROSE_F0], p[PROSE_AV]);
}

static int PROSE_CALL no_audio(prose_h h, const int16_t *pcm, size_t count, uint32_t pos, void *user)
{
	(void)h, (void)pcm, (void)count, (void)pos, (void)user;
	return 0;
}

int main(int argc, char **argv)
{
	int voice = -1, pitch = -1, rate = -1, warm = 0, i;
	for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
		if (!strcmp(argv[i], "-W"))
			warm = 1;
		else if (i + 1 < argc && !strcmp(argv[i], "-v"))
			voice = atoi(argv[++i]);
		else if (i + 1 < argc && !strcmp(argv[i], "-p"))
			pitch = atoi(argv[++i]);
		else if (i + 1 < argc && !strcmp(argv[i], "-r"))
			rate = atoi(argv[++i]);
		else
			break;
	}
	if (i != argc - 1) {
		fprintf(stderr, "usage: pitch_trace [-v VOICE] [-p PITCH] [-r WPM] [-W] TEXT\n");
		return 2;
	}
	static char text[8192];
	size_t k = 0;
	for (const char *s = argv[i]; *s && k + 1 < sizeof text; s++) {
		if (s[0] == '\\' && s[1] == 'e') {
			text[k++] = 0x1B;
			s++;
		} else {
			text[k++] = *s;
		}
	}

	prose_h h;
	int err = prose_open(&h, PROSE_V341);
	if (err) {
		fprintf(stderr, "pitch_trace: %s\n", prose_error_string(err));
		return 1;
	}
	engine_segment_hook = pg_segment_hook;
	pg_segment_hook = on_segment;
	pr_f0_hook = on_f0;
	if (warm && prose_speak_to_buffer(h, "The old man walked slowly to the store.", NULL, 0, no_audio, NULL) < 0)
		return 1;
	/* after the warm-up: the end of an utterance refills the stages' settings from the host settings, where
	 * ESC[V's pitch (the voice's default) is not kept (PITCH_SYSTEM.md §2) */
	if (voice >= 0)
		prose_set_voice(h, voice);
	if (pitch >= 0)
		prose_set_pitch(h, pitch);
	if (rate >= 0)
		prose_set_rate(h, rate);

	printf("#\ttext\t%s\n#\tsettings\tvoice=%d\tpitch=%d\trate=%d\twarm=%d\n", argv[i], voice, pitch, rate, warm);
	recording = 1;
	prose_callbacks cb = {0};
	cb.on_phoneme = on_phoneme;
	cb.on_params = on_params;
	prose_set_callbacks(h, &cb, NULL);
	int32_t n = prose_speak_to_buffer(h, text, NULL, 0, no_audio, NULL);
	prose_close(h);
	if (n < 0) {
		fprintf(stderr, "pitch_trace: %s\n", prose_error_string(n));
		return 1;
	}
	return 0;
}

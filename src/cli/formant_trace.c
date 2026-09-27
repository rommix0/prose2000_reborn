/* formant_trace: speak a text with the v3.4.1 firmware and write what the parameter generator did for each segment,
 * with the parameter tracks as played, as tab-separated lines on standard output (FORMANT_SYSTEM.md;
 * formant_graphs/make_graphs.py plots them).
 *
 *   formant_trace [-v VOICE] [-r WPM] [-W] TEXT
 *   formant_trace -T
 *
 * -W first speaks a sentence, as pitch_trace does. \e in the text is ESC. -T prints the target tables instead.
 *
 * Positions are ring frames (10 ms each), counted on across the rebase of the ring (REFERENCE §12.7); the first F
 * line is the start of the utterance. Output lines:
 *   F  pos  b0 ... b21             one frame as played: the 22 track bytes (codings in REFERENCE §11.4)
 *   S  pos  ch  prev  next  dur  stress  flags
 *                                  a segment starts (pos: its first frame on the F1 track); the phoneme and its
 *                                  neighbours ('_' pause), its duration in frames, stress and node flags
 *   R  group  index                the rule applied: group of DS:9376 and the rule's index in it
 *   C  name                        a routine of the rule's function list
 *   E  pos  name  a  b             an event of a routine (pg.h, PG_TR_*): reduction, onglide, aspiration, closure,
 *                                  release
 *   Q  p  type  durB  durF  len  locB  onset  target  weight  locus  pos
 *                                  parameter p's segment struct as it is written (REFERENCE §12.2), in natural units
 *                                  (Hz, dB), with its coarticulation weight (q15) and locus, and its write position
 * -T lines:
 *   T  ch  idx  F1 F2 F3 F4 B1 B2 B3 AV  place  durF  class  [oF1 oF2 oF3 oB1 oB2 oB3 hold glide]
 *                                  the targets of phoneme ch (Hz, dB; F4 for voice 0), its place of articulation
 *                                  (DS:[AEA8]) and the formant transition length DS:9856[place] of a closure, its
 *                                  class as a following sound (DS:98C2), and for diphthong-capable vowels the
 *                                  offglide targets and DS:9720 (hold, q15 of F1's length) and DS:9744
 *   N  F1 F2 F3                    the neutral vowel of vowel reduction
 *   D  k  pull                     the reduction table: pull (q15) for duration*10/16 = k
 *   W  prev  cur  weight           the default weight table DS:9864, classes 0 vowel, 1 voiced, 2 other, 3 closure
 *   K  k  r1 r2 ...                smoothing ramp k (DS:9B9E): the fraction of the way (/256) at each frame
 *   L  d0 ... d21                  the default transition length per parameter, DS:98A4 */
#include "prose.h"
#include "input.h"
#include "pg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int recording, offset, last_read = -1;
static void (*engine_frame_hook)(unsigned pos, int mark, int alt, const uint8_t track[22]);

/* the ring positions go back by 0x400 now and then (param_ring_ctl(2)); count on across that */
static int abs_pos(int raw)
{
	int r = ruw(RING_READ);
	if (last_read >= 0 && r < last_read - 0x200)
		offset += 0x400;
	last_read = r;
	return s16(raw) + offset;
}

static int pchar(int ch) { return ch == ' ' ? '_' : ch; }

static const char *routine_name(int linear)
{
	switch (linear) {
	case 0xD3FFB: return "pg_shift_aspiration";
	case 0xDEBFA: return "pg_voiceless_onset";
	case 0xDEC87: return "pg_after_closure";
	case 0xDEE85: return "pg_sonorant_onset";
	case 0xDF245: return "pg_vowel";
	case 0xDFB5B: return "pg_sonorant_consonant";
	case 0xDFE87: return "pg_obstruent_voicing";
	case 0xE01C0: return "pg_fricative_amps";
	case 0xE067F: return "pg_closure_types";
	case 0xE08EA: return "pg_stop_burst";
	case 0xE2FB8: return "pg_finalize";
	}
	return "?";
}

static void on_trace(int event, int a, int b)
{
	int n;
	if (!recording)
		return;
	switch (event) {
	case PG_TR_SEGMENT:
		n = a;
		printf("S\t%d\t%c\t%c\t%c\t%d\t%d\t%d\n", abs_pos(rw(TRK_POS(P_F1))), pchar(node_char(n)),
		       pchar(node_char(rw(NODE_PREV))), pchar(node_char(rw(NODE_NEXT))), node_dur(n), node_stress(n),
		       ruw(n + N_FLAGS));
		break;
	case PG_TR_RULE:
		printf("R\t%d\t%d\n", a, b);
		break;
	case PG_TR_ROUTINE:
		printf("C\t%s\n", routine_name(a));
		break;
	case PG_TR_REDUCTION:
		printf("E\t%d\treduction\t%d\t0\n", abs_pos(rw(TRK_POS(P_F1))), a);
		break;
	case PG_TR_ONGLIDE:
		printf("E\t%d\tonglide\t%d\t%d\n", abs_pos(rw(TRK_POS(P_F1))), a, b);
		break;
	case PG_TR_ASPIRATION:
		printf("E\t%d\taspiration\t%d\t0\n", abs_pos(rw(TRK_POS(P_AH))), a);
		break;
	case PG_TR_CLOSURE:
		printf("E\t%d\tclosure\t%d\t%d\n", abs_pos(rw(TRK_POS(P_AF))), a, b);
		break;
	case PG_TR_RELEASE:
		printf("E\t%d\trelease\t%d\t%d\n", abs_pos(rw(TRK_POS(P_AV))), a, b);
		break;
	case PG_TR_EMIT:
		for (int p = 0; p < NPARAM; p++)
			printf("Q\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n", p, rw(PARAM(p, F_TYPE)), rw(PARAM(p, F_DURB)),
			       rw(PARAM(p, F_DURF)), rw(PARAM(p, F_LEN)), rw(PARAM(p, F_LOCB)), rw(PARAM(p, F_ONSET)),
			       rw(PARAM(p, F_TARGET)), rw(WEIGHT(p)), rw(LOCUS(p)), abs_pos(rw(TRK_POS(p))));
		break;
	}
}

static void on_frame(unsigned pos, int mark, int alt, const uint8_t t[22])
{
	if (recording) {
		printf("F\t%d", abs_pos((int)pos));
		for (int p = 0; p < 22; p++)
			printf("\t%u", t[p]);
		putchar('\n');
	}
	engine_frame_hook(pos, mark, alt, t);
}

static int PROSE_CALL no_audio(prose_h h, const int16_t *pcm, size_t count, uint32_t pos, void *user)
{
	(void)h, (void)pcm, (void)count, (void)pos, (void)user;
	return 0;
}

static void print_tables(void)
{
	for (int ch = 0x20; ch < 0x7F; ch++) {
		int idx = phoneme_index(ch);
		if (idx < 0 || idx > 57 || (ch != ' ' && phoneme_index(ch) == phoneme_index(' ')))
			continue;
		int place = rsb(rw(0xAEA8) + ch);
		printf("T\t%c\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d", pchar(ch), idx, rb(0x944C + idx) * 4,
		       rb(0x9486 + idx) * 8 + 500, rb(0x94C0 + idx) * 16, rb(0x94FA + idx) * 16 + rw(0x5348),
		       rb(0x9534 + idx) * 2, rb(0x956E + idx) * 2, rb(0x95A8 + idx) * 2, rsb(0x95E2 + idx), place,
		       rsb(0x9856 + place), next_class(idx));
		if (idx < 0x12)
			printf("\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d", rb(0x9690 + idx) * 4, rb(0x96A2 + idx) * 8 + 500,
			       rb(0x96B4 + idx) * 16, rb(0x96C6 + idx) * 2, rb(0x96D8 + idx) * 2, rb(0x96EA + idx) * 2,
			       rw(0x9720 + 2 * idx), rsb(0x9744 + idx));
		putchar('\n');
	}
	printf("N\t%d\t%d\t%d\n", rw(0x985E), rw(0x9860), rw(0x9862));
	for (int k = 0; k < 16; k++)
		printf("D\t%d\t%d\n", k, rw(0x9884 + 2 * k));
	for (int a = 0; a < 4; a++)
		for (int b = 0; b < 4; b++)
			printf("W\t%d\t%d\t%d\n", a, b, rw(0x9864 + 8 * a + 2 * b));
	for (int k = 0; k <= 20; k++) {
		printf("K\t%d", k);
		for (unsigned r = ruw(RAMPS + 2u * (unsigned)k); rb(r) != 0; r++)
			printf("\t%u", rb(r));
		putchar('\n');
	}
	printf("L");
	for (int p = 0; p < NPARAM; p++)
		printf("\t%d", rsb(0x98A4 + p));
	putchar('\n');
}

int main(int argc, char **argv)
{
	int voice = -1, rate = -1, warm = 0, tables = 0, i;
	for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
		if (!strcmp(argv[i], "-W"))
			warm = 1;
		else if (!strcmp(argv[i], "-T"))
			tables = 1;
		else if (i + 1 < argc && !strcmp(argv[i], "-v"))
			voice = atoi(argv[++i]);
		else if (i + 1 < argc && !strcmp(argv[i], "-r"))
			rate = atoi(argv[++i]);
		else
			break;
	}
	if (tables ? i != argc : i != argc - 1) {
		fprintf(stderr, "usage: formant_trace [-v VOICE] [-r WPM] [-W] TEXT\n       formant_trace -T\n");
		return 2;
	}

	prose_h h;
	int err = prose_open(&h, PROSE_V341);
	if (err) {
		fprintf(stderr, "formant_trace: %s\n", prose_error_string(err));
		return 1;
	}
	if (tables) {
		print_tables();
		prose_close(h);
		return 0;
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
	engine_frame_hook = frame_trace_hook;
	frame_trace_hook = on_frame;
	pg_trace_hook = on_trace;
	if (warm && prose_speak_to_buffer(h, "The old man walked slowly to the store.", NULL, 0, no_audio, NULL) < 0)
		return 1;
	if (voice >= 0)
		prose_set_voice(h, voice);
	if (rate >= 0)
		prose_set_rate(h, rate);

	printf("#\ttext\t%s\n#\tsettings\tvoice=%d\trate=%d\twarm=%d\n", argv[i], voice, rate, warm);
	recording = 1;
	int32_t n = prose_speak_to_buffer(h, text, NULL, 0, no_audio, NULL);
	prose_close(h);
	if (n < 0) {
		fprintf(stderr, "formant_trace: %s\n", prose_error_string(n));
		return 1;
	}
	return 0;
}

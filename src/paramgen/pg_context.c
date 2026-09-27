/*
 * Context rules (REFERENCE §12.5): the routines the rule lists call before pg_finalize.
 */
#include "pg.h"

/* DEBFA: a voiceless sound after a sonorant: aspiration starts one frame early (two for a fricative's AF). */
void pg_voiceless_onset(void)
{
	int ch = node_char(rw(NODE_CUR));
	if ((feature(node_char(rw(NODE_PREV)), 0) & 2) && !(feature(ch, 0x100) & 1)) {
		ww(0xEBB2, 1);
		if ((feature(ch, 0) & 0x40) && rw(TRK_POS(P_AF)) >= 2) {
			ww(PARAM(P_AF, F_LEN), rw(PARAM(P_AF, F_LEN)) + 2);
			ww(TRK_POS(P_AF), rw(TRK_POS(P_AF)) - 2);
		}
		pg_shift_aspiration();
	}
	if (node_char(rw(NODE_CUR)) != 'S' || node_char(rw(NODE_NEXT)) != ' ' ||
	    !(feature(node_char(rw(NODE_PREV)), 0x100) & 2))
		ww(PARAM(P_F0, F_TYPE), 4);
	ww(PARAM(P_AV, F_TYPE), 5);
}

/* DEC87: the segment after a stop, nasal or glottal closure. Formants move over DS:9856[place of prev] frames (F1
 * over half as many); the source amplitudes jump (type 4) after a stop and ramp (6) otherwise. */
void pg_after_closure(void)
{
	int prev = node_char(rw(NODE_PREV)), p;
	for (p = P_F1; p <= P_FN; p++) /* DS:[AEA8]: place of articulation per phoneme char */
		ww(PARAM(p, F_DURF), rsb(0x9856 + rsb(rw(0xAEA8) + node_char(rw(NODE_PREV)))));
	ww(PARAM(P_F1, F_DURF), rw(PARAM(P_F1, F_DURF)) / 2 + 1);
	if (feature(prev, 0x80) & 8) { /* affricate */
		ww(PARAM(P_AH, F_TYPE), 6);
		ww(PARAM(P_AF, F_TYPE), 6);
		ww(PARAM(P_AV, F_TYPE), 6);
		for (p = P_A2; p <= P_AB; p++)
			ww(PARAM(p, F_TYPE), 4);
	} else {
		for (p = P_AV; p <= P_AB; p++)
			ww(PARAM(p, F_TYPE), feature(prev, 0) & 0x20 ? 4 : 6);
	}
	for (p = P_F1; p <= P_FN; p++)
		ww(PARAM(p, F_TYPE), 7);
	if ((feature(node_char(rw(NODE_CUR)), 0x100) & 2) && (feature(node_char(rw(NODE_PREV)), 0) & 0x20)) {
		/* stop + vowel: F2 and F3 start from the burst locus */
		ww(PARAM(P_F3, F_TYPE), 6);
		ww(PARAM(P_F2, F_TYPE), 6);
		ww(WEIGHT(P_F3), 0x0CD0);
		ww(WEIGHT(P_F2), 0x0CD0);
		if (node_char(rw(NODE_CUR)) == '3' && node_char(rw(NODE_PREV)) == 'T')
			ww(WEIGHT(P_F4), 0x0CD0);
	}
	if ((feature(node_char(rw(NODE_PREV)), 0) & 0x10) && (feature(node_char(rw(NODE_CUR)), 0) & 4))
		ww(PARAM(P_AV, F_TYPE), 4); /* nasal + voiced */
	if (node_char(rw(NODE_CUR)) == 'p' && node_char(rw(NODE_PREV)) == 'K') {
		for (p = P_A2; p <= P_A6; p++) {
			ww(PARAM(p, F_TYPE), 6);
			ww(PARAM(p, F_DURF), rw(PARAM(p, F_DURF)) + 3);
		}
	}
	if (node_char(rw(NODE_CUR)) == 's' && node_char(rw(NODE_NEXT)) == '@') {
		ww(PARAM(P_AF, F_TYPE), 6);
		ww(PARAM(P_AF, F_DURF), rw(PARAM(P_AF, F_DURF)) + 3);
		for (p = P_A3; p <= P_A6; p++) {
			ww(PARAM(p, F_TYPE), 6);
			ww(PARAM(p, F_DURF), rw(PARAM(p, F_DURF)) + 3);
		}
		if (node_char(rw(NODE_PREV)) == 'P') {
			ww(PARAM(P_A4, F_TYPE), 4);
			ww(PARAM(P_A3, F_TYPE), 4);
		}
	}
}

/* DEE85: a sonorant (vowel, glide, liquid, nasal, h). After a voiceless stop the release delay [EBB4] (voice
 * onset time) lengthens the segment; after a glide the formants move more slowly; after some consonants the
 * parallel amplitudes carry over. */
void pg_sonorant_onset(void)
{
	int cur = node_char(rw(NODE_CUR)), prev = node_char(rw(NODE_PREV)), p, dur_f;
	int cur_idx = phoneme_index(cur);
	if (prev == 'T' && !node_bit(rw(NODE_CUR), 5) && (feature(cur, 0x100) & 2) && cur != 'p') {
		int before = node_char(rw(rw(NODE_PREV) + 2)); /* the node before prev */
		if (before == 'S' || before == 'j')
			ww(0xEBB4, 0x7F); /* "st", "lt": no aspiration */
	}
	if (prev == 'T' && (cur == 'n' || cur == '3'))
		ww(0xEBB4, 2);
	if ((feature(cur, 0x180) & 1) && (feature(prev, 0) & 0x40) && (feature(prev, 0) & 4)) {
		pg_release_onset(); /* glide or liquid after a voiced fricative */
	} else if (!(feature(prev, 0) & 4) && prev != ' ' && prev != 'H' && !node_bit(rw(NODE_PREV), 6) &&
	           !(node_bit(rw(NODE_CUR), 5) && (feature(cur, 0x100) & 2) && prev == 'D') && rw(0xEBB4) != 0x7F) {
		pg_release_onset();
		if ((feature(prev, 0) & 0x20) && node_bit(rw(NODE_PREV), 5) && (feature(cur, 0x100) & 2)) {
			/* stressed vowel after a voiceless stop: add half the release delay */
			int n = rw(NODE_CUR), dur = (rw(0xEBB4) / 2 + (rw(n + N_DUR) & 0xFF)) & 0xFF;
			ww(n + N_DUR, (rw(n + N_DUR) & 0xFF00) | dur);
			for (p = 0; p < NPARAM; p++)
				ww(PARAM(p, F_LEN), rw(PARAM(p, F_LEN)) + rw(0xEBB4) / 2);
		}
	}
	if ((feature(prev, 0) & 0x20) || (feature(prev, 0) & 0x40))
		ww(PARAM(P_AF, F_LEN), rw(PARAM(P_AF, F_LEN)) - rw(0xEBB8));
	if (feature(prev, 0x180) & 1) { /* after a glide or liquid */
		dur_f = (feature(cur, 0x180) & 0x20) && prev == 'Y' ? 11 : 7;
		for (p = P_F1; p <= P_FN; p++)
			ww(PARAM(p, F_DURF), dur_f);
		if (feature(prev, 0x100) & 8)
			ww(PARAM(P_F3, F_DURF), 9);
	}
	if (!(feature(cur, 0x100) & 0x10) && ((feature(prev, 0x200) & 4) || (feature(prev, 0x180) & 0x40))) {
		ww(WEIGHT(P_F1), 0x2CD8);
		ww(WEIGHT(P_F2), 0x2CD8);
		ww(WEIGHT(P_F3), 0x2CD8);
	}
	if ((feature(cur, 0x180) & 0x20) && (feature(prev, 0x180) & 4))
		ww(PARAM(P_F2, F_DURF), rw(PARAM(P_F2, F_DURF)) + 5);
	if (((feature(cur, 0x100) & 2) && prev == 'T') ||
	    (prev == 'K' && (next_class(cur_idx) == 3 || next_class(cur_idx) == 2 || cur == 'a' || cur == 'l' ||
	                     cur == 'W' || cur == 'L' || cur == 'Y' || cur == 'p')) ||
	    (cur == 'i' && prev == 'P') || ((cur == 'R' || (feature(cur, 0x100) & 2)) && prev == 'X') ||
	    (cur == 'R' && prev == 'P' && node_bit(rw(NODE_NEXT), 5) && (feature(node_char(rw(NODE_NEXT)), 0x100) & 2)) ||
	    (cur == 'n' && prev == 'T')) {
		/* keep the consonant's parallel amplitudes */
		for (p = P_A2; p <= P_AB; p++)
			ww(PARAM(p, F_TARGET), rw(LOCUS(p)));
	}
	if (cur == 'R' && prev == 'P' && node_bit(rw(NODE_NEXT), 5) && (feature(node_char(rw(NODE_NEXT)), 0x100) & 2))
		ww(PARAM(P_AB, F_TARGET), 0x36);
}

#define OFFGLIDE(p) (0xEB44u + 2u * (unsigned)((p) - P_F1)) /* diphthong offglide targets, F1-F3 and B1-B3 */
#define NEUTRAL_F1 0x985E  /* 490, 1450, 2500 Hz */
#define REDUCTION 0x9884   /* pull toward the neutral vowel by dur*10/16, q15 */

/* The second half of pg_vowel's diphthong: write the onglide of formant/bandwidth p as a held stretch plus a
 * transition to the offglide, by emitting a temporary segment and then restoring p's struct. */
static void diphthong_onglide(int p, int cur)
{
	int prev_bound = rw(TRK_PREV(p)), dur_f = rw(PARAM(p, F_DURF)), dur_b = rw(PARAM(p, F_DURB));
	int onset = rw(PARAM(p, F_ONSET)), locb = rw(PARAM(p, F_LOCB)), target = rw(PARAM(p, F_TARGET));
	int len = rw(PARAM(p, F_LEN)), pos = rw(TRK_POS(p)), mid;
	ww(PARAM(p, F_TYPE), 7);
	if ((feature(cur, 0x200) & 2) && !node_bit(rw(NODE_CUR), 5)) /* unstressed: onglide a quarter nearer */
		target = s16(target - (s16(target - rw(OFFGLIDE(p))) >> 2));
	mid = s16(rw(OFFGLIDE(p)) + target) / 2;
	if (p == P_F3) {
		if (cur == 'I' || cur == 'y')
			mid -= 200;
		else if (cur == 'U')
			ww(0xEBBC, rw(0xEBBC) - 7);
	} else if ((p == P_F2 && cur == 'I') || cur == 'y') {
		mid += 200; /* for 'y' every parameter but F3 */
	}
	if (rw(0xEBBC) <= 0) {
		mid = target;
	} else {
		if (rw(0xEBBC) >= rw(PARAM(p, F_LEN))) {
			mid = rw(OFFGLIDE(p));
			ww(PARAM(p, F_TYPE), 3);
			ww(0xEBBC, rw(PARAM(p, F_LEN)));
		}
		/* hold the onglide for [EBBC] frames */
		if (p == P_F1)
			track_fill(rw(TRK_BASE(p)), rw(TRK_POS(p)), rw(0xEBBC), target >> 2);
		else if (p == P_F2)
			track_fill(rw(TRK_BASE(p)), rw(TRK_POS(p)), rw(0xEBBC), s16(target - 500) >> 3);
		else if (p == P_F3)
			track_fill(rw(TRK_BASE(p)), rw(TRK_POS(p)), rw(0xEBBC), target >> 4);
		else if (p >= P_B1 && p <= P_B3)
			track_fill(rw(TRK_BASE(p)), rw(TRK_POS(p)), rw(0xEBBC), target >> 1);
		ww(PARAM(p, F_LEN), rw(PARAM(p, F_LEN)) - rw(0xEBBC));
		ww(TRK_POS(p), rw(TRK_POS(p)) + rw(0xEBBC));
	}
	/* then move to the offglide */
	ww(TRK_PREV(p), pos);
	ww(PARAM(p, F_TARGET), rw(OFFGLIDE(p)));
	ww(PARAM(p, F_LOCB), mid);
	ww(PARAM(p, F_ONSET), mid);
	ww(PARAM(p, F_DURF), rw(LOOKAHEAD) < rw(0xEBBA) ? rw(LOOKAHEAD) : rw(0xEBBA));
	ww(PARAM(p, F_DURB), rw(PARAM(p, F_DURF)));
	param_emit_segment(p);
	ww(PARAM(p, F_TARGET), target);
	ww(PARAM(p, F_LEN), len);
	ww(TRK_POS(p), pos);
	ww(PARAM(p, F_TYPE), 3);
	ww(TRK_PREV(p), prev_bound);
	ww(PARAM(p, F_DURF), dur_f);
	ww(PARAM(p, F_DURB), dur_b);
	ww(PARAM(p, F_ONSET), onset);
	ww(PARAM(p, F_LOCB), locb);
}

/* DF245: vowels. Context shifts of F2/F3 (r-colouring, laterals, velars), vowel reduction toward the neutral
 * vowel for short vowels, diphthongs, and stress on AV. */
void pg_vowel(void)
{
	int cur = node_char(rw(NODE_CUR)), prev = node_char(rw(NODE_PREV)), next = node_char(rw(NODE_NEXT));
	if (cur == 'p') { /* release vowel */
		if ((feature(prev, 0) & 4) && (feature(prev, 0) & 0x20))
			ww(PARAM(P_AV, F_TARGET), 0x34);
		else if (prev == 'X')
			ww(PARAM(P_AV, F_TARGET), 0);
	}
	if ((feature(cur, 0x100) & 0x20) && (feature(cur, 0x180) & 0x80) && (feature(next, 0x100) & 0x80))
		ww(OFFGLIDE(P_F2), rb(0x9486 + phoneme_index(cur)) * 8 + 500); /* lax front vowel before a velar */
	if (cur == 'u' && next == 'j')
		ww(PARAM(P_F2, F_TARGET), rw(PARAM(P_F2, F_TARGET)) - 150);
	if (cur == 'b' && (node_char(rw(NODE_PREV)) == 'R' || (feature(next, 0x100) & 2))) {
		ww(PARAM(P_F2, F_TARGET), 0x48C);
	} else if (!(feature(next, 0x100) & 0x40)) {
		if (feature(prev, 0x100) & 8) { /* after r: F3 (and F2 of front vowels) a quarter of the way down */
			ww(PARAM(P_F3, F_TARGET), (rw(PARAM(P_F3, F_TARGET)) * 3 + 1800) / 4);
			ww(OFFGLIDE(P_F3), (rw(OFFGLIDE(P_F3)) * 3 + 1800) / 4);
			if (feature(cur, 0x100) & 0x20) {
				ww(PARAM(P_F2, F_TARGET), (rw(PARAM(P_F2, F_TARGET)) * 3 + 1400) / 4);
				ww(OFFGLIDE(P_F2), (rw(OFFGLIDE(P_F2)) * 3 + 1400) / 4);
			}
		}
	} else if ((feature(cur, 0x180) & 0x40) || (feature(cur, 0x100) & 0x20)) {
		/* before a lateral: F2 300 Hz lower */
		ww(OFFGLIDE(P_F2), rw(OFFGLIDE(P_F2)) - 300);
		if (cur != 'I' && cur != 'y')
			ww(PARAM(P_F2, F_TARGET), rw(PARAM(P_F2, F_TARGET)) - 300);
	}
	if ((feature(next, 0) & 0x20) && (feature(next, 0x100) & 4) && cur == 'U')
		ww(OFFGLIDE(P_F2), rw(OFFGLIDE(P_F2)) + 400);
	if ((feature(prev, 0) & 0x20) && (feature(prev, 0x100) & 0x80) && cur == 'E') {
		ww(PARAM(P_F3, F_TARGET), rw(PARAM(P_F3, F_TARGET)) + 300);
		ww(PARAM(P_F4, F_TARGET), rw(PARAM(P_F4, F_TARGET)) - 200);
		ww(PARAM(P_A5, F_TYPE), 5);
	}
	if (prev == 'P' && cur == 'o') {
		ww(PARAM(P_A3, F_TYPE), 5);
		ww(PARAM(P_A2, F_TYPE), 5);
	}
	if ((feature(prev, 0x180) & 8) && (feature(cur, 0x180) & 0x20))
		ww(PARAM(P_F2, F_DURF), rw(PARAM(P_F2, F_DURF)) + 5);
	if ((feature(cur, 0x180) & 0x40) && (feature(next, 0x100) & 4)) {
		ww(OFFGLIDE(P_F2), rw(OFFGLIDE(P_F2)) - 150);
		if (rw(OFFGLIDE(P_F2)) < rw(PARAM(P_F2, F_TARGET)))
			ww(OFFGLIDE(P_F2), rw(PARAM(P_F2, F_TARGET)));
	}
	if (!(feature(cur, 0x100) & 8) && !(feature(cur, 0x200) & 0x40)) {
		/* vowel reduction: short vowels move toward the neutral vowel */
		unsigned d = (unsigned)node_dur(rw(NODE_CUR)) * 10;
		int k;
		if (d > 0xFF)
			d = 0xFF;
		k = rw(REDUCTION + 2 * (d / 16));
		PG_TRACE(PG_TR_REDUCTION, k, 0);
		for (int i = 0; i < 3; i++) {
			ww(PARAM(P_F1 + i, F_TARGET),
			   rw(PARAM(P_F1 + i, F_TARGET)) + fx_mul_q15(rw(NEUTRAL_F1 + 2 * i) - rw(PARAM(P_F1 + i, F_TARGET)), k));
		}
		for (int i = 0; i < 3; i++)
			ww(OFFGLIDE(P_F1 + i), rw(OFFGLIDE(P_F1 + i)) + fx_mul_q15(rw(NEUTRAL_F1 + 2 * i) - rw(OFFGLIDE(P_F1 + i)), k));
	}
	if (feature(cur, 0x80) & 0x10) {
		/* reduced vowel: F3 is the mean of its target, the previous value and the next phoneme's */
		ww(PARAM(P_F3, F_TARGET), fx_mul_q15(rw(PARAM(P_F3, F_TARGET)) + rw(LOCUS(P_F3)) + rw(0xEB2E), 0x2A48));
		if (cur == 'p' && prev == 'K') {
			ww(PARAM(P_F2, F_TARGET), 0x674);
			ww(PARAM(P_F3, F_TARGET), 0x740);
		}
		ww(PARAM(P_AV, F_TARGET), rw(PARAM(P_AV, F_TARGET)) + (node_bit(rw(NODE_CUR), 5) ? 2 : -3));
		if (cur == 'o' || cur == 'a' || cur == '@' || cur == 'w')
			ww(PARAM(P_AV, F_TARGET), rw(PARAM(P_AV, F_TARGET)) - 3);
	} else {
		if ((feature(cur, 0x180) & 8) && !(feature(prev, 0x100) & 0x10))
			ww(PARAM(P_F2, F_DURF), rw(PARAM(P_F2, F_DURF)) - 2);
		if ((feature(cur, 0x180) & 0x10) && !(cur == 'u' && next == 'j')) {
			/* diphthong: the onglide is held for [EBBC] frames, then moves to the offglide over [EBBA] */
			int f1_len = rw(PARAM(P_F1, F_LEN));
			ww(0xEBBA, s16(s16(s16(f1_len * rw(0xEBBA)) / rsb(rw(0xADE4) + cur)) + rw(0xEBBA)) >> 1);
			ww(0xEBBC, fx_mul_q15(f1_len, rw(0x9720 + 2 * phoneme_index(cur))));
			PG_TRACE(PG_TR_ONGLIDE, rw(0xEBBC), rw(0xEBBA));
			for (int p = P_F1; p <= P_B3; p++)
				if (p != P_F4)
					diphthong_onglide(p, cur);
		}
		if ((feature(node_char(rw(NODE_CUR)), 0x100) & 2) &&
		    (node_char(rw(NODE_PREV)) == 'K' || node_char(rw(NODE_PREV)) == 'G')) {
			ww(PARAM(P_F4, F_TYPE), 6);
			ww(WEIGHT(P_F4), 0);
		}
		ww(PARAM(P_AV, F_TARGET), rw(PARAM(P_AV, F_TARGET)) + (node_bit(rw(NODE_CUR), 5) ? 2 : -3));
		if (cur == 'o' || cur == 'a')
			ww(PARAM(P_AV, F_TARGET), rw(PARAM(P_AV, F_TARGET)) - 3);
	}
	if (next_class(phoneme_index(cur)) != 3 && next_class(phoneme_index(cur)) != 2)
		return;
	if (node_char(rw(NODE_PREV)) == 'G')
		ww(PARAM(P_F1, F_TYPE), 6);
}

/* The next phoneme's F2 target in Hz. */
static int next_f2(void) { return rb(0x9486 + phoneme_index(node_char(rw(NODE_NEXT)))) * 8 + 500; }

/* DFB5B: glides and liquids. R and L take part of the next vowel's F2; formants move over 9 frames (7 for a
 * glide, 5 between two glides). */
void pg_sonorant_consonant(void)
{
	int cur = node_char(rw(NODE_CUR)), prev = node_char(rw(NODE_PREV)), next = rw(NODE_NEXT), dur_f, p;
	if (prev == ' ' && (feature(cur, 0) & 4))
		ww(PARAM(P_AV, F_TARGET), 0x38);
	if (cur == 'R' && prev != 'K' &&
	    !(prev == 'P' && node_bit(next, 5) && (feature(node_char(next), 0x100) & 2))) {
		if (feature(prev, 0x100) & 0x80) { /* after a velar */
			ww(PARAM(P_AV, F_TARGET), 0x37);
			ww(PARAM(P_F2, F_TARGET), 0x474);
			ww(PARAM(P_F3, F_TARGET), 0x578);
		} else {
			int cls = next_class(phoneme_index(node_char(next)));
			if (cls == 2) {
				ww(PARAM(P_AV, F_TARGET), 0x3C);
				ww(PARAM(P_F2, F_TARGET), 0x474);
				ww(PARAM(P_F3, F_TARGET), 0x5A0);
			} else {
				if (cls == 0)
					ww(PARAM(P_AV, F_TARGET), 0x3C);
				/* F2 a quarter of the way to the next phoneme's; F3 400 Hz above */
				ww(PARAM(P_F2, F_TARGET), rw(PARAM(P_F2, F_TARGET)) - fx_mul_q15(rw(PARAM(P_F2, F_TARGET)), 0x2008));
				ww(PARAM(P_F2, F_TARGET), rw(PARAM(P_F2, F_TARGET)) + fx_mul_q15(next_f2(), 0x2008));
				ww(PARAM(P_F3, F_TARGET), rw(PARAM(P_F2, F_TARGET)) + 400);
			}
		}
		if (prev == 'P' && (feature(node_char(next), 0x100) & 2) && !node_bit(next, 5)) {
			ww(PARAM(P_F2, F_TARGET), 0x3E4);
			ww(PARAM(P_F3, F_TARGET), 0x5C0);
		} else if (prev == 's' && node_char(rw(rw(NODE_PREV) + 2)) == 'C') { /* "tr" = C s R */
			ww(PARAM(P_F2, F_TARGET), 0x50C);
			ww(PARAM(P_F3, F_TARGET), 0x6E0);
		}
	} else if (cur == 'L') {
		ww(PARAM(P_AV, F_TARGET), rw(PARAM(P_AV, F_TARGET)) - 3);
		if (feature(prev, 0x100) & 0x80) {
			ww(PARAM(P_F2, F_TARGET), 0x36C);
		} else { /* F2 a tenth of the way to the next phoneme's */
			ww(PARAM(P_F2, F_TARGET), rw(PARAM(P_F2, F_TARGET)) - fx_mul_q15(rw(PARAM(P_F2, F_TARGET)), 0x0CD0));
			ww(PARAM(P_F2, F_TARGET), rw(PARAM(P_F2, F_TARGET)) + fx_mul_q15(next_f2(), 0x0CD0));
		}
	} else if (cur == 'W' && prev == 'K') {
		ww(PARAM(P_F2, F_TARGET), 0x320);
	}
	if ((feature(cur, 0x100) & 0x40) && (feature(prev, 0) & 2) && !(feature(prev, 0) & 0x10)) {
		ww(WEIGHT(P_F3), 0x7350);
		ww(WEIGHT(P_F2), 0x7350);
		ww(WEIGHT(P_F1), 0x7350);
	}
	dur_f = 9;
	if (feature(cur, 0x180) & 1) {
		dur_f = 7;
		if (cur == 'W' && prev == ' ' && node_char(next) == 'E')
			ww(PARAM(P_B3, F_TARGET), 0xFA);
		if (feature(prev, 0x180) & 1)
			dur_f = 5;
	}
	for (p = P_F1; p <= P_FN; p++)
		ww(PARAM(p, F_DURF), dur_f);
	if (feature(cur, 0x100) & 8) { /* r */
		ww(PARAM(P_F2, F_DURF), 6);
		ww(PARAM(P_F3, F_DURF), 6);
		ww(PARAM(P_F4, F_DURF), 6);
	}
	if (cur == 'n') {
		if (node_char(next) == ' ' && node_char(rw(next)) == ' ')
			for (p = P_F2; p <= P_F4; p++)
				ww(PARAM(p, F_DURF), 4);
		if (node_char(rw(NODE_PREV)) == 'D')
			ww(PARAM(P_F2, F_TYPE), 6);
	}
	if (cur == 'Y' && prev == 'G') {
		ww(PARAM(P_F2, F_TARGET), 0x7F4);
		ww(PARAM(P_F3, F_TARGET), 0xA20);
	}
}

/* DFE87: voicing of stops and fricatives: the voice bar of voiced stops, the release delay [EBB8] before a
 * sonorant, and the affricates J+z and C+s+R. */
void pg_obstruent_voicing(void)
{
	int cur = node_char(rw(NODE_CUR)), next = node_char(rw(NODE_NEXT)), prev = node_char(rw(NODE_PREV));
	if ((feature(cur, 0) & 0x20) && (feature(cur, 0) & 4) && !(feature(next, 0x100) & 2) &&
	    !(cur == 'G' && next == 'Y') && next != 'Z' && !(cur == 'D' && next == 'n'))
		ww(PARAM(P_AV, F_TARGET), rw(PARAM(P_AV, F_TARGET)) - 20); /* voiced stop not before a vowel */
	if (feature(cur, 0x180) & 4) {
		if (cur == 's' && !(feature(next, 0x100) & 0x20) && !(feature(next, 0x180) & 0x20))
			ww(PARAM(P_AF, F_DURF), rw(PARAM(P_AF, F_DURF)) - 2);
		else
			ww(PARAM(P_AF, F_DURF), 6);
		if ((feature(cur, 0x80) & 8) && !(feature(prev, 0) & 4))
			ww(PARAM(P_AV, F_TARGET), 0);
	}
	if (feature(next, 0) & 2) {
		/* before a sonorant, frication overlaps its first frames */
		int d = node_dur(rw(NODE_NEXT));
		ww(0xEBB8, 2);
		if (d <= 2)
			ww(0xEBB8, d - 1);
		if (feature(cur, 0) & 0x20)
			ww(0xEBB8, 0);
		if ((cur == 'X' && next == 'R') || cur == 'S')
			ww(0xEBB8, 0);
		ww(PARAM(P_AF, F_LEN), rw(PARAM(P_AF, F_LEN)) + rw(0xEBB8));
	}
	if (!(cur == 'G' && next_class(phoneme_index(next)) == 1) && (feature(cur, 0) & 4) && (feature(cur, 0) & 0x20)) {
		/* voiced stop: the voice bar */
		int keep_a3a4 = 1;
		if (!(feature(prev, 0) & 4))
			ww(PARAM(P_AV, F_TARGET), cur == 'J' ? 0x1E : 0);
		else if (!(feature(prev, 0) & 2))
			ww(PARAM(P_AV, F_TARGET), rw(PARAM(P_AV, F_TARGET)) - 0x1E);
		if (cur == 'D' && (feature(next, 0x100) & 2)) {
			if (prev != ' ' && prev != 'S') {
				if (node_bit(rw(NODE_NEXT), 5))
					ww(PARAM(P_AV, F_TARGET), 0x2B);
				else if ((feature(prev, 0) & 1) && next != 'p')
					keep_a3a4 = 0; /* flap-like d between vowels */
			}
		} else if (!(cur == 'D' && next == 'n') && !node_bit(rw(NODE_NEXT), 5)) {
			keep_a3a4 = 0;
		}
		if (!keep_a3a4) {
			ww(PARAM(P_A4, F_TARGET), 0);
			ww(PARAM(P_A3, F_TARGET), 0);
		}
		if (cur == 'B' && (feature(next, 0x100) & 2))
			ww(PARAM(P_AV, F_TARGET), 0x32);
	}
	if (cur == 's' && next == '@')
		ww(PARAM(P_AF, F_TARGET), rw(PARAM(P_AF, F_TARGET)) - 3);
	if ((cur == 'B' || cur == 'G') && node_char(rw(NODE_PREV)) == 'S')
		ww(PARAM(P_AV, F_TARGET), 0);
	if (cur == 'G' && (feature(next, 0x100) & 2))
		ww(PARAM(P_SRC18, F_TARGET), 0);
	if (cur == 'J' && next == 'z' && next_class(phoneme_index(node_char(rw(rw(NODE_NEXT))))) == 1) {
		/* dZ before a voiced sound: a voiced burst */
		ww(0xEBC8, 0x3C);
		ww(0xEBCA, 0x30);
		ww(PARAM(P_AV, F_TARGET), 0);
		ww(PARAM(P_F1, F_TARGET), 0x11C);
		ww(PARAM(P_F2, F_TARGET), 0x754);
		ww(PARAM(P_F3, F_TARGET), 0x9E0);
		ww(PARAM(P_F4, F_TARGET), 0xCA0);
	}
	if (cur == 'C' && next == 's' && node_char(rw(rw(NODE_NEXT))) == 'R' && node_bit(rw(NODE_CUR), 5)) {
		ww(0xEBC8, 0x3F);
		ww(0xEBCA, 0);
		ww(PARAM(P_A3, F_TARGET), 0x3C);
		ww(PARAM(P_A6, F_TARGET), 0x41);
	}
}

/* E01C0: fricatives. Frication and parallel amplitudes by context (short fricatives are boosted, s before an
 * unstressed vowel, z voicing, the affricate releases of J+z and C+s). */
void pg_fricative_amps(void)
{
	int cur = node_char(rw(NODE_CUR)), prev = node_char(rw(NODE_PREV)), next = node_char(rw(NODE_NEXT)), p;
	if (prev == ' ')
		for (p = P_A2; p <= P_A6; p++)
			ww(PARAM(p, F_TYPE), 4);
	if (next == ' ' && cur != 'Z' && !(feature(cur, 0) & 4))
		ww(PARAM(P_AV, F_TARGET), rw(PARAM(P_AV, F_TARGET)) + 6);
	if (cur == 'z' && prev == 'J') {
		ww(PARAM(P_AV, F_TARGET), next == ' ' ? 0x3C : 0x2F);
		ww(PARAM(P_AF, F_TARGET), 0x3A);
	}
	if (!(feature(prev, 0) & 0x40) && !(feature(prev, 0x100) & 1) && !(feature(next, 0) & 0x40) &&
	    rw(PARAM(P_AF, F_LEN)) < 8) /* short fricative: 1 dB more per missing frame */
		ww(PARAM(P_AF, F_TARGET), rw(PARAM(P_AF, F_TARGET)) + 8 - rw(PARAM(P_AF, F_LEN)));
	if (!(ruw(0xC288) & 1) &&
	    (cur == 'S' || (cur == 'Z' && (!(feature(next, 0) & 4) || next == 'd' || next == ' '))))
		ww(PARAM(P_AF, F_TARGET), rw(PARAM(P_AF, F_TARGET)) - 12);
	if (!(feature(cur, 0) & 4)) {
		int skip;
		if (!(feature(next, 0x100) & 2))
			skip = (cur == 'X' && next == 'R') || (prev == 'j' && cur == 'F' && next == 'S') ||
			       (cur == 'S' && next == ' ') || (cur == 'S' && (feature(next, 0) & 0x40));
		else
			skip = cur == 'S' || !(feature(next, 0x100) & 0x20) || cur == 'X';
		if (!skip)
			ww(PARAM(P_AH, F_TARGET), rw(0xEBC2) - 20);
	}
	if (!node_bit(rw(NODE_NEXT), 5) && cur == 'S' && (feature(next, 0x100) & 2)) {
		/* s before an unstressed vowel */
		ww(PARAM(P_AF, F_TARGET), 0x40);
		ww(PARAM(P_AH, F_TARGET), rw(PARAM(P_AH, F_TARGET)) - 2);
		ww(PARAM(P_A2, F_TARGET), 0);
		ww(PARAM(P_A3, F_TARGET), 0x32);
		ww(PARAM(P_AB, F_TARGET), rw(PARAM(P_AB, F_TARGET)) + 5);
		ww(PARAM(P_A4, F_TARGET), rw(PARAM(P_A4, F_TARGET)) - 2);
		ww(PARAM(P_A5, F_TARGET), rw(PARAM(P_A5, F_TARGET)) - 2);
		ww(PARAM(P_A6, F_TARGET), rw(PARAM(P_A6, F_TARGET)) - 2);
	}
	if (cur == 'S' && (feature(prev, 0) & 4))
		ww(PARAM(P_AV, F_TARGET), 0x1E);
	if (cur == 'Z') {
		if (!(feature(next, 0) & 2)) {
			if (feature(next, 0) & 0x20) { /* z before a stop */
				for (p = P_A4; p <= P_A6; p++)
					ww(PARAM(p, F_TARGET), 0x4A);
				if (feature(next, 0) & 4) {
					ww(PARAM(P_AV, F_TARGET), rw(LOCUS(P_AV)) - 2);
					if (rw(PARAM(P_AV, F_TARGET)) < 0x32)
						ww(PARAM(P_AV, F_TARGET), 0x32);
				}
			}
		} else {
			if (next != 'd') {
				ww(PARAM(P_AV, F_TARGET), rw(LOCUS(P_AV)) - 2);
				if (rw(PARAM(P_AV, F_TARGET)) < 0x32)
					ww(PARAM(P_AV, F_TARGET), 0x32);
			}
			if (prev != ' ') {
				ww(PARAM(P_SRC18, F_TARGET), 10);
				ww(PARAM(P_SRC18, F_DURB), node_dur(rw(NODE_PREV)) < 5 ? node_dur(rw(NODE_PREV)) : 5);
			}
			if (!(feature(prev, 0x100) & 2)) {
				if (prev == ' ') {
					ww(PARAM(P_A4, F_TARGET), 0x48);
					ww(PARAM(P_A6, F_TARGET), 0x48);
				} else {
					for (p = P_A4; p <= P_A6; p++)
						ww(PARAM(p, F_TARGET), 0x4A);
				}
			} else { /* between a vowel and a sonorant */
				ww(PARAM(P_AF, F_TARGET), 0x40);
				ww(PARAM(P_AF, F_DURF), 2);
				ww(PARAM(P_A3, F_TARGET), 0x2D);
				ww(PARAM(P_A2, F_TARGET), 0x2D);
				ww(PARAM(P_A5, F_TARGET), 0x48);
				ww(PARAM(P_A4, F_TARGET), 0x48);
				ww(PARAM(P_A6, F_TARGET), 0x46);
			}
		}
	}
	if (cur == 'z' && prev == 'J' && next_class(phoneme_index(next)) == 1) {
		ww(PARAM(P_AV, F_TARGET), 0x32);
		ww(PARAM(P_AF, F_TARGET), rw(LOCUS(P_AF)));
		ww(PARAM(P_AH, F_TARGET), rw(LOCUS(P_AH)));
		ww(PARAM(P_A3, F_TARGET), 0x43);
		ww(PARAM(P_A4, F_TARGET), 0x3E);
		ww(PARAM(P_A5, F_TARGET), 0x3E);
		for (p = P_F1; p <= P_F4; p++)
			ww(PARAM(p, F_TARGET), rw(LOCUS(p)));
		ww(PARAM(P_F3, F_TARGET), 0x930);
		ww(PARAM(P_SRC18, F_TARGET), 12);
	}
	if (cur == 's' && prev == 'C') {
		if (next == 'R') { /* "tr" */
			ww(PARAM(P_AF, F_TARGET), 0x38);
			ww(PARAM(P_AH, F_TARGET), 0x32);
			ww(PARAM(P_A6, F_TARGET), 0x3C);
			ww(PARAM(P_F2, F_TARGET), 0x574);
			ww(PARAM(P_F3, F_TARGET), 0x790);
			if (node_bit(rw(NODE_CUR), 5)) {
				ww(PARAM(P_AF, F_TARGET), 0x3F);
				ww(PARAM(P_A3, F_TARGET), 0x42);
			}
		} else if (next == ' ') {
			ww(PARAM(P_AF, F_TARGET), 0x3C);
			ww(PARAM(P_F2, F_TARGET), 0x9EC);
			ww(PARAM(P_F3, F_TARGET), 0xB70);
			ww(PARAM(P_F4, F_TARGET), 0xEA0);
		}
	}
}

/* E067F: stops and nasals. Transition types (5/7: blend back into the previous segment), formant durF from
 * DS:9856, burst settings for P and K, then the burst itself. */
void pg_closure_types(void)
{
	int cur = node_char(rw(NODE_CUR)), prev = node_char(rw(NODE_PREV)), next = node_char(rw(NODE_NEXT));
	int p, dur_f;
	if (!(feature(prev, 0x100) & 1)) { /* not after another closure */
		for (p = P_AV; p <= P_F1; p++)
			ww(PARAM(p, F_TYPE), 5);
		for (p = P_F2; p <= P_FN; p++)
			ww(PARAM(p, F_TYPE), 7);
		if (cur == 'T')
			ww(PARAM(P_F1, F_TYPE), 7);
	}
	if (feature(cur, 0) & 4) {
		if (cur == 'G' && next == 'Y')
			ww(PARAM(P_AV, F_TYPE), 4);
		if (feature(cur, 0) & 0x20)
			ww(PARAM(P_AV, F_TYPE), 6);
		else if (feature(cur, 0) & 0x10)
			ww(PARAM(P_AV, F_TYPE), feature(prev, 0) & 4 ? 4 : 6);
	}
	if (cur == 'C' && prev == 'E')
		ww(PARAM(P_F1, F_TYPE), 7);
	if (feature(prev, 0x100) & 0x40) { /* after l */
		ww(PARAM(P_F2, F_TYPE), 5);
		ww(PARAM(P_F3, F_TYPE), 5);
	}
	dur_f = feature(prev, 0x200) & 1 ? 5 : rsb(0x9856 + rsb(rw(0xAEA8) + cur));
	for (p = P_F1; p <= P_FN; p++)
		ww(PARAM(p, F_DURF), dur_f);
	ww(PARAM(P_F1, F_DURF), rw(PARAM(P_F1, F_DURF)) / 2 + 1);
	if (cur == 'n' && next == ' ' && node_char(rw(rw(NODE_NEXT))) == ' ')
		for (p = P_F2; p <= P_F4; p++)
			ww(PARAM(p, F_DURF), 4);
	if (cur == 'P') {
		if (next == 'T') {
			ww(0xEBC6, 0);
			ww(0xEBC8, 0x3C);
			ww(0xEBCA, 0x2B);
			ww(0xEBB6, 2);
			ww(PARAM(P_A2, F_TARGET), 0x41);
		} else if (feature(next, 0) & 0x40) {
			ww(0xEBB6, 1);
		}
	} else {
		if (cur != 'K' || (feature(next, 0) & 0x20))
			return;
		if (!(feature(next, 0x100) & 1)) {
			if (feature(prev, 0x180) & 0x10)
				ww(PARAM(P_F2, F_TYPE), 6);
		} else {
			ww(0xEBC8, 0x39);
			ww(0xEBB6, 2);
			ww(0xEBB4, 7);
			ww(PARAM(P_AB, F_TARGET), 0x2D);
		}
	}
	pg_stop_burst();
}

#define BURST(p) (0xEBC6u + 2u * (unsigned)(p)) /* burst AV, AF, AH; 0x7F = leave the parameter alone */
#define BURST_LEN 0xEBB6

/* E08EA: split a stop into its closure and a burst of [EBB6] frames (REFERENCE §12.5). The closure frames of
 * AV/AF/AH are written here; the segment that remains is the burst, with the burst values as targets, decaying
 * per frame by 1 (K), 3, or 6 after a nasal. After a pause, B and D are prevoiced. */
void pg_stop_burst(void)
{
	int cur = node_char(rw(NODE_CUR)), prev = node_char(rw(NODE_PREV)), next = node_char(rw(NODE_NEXT));
	int decay = 0, closure, p;
	if ((feature(cur, 0) & 4) && (feature(cur, 0) & 0x20) && next == 'Z')
		ww(BURST(P_AV), 0x32);
	if (cur == 'B' && prev == 'S') {
		ww(BURST(P_AV), 0x7F);
		ww(BURST(P_AF), 0x3C);
		ww(BURST_LEN, 2);
	}
	if (cur == 'G' && next == 'Z') {
		ww(BURST(P_AF), 0x3F);
		ww(BURST(P_AH), 0);
		ww(PARAM(P_AB, F_TARGET), 0);
		ww(BURST_LEN, 2);
	}
	if (cur == 'G' && next == 'R' && prev == 'S') {
		ww(BURST(P_AF), 0x3E);
		ww(PARAM(P_AV, F_TARGET), 0);
	}
	if (cur == 'D' && prev == 'S' && node_bit(rw(NODE_NEXT), 5) && (feature(next, 0x100) & 2)) {
		ww(BURST(P_AV), 0x7F);
		ww(BURST(P_AF), 0x3A);
		ww(BURST(P_AH), 0x30);
		ww(BURST_LEN, 2);
	}
	if (cur == 'G' && !node_bit(rw(NODE_NEXT), 5) && (feature(next, 0x100) & 2)) {
		ww(BURST(P_AV), 0x33);
		ww(BURST_LEN, 1);
	}
	if (cur == 'K' && rw(BURST(P_AV)) == 0)
		ww(BURST(P_AV), 0x7F);
	if (!(ruw(0xC288) & 1) && rw(BURST(P_AF)) != 0x7F && rw(BURST(P_AF)) < 0)
		ww(BURST(P_AF), 0);
	if (!node_bit(rw(NODE_CUR), 5) && !(cur == 'D' && (feature(next, 0) & 2)) &&
	    !((feature(cur, 0) & 4) && (feature(cur, 0) & 0x20) && next == 'Z') &&
	    !(cur == 'T' && (feature(next, 0x100) & 2)) && !(cur == 'T' && next == 'n') &&
	    !(cur == 'C' && next == 's' && node_char(rw(rw(NODE_NEXT))) == 'R') && cur != 'B') {
		if (cur == 'K')
			decay = 1;
		else if (cur == 'G')
			ww(BURST(P_AF), 0x38);
		else
			decay = feature(prev, 0) & 0x10 ? 6 : 3;
	}
	closure = rw(PARAM(P_AF, F_LEN));
	if (rw(BURST_LEN) >= closure)
		ww(BURST_LEN, closure >> 1);
	if (rw(BURST_LEN) <= 0)
		ww(BURST_LEN, 1);
	track_decay_back(rw(TRK_BASE(P_AF)), P_AF, rw(TRK_POS(P_AF)), 3, s16(rw(TRK_POS(P_AF)) - rw(TRK_PREV(P_AF))), -20);
	if (cur == 'K' && (((feature(prev, 0x100) & 2) && (feature(prev, 0x180) & 0x20)) ||
	                   ((feature(next, 0x100) & 2) && (feature(next, 0x180) & 0x20))))
		ww(BURST_LEN, 1); /* K next to a back vowel */
	if (rw(BURST_LEN) < closure) {
		closure -= rw(BURST_LEN);
		PG_TRACE(PG_TR_CLOSURE, closure, rw(BURST_LEN));
		for (p = P_AV; p <= P_AH; p++) {
			int track = rw(TRK_BASE(p)), pos = rw(TRK_POS(p)), target;
			if (rw(BURST(p)) == 0x7F)
				continue;
			ww(PARAM(p, F_TARGET), rw(PARAM(p, F_TARGET)) - rw(ATTEN) < 0 ? 0 : rw(PARAM(p, F_TARGET)) - rw(ATTEN));
			target = rw(PARAM(p, F_TARGET));
			if (p == P_AV && node_char(rw(NODE_PREV)) == ' ') {
				/* after a pause: prevoicing */
				if (cur == 'D' && (feature(next, 0x100) & 2) && node_bit(rw(NODE_NEXT), 5) && closure > 3) {
					track_fill(track, pos, closure - 3, target);
					track_fill(track, pos + closure - 3, 3, 0x2B);
				} else if (cur == 'D' && (feature(next, 0x100) & 2) && !node_bit(rw(NODE_NEXT), 5) && closure > 2) {
					track_fill(track, pos, closure - 2, target);
					track_fill(track, pos + closure - 2, 2, 0x2D);
				} else if (cur != 'D' && cur == 'B' && closure > 3) {
					track_fill(track, pos, closure - 3, 0);
					track_fill(track, pos + closure - 3, 3, target);
				} else {
					track_fill(track, pos, closure, target);
					track_blend_fwd(rw(TRK_BASE(P_AV)), rw(TRK_POS(P_AV)), closure >> 1, closure,
					                rw(PARAM(P_AV, F_TARGET)) >> 1);
				}
			} else {
				if (p == P_AV && cur == 'T' && (feature(prev, 0x100) & 2) && (feature(next, 0x100) & 2))
					track_blend_back(track, pos, 6, 6, 0x28); /* intervocalic t */
				else if (p == P_AV && (cur == 'B' || cur == 'D') && (feature(prev, 0) & 4))
					track_blend_back(track, pos, node_dur(rw(NODE_PREV)) >> 1,
					                 s16(rw(TRK_POS(P_AV)) - rw(TRK_PREV(P_AV))),
					                 s16(rw(LOCUS(P_AV)) / 4 + rw(PARAM(P_AV, F_TARGET)) - rw(PARAM(P_AV, F_TARGET)) / 4));
				track_fill(track, pos, closure, rw(PARAM(p, F_TARGET)));
			}
			ww(TRK_POS(p), rw(TRK_POS(p)) + closure);
			ww(PARAM(p, F_LEN), rw(BURST_LEN));
			ww(LOCUS(p), rw(PARAM(p, F_TARGET)));
			ww(BURST(p), rw(BURST(p)) - decay < 0 ? 0 : rw(BURST(p)) - decay);
			ww(PARAM(p, F_TARGET), rw(BURST(p)));
			if (p == P_AV && (next == 'p' || (node_bit(rw(NODE_NEXT), 5) && cur == 'D' &&
			                                  next_class(phoneme_index(next)) == 1)))
				ww(PARAM(p, F_TYPE), 5);
			else
				ww(PARAM(p, F_TYPE), 4);
		}
	}
	ww(0xEBC0, closure);
	if (cur == 'D' && next == 'p')
		ww(PARAM(P_AF, F_TARGET), 0x3E);
}

static void fill_and_advance(int p, int frames, int value)
{
	track_fill(rw(TRK_BASE(p)), rw(TRK_POS(p)), frames, value);
	ww(TRK_POS(p), rw(TRK_POS(p)) + frames);
	ww(PARAM(p, F_LEN), rw(PARAM(p, F_LEN)) - frames);
}

/* E30C1: the release of the previous consonant into a sonorant. [EBB4] frames at the start of the segment get
 * AV 0 (voice onset time), aspiration at [EBC4] and B1 widened to 150 Hz; after a stop, frication fills the burst
 * frames too and F0 is 0 until voicing starts. */
void pg_release_onset(void)
{
	int cur = node_char(rw(NODE_CUR)), prev = node_char(rw(NODE_PREV)), cls = next_class(phoneme_index(cur));
	int n, af;
	if (!node_bit(rw(NODE_PREV), 5)) /* unstressed: 3 dB less aspiration */
		ww(0xEBC4, rw(0xEBC4) - 3 < 0 ? 0 : rw(0xEBC4) - 3);
	if (!(feature(prev, 0) & 0x20)) {
		/* after a fricative or h: a short aspirated onset */
		ww(0xEBB4, 1);
		if (!(feature(cur, 0) & 1))
			ww(0xEBB4, rw(PARAM(P_AH, F_LEN)) / 2);
		if (!((cur == 'R' || (feature(cur, 0x100) & 2)) && prev == 'X'))
			track_decay_back(rw(TRK_BASE(P_AH)), P_AH, rw(TRK_POS(P_AH)), 3,
			                 s16(rw(TRK_POS(P_AH)) - rw(TRK_PREV(P_AH))), s16(rw(0xEBC4) - rw(LOCUS(P_AH))));
	} else {
		/* after a stop */
		if (ruw(0xC288) & 0x10)
			ww(0xEBB4, 2);
		if ((feature(cur, 0x100) & 8) && (feature(prev, 0x100) & 4))
			ww(0xEBC4, rw(0xEBC4) + 9);
		if ((prev == 'T' && ((feature(cur, 0x100) & 2) || cur == 'n')) ||
		    (prev == 'P' && cur == 'R' && (feature(node_char(rw(NODE_NEXT)), 0x100) & 2) && node_bit(rw(NODE_NEXT), 5)))
			ww(0xEBC4, rw(LOCUS(P_AH)));
		if (prev == 'T' && cls == 3 && cur != '3')
			ww(0xEBC4, 0x30);
		if (prev == 'K') {
			if (cls == 0 || cls == 2) {
				ww(0xEBC4, 0x1F);
			} else if (cur == 'a') {
				ww(0xEBC4, 0x30);
			} else if (cls == 1) {
				ww(0xEBC4, 0);
			} else if (cls == 3) {
				ww(0xEBC4, rw(LOCUS(P_AH)));
			} else {
				ww(0xEBC4, 0x1F);
				if (cur == 'W')
					ww(0xEBB4, 7);
				else if (cur == 'Y')
					ww(0xEBB4, 5);
			}
		}
		if (cur == 'p') {
			/* release vowel: a short frication burst */
			af = prev == 'T' ? 0x37 : prev == 'K' ? 0x34 : prev == 'P' && rw(LOCUS(P_F2)) < 2000 ? 0x41 : 0;
			if (af != 0)
				fill_and_advance(P_AF, 3, af);
			ww(PARAM(P_AF, F_TYPE), 6);
		} else if (!node_bit(rw(NODE_PREV), 5)) {
			ww(0xEBB4, 2);
		}
		if ((unsigned)ruw(0xEBB4) > (unsigned)node_dur(rw(NODE_CUR)))
			ww(0xEBB4, node_dur(rw(NODE_CUR)));
		if ((node_bit(rw(NODE_PREV), 5) && (feature(cur, 0) & 1)) ||
		    (!node_bit(rw(NODE_PREV), 5) && prev == 'T' && cur == '@') ||
		    (cur == 'R' && prev == 'P' && (feature(node_char(rw(NODE_NEXT)), 0x100) & 2) && node_bit(rw(NODE_NEXT), 5)) ||
		    (cur == 'L' && prev == 'P') || (cur == 'W' && prev == 'T') || (cur == 'n' && prev == 'T') ||
		    ((cur == 'W' || cur == 'Y' || cur == 'L') && prev == 'K')) {
			/* frication through the release, by place */
			if (prev == 'K') {
				if (cur == 'a')
					af = 0x3C;
				else if (cls == 0 || cls == 1 || cls == 2 || cur == 'W')
					af = 0x32;
				else if (cur == 'Y')
					af = 0x36;
				else if (cur == 'L')
					af = 0x3C;
				else
					af = 0x37;
			} else if (prev == 'T') {
				if (cls == 3 && cur != '3')
					af = 0x36;
				else if ((feature(cur, 0x100) & 2) || cur == 'W' || cur == 'n')
					af = rw(LOCUS(P_AF));
				else
					af = -1; /* not reached with the conditions above */
			} else if (cur == 'R' && node_char(rw(NODE_PREV)) == 'P' &&
			           (feature(node_char(rw(NODE_NEXT)), 0x100) & 2) && node_bit(rw(NODE_NEXT), 5)) {
				af = 0x41;
			} else if (prev == 'P' && (cur == 'L' || cls == 1)) {
				af = rw(LOCUS(P_AF));
			} else {
				af = 0x3C;
			}
			if (af >= 0)
				track_fill(rw(TRK_BASE(P_AF)), rw(TRK_POS(P_AF)), rw(0xEBB4), af);
			ww(TRK_POS(P_AF), rw(TRK_POS(P_AF)) + rw(0xEBB4));
			ww(PARAM(P_AF, F_LEN), rw(PARAM(P_AF, F_LEN)) - rw(0xEBB4));
			if (prev == 'K' && rw(0xEBB4) > 2)
				n = rw(0xEBB4) - 2;
			else if (prev == 'P' && cur == 'L' && rw(0xEBB4) > 3)
				n = rw(0xEBB4) - 3;
			else
				n = rw(0xEBB4) - 1;
			fill_and_advance(P_F0, n, 0);
			ww(LOCUS(P_F0), rw(0xEAFA));
		}
		ww(TRK_PREV(P_AH), rw(TRK_POS(P_AH)));
	}
	if (rw(0xEBB4) > rw(PARAM(P_AH, F_LEN)))
		ww(0xEBB4, rw(PARAM(P_AH, F_LEN)));
	if (rw(0xEBB4) > rw(PARAM(P_AV, F_LEN)))
		ww(0xEBB4, rw(PARAM(P_AV, F_LEN)));
	if (rw(0xEBB4) <= 0)
		return;
	n = rw(0xEBB4);
	PG_TRACE(PG_TR_RELEASE, n, rw(0xEBC4));
	if (node_char(rw(NODE_PREV)) == 'K' && n > 2) {
		track_fill(rw(TRK_BASE(P_AV)), rw(TRK_POS(P_AV)), n - 2, 0);
		track_fill(rw(TRK_BASE(P_AV)), rw(TRK_POS(P_AV)) + n - 2, 2, 0x37);
	} else if (node_char(rw(NODE_PREV)) == 'P' && node_char(rw(NODE_CUR)) == 'L' && n > 2) {
		track_fill(rw(TRK_BASE(P_AV)), rw(TRK_POS(P_AV)), n - 2, 0);
		track_fill(rw(TRK_BASE(P_AV)), rw(TRK_POS(P_AV)) + n - 2, 2, 0x31);
	} else {
		track_fill(rw(TRK_BASE(P_AV)), rw(TRK_POS(P_AV)), n, 0);
	}
	if ((cur == 'R' || (feature(cur, 0x100) & 2)) && prev == 'X') {
		track_fill(rw(TRK_BASE(P_AH)), rw(TRK_POS(P_AH)), n, rw(LOCUS(P_AH)));
		fill_and_advance(P_AF, n, rw(LOCUS(P_AF)));
	} else {
		track_fill(rw(TRK_BASE(P_AH)), rw(TRK_POS(P_AH)), n, rw(0xEBC4));
	}
	track_fill(rw(TRK_BASE(P_B1)), rw(TRK_POS(P_B1)), n, 0x4B);
	ww(LOCUS(P_B1), 0x96);
	ww(LOCUS(P_AH), rw(0xEBC4));
	ww(PARAM(P_AV, F_TYPE), 6);
	ww(PARAM(P_AH, F_LEN), rw(PARAM(P_AH, F_LEN)) - n);
	ww(TRK_POS(P_AH), rw(TRK_POS(P_AH)) + n);
	ww(PARAM(P_AV, F_LEN), rw(PARAM(P_AV, F_LEN)) - n);
	ww(TRK_POS(P_AV), rw(TRK_POS(P_AV)) + n);
	ww(PARAM(P_B1, F_LEN), rw(PARAM(P_B1, F_LEN)) - n);
	ww(TRK_POS(P_B1), rw(TRK_POS(P_B1)) + n);
	if (node_char(rw(NODE_PREV)) == 'K') {
		track_fill(rw(TRK_BASE(P_SRC19)), rw(TRK_POS(P_SRC19)), n, 0x0F);
		track_blend_fwd(rw(TRK_BASE(P_SRC19)), rw(TRK_POS(P_SRC19)), n, n, rw(LOCUS(P_SRC19)));
		ww(LOCUS(P_SRC19), 0x0F);
		ww(TRK_POS(P_SRC19), rw(TRK_POS(P_SRC19)) + n);
		ww(PARAM(P_SRC19, F_LEN), rw(PARAM(P_SRC19, F_LEN)) - n);
	}
}

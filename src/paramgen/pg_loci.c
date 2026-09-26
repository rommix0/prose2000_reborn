/*
 * The locus model (REFERENCE §12.5a): where each parameter's transition starts. pg_finalize ends every rule
 * function list.
 */
#include "pg.h"

/* E1024: coarticulation weights W[p] and fixed consonant loci L[p] by place and manner of prev and cur. The first
 * half handles a consonant after a sound of a lower class (vowel into consonant), the second the reverse. */
void pg_locus_weights(void)
{
	int prev;
	int cur;
	int p;

	prev = node_char(rw(NODE_PREV));
	cur = node_char(rw(NODE_CUR));
	if (rw(CLASS_PREV) < rw(CLASS_CUR)) {
		if (prev != ' ') {
			if (feature(prev, 0x100) & 8) {
				ww(WEIGHT(P_F3), 0x2008);
				ww(WEIGHT(P_F2), 0x2008);
			}
			if ((feature(cur, 0x180) & 4) && !(feature(cur, 0) & 2)) {
				ww(WEIGHT(P_F2), 0x6680);
				ww(PARAM(P_F2, F_DURF), rw(PARAM(P_F2, F_DURF)) + 3);
			}
			if ((feature(prev, 0x180) & 4) && (feature(cur, 0x80) & 8))
				ww(LOCUS(P_F3), rw(LOCUS(P_F3)) + 300);
			if (feature(prev, 0x100) & 1)
				return;
			if (!(feature(cur, 0x100) & 1))
				return;
			if (cur == 'G' && node_char(rw(NODE_PREV)) != ' ') {
				ww(WEIGHT(P_F1), 0x7ED8);
				ww(PARAM(P_F1, F_DURB), 3);
			} else if (cur == 'q' && (feature(node_char(rw(NODE_PREV)), 0x100) & 2)) {
				ww(WEIGHT(P_F1), 0x7ED8);
				ww(PARAM(P_F1, F_DURB), node_dur(rw(NODE_PREV)) - node_dur(rw(NODE_PREV)) / 3);
			} else {
				ww(WEIGHT(P_F1), 0x4010);
			}
			if (!(feature(cur, 0x100) & 0x10) && cur != 'T') {
				ww(WEIGHT(P_F3), 0x7FFE);
				ww(WEIGHT(P_F2), 0x7FFE);
			}
			if (cur == 'T' && node_char(rw(NODE_NEXT)) == '3') {
				ww(WEIGHT(P_F2), 0x2008);
				ww(WEIGHT(P_F4), 0x7FFE);
				ww(WEIGHT(P_F3), 0x7FFE);
			}
			if (cur == 'C' && node_char(rw(NODE_NEXT)) == 's' && node_char(rw(rw(NODE_NEXT))) == ' ') {
				ww(WEIGHT(P_F3), 0xCD0);
				ww(WEIGHT(P_F2), 0xCD0);
			}
			if ((feature(cur, 0x100) & 4) && !(feature(cur, 0) & 1) &&
			    (cur != 'D' || node_char(rw(NODE_NEXT)) != 'p') &&
			    (cur != 'D' || node_char(rw(NODE_PREV)) != 'j') &&
			    (cur != 'D' || node_char(rw(NODE_NEXT)) != 'n') &&
			    (cur != 'T' || !(feature(node_char(rw(NODE_NEXT)), 0x100) & 2))) {
				ww(PARAM(P_F2, F_TARGET), 0x640);
				ww(PARAM(P_F3, F_TARGET), 0xA3C);
				if (!(feature(prev, 0x100) & 0x40)) {
					if (feature(prev, 0x100) & 8)
						ww(PARAM(P_F3, F_TARGET), 0x8FC);
				} else {
					ww(PARAM(P_F2, F_TARGET), 0x41A);
				}
			}
			if ((feature(cur, 0x100) & 0x80) && cur != 'G') {
				ww(WEIGHT(P_F1), 0x7FFE);
				if (feature(prev, 0x100) & 0x10)
					ww(PARAM(P_F3, F_TARGET), rw(PARAM(P_F3, F_TARGET)) + 400);
				if (cur == '~' && !(feature(node_char(rw(NODE_NEXT)), 0) & 2)) {
					ww(PARAM(P_F2, F_TARGET), (rw(LOCUS(P_F2)) + rw(PARAM(P_F2, F_TARGET))) / 2);
					ww(PARAM(P_F3, F_TARGET), (rw(LOCUS(P_F3)) + rw(PARAM(P_F3, F_TARGET))) / 2);
					if (!(feature(prev, 0x100) & 0x20)) {
						ww(PARAM(P_F2, F_TARGET), rw(PARAM(P_F2, F_TARGET)) + 300);
					} else {
						ww(PARAM(P_F2, F_TARGET), 0x4B0);
						ww(PARAM(P_F3, F_TARGET), 0x9C4);
					}
				} else if (cur == '~' && (feature(node_char(rw(NODE_NEXT)), 0) & 2)) {
					ww(LOCUS(P_F2), 900);
					ww(PARAM(P_F2, F_TARGET), 900);
					ww(LOCUS(P_F3), 0x880);
					ww(PARAM(P_F3, F_TARGET), 0x880);
				}
			}
		}
		if (cur == 'K' && (feature(node_char(rw(NODE_PREV)), 0x180) & 0x10))
			ww(WEIGHT(P_F2), 0x19A0);
		if (feature(cur, 0) & 0x10) {
			ww(WEIGHT(P_F1), 0x6018);
			for (p = P_F1; p <= P_FN; p++)
				ww(PARAM(p, F_TYPE), 5);
			if (!(feature(prev, 0) & 2)) {
				if (prev == ' ') {
					ww(WEIGHT(P_AV), 0x3340);
					ww(PARAM(P_AV, F_DURF), 6);
				}
			} else {
				ww(WEIGHT(P_FN), 0x4678);
				ww(PARAM(P_FN, F_DURF), 10);
				ww(WEIGHT(P_B1), 0);
				ww(PARAM(P_B1, F_DURF), 10);
				ww(LOCUS(P_B1), 300);
				ww(WEIGHT(P_F3), 0x7350);
				if (!(feature(cur, 0x100) & 0x10)) {
					if (!(feature(cur, 0x100) & 4)) {
						if (cur == '~' && !(feature(node_char(rw(NODE_NEXT)), 0) & 2)) {
							ww(LOCUS(P_F2), 0x834);
							ww(PARAM(P_F2, F_DURF), 7);
							ww(LOCUS(P_B3), 0xFA);
							ww(LOCUS(P_B2), 0xFA);
							ww(PARAM(P_B3, F_DURF), 10);
							ww(PARAM(P_B2, F_DURF), 10);
						} else if (cur == '~' && (feature(node_char(rw(NODE_NEXT)), 0) & 2)) {
							for (p = P_F2; p <= P_FN; p++)
								ww(LOCUS(p), rw(PARAM(p, F_TARGET)));
						}
					} else {
						ww(LOCUS(P_F3), 0xA28);
						ww(LOCUS(P_F4), 0xE10);
						if ((feature(prev, 0x200) & 1) && !(feature(prev, 0x100) & 0x20))
							ww(LOCUS(P_F2), 0x686);
					}
				} else {
					ww(WEIGHT(P_F4), 0xCD0);
					ww(WEIGHT(P_F4), 0x2670);
					ww(PARAM(P_F4, F_DURF), 9);
					ww(PARAM(P_F4, F_TYPE), 7);
					if (!(feature(prev, 0x100) & 0x20)) {
						ww(WEIGHT(P_F2), 0x7350);
						ww(LOCUS(P_F2), 700);
					}
				}
			}
		}
	}
	if (cur == 's' && !(feature(prev, 0x100) & 0x20) && node_char(rw(NODE_NEXT)) == ' ') {
		ww(PARAM(P_F3, F_TYPE), 5);
	} else {
		if (prev != ' ') {
			if (feature(cur, 0x100) & 8) {
				ww(WEIGHT(P_F3), 0x6018);
				ww(WEIGHT(P_F2), 0x6018);
			}
			if (cur == 'R' && prev == 's' && node_char(rw(rw(NODE_PREV) + 2)) == 'C') {
				ww(WEIGHT(P_F3), 0x7ED8);
				ww(WEIGHT(P_F2), 0x7ED8);
			} else if ((feature(prev, 0x180) & 4) && !(feature(prev, 0) & 2)) {
				ww(WEIGHT(P_F2), 0x19A0);
				ww(PARAM(P_F2, F_DURF), rw(PARAM(P_F2, F_DURF)) + 3);
			}
			if ((feature(cur, 0x180) & 4) && (feature(prev, 0x80) & 8) &&
			    (cur != 's' || (prev != 'C' || !(feature(node_char(rw(NODE_NEXT)), 0x100) & 2))) &&
			    (cur != 's' || (prev != 'C' || node_char(rw(NODE_NEXT)) != 'R')) &&
			    (cur != 'z' || (prev != 'J' || next_class(phoneme_index(node_char(rw(NODE_NEXT)))) != 1))) {
				ww(PARAM(P_F3, F_TARGET), rw(PARAM(P_F3, F_TARGET)) + 300);
			}
			if ((feature(cur, 0x100) & 2) && (feature(prev, 0) & 0x40)) {
				if (!(feature(cur, 0x100) & 0x20) && !(feature(cur, 0x180) & 0x20) && prev == 'S') {
					ww(WEIGHT(P_F3), 0xCD0);
					ww(WEIGHT(P_F2), 0xCD0);
				} else {
					ww(WEIGHT(P_F3), 0x19A0);
					ww(WEIGHT(P_F2), 0x19A0);
				}
			}
			if ((feature(cur, 0x100) & 2) && prev == 'S')
				ww(PARAM(P_AF, F_TYPE), 4);
			if ((feature(prev, 0) & 0x10) && !(feature(cur, 0) & 2)) {
				for (p = P_F2; p <= P_F4; p++)
					ww(PARAM(p, F_DURF), 5);
				for (p = P_B1; p <= P_FN; p++)
					ww(PARAM(p, F_DURF), 2);
			}
			if (feature(cur, 0x100) & 1)
				return;
			if (!(feature(prev, 0x100) & 1))
				return;
			if ((feature(prev, 0) & 0x20) && (feature(prev, 0) & 4) && (feature(cur, 0x100) & 2))
				ww(PARAM(P_AF, F_TYPE), 5);
			ww(WEIGHT(P_F1), 0x4010);
			ww(WEIGHT(P_F3), 0);
			ww(WEIGHT(P_F2), 0);
			if (feature(prev, 0x100) & 0x10) {
				if (prev != 'P' || cur != 'S') {
					ww(WEIGHT(P_F2), 0x19A0);
					ww(WEIGHT(P_F3), 0x59B0);
				}
				if (feature(cur, 0x100) & 0x20) {
					ww(WEIGHT(P_F2), 0x4010);
					ww(WEIGHT(P_F3), 0x19A0);
					if (feature(prev, 0) & 0x20)
						ww(WEIGHT(P_F2), 0xCD0);
				}
				if (feature(cur, 0x100) & 8) {
					ww(WEIGHT(P_F2), 0x19A0);
					ww(LOCUS(P_F3), 0x6D6);
				}
			}
			if (feature(prev, 0x100) & 4) {
				if (!(feature(prev, 0) & 0x10) && prev != 'D' && prev != 'T')
					ww(LOCUS(P_F2), 0x640);
				if (prev != 'T')
					ww(LOCUS(P_F3), 0xA3C);
				if ((feature(cur, 0x100) & 8) && prev != 'D')
					ww(LOCUS(P_F3), 0x8FC);
			}
			if (feature(prev, 0x100) & 0x80) {
				ww(WEIGHT(P_F1), 0);
				if ((feature(cur, 0x100) & 0x10) && !(feature(cur, 0) & 2))
					ww(LOCUS(P_F3), rw(LOCUS(P_F3)) + 400);
				if (cur == 'R') {
					ww(PARAM(P_F3, F_TYPE), 7);
					ww(PARAM(P_F2, F_TYPE), 7);
				}
				if (prev == '~' && (cur != 'p' || node_char(rw(NODE_NEXT)) != ' ')) {
					ww(LOCUS(P_F2), (rw(PARAM(P_F2, F_TARGET)) + rw(LOCUS(P_F2))) / 2);
					ww(LOCUS(P_F3), (rw(PARAM(P_F3, F_TARGET)) + rw(LOCUS(P_F3))) / 2);
					if (!(feature(cur, 0x100) & 0x20))
						ww(LOCUS(P_F2), rw(LOCUS(P_F2)) + 300);
				}
			}
		}
		if ((feature(prev, 0) & 0x10) && (feature(cur, 0) & 2)) {
			ww(WEIGHT(P_F1), 0x2008);
			if (!(feature(cur, 0x180) & 0x10))
				for (p = P_F1; p <= P_F3; p++)
					ww(PARAM(p, F_TYPE), 6);
			else
				for (p = P_F1; p <= P_F3; p++)
					ww(PARAM(p, F_TYPE), 2);
			for (p = P_F4; p <= P_FN; p++)
				ww(PARAM(p, F_TYPE), 6);
			if ((prev != '~' || cur != 'p') || node_char(rw(NODE_NEXT)) != ' ') {
				ww(WEIGHT(P_FN), 0x4CE0);
				ww(PARAM(P_FN, F_DURF), 9);
				ww(WEIGHT(P_B1), 0);
				ww(PARAM(P_B1, F_DURF), 9);
				ww(LOCUS(P_B1), 300);
				ww(WEIGHT(P_F3), 0xCD0);
			}
			if (!(feature(prev, 0x100) & 0x10)) {
				if (!(feature(prev, 0x100) & 4)) {
					if (prev == '~' && (cur != 'p' || node_char(rw(NODE_NEXT)) != ' ')) {
						if (cur != 'W') {
							ww(LOCUS(P_F2), 0x834);
							ww(PARAM(P_F2, F_DURF), 5);
							ww(LOCUS(P_B3), 0xFA);
							ww(LOCUS(P_B2), 0xFA);
							ww(PARAM(P_B3, F_DURF), 9);
							ww(PARAM(P_B2, F_DURF), 9);
						}
					} else if (prev == '~' && (cur == 'p' && node_char(rw(NODE_NEXT)) == ' ')) {
						for (p = P_F2; p <= P_FN; p++) {
							ww(PARAM(p, F_TARGET), rw(LOCUS(p)));
							ww(PARAM(p, F_TYPE), 4);
						}
					}
				} else {
					ww(LOCUS(P_F3), 0xA28);
					ww(LOCUS(P_F4), 0xD48);
					if ((feature(cur, 0x200) & 1) && !(feature(cur, 0x100) & 0x20))
						ww(LOCUS(P_F2), 0x686);
				}
			} else {
				ww(WEIGHT(P_F4), 0x7350);
				if (!(feature(cur, 0x100) & 0x20)) {
					ww(WEIGHT(P_F2), 0xCD0);
				} else if (feature(cur, 0x200) & 1) {
					ww(WEIGHT(P_F4), 0x4010);
					ww(PARAM(P_F4, F_DURF), 5);
					ww(PARAM(P_F4, F_TYPE), 7);
				}
			}
		}
	}
	if ((feature(cur, 0) & 1) && (feature(prev, 0) & 4) && (node_bit(rw(NODE_PREV), 5)) &&
	    !node_bit(rw(NODE_PREV), 6) && (node_bit(rw(NODE_CUR), 5)) && !node_bit(rw(NODE_CUR), 6)) {
		ww(WEIGHT(P_F0), 0x4010);
		ww(PARAM(P_F0, F_TYPE), 7);
	}
}

/* E1A49: more place-specific loci and targets: W after an alveolar stop, consonants after r, velars and l. */
void pg_consonant_loci(void)
{
	int prev;
	int cur;

	prev = node_char(rw(NODE_PREV));
	cur = node_char(rw(NODE_CUR));
	if (cur == 'W' && (feature(prev, 0x100) & 4) && (feature(prev, 0) & 0x20)) {
		ww(LOCUS(P_F2), 0x4B0);
		ww(LOCUS(P_F3), 0x802);
		ww(LOCUS(P_F4), 0x9C4);
		ww(WEIGHT(P_F4), 0);
		ww(PARAM(P_F4, F_DURF), 3);
	}
	if ((feature(prev, 0x100) & 8) || (feature(prev, 0x200) & 2)) {
		if ((!(feature(cur, 0x100) & 4) || ((feature(cur, 0) & 0x40) || cur == 'T')) ||
		    (cur == 'D' && node_char(rw(NODE_NEXT)) == 'n')) {
			if ((cur == 'K' || cur == '~') &&
			    (cur != 'K' ||
			     (!(feature(node_char(rw(NODE_NEXT)), 0x100) & 2) && node_char(rw(NODE_NEXT)) != 'p'))) {
				ww(PARAM(P_F2, F_TARGET), 0x6A4);
				ww(PARAM(P_F3, F_TARGET), 0x76C);
			}
		} else {
			ww(PARAM(P_F4, F_TARGET), rw(PARAM(P_F4, F_TARGET)) - 500);
			ww(PARAM(P_F2, F_TARGET), 0x73A);
			ww(PARAM(P_F3, F_TARGET), 0x898);
		}
		if (feature(cur, 0x100) & 2) {
			ww(PARAM(P_F3, F_DURF), 5);
			if (cur == 'E' && prev == 'R')
				ww(PARAM(P_AV, F_DURF), 6);
		}
	}
	if (!(feature(cur, 0x100) & 8)) {
		if ((!(feature(prev, 0x180) & 0x40) || !(feature(cur, 0x100) & 4)) ||
		    (cur == 'T' || (cur == 'S' && (feature(node_char(rw(NODE_NEXT)), 0x100) & 2)))) {
			if ((feature(prev, 0x100) & 4) && (feature(prev, 0) & 0x10) && (feature(cur, 0x180) & 8))
				ww(LOCUS(P_F2), 0x73A);
		} else {
			ww(PARAM(P_F2, F_TARGET), 0x76C);
		}
	} else if (!(feature(prev, 0x100) & 0x10)) {
		if ((feature(prev, 0x100) & 4) && prev != 'D' && (prev != 'S' || cur != '3'))
			ww(LOCUS(P_F3), 0x834);
	} else {
		ww(LOCUS(P_F3), 0x6A4);
	}
}

/* E1C1A: onset = locB = W·target + (1 − W)·L for the formants, bandwidths, FN and F0, plus transition lengths. */
void pg_apply_loci(void)
{
	int p, a, b;

	if (node_char(rw(NODE_CUR)) == 't') {
		/* flap: F1-F3 are the mean of the previous value, its own target and the next phoneme's (0x2A48 = 1/3) */
		for (p = P_F1; p <= P_F3; p++)
			ww(PARAM(p, F_TARGET),
			   fx_mul_q15(rw(PARAM(p, F_TARGET)) + rw(LOCUS(p)) + rw(0xEB2A + 2 * (p - P_F1)), 0x2A48));
	}
	if ((feature(node_char(rw(NODE_PREV)), 0x180) & 8) && !(feature(node_char(rw(NODE_CUR)), 0) & 0x10) &&
	    !(feature(node_char(rw(NODE_CUR)), 0x200) & 0x10) && !(feature(node_char(rw(NODE_CUR)), 0x100) & 0x10) &&
	    !(feature(node_char(rw(NODE_CUR)), 0x100) & 0x80)) {
		ww(PARAM(P_F2, F_DURF), rw(PARAM(P_F2, F_DURF)) - 2);
	}
	if (0 < rw(PARAM(P_AV, F_TARGET)) || 0 < rw(PARAM(P_AH, F_TARGET))) {
		if (rw(0x5358 + 2 * rw(VOICE)) < rw(PARAM(P_F4, F_TARGET)))
			ww(PARAM(P_F4, F_TARGET), rw(0x5358 + 2 * rw(VOICE)));
		for (p = P_F1; p <= P_F3; p++) /* F(n+1) >= F(n) + 200 */
			if (rw(PARAM(p + 1, F_TARGET)) < rw(PARAM(p, F_TARGET)) + 200)
				ww(PARAM(p + 1, F_TARGET), rw(PARAM(p, F_TARGET)) + 200);
	}
	for (p = 0; p < NPARAM; p++) {
		if (rw(PARAM(p, F_DURF)) > rw(LOOKAHEAD))
			ww(PARAM(p, F_DURF), rw(LOOKAHEAD));
		ww(PARAM(p, F_DURB), rw(PARAM(p, F_DURF)));
		if (rw(PARAM(p, F_LEN)) > 60)
			ww(PARAM(p, F_LEN), 60);
		if (p >= P_F1 && p <= P_F0) {
			a = fx_mul_q15(rw(PARAM(p, F_TARGET)), rw(WEIGHT(p)));
			b = fx_mul_q15(rw(LOCUS(p)), 0x7FFE - rw(WEIGHT(p)));
			ww(PARAM(p, F_ONSET), a + b);
			ww(PARAM(p, F_LOCB), a + b);
		}
	}
	if ((feature(node_char(rw(NODE_CUR)), 0x80) & 8) && !(feature(node_char(rw(NODE_PREV)), 0) & 4) &&
	    node_char(rw(NODE_CUR)) == 'J') {
		ww(PARAM(P_AV, F_DURF), 8);
	}
	if (node_char(rw(NODE_CUR)) == 'R' && node_char(rw(NODE_NEXT)) == 'E')
		ww(PARAM(P_AV, F_DURF), 5);
	if (node_char(rw(NODE_CUR)) == 'G' && node_char(rw(NODE_NEXT)) == 'p')
		ww(PARAM(P_F2, F_DURB), 7);
	if (node_char(rw(NODE_CUR)) == 'F' && node_char(rw(NODE_PREV)) == ' ') {
		ww(PARAM(P_AF, F_DURF), 7);
		ww(PARAM(P_AB, F_DURF), 7);
	}
	if (!(feature(node_char(rw(NODE_CUR)), 0) & 0x40) || !(feature(node_char(rw(NODE_PREV)), 0x100) & 2)) {
		if ((feature(node_char(rw(NODE_PREV)), 0) & 0x40) && (feature(node_char(rw(NODE_CUR)), 0x100) & 2)) {
			if (!(feature(node_char(rw(NODE_PREV)), 0x200) & 0x10) &&
			    !(feature(node_char(rw(NODE_PREV)), 0x100) & 0x10)) {
				if (feature(node_char(rw(NODE_PREV)), 0x100) & 4) {
					ww(PARAM(P_F3, F_DURB), 3);
					ww(PARAM(P_F2, F_DURB), 3);
					ww(PARAM(P_F3, F_DURF), 8);
					ww(PARAM(P_F2, F_DURF), 8);
					ww(PARAM(P_A4, F_DURF), 7);
					ww(PARAM(P_A3, F_DURF), 7);
					ww(PARAM(P_A2, F_DURF), 7);
					ww(PARAM(P_A6, F_DURF), 7);
					ww(PARAM(P_A5, F_DURF), 7);
					if (feature(node_char(rw(NODE_PREV)), 0) & 4) {
						ww(PARAM(P_F3, F_DURF), rw(PARAM(P_F3, F_DURF)) + 2);
						ww(PARAM(P_F2, F_DURF), rw(PARAM(P_F3, F_DURF)));
					}
				}
			} else {
				ww(PARAM(P_F3, F_DURB), 2);
				ww(PARAM(P_F2, F_DURB), 2);
				if (feature(node_char(rw(NODE_PREV)), 0) & 4) {
					ww(PARAM(P_F3, F_DURB), 3);
					ww(PARAM(P_F2, F_DURB), 3);
					ww(PARAM(P_AV, F_DURF), 5);
				}
				if (node_char(rw(NODE_PREV)) == 'X')
					ww(PARAM(P_F1, F_DURB), 2);
			}
			if (node_char(rw(NODE_PREV)) == 's')
				ww(PARAM(P_AF, F_DURF), 5);
		}
	} else {
		ww(PARAM(P_AV, F_DURB), 5);
		if (!(feature(node_char(rw(NODE_CUR)), 0x200) & 0x10) &&
		    !(feature(node_char(rw(NODE_CUR)), 0x100) & 0x10)) {
			if (feature(node_char(rw(NODE_CUR)), 0x100) & 4) {
				ww(PARAM(P_F3, F_DURB), 6);
				ww(PARAM(P_F2, F_DURB), 6);
				ww(PARAM(P_F3, F_DURF), 2);
				ww(PARAM(P_F2, F_DURF), 2);
			}
		} else {
			ww(PARAM(P_F3, F_DURB), 7);
			ww(PARAM(P_F2, F_DURB), 7);
			ww(PARAM(P_F3, F_DURF), 2);
			ww(PARAM(P_F2, F_DURF), 2);
			ww(PARAM(P_AV, F_DURF), 4);
		}
	}
	if (feature(node_char(rw(NODE_PREV)), 0) & 0x20) {
		if (!(feature(node_char(rw(NODE_PREV)), 0x100) & 0x80)) {
			if (!(feature(node_char(rw(NODE_PREV)), 0x100) & 0x10)) {
				if (feature(node_char(rw(NODE_PREV)), 0x100) & 4) {
					if (node_char(rw(NODE_PREV)) == 'D') {
						if (feature(node_char(rw(NODE_CUR)), 0x100) & 0x20) {
							ww(PARAM(P_F3, F_DURB), 0);
							ww(PARAM(P_F2, F_DURB), 0);
							ww(PARAM(P_F3, F_DURF), 5);
							ww(PARAM(P_F2, F_DURF), 5);
						}
						if ((node_char(rw(NODE_CUR)) == 'o' ||
						     node_char(rw(NODE_CUR)) == 'u') ||
						    node_char(rw(NODE_CUR)) == 'b') {
							ww(PARAM(P_AV, F_DURF), 4);
							ww(PARAM(P_F3, F_DURF), 5);
							ww(PARAM(P_F2, F_DURF), 5);
						}
						if (next_class(phoneme_index(node_char(rw(NODE_CUR)))) == 0)
							ww(PARAM(P_F2, F_ONSET), 0x784);
					}
					if (node_char(rw(NODE_PREV)) == 'T' &&
					    (next_class(phoneme_index(node_char(rw(NODE_CUR)))) == 0 ||
					     next_class(phoneme_index(node_char(rw(NODE_CUR)))) == 1) &&
					    node_char(rw(NODE_PREV)) == 'T' &&
					    (feature(node_char(rw(NODE_CUR)), 0x100) & 0x20) &&
					    (feature(node_char(rw(NODE_CUR)), 0x200) & 1)) {
						ww(PARAM(P_F3, F_ONSET), 0xA8C);
						ww(PARAM(P_F3, F_LOCB), 0xA8C);
					}
				}
			} else {
				ww(PARAM(P_F3, F_DURB), 0);
				ww(PARAM(P_F2, F_DURB), 0);
				ww(PARAM(P_AV, F_DURF), 2);
				if (node_char(rw(NODE_PREV)) == 'B' && (feature(node_char(rw(NODE_CUR)), 0x100) & 2)) {
					ww(PARAM(P_F1, F_DURF), 3);
					ww(PARAM(P_F3, F_DURF), 3);
					ww(PARAM(P_F2, F_DURF), 3);
					if (node_char(rw(NODE_CUR)) == 'b' || node_char(rw(NODE_CUR)) == 'u') {
						ww(PARAM(P_F3, F_DURF), 3);
						ww(PARAM(P_F2, F_DURF), 3);
					}
				}
				if (node_char(rw(NODE_PREV)) == 'P' && node_char(rw(NODE_CUR)) == 'o') {
					ww(PARAM(P_F1, F_DURF), 3);
					ww(PARAM(P_F2, F_DURF), 3);
					ww(PARAM(P_F3, F_DURF), 3);
				}
			}
		} else {
			if (node_char(rw(NODE_CUR)) != 'R' &&
			    (node_char(rw(NODE_PREV)) != 'K' || !(feature(node_char(rw(NODE_CUR)), 0) & 0x20))) {
				ww(PARAM(P_F3, F_DURB), 0);
				ww(PARAM(P_F2, F_DURB), 0);
			}
			ww(PARAM(P_AV, F_DURF), 5);
			if (node_char(rw(NODE_PREV)) == 'K') {
				ww(PARAM(P_AV, F_DURF), 3);
				if (node_char(rw(NODE_CUR)) == 'b' || node_char(rw(NODE_CUR)) == 'u')
					ww(PARAM(P_F3, F_DURF), 6);
				if (node_char(rw(NODE_CUR)) == 'E') {
					ww(PARAM(P_F2, F_DURF), 6);
					ww(PARAM(P_F3, F_DURF), 6);
				}
			} else if (node_char(rw(NODE_PREV)) == 'G') {
				ww(PARAM(P_F3, F_DURB), 0);
				ww(PARAM(P_F2, F_DURB), 0);
				if (next_class(phoneme_index(node_char(rw(NODE_CUR)))) == 2 ||
				    next_class(phoneme_index(node_char(rw(NODE_CUR)))) == 3) {
					for (p = P_F1; p <= P_F4; p++)
						ww(PARAM(p, F_DURF), 5);
				} else {
					ww(PARAM(P_F3, F_DURF), 7);
					ww(PARAM(P_F2, F_DURF), 7);
				}
				if (node_char(rw(NODE_CUR)) == 'o') {
					ww(PARAM(P_AV, F_DURF), 4);
				} else if (node_char(rw(NODE_CUR)) == 'E') {
					ww(PARAM(P_AV, F_DURF), 4);
					ww(PARAM(P_F3, F_DURF), 10);
				}
			}
		}
	}
	if ((feature(node_char(rw(NODE_CUR)), 0) & 4) && (feature(node_char(rw(NODE_CUR)), 0) & 0x20) &&
	    node_char(rw(NODE_CUR)) == 'G' && node_char(rw(NODE_NEXT)) != 'o' &&
	    ((feature(node_char(rw(NODE_NEXT)), 0x100) & 0x20) || !(feature(node_char(rw(NODE_NEXT)), 0x200) & 1))) {
		ww(PARAM(P_AV, F_DURF), 8);
	}
	if (feature(node_char(rw(NODE_PREV)), 0x100) & 0x40) {
		ww(PARAM(P_F1, F_LOCB), rw(PARAM(P_F1, F_LOCB)) - 0x32);
		ww(PARAM(P_F1, F_ONSET), rw(PARAM(P_F1, F_ONSET)) + 0x32);
		ww(PARAM(P_F2, F_LOCB), rw(PARAM(P_F2, F_LOCB)) - 0x32);
		ww(PARAM(P_F2, F_ONSET), rw(PARAM(P_F2, F_ONSET)) + 0x32);
		if (feature(node_char(rw(NODE_CUR)), 0x180) & 4) {
			ww(PARAM(P_F2, F_DURB), 3);
			ww(PARAM(P_F3, F_DURB), 3);
		}
	}
	if (node_char(rw(NODE_PREV)) == 'A')
		ww(PARAM(P_F2, F_DURB), 4);
	ww(PARAM(P_F0, F_DURF), rw(PARAM(P_F0, F_LEN)));
	if (rw(PARAM(P_F0, F_DURF)) < 7)
		ww(PARAM(P_F0, F_DURF), 7);
	if (rw(LOOKAHEAD) < rw(PARAM(P_F0, F_DURF)))
		ww(PARAM(P_F0, F_DURF), rw(LOOKAHEAD));
	if ((!(feature(node_char(rw(NODE_CUR)), 0) & 1) || !node_bit(rw(NODE_CUR), 5)) ||
	    (node_bit(rw(NODE_CUR), 6) || ((!node_bit(rw(NODE_PREV), 5) || (node_bit(rw(NODE_PREV), 6))) ||
	                                   !(feature(node_char(rw(NODE_PREV)), 0) & 4)))) {
		ww(PARAM(P_F0, F_DURB), 0);
	} else {
		ww(PARAM(P_F0, F_DURB), node_dur(rw(NODE_PREV)));
		if (rw(RING_LAG) < rw(PARAM(P_F0, F_DURB)))
			ww(PARAM(P_F0, F_DURB), rw(RING_LAG));
	}
	if (node_char(rw(NODE_CUR)) == 'G' && node_char(rw(NODE_PREV)) != ' ' && node_char(rw(NODE_NEXT)) != 'Y') {
		p = node_dur(rw(NODE_PREV)) - (node_dur(rw(NODE_PREV)) >> 2);
		ww(PARAM(P_F3, F_DURB), p);
		ww(PARAM(P_F2, F_DURB), p);
	}
	if (node_char(rw(NODE_CUR)) == 'Z' && (feature(node_char(rw(NODE_NEXT)), 0x100) & 2))
		for (p = P_F1; p <= P_F4; p++)
			ww(PARAM(p, F_DURF), 3);
	if (node_char(rw(NODE_CUR)) == 'R' && node_char(rw(NODE_PREV)) == 'X')
		ww(PARAM(P_F3, F_DURB), 3);
}

/* E2535: onset = locB = (L + target)/2 for the amplitudes and source parameters, but for p < 9 no more than d dB
 * below either side; then AV timing at voicing changes, the pre-pausal fall and many special cases. */
void pg_amplitude_boundaries(void)
{
	int v, cur, prev, p, d;

	cur = node_char(rw(NODE_CUR));
	prev = node_char(rw(NODE_PREV));
	for (p = 0; p < NPARAM; p++) {
		if (p >= P_F1 && p <= P_F0)
			continue;
		ww(PARAM(p, F_ONSET), (rw(LOCUS(p)) + rw(PARAM(p, F_TARGET))) / 2);
		d = 9;
		if (cur == ' ')
			d = 15;
		if (p == P_AH)
			d = 0;
		if (!(feature(prev, 0) & 4))
			d = 10; /* after a voiceless sound, AH included */
		if (p > P_AH)
			d = 0;
		if (p == P_AV && (feature(cur, 0) & 0x10) && prev == ' ')
			d = 20;
		if (p < P_F1) {
			if (rw(PARAM(p, F_ONSET)) < rw(PARAM(p, F_TARGET)) - d)
				ww(PARAM(p, F_ONSET), rw(PARAM(p, F_TARGET)) - d);
			if (rw(PARAM(p, F_ONSET)) < rw(LOCUS(p)) - d)
				ww(PARAM(p, F_ONSET), rw(LOCUS(p)) - d);
		}
		ww(PARAM(p, F_LOCB), rw(PARAM(p, F_ONSET)));
	}
	if (cur == 's' && node_char(rw(NODE_NEXT)) == ' ')
		ww(PARAM(P_AF, F_ONSET), rw(PARAM(P_AF, F_ONSET)) - 10);
	if (cur == 'Z' && node_char(rw(NODE_NEXT)) != ' ') {
		ww(PARAM(P_AF, F_LOCB), 10);
		ww(PARAM(P_AF, F_ONSET), 10);
	}
	if (cur == ' ' && node_char(rw(NODE_PREV)) == 'Z') {
		ww(PARAM(P_AF, F_LOCB), 0x28);
		ww(PARAM(P_AF, F_ONSET), 0x28);
		ww(PARAM(P_AF, F_DURB), node_dur(rw(NODE_PREV)) >> 1);
	}
	if (feature(prev, 0) & 4) {
		if ((!(feature(cur, 0) & 4) || !(feature(cur, 0) & 2)) && (cur != ' ' || prev != 'z'))
			ww(PARAM(P_AV, F_LOCB), rw(PARAM(P_AV, F_LOCB)) - 6);
		if ((feature(cur, 0) & 2) && !(feature(cur, 0) & 0x10) &&
		    ((feature(prev, 0x100) & 1) || (feature(prev, 0) & 0x40))) {
			ww(PARAM(P_AV, F_LOCB), rw(PARAM(P_AV, F_LOCB)) - 4);
		}
		if (feature(cur, 0) & 2) {
			if ((feature(prev, 0) & 4) && (feature(prev, 0) & 0x20) && (feature(cur, 0x100) & 2)) {
				ww(PARAM(P_AV, F_ONSET), 0x3C);
				if (prev == 'G' && ((!(feature(cur, 0x200) & 1) && !(feature(cur, 0x100) & 0x20) &&
				                     (cur != 'p' && cur != 'o')) ||
				                    cur == 'E')) {
					ww(PARAM(P_AV, F_ONSET), rw(PARAM(P_AV, F_ONSET)) - 4);
				}
				if (prev == 'B' && (feature(cur, 0x200) & 1) && !(feature(cur, 0x100) & 0x20)) {
					ww(PARAM(P_AV, F_ONSET), rw(PARAM(P_AV, F_ONSET)) - 4);
					ww(PARAM(P_B2, F_LOCB), 0x4B);
					ww(PARAM(P_B1, F_LOCB), 0x4B);
					ww(PARAM(P_B2, F_ONSET), 0x4B);
					ww(PARAM(P_B1, F_ONSET), 0x4B);
					if (cur == 'o')
						ww(PARAM(P_F2, F_ONSET), rw(PARAM(P_F2, F_ONSET)) + 100);
				}
				if (prev == 'B' && (cur == 'E' || cur == 'o')) {
					ww(PARAM(P_AV, F_ONSET), rw(PARAM(P_AV, F_ONSET)) - 4);
					if (cur == 'o')
						ww(PARAM(P_F2, F_ONSET), rw(PARAM(P_F2, F_ONSET)) + 100);
				}
				if (prev == 'D' && cur == 'E') {
					ww(PARAM(P_AV, F_ONSET), rw(PARAM(P_AV, F_ONSET)) - 4);
					ww(PARAM(P_B2, F_ONSET), 0x4B);
					ww(PARAM(P_B1, F_ONSET), 0x4B);
				}
				if (cur == 'u' || cur == 'b')
					ww(PARAM(P_AV, F_ONSET), rw(PARAM(P_AV, F_ONSET)) - 4);
			}
			if (feature(prev, 0) & 0x40)
				ww(PARAM(P_AV, F_ONSET), rw(PARAM(P_AV, F_ONSET)) - 6);
			if (cur == '~' && (feature(prev, 0x100) & 0x20))
				ww(PARAM(P_F2, F_LOCB), 0x834);
		}
		if (prev == 'E' && cur == 'C') {
			ww(PARAM(P_F1, F_ONSET), rw(PARAM(P_F1, F_ONSET)) - 0x23);
			ww(PARAM(P_B1, F_ONSET), rw(PARAM(P_B1, F_ONSET)) - 10);
		}
	}
	if ((feature(cur, 0) & 4) && (feature(cur, 0) & 0x20)) {
		if (cur == 'G' && node_char(rw(NODE_NEXT)) != 'o' &&
		    ((feature(node_char(rw(NODE_NEXT)), 0x100) & 0x20) ||
		     !(feature(node_char(rw(NODE_NEXT)), 0x200) & 1))) {
			ww(PARAM(P_AV, F_ONSET), 0);
			ww(PARAM(P_AV, F_LOCB), 0);
		}
		if (cur == 'D' && node_char(rw(NODE_NEXT)) == 'o')
			ww(PARAM(P_AV, F_ONSET), 0);
	}
	if (prev == 'W' && cur == 'E') {
		ww(PARAM(P_F2, F_LOCB), 0x5AA);
		ww(PARAM(P_F2, F_ONSET), 0x5AA);
		ww(PARAM(P_B3, F_LOCB), 0xFA);
		ww(PARAM(P_F3, F_ONSET), 0x834);
		ww(PARAM(P_F3, F_LOCB), 0x834);
	}
	if (cur == 'W' && node_char(rw(NODE_NEXT)) == 'E' && prev == ' ') {
		ww(PARAM(P_F3, F_ONSET), 0x866);
		ww(PARAM(P_B3, F_ONSET), 0xFA);
	}
	if (cur == 'H' && (next_class(phoneme_index(node_char(rw(NODE_NEXT)))) == 0 ||
	                   next_class(phoneme_index(node_char(rw(NODE_NEXT)))) == 1)) {
		ww(PARAM(P_AH, F_ONSET), 10);
		ww(PARAM(P_AH, F_DURF), 5);
	}
	if (cur == 'E' && prev == 'R')
		ww(PARAM(P_AV, F_ONSET), 0x39);
	if (cur == 'p' && prev == 'K') {
		ww(PARAM(P_AF, F_ONSET), 0x2B);
		ww(PARAM(P_AF, F_LOCB), 0x2B);
		ww(PARAM(P_AF, F_DURF), 6);
	}
	if (node_char(rw(NODE_CUR)) == 'F' && node_char(rw(NODE_PREV)) == ' ') {
		ww(PARAM(P_AF, F_LOCB), 0xF);
		ww(PARAM(P_AF, F_ONSET), 0xF);
		ww(PARAM(P_AB, F_LOCB), 0xF);
		ww(PARAM(P_AB, F_ONSET), 0xF);
	}
	if (cur == 'R' && prev == 'X')
		ww(PARAM(P_AF, F_ONSET), 0);
	if (cur == 's' && ((node_char(rw(NODE_PREV)) == 'C' && (feature(node_char(rw(NODE_NEXT)), 0x100) & 2)) ||
	                   node_char(rw(NODE_NEXT)) == '@')) {
		for (p = P_A3; p <= P_A6; p++) {
			v = rw(LOCUS(p));
			ww(PARAM(p, F_LOCB), v);
			ww(PARAM(p, F_ONSET), v);
		}
	}
	if (cur == 'T' && (feature(node_char(rw(NODE_PREV)), 0x100) & 2) &&
	    (feature(node_char(rw(NODE_NEXT)), 0x100) & 2)) {
		ww(PARAM(P_AV, F_LOCB), 0x2D);
		ww(PARAM(P_AV, F_ONSET), 0x2D);
	}
	if (cur == 'D' && !node_bit(rw(NODE_CUR), 5) && (feature(node_char(rw(NODE_PREV)), 0) & 2) &&
	    (feature(node_char(rw(NODE_NEXT)), 0) & 2)) {
		ww(PARAM(P_AB, F_LOCB), 0);
		ww(PARAM(P_AB, F_ONSET), 0);
	}
	if (!(feature(cur, 0) & 4)) {
		if (feature(node_char(rw(NODE_PREV)), 0) & 4) {
			ww(PARAM(P_AV, F_DURB), node_dur(rw(NODE_PREV)) >> 2);
			if (rw(PARAM(P_AV, F_TARGET)) == 0)
				ww(PARAM(P_AV, F_LOCB), 0x28);
			else
				ww(PARAM(P_AV, F_LOCB), rw(PARAM(P_AV, F_TARGET)));
		}
	} else if (((!(feature(cur, 0) & 0x20) || (feature(cur, 0x180) & 2)) ||
	            (feature(node_char(rw(NODE_NEXT)), 0x100) & 1)) ||
	           node_char(rw(NODE_NEXT)) == ' ') {
		if ((!(feature(node_char(rw(NODE_PREV)), 0) & 0x20) ||
		     (feature(node_char(rw(NODE_PREV)), 0x180) & 2)) ||
		    ((feature(node_char(rw(NODE_CUR)), 0x100) & 1) || node_char(rw(NODE_CUR)) == ' ')) {
			ww(PARAM(P_AV, F_TYPE), 7);
		} else {
			ww(PARAM(P_AV, F_TYPE), 6);
		}
		if (next_class(phoneme_index(node_char(rw(NODE_CUR)))) == 3 && node_char(rw(NODE_PREV)) == 'B')
			ww(PARAM(P_AV, F_TYPE), 7);
		if (node_char(rw(NODE_NEXT)) == ' ' || (node_bit(rw(NODE_CUR), 5)))
			ww(PARAM(P_AV, F_DURF), node_dur(rw(NODE_CUR)) >> 2);
		else
			ww(PARAM(P_AV, F_DURF), node_dur(rw(NODE_CUR)) >> 1);
		ww(PARAM(P_AV, F_DURB), node_dur(rw(NODE_PREV)) >> 2);
	}
	if (cur == 'K' && (feature(node_char(rw(NODE_PREV)), 0) & 4)) {
		ww(PARAM(P_AV, F_TYPE), 6);
		ww(PARAM(P_AV, F_ONSET), rw(LOCUS(P_AV)));
		ww(PARAM(P_AV, F_DURF), node_dur(rw(NODE_CUR)) - (node_dur(rw(NODE_CUR)) >> 2));
	}
	if (cur == 't')
		ww(PARAM(P_AV, F_TYPE), 4);
	if (node_char(rw(NODE_PREV)) == 'B' && (feature(node_char(rw(NODE_CUR)), 0) & 4)) {
		if (rw(LOCUS(P_AV)) == 0) {
			ww(PARAM(P_AV, F_TYPE), 4);
		} else {
			ww(PARAM(P_AV, F_TYPE), ruw(PARAM(P_AV, F_TYPE)) | 2);
			ww(PARAM(P_AV, F_DURF), node_dur(rw(NODE_CUR)) >> 1);
			ww(PARAM(P_AV, F_ONSET), rw(LOCUS(P_AV)));
		}
	}
	if (cur == 'B' && (((next_class(phoneme_index(node_char(rw(NODE_NEXT)))) == 3 ||
	                     next_class(phoneme_index(node_char(rw(NODE_NEXT)))) == 2) ||
	                    next_class(phoneme_index(node_char(rw(NODE_NEXT)))) == 0))) {
		ww(PARAM(P_SRC18, F_TARGET), 0);
		ww(PARAM(P_SRC19, F_TARGET), 0xF);
	}
	if (cur == 'p' && node_char(rw(NODE_PREV)) == 'D') {
		ww(PARAM(P_SRC18, F_TARGET), 10);
		ww(PARAM(P_SRC19, F_TARGET), 0xF);
	}
	if (node_char(rw(NODE_PREV)) == 'Z') {
		ww(PARAM(P_SRC18, F_ONSET), rw(LOCUS(P_SRC18)));
		if (node_dur(rw(NODE_CUR)) < 5)
			ww(PARAM(P_SRC18, F_DURF), node_dur(rw(NODE_CUR)));
		else
			ww(PARAM(P_SRC18, F_DURF), 5);
	}
	if ((cur == 'S' || cur == 'Z') && (feature(prev, 0) & 4)) {
		ww(PARAM(P_AV, F_TYPE), 7);
		if (cur == 'S')
			ww(PARAM(P_AV, F_DURF), 5);
		else
			ww(PARAM(P_AV, F_DURF), 6);
		if (node_char(rw(NODE_NEXT)) == ' ') {
			ww(PARAM(P_AV, F_DURB), node_dur(rw(NODE_PREV)) >> 1);
			ww(PARAM(P_AV, F_LOCB), 0x32);
			ww(PARAM(P_AV, F_ONSET), 0x32);
		} else {
			int from_locus = 1;
			if (feature(prev, 0x100) & 2) {
				ww(PARAM(P_AV, F_DURB), node_dur(rw(NODE_PREV)) >> 2);
				from_locus = cur != 'Z';
			}
			v = from_locus ? rw(LOCUS(P_AV)) : 0x34;
			ww(PARAM(P_AV, F_LOCB), v);
			ww(PARAM(P_AV, F_ONSET), v);
		}
	}
	if (node_char(rw(NODE_CUR)) == 'Z' && node_char(rw(NODE_NEXT)) != ' ' &&
	    !(feature(node_char(rw(NODE_PREV)), 0x100) & 2) && !(feature(node_char(rw(NODE_NEXT)), 0x100) & 2)) {
		ww(PARAM(P_F0, F_DURB), 4);
		ww(PARAM(P_F0, F_DURF), 4);
	}
	if (node_char(rw(NODE_CUR)) == ' ' && (feature(node_char(rw(NODE_PREV)), 0) & 4) &&
	    node_char(rw(NODE_PREV)) != 'Z') {
		ww(PARAM(P_F0, F_TYPE), ruw(PARAM(P_F0, F_TYPE)) | 1);
		ww(PARAM(P_F0, F_LOCB), 0x3A);
		ww(PARAM(P_F0, F_DURB), node_dur(rw(NODE_PREV)) - node_dur(rw(NODE_PREV)) / 3);
		ww(PARAM(P_AV, F_TYPE), ruw(PARAM(P_AV, F_TYPE)) | 1);
		ww(PARAM(P_AV, F_LOCB), 0x30);
		ww(PARAM(P_AV, F_DURB), node_dur(rw(NODE_PREV)) - node_dur(rw(NODE_PREV)) / 3);
	}
	if (node_char(rw(NODE_CUR)) == ' ' && node_char(rw(NODE_NEXT)) == ' ' && node_char(rw(NODE_PREV)) == 'n') {
		ww(PARAM(P_F2, F_LOCB), 0x37C);
		ww(PARAM(P_F2, F_DURB), 9);
		ww(PARAM(P_F2, F_TYPE), ruw(PARAM(P_F2, F_TYPE)) | 1);
		ww(PARAM(P_AV, F_TYPE), ruw(PARAM(P_AV, F_TYPE)) | 1);
		ww(PARAM(P_AV, F_LOCB), rw(LOCUS(P_AV)));
		ww(PARAM(P_AV, F_DURB), 8);
	}
	if (rw(TRK_POS(P_F0)) - rw(TRK_PREV(P_F0)) < rw(PARAM(P_F0, F_DURB)))
		ww(PARAM(P_F0, F_DURB), rw(TRK_POS(P_F0)) - rw(TRK_PREV(P_F0)));
	if (rw(TRK_POS(P_AV)) - rw(TRK_PREV(P_AV)) < rw(PARAM(P_AV, F_DURB)))
		ww(PARAM(P_AV, F_DURB), rw(TRK_POS(P_AV)) - rw(TRK_PREV(P_AV)));
	if (node_char(rw(NODE_CUR)) == 'q' && (feature(node_char(rw(NODE_PREV)), 0) & 2)) {
		ww(PARAM(P_AV, F_ONSET), 0x37);
		ww(PARAM(P_AV, F_LOCB), 0x37);
		ww(PARAM(P_F0, F_LOCB), 0x28);
		ww(PARAM(P_F0, F_ONSET), 0x28);
		ww(PARAM(P_F0, F_DURF), 5);
		ww(PARAM(P_F0, F_DURB), 3);
		ww(PARAM(P_F0, F_TYPE), 7);
	}
}

/* E2FB8: the end of every rule function list. */
void pg_finalize(void)
{
	int p;

	pg_locus_weights();
	pg_consonant_loci();
	pg_apply_loci();
	pg_amplitude_boundaries();
	if (node_char(rw(NODE_CUR)) != 'T' || node_char(rw(NODE_NEXT)) != '3') {
		for (p = P_F1; p <= P_F3; p++) { /* F(n+1) >= F(n) + 200 at the start of the segment too */
			if (rw(PARAM(p + 1, F_ONSET)) < rw(PARAM(p, F_ONSET)) + 200)
				ww(PARAM(p + 1, F_ONSET), rw(PARAM(p, F_ONSET)) + 200);
			if (rw(PARAM(p + 1, F_LOCB)) < rw(PARAM(p, F_LOCB)) + 200)
				ww(PARAM(p + 1, F_LOCB), rw(PARAM(p, F_LOCB)) + 200);
		}
	}
	for (p = 0; p < NPARAM; p++) {
		if (rw(PARAM(p, F_DURF)) > 20)
			ww(PARAM(p, F_DURF), 20);
		if (rw(PARAM(p, F_DURB)) > 20)
			ww(PARAM(p, F_DURB), 20);
	}
}

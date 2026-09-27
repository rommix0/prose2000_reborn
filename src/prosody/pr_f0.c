/* F0: a line falling across the phrase from the pitch setting, shaped by the contour type, plus accents. */
#include "prosody.h"

/* The trace hook (prosody.h): each contribution to a target, for tools such as pitch_trace. NULL in normal use. */
void (*pr_f0_hook)(int node, int event, int value);
#define TRACE(event, value)                                                                                            	do {                                                                                                           		if (pr_f0_hook)                                                                                        			pr_f0_hook(n, (event), (value));                                                               	} while (0)

static int stress_of(int n)
{
	return node_stress(n);
}

/* DBC84: node +8 = F0 / 2 for the phoneme at PR_CUR; word boundaries of types 12 and 17 (their +6, from the
 * marks of two-letter phoneme input) set LOWER_F0 and DECL_LOW. PITCH_SYSTEM.md describes the rules. */
void f0_target(void)
{
	int n = rw(PR_CUR), di, fx, acc, accent, contour_p = rw(PR_PROSODY) & 0xFF;
	if (rw(CUR_PHONEME) == 0) {
		if (!(feature(node_char(n), 0) & 8))
			return;
		switch (node_dur(n)) {
		case 12:
			ww(LOWER_F0, 1);
			TRACE(PR_F0_TYPE12, 0);
			return;
		case 17:
			ww(DECL_LOW, 1);
			TRACE(PR_F0_TYPE17, 0);
			return;
		default:
			if (rw(DECL_LOW) != 0 && rw(ACCENT_DONE) != 0 && rw(PHRASE_FINAL) == 0) {
				ww(DECL_LOW, 0);
				TRACE(PR_F0_DECL_CLEARED, 0);
			}
			return;
		}
	}
	ww(F0_POS, rw(F0_POS) + 1);
	if (rw(PR_PITCH) == 0) {
		di = 0;
		TRACE(PR_F0_ZERO, 0);
		goto store;
	}
	if (rw(PR_MODE) & 0x800) { /* mode flag 12: monotone */
		di = rw(PR_PITCH);
		TRACE(PR_F0_MONOTONE, di);
		goto store;
	}
	if (rw(F0_HOLD) != 0) {
		di = rw(F0_HOLD);
		TRACE(PR_F0_HOLD, di);
		goto store;
	}

	/* the phrase line: from pitch + fx down to pitch, bent at the positions of the marked words */
	fx = fx_mul_q15(rw(PR_PITCH) / 3, rw(F0_SCALE + 2 * rw(PR_VOICE)));
	if (rw(PHRASE_LEN) == 0)
		ww(PHRASE_LEN, 1);
	di = rw(PR_PITCH) + fx;
	int pos = rw(F0_POS), len = rw(PHRASE_LEN), p6 = rw(POS_6), p7 = rw(POS_7), p8;
	switch ((unsigned)rw(CONTOUR)) {
	case 0: /* straight declination */
		di -= mul_div_s(fx, pos, len);
		break;
	case 6: /* rise to the type-6 word, then fall */
		if (p6 == 0)
			ww(POS_6, p6 = len);
		if (pos <= p6)
			di -= mul_div_s(p6 - pos, fx, p6);
		else
			di -= mul_div_s(pos - p6, fx, len - p6);
		break;
	case 7:
		if (pos > p7)
			di -= mul_div_s(len - pos, fx, len - p7);
		else
			di -= mul_div_s(fx, pos, p7);
		break;
	case 8:
		ww(PR_PROSODY, 6);
		p8 = rw(POS_8);
		if (p8 == 0)
			ww(POS_8, p8 = len);
		if (pos <= p8)
			di -= mul_div_s(p8 - pos, fx, p8);
		else
			di -= mul_div_s(pos - p8, fx, len - p8);
		break;
	case 16: /* type 6 before type 7 */
		ww(PR_PROSODY, 3);
		if (pos <= p6)
			di -= mul_div_s(p6 - pos, fx, p6);
		else if (pos <= p7)
			di -= mul_div_s(pos - p6, fx, p7 - p6);
		else
			di -= mul_div_s(len - pos, fx, len - p7);
		break;
	case 15: /* type 7 before type 6 */
		ww(PR_PROSODY, 6);
		if (pos <= p7)
			di -= mul_div_s(fx, pos, p7);
		else if (pos <= p6)
			di -= mul_div_s(p6 - pos, fx, p6 - p7);
		else
			di -= mul_div_s(pos - p6, fx, len - p6);
		break;
	}
	contour_p = rw(PR_PROSODY) & 0xFF;
	TRACE(PR_F0_LINE, di);
	if (rw(LOWER_F0) != 0) {
		int line = di;
		di = mul_div_s(di, 108, 100);
		TRACE(PR_F0_REGISTER, di - line);
	}

	/* accents */
	accent = fx_mul_q15(di, rw(F0_SCALE + 2 * rw(PR_VOICE))) >> 2;
	acc = accent - (accent >> 2);
	if (rw(ACCENT_DONE) == 0) {
		if ((rw(PR_PROSODY) >> 8) == 1 || rw(DECL_LOW) != 0)
			acc -= acc >> 2;
		else
			acc += acc >> 4;
	}
	acc = s16(acc);
	if (rw(ACCENT_NODE) != 0 && stress_of(rw(ACCENT_NODE)) == 3)
		acc >>= 1;
	if ((rw(NEXT_SEG) != 0 && stress_of(rw(NEXT_SEG)) > 1 && rw(ACCENT_HERE) == 0) ||
	    (rw(F0_POS) == 1 && rw(CUR_VOWEL) != 0 && rw(CUR_B5) != 0) ||
	    (rw(POS_17) != 0 && rw(F0_POS) >= rw(POS_17) && rw(PHRASE_FINAL) != 0))
		ww(PEAK, 1);
	if (rw(NEXT_SEG) == rw(ACCENT_NODE) && rw(NEXT_SEG) != 0 && contour_p != 6) {
		di += acc;
		TRACE(PR_F0_PRENUCLEUS, acc);
		if (rw(NEXT_SEG) != 0 && stress_of(rw(NEXT_SEG)) == 3) {
			di += s16(acc << 1);
			TRACE(PR_F0_PRENUCLEUS, s16(acc << 1));
		}
	}
	switch (stress_of(n)) {
	case 1:
		di += acc >> 1;
		TRACE(PR_F0_ACCENT, acc >> 1);
		break;
	case 2:
	case 3:
		ww(ACCENT_DONE, 1);
		if (n == rw(ACCENT_NODE) && contour_p != 6) {
			ww(n + N_FLAGS, rw(n + N_FLAGS) | 0x80); /* the F0 peak of the phrase */
			ww(PEAK, 0);
			TRACE(PR_F0_NUCLEUS, 0);
		} else {
			di += acc;
			TRACE(PR_F0_ACCENT, acc);
			if (stress_of(n) == 3) {
				di += s16(acc << 1);
				TRACE(PR_F0_ACCENT, s16(acc << 1));
			}
		}
		break;
	}
	if (rw(PEAK) != 0) {
		di += accent;
		TRACE(PR_F0_HAT, accent);
	}

	/* voiced consonants dip */
	if (feature(rsb(CUR_CH), 0) & 4) {
		int ns = rw(NEXT_SEG), ch = rsb(CUR_CH);
		int before_unstressed =
		    rw(CUR_B5) == 0 && ns != 0 && !(feature(node_char(ns), 0x100) & 2) && stress_of(ns) < 1;
		int stop_like = (feature(ch, 0x80) & 0x20) && (feature(ch, 0x180) & 1) && (feature(ch, 0) & 0x10);
		if ((before_unstressed && !stop_like) || n == rw(ACCENT_NODE)) {
			di -= 3;
			if (rw(CUR_SON) == 0)
				di -= 5;
			TRACE(PR_F0_DIP, rw(CUR_SON) == 0 ? -8 : -3);
		}
	}
	if (rw(PHRASE_FINAL) == 0 && (feature(rsb(CUR_CH), 0x200) & 1)) {
		di += 3;
		TRACE(PR_F0_HIGH, 3);
	}
	di = s16(di);
	if (rw(PHRASE_FINAL) != 0 && rw(DECL_LOW) != 0)
		TRACE(PR_F0_END_SKIPPED, 0);
	int before = di;
	if (rw(PHRASE_FINAL) != 0 && rw(DECL_LOW) == 0) {
		/* the end of the phrase, by the phrase kind of the P command */
		switch ((unsigned)(rw(PR_PROSODY) & 0xFF)) {
		case 1: /* statement: fall */
			di -= accent >> 2;
			if ((feature(rsb(CUR_CH), 0) & 1) && rw(CUR_B5) == 0 && node_char(rw(NEXT_NODE)) == '.')
				di -= 4;
			break;
		case 3: /* comma: slight rise */
			di += accent >> 3;
			break;
		case 4: /* question: rise */
		case 5:
			di = rw(PR_PITCH) * 2 - (rw(PR_PITCH) >> 3);
			break;
		}
		di = s16(di);
		TRACE(PR_F0_END, di - before);
		before = di;
		if (di > 240)
			di = 240;
		else if (di < 50)
			di = 50;
		ww(F0_HOLD, di);
	} else {
		if (di > 240)
			di = 240;
		else if (di < 50)
			di = 50;
	}
	if (di != before)
		TRACE(PR_F0_CLAMP, di - before);
store:
	wb(n + N_F0, s16(di) >> 1);
	TRACE(PR_F0_TARGET, (s16(di) >> 1) * 2);
}

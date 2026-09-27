/*
 * One phoneme segment (REFERENCE §12.1-12.3): load the targets, set the context flags, run the rule engine and
 * its context routines, apply the host's attenuation and voice, and write the 22 tracks.
 */
#include "pg.h"

/* Context flags read by rule conditions of kind 0 (§12.5): word n at DS:EE1C + 2n. Flags 6-9 give the class of the
 * next sound when it is a vowel (DS:98C2): V0 high front, V1 other front, V2 back rounded, V3 central/low. */
#define CTX(n) (0xEE1Cu + 2u * (unsigned)(n))
enum {
	CTX_VOICED, CTX_PREV_VOWEL, CTX_PREV_CLOSURE, CTX_PREV_VOICED, CTX_PREV_OTHER, CTX_RELEASED,
	CTX_NEXT_V0, CTX_NEXT_V1, CTX_NEXT_V3, CTX_NEXT_V2, CTX_NEXT_STRESSED
};

#define NEXT_CLASS_TABLE 0x98C2 /* phoneme index -> class of a following phoneme (0-3, 8, 9) */
#define V_EBAE 0xEBAE
#define V_EBB0 0xEBB0
#define V_EAFA 0xEAFA

/* Targets of the current phoneme (58-byte tables indexed by phoneme index, §12.3). */
#define T_F1 0x944C
#define T_F2 0x9486
#define T_F3 0x94C0
#define T_F4 0x94FA
#define T_B1 0x9534
#define T_B2 0x956E
#define T_B3 0x95A8
#define T_AV 0x95E2
#define VOICE_F4 0x5348  /* per-voice F4 offset (words) */
#define VOICE_SRC 0x5308 /* per-voice p18-p21 bytes: 5308, 5310, 5318, 5320 */
#define VOICE_SCALE 0x52F8 /* per-voice formant scale, q15 (words) */
#define VOICE_F4_MAX 0x5358

/* Next-phoneme targets for anticipation, and the offglide targets of diphthongs. */
#define NEXT_F1 0xEB2A
#define OFFGLIDE_F1 0xEB44

void paramgen_load_targets(void)
{
	int cur = rw(NODE_CUR), prev = rw(NODE_PREV);
	int idx = phoneme_index(node_char(cur)), ch = node_char(cur), pidx = phoneme_index(node_char(prev));
	int nidx;

	if (pidx < 0x12 && node_char(prev) != 'u' && node_char(cur) != 'j')
		ww(V_EBAE, rw(0x96FC + 2 * pidx));
	else
		ww(V_EBAE, rw(V_EBB0));
	ww(CLASS_PREV, rw(CLASS_CUR));
	ww(V_EBB0, rw(0x961C + 2 * idx));
	ww(0xEBC2, 0x33);
	ww(0xEBC4, 0x33);
	ww(0xEBCA, 0);
	ww(0xEBC8, 0);
	ww(0xEBC6, 0);
	if (feature(ch, 0x100) & 2)
		ww(CLASS_CUR, 0); /* vowel */
	else if (feature(ch, 0x100) & 1)
		ww(CLASS_CUR, 3); /* closure */
	else if (feature(ch, 0) & 2)
		ww(CLASS_CUR, 1); /* sonorant */
	else
		ww(CLASS_CUR, 2);

	ww(PARAM(P_F1, F_TARGET), rb(T_F1 + idx) * 4);
	ww(PARAM(P_F2, F_TARGET), rb(T_F2 + idx) * 8 + 500);
	ww(PARAM(P_F3, F_TARGET), rb(T_F3 + idx) * 16);
	ww(PARAM(P_F4, F_TARGET), rw(VOICE_F4 + 2 * rw(VOICE)) + rb(T_F4 + idx) * 16);
	ww(PARAM(P_B1, F_TARGET), rb(T_B1 + idx) * 2);
	ww(PARAM(P_B2, F_TARGET), rb(T_B2 + idx) * 2);
	ww(PARAM(P_B3, F_TARGET), rb(T_B3 + idx) * 2);
	ww(PARAM(P_AV, F_TARGET), rsb(T_AV + idx));
	ww(PARAM(P_F0, F_TARGET), rsb(cur + N_F0) * 2);
	for (int p = P_SRC18; p <= P_SRC21; p++)
		ww(PARAM(p, F_TARGET), rsb(VOICE_SRC + 8 * (p - P_SRC18) + rw(VOICE)));

	if (idx < 29) {
		/* vowels and glides: no frication, flat parallel amplitudes */
		ww(PARAM(P_AF, F_TARGET), 0);
		ww(PARAM(P_AH, F_TARGET), 0);
		for (int p = P_A2; p <= P_A6; p++)
			ww(PARAM(p, F_TARGET), 60);
		ww(PARAM(P_AB, F_TARGET), 0);
		if (idx < 0x12) {
			ww(0xEBBA, rsb(0x9744 + idx));
			ww(OFFGLIDE_F1, rb(0x9690 + idx) * 4);
			ww(OFFGLIDE_F1 + 2, rb(0x96A2 + idx) * 8 + 500);
			ww(OFFGLIDE_F1 + 4, rb(0x96B4 + idx) * 16);
			ww(OFFGLIDE_F1 + 8, rb(0x96C6 + idx) * 2);
			ww(OFFGLIDE_F1 + 10, rb(0x96D8 + idx) * 2);
			ww(OFFGLIDE_F1 + 12, rb(0x96EA + idx) * 2);
		}
		if (ch == 'u' && node_char(rw(NODE_NEXT)) == 'j')
			ww(V_EBB0, 0);
	} else {
		/* consonants: AF AH A2-A6 AB from the tables behind DS:9774-9854 */
		for (int p = P_AF; p <= P_AB; p++)
			ww(PARAM(p, F_TARGET), rsb(rw(0x9774 + 0x20 * (p - P_AF)) + idx));
		if (idx > 0x20 && idx < 0x2A) {
			/* stops: burst and release data by the class of the next phoneme */
			int j = rsb(NEXT_CLASS_TABLE + phoneme_index(node_char(rw(NODE_NEXT))));
			if (j != 8 && j != 9) {
				j += rsb(0x98DB + idx);
				ww(0xEBC6, rsb(0x99DD + j));
				ww(0xEBC8, rsb(0x9905 + j));
				ww(0xEBCA, rsb(0x994D + j));
				ww(0xEBB6, rsb(0x9A25 + j));
				ww(0xEBB4, rsb(0x9A6D + j));
				if (rsb(0x9995 + j) != 0x7F)
					ww(PARAM(P_AB, F_TARGET), rsb(0x9995 + j));
			}
		}
	}
	if (idx < 0x34)
		ww(PARAM(P_FN, F_TARGET), 248);
	else
		ww(PARAM(P_FN, F_TARGET), rb(rw(0x98C0) + idx) * 4 + 192);

	for (int p = 0; p < NPARAM; p++) {
		ww(PARAM(p, F_DURF), rsb(0x98A4 + p));
		ww(PARAM(p, F_TYPE), 7);
		ww(PARAM(p, F_LEN), node_dur(cur));
		ww(WEIGHT(p), rw(0x9864 + 8 * rw(CLASS_PREV) + 2 * rw(CLASS_CUR)));
	}
	ww(PARAM(P_SRC20, F_TYPE), 4);
	ww(PARAM(P_SRC21, F_TYPE), 4);

	nidx = phoneme_index(node_char(rw(NODE_NEXT)));
	ww(NEXT_F1, rb(T_F1 + nidx) * 4);
	ww(NEXT_F1 + 2, rb(T_F2 + nidx) * 8 + 500);
	ww(NEXT_F1 + 4, rb(T_F3 + nidx) * 16);
	if (nidx > 0x20) {
		ww(NEXT_F1 + 8, rb(T_B1 + nidx) * 2);
		ww(NEXT_F1 + 10, rb(T_B2 + nidx) * 2);
		ww(NEXT_F1 + 12, rb(T_B3 + nidx) * 2);
	}
}

/* DEB3A */
void paramgen_segment_setup(void)
{
	int pch = node_char(rw(NODE_PREV));
	ww(0xEB74, 0);
	ww(PARAM(P_F0, F_TYPE), 6);
	if (!(feature(pch, 0) & 4)) { /* after a voiceless sound: F0 and AV start fresh */
		ww(TRK_PREV(P_F0), rw(TRK_POS(P_F0)));
		ww(PARAM(P_AV, F_TYPE), 6);
	}
	if (feature(pch, 0x180) & 2) {
		ww(WEIGHT(P_F3), 0x7FFE);
		ww(WEIGHT(P_F2), 0x7FFE);
		ww(WEIGHT(P_F1), 0x7FFE);
	}
	if (feature(pch, 0) & 0x20) /* after a stop: no blend back for AF */
		ww(TRK_PREV(P_AF), rw(TRK_POS(P_AF)));
	if (node_char(rw(NODE_CUR)) == 'Z' && !(feature(pch, 0x100) & 2) &&
	    !(feature(node_char(rw(NODE_NEXT)), 0x100) & 2) && node_char(rw(NODE_NEXT)) != ' ') {
		ww(PARAM(P_F0, F_TYPE), 7);
		ww(0xEB74, 0x4010);
		ww(PARAM(P_F0, F_TARGET), 0x50);
	}
}

void paramgen_segment(void)
{
	int ch = node_char(rw(NODE_CUR)), v;

	paramgen_load_targets();
	paramgen_segment_setup();
	for (int i = 0; i < 11; i++)
		ww(CTX(i), 0);
	if (node_bit(rw(NODE_NEXT), 5))
		ww(CTX(CTX_NEXT_STRESSED), 1);
	if (feature(ch, 0) & 4)
		ww(CTX(CTX_VOICED), 1);
	switch (rw(CLASS_PREV)) {
	case 0: ww(CTX(CTX_PREV_VOWEL), 1); break;
	case 3: ww(CTX(CTX_PREV_CLOSURE), 1); break;
	case 1: ww(CTX(CTX_PREV_VOICED), 1); break;
	default: ww(CTX(CTX_PREV_OTHER), 1); break;
	}
	if ((rw(CTX(CTX_PREV_CLOSURE)) && (feature(node_char(rw(NODE_PREV)), 0) & 0x10)) || rw(CTX(CTX_PREV_VOWEL)))
		ww(CTX(CTX_PREV_VOICED), 1);
	if (rw(CLASS_CUR) != 0) {
		int cur = rw(NODE_CUR), next = rw(NODE_NEXT);
		if ((feature(node_char(cur), 0) & 0x20) &&
		    ((feature(node_char(next), 0x100) & 1) || node_char(next) == ' ' || node_bit(cur, 6)))
			ww(CTX(CTX_RELEASED), 1);
		switch (rsb(NEXT_CLASS_TABLE + phoneme_index(node_char(next)))) {
		case 0: ww(CTX(CTX_NEXT_V0), 1); break;
		case 1: ww(CTX(CTX_NEXT_V1), 1); break;
		case 3: ww(CTX(CTX_NEXT_V3), 1); break;
		case 2: ww(CTX(CTX_NEXT_V2), 1); break;
		}
	}
	paramgen_apply_rules();

	if (!(feature(node_char(rw(NODE_CUR)), 0) & 4)) {
		ww(V_EAFA, rw(PARAM(P_F0, F_TARGET)));
		ww(PARAM(P_F0, F_TARGET), 0);
		ww(PARAM(P_F0, F_TYPE), rw(PARAM(P_F0, F_TYPE)) & ~2);
	}
	if (rb(rw(NODE_CUR) + N_F0) == 0) {
		ww(PARAM(P_AH, F_TARGET), rw(PARAM(P_AV, F_TARGET)));
		ww(PARAM(P_AV, F_TARGET), 0);
		ww(PARAM(P_AV, F_ONSET), 0);
		ww(PARAM(P_AV, F_LOCB), 0);
		ww(PARAM(P_F0, F_TYPE), rw(PARAM(P_F0, F_TYPE)) & ~2);
	}
	if (!(feature(node_char(rw(NODE_PREV)), 0) & 4) || rb(rw(NODE_PREV) + N_F0) == 0)
		ww(PARAM(P_F0, F_TYPE), rw(PARAM(P_F0, F_TYPE)) & ~1);

	/* ESC[a: attenuate AV, AF and AH (target, locB, onset) */
	for (int p = P_AV; p <= P_AH; p++)
		for (int f = F_LOCB; f <= F_TARGET; f += 2) {
			v = rw(PARAM(p, f)) - rw(ATTEN);
			ww(PARAM(p, f), v < 0 ? 0 : v);
		}
	/* ESC[V: scale F1-F3 by the voice's factor; F4 moves by F3's (scaled) amount, capped */
	if (rw(VOICE) != 0) {
		int k = rw(VOICE_SCALE + 2 * rw(VOICE));
		ww(PARAM(P_F1, F_TARGET), rw(PARAM(P_F1, F_TARGET)) + fx_mul_q15(rw(PARAM(P_F1, F_TARGET)), k));
		ww(PARAM(P_F2, F_TARGET), rw(PARAM(P_F2, F_TARGET)) + fx_mul_q15(rw(PARAM(P_F2, F_TARGET)), k));
		ww(PARAM(P_F3, F_TARGET), rw(PARAM(P_F3, F_TARGET)) + fx_mul_q15(rw(PARAM(P_F3, F_TARGET)), k));
		ww(PARAM(P_F4, F_TARGET), rw(PARAM(P_F4, F_TARGET)) + fx_mul_q15(rw(PARAM(P_F3, F_TARGET)), k));
		if (rw(PARAM(P_F4, F_TARGET)) > rw(VOICE_F4_MAX + 2 * rw(VOICE)))
			ww(PARAM(P_F4, F_TARGET), rw(VOICE_F4_MAX + 2 * rw(VOICE)));
	}
	ww(PARAM(P_SRC21, F_TARGET), rw(PARAM(P_SRC21, F_TARGET)) | rw(VOICE) << 4);
	ww(PARAM(P_SRC20, F_TARGET), rw(PARAM(P_SRC20, F_TARGET)) | rw(ATTEN) << 4);
	if (rw(0xEAE6) != 0) {
		for (int p = 0; p < NPARAM; p++)
			ww(TRK_PREV(p), rw(TRK_POS(p)));
		ww(0xEAE6, 0);
	}
	for (int p = 0; p < NPARAM; p++)
		param_emit_segment(p);
	if (node_bit(rw(NODE_CUR), 6)) {
		/* glottal onset (node bit 6): F0 dips to 40 Hz at the start of the segment */
		ww(PARAM(P_F0, F_ONSET), 0x28);
		ww(PARAM(P_F0, F_LOCB), 0x28);
		ww(PARAM(P_F0, F_DURF), 3);
		ww(PARAM(P_F0, F_DURB), 5);
		ww(PARAM(P_F0, F_TYPE), 3);
		param_emit_segment(P_F0);
	}
}

/* DE918: the phoneme char of the node a condition byte refers to (bits 1-2: cur, next, the one after next, the
 * one before prev). */
static int rule_cond_node_char(int head)
{
	switch ((head & 6) >> 1) {
	case 0: return node_char(rw(NODE_CUR));
	case 1: return node_char(rw(NODE_NEXT));
	case 2: return node_char(rw(rw(NODE_NEXT)));
	default: return node_char(rw(rw(NODE_PREV) + 2));
	}
}

/* Does the condition string at c hold? (§12.5; the list ends with 0x18) */
static int rule_condition(unsigned c)
{
	do {
		int head = rsb(c), want = head & 1;
		switch (head >> 3) {
		case 0: /* context flag */
			c++;
			if (want != rw(CTX(rsb(c))))
				return 0;
			c++;
			break;
		case 1: { /* feature bit of a node */
			int ch = rule_cond_node_char(head), m, has;
			c++;
			m = rw(0x48 + 2 * (rsb(c) & 0x7F));
			has = (m & 0xFF & rsb(FEATURES + (((m & 0xFF00) >> 1) | ch))) != 0;
			if (want != has)
				return 0;
			c++;
			break;
		}
		case 2: { /* the node's char is in the list */
			int ch = rule_cond_node_char(head) & 0xFF, found = 0;
			while (rsb(++c) > 0x18) {
				if (rb(c) == (unsigned)ch) {
					if (!want)
						return 0;
					found = 1;
					while (rsb(c + 1) > 0x18)
						c++;
				}
			}
			if (found != want)
				return 0;
			break;
		}
		default:
			fatal_error(0x2E);
		}
	} while (rb(c) != 0x18);
	return 1;
}

/* The rule function lists hold far pointers; these are the C routines behind them. */
typedef void (*pg_routine)(void);
static const struct {
	uint32_t linear;
	pg_routine fn;
} routines[] = {
	/* the 11 far pointers that occur in the lists at DS:7842-798E */
	{0xD3FFB, pg_shift_aspiration},  {0xDEBFA, pg_voiceless_onset}, {0xDEC87, pg_after_closure},
	{0xDEE85, pg_sonorant_onset},    {0xDF245, pg_vowel},           {0xDFB5B, pg_sonorant_consonant},
	{0xDFE87, pg_obstruent_voicing}, {0xE01C0, pg_fricative_amps},  {0xE067F, pg_closure_types},
	{0xE08EA, pg_stop_burst},        {0xE2FB8, pg_finalize},
};

static void call_routine(unsigned far_ptr)
{
	uint32_t linear = (uint32_t)ruw(far_ptr + 2) * 16 + ruw(far_ptr);
	for (unsigned i = 0; i < sizeof routines / sizeof routines[0]; i++)
		if (routines[i].linear == linear) {
			routines[i].fn();
			return;
		}
	fatal_error(0x2E); /* not in the firmware: every list entry is one of the routines above */
}

/* DE96E: the first rule of the group for (class of cur, class of prev) whose condition holds is applied. */
void paramgen_apply_rules(void)
{
	int mask = 1 << (rw(VOICE) & 31);
	int group = rb(rw(0x9376 + 2 * phoneme_index(node_char(rw(NODE_CUR)))) +
	               phoneme_index(node_char(rw(NODE_PREV))));
	unsigned rule = ruw(0x8CFE + 2 * group);
	int count = rsb(0x8DAE + group);
	for (int i = 0; i < count; i++, rule += 8) {
		unsigned cond = ruw(rule), list;
		if (cond != 0 && !rule_condition(cond))
			continue;
		if (rw(rule + 2))
			paramgen_rule_action(ruw(rule + 2), mask);
		list = ruw(rule + 4);
		if (list)
			for (; rw(list + 2) != 0 || rw(list) != 0; list += 4)
				call_routine(list);
		if (rw(rule + 6))
			paramgen_rule_action(ruw(rule + 6), mask);
		return;
	}
	fatal_error(0x2F);
}

/* DE81F: run an action list, a 0-terminated list of pointers to runs of 6-byte entries {op, voice mask, value,
 * address}; bit 7 of op continues the run. */
void paramgen_rule_action(int list, int voice_mask)
{
	int acc = 0; /* a stack slot in the firmware, uninitialised until op 5 or 6 */
	unsigned e;
	for (;;) {
		e = ruw(list);
		list += 2;
		if (e == 0)
			return;
		do {
			if (voice_mask & rb(e + 1)) {
				int value = rw(e + 2);
				unsigned a = ruw(e + 4);
				switch (rb(e) & 0x7F) {
				case 0: ww(a, value); break;
				case 1: ww(a, rw(a) + value); break;
				case 2: ww(a, rw(a) - value); break;
				case 3: ww(a, fx_mul_q15(rw(a), value)); break;
				case 4: ww(a, rw(a) + fx_mul_q15(rw(a), value)); break;
				case 5: acc = value; break;
				case 6: acc = rw(a); break;
				case 7: ww(a, acc); break;
				case 8:
					for (int i = 0; i <= value; i++)
						ww(a + 2 * i, acc);
					break;
				default: fatal_error(0x2C);
				}
			}
			e += 6;
		} while (rb(e - 6) & 0x80);
	}
}

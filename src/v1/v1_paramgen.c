/* v1.1 parameter generator (F1659): turns the timed segments into the 18 parameter tracks the frame builder reads.
 *
 * The design is v3.4.1's (REFERENCE §12): each segment loads per-parameter targets into 14-byte structs, rules adjust
 * targets, loci, transition lengths and types, and each parameter's segment is written into its 128-byte ring track
 * with smoothing ramps. v1.1 has no rule table: its context rules are hard-coded routines (rules), chosen by the
 * feature bits of the previous, current and next segment. The tracks are p0-p17: AV AF AH A2-A6 AB F1-F4 B1-B3 FN F0.
 *
 * All arithmetic is 16-bit as on the 8086. mul15 is the firmware's Q15 multiply. */
#include "v1.h"

#define R V1_REC_PARAMGEN
#define PREV_SEG (R + V1_REC_DONE)  /* the segment before (8D00) */
#define CUR_SEG (R + V1_REC_CURSOR) /* the segment being generated (8D02) */
#define NEXT_SEG (R + V1_REC_AHEAD) /* the one after (8D04) */
#define PG_LAST (R + V1_REC_LAST)
#define PG_WORD (R + V1_REC_WORD)
#define PG_AMPLITUDE (R + V1_REC_AMPLITUDE) /* ESC[a: subtracted from AV, AF, AH */
#define PG_MODE (R + V1_REC_MODE)

/* the per-parameter struct (v3.4.1's layout, §12.2) */
#define PS(p) (0x9766u + 14u * (unsigned)(p))
#define TYPE 0
#define DURB 2
#define DURF 4
#define LEN 6
#define LOCB 8
#define ONSET 10
#define TARGET 12
#define POS(p) (0x96FAu + 2u * (unsigned)(p))     /* write position in the ring */
#define PREVB(p) (0x971Eu + 2u * (unsigned)(p))   /* the previous segment's boundary */
#define TRK(p) (V1_TRACKS + 2u * (unsigned)(p))   /* -> the 128-byte ring */
#define CUR(p) (0xA180u + 2u * (unsigned)(p))     /* the value the track ends on */
#define WEIGHT(p) (0xA1A4u + 2u * (unsigned)(p))  /* Q15 weight of the target in the onset (locus) */
#define SECOND(p) (0xA1B6u + 2u * (unsigned)(p))  /* p9-p11: a diphthong's second target */
#define NEXTT(p) (0xA1CEu + 2u * (unsigned)(p))   /* p9-p15: the next segment's targets */

#define STOP_AT 0x96E0     /* ring position where the frame builder must stop, or -1 */
#define STOP_WAIT 0x96DC
#define WRITE_MIN 0x96E2   /* lowest write position over the tracks */
#define WRITE_MAX 0x96E4   /* highest */
#define MARKS 0x96E6       /* 128-bit set: a segment boundary before this frame */
#define HOLD_FRAMES 0x96F8 /* frames the last hold or segment added (≤ 10) */
#define CLASS_PREV 0xA162  /* previous segment's class (0 vowel-like, 1, 2, 3 nasal), for the weights */
#define CLASS_CUR 0xA164
#define LOCUS_PREV 0xA166  /* F2 locus figures (Q15, 0xCCC0 = none) of the previous and current segment */
#define LOCUS_CUR 0xA168
#define ASP_LEN 0xA16A     /* frames of aspiration moved back into the segment before */
#define BURST_LEN 0xA16C
#define STOP_LEN 0xA16E
#define GLIDE_ADJ 0xA170
#define DIPH_DUR 0xA172    /* frames to the diphthong's second target */
#define DIPH_LEN 0xA174
#define VOT 0xA176
#define STOP_REST 0xA178
#define AH_BASE 0xA17A     /* 0x33 */
#define AH_LEVEL 0xA17C
#define AF_LEVEL 0xA17E    /* 0x39 */
#define AH_LAST 0xA184
#define HOLD_NEW 0xA204    /* the next hold node starts a new hold */
#define SEG_STATE 0xA206   /* what ends the look-ahead: 0 segment, 1 C, 2 x/i, 3 hold, 4 far, 5 flush */
#define STOP_STATE 0xA208
#define SAVED_F0 0xA20A
#define END_PAUSE 0xA20C
#define INDEX_MARKS 0xA20E /* 128-bit set: frames of a t hold with an index */
#define LOOKAHEAD 0xA21E   /* frames kept back from the frame builder: 12 (2 fast) */
#define ROOM_KEEP 0xA220   /* 15 (3) */
#define DUR_MAX 0xA222     /* transition length limit: 20 (4) */
#define HOLD_LEFT 0xA224
#define HOLD_CH 0xA226     /* byte */
#define HOLD_FIRST 0xA228
#define HOLD_VALUES 0xA22A /* 18 bytes */
#define HOLD_INDEX 0xA23C

static int ch_of(unsigned n) { return rsb(n + V1_NODE_CH); }
static int kind_of(unsigned n) { return rw(n + V1_NODE_FLAGS) & 7; }
static int stress_of(unsigned n) { return (rw(n + V1_NODE_FLAGS) >> 3) & 3; }
static int bit5(unsigned n) { return (rw(n + V1_NODE_FLAGS) >> 5) & 1; }
static int bit6(unsigned n) { return (rw(n + V1_NODE_FLAGS) >> 6) & 1; }
static int arg0_of(unsigned n) { return (rw(n + V1_NODE_FLAGS) >> 8) & 0xFF; }
static void set_arg0(unsigned n, int v) { ww(n + V1_NODE_FLAGS, (rw(n + V1_NODE_FLAGS) & 0xFF) | v << 8); }
static int feat(int c, int plane) { return rb(0x90u + (((unsigned)c | (unsigned)plane) & 0xFFFFu)); }
static int gf(int p, int f) { return rw(PS(p) + (unsigned)f); }
static void sf(int p, int f, int v) { ww(PS(p) + (unsigned)f, v); }
static void add(unsigned a, int d) { ww(a, rw(a) + d); }
static int cur(void) { return ruw(CUR_SEG); }
static int cc(void) { return ch_of(ruw(CUR_SEG)); }   /* the current segment's character */
static int pc(void) { return ch_of(ruw(PREV_SEG)); }  /* the previous one's */
static int nc(void) { return ch_of(ruw(NEXT_SEG)); }  /* the next one's */
static int bitmask(int i) { return rb(0x48A6u + 2u * (unsigned)(i & 7)); }
static void mark(unsigned set, int pos) { wb(set + (unsigned)((pos & 0x7F) >> 3), rb(set + (unsigned)((pos & 0x7F) >> 3)) | bitmask(pos)); }

/* F52A3: Q15 multiply */
int v1_pg_mul15(int a, int b) { return (int16_t)(((int32_t)(int16_t)a * (int16_t)b) >> 15); }

/* the ramp curves: k-frame fractions of 256 at [DS:28F1 + 2k], 0-terminated */
static unsigned curve(int k) { return ruw(0x28F1u + 2u * (unsigned)k); }

/* F51F0: k frames of ramp from `from` to `to`, then `to`, len frames in all */
void v1_pg_ramp(unsigned trk, unsigned pos, int k, int len, int from, int to)
{
	unsigned b = curve(k);
	int n = (len + 1) & 0xFFFF;
	uint8_t lo = (uint8_t)to, d = (uint8_t)(from - to);
	int down = (uint8_t)from < lo;
	if (down)
		d = (uint8_t)-d;
	for (;;) {
		int f = rb(b++);
		if (f == 0)
			break;
		if (--n == 0)
			return;
		int step = (d * f) >> 8;
		wb(trk + (pos & 0x7F), down ? lo - step : lo + step);
		pos = (pos & 0x7F) + 1;
	}
	while (--n != 0) {
		wb(trk + (pos & 0x7F), lo);
		pos = (pos & 0x7F) + 1;
	}
}

/* F52B4: blend the gap frames before pos back toward v along curve k */
void v1_pg_blend_back(unsigned trk, unsigned pos, int k, int gap, int v)
{
	unsigned b = curve(k);
	int n = (gap + 1) & 0xFFFF;
	for (;;) {
		pos = (pos - 1) & 0x7F;
		unsigned a = trk + pos;
		int f = rb(++b);
		if (f == 0 || --n == 0)
			return;
		uint8_t x = (uint8_t)rb(a), t = (uint8_t)v;
		if (t >= x)
			wb(a, x + ((uint8_t)(t - x) * f >> 8));
		else
			wb(a, x - ((uint8_t)(x - t) * f >> 8));
	}
}

/* F52FD: blend the len frames from pos toward v along curve k */
void v1_pg_blend_fwd(unsigned trk, unsigned pos, int k, int len, int v)
{
	unsigned b = curve(k);
	int n = (len + 1) & 0xFFFF;
	for (;;) {
		unsigned a = trk + (pos & 0x7F);
		pos = (pos & 0x7F) + 1;
		int f = rb(b++);
		if (f == 0 || --n == 0)
			return;
		uint8_t x = (uint8_t)rb(a), t = (uint8_t)v;
		if (t >= x)
			wb(a, x + ((uint8_t)(t - x) * f >> 8));
		else
			wb(a, x - ((uint8_t)(x - t) * f >> 8));
	}
}

/* F36DF: n frames of v */
void v1_pg_fill(unsigned trk, unsigned pos, int n, int v)
{
	while (n != 0) {
		wb(trk + (pos & 0x7F), v);
		pos++;
		n = s16(n - 1);
	}
}

/* F3708: add d, fading along curve k, to up to n frames before pos (d is first limited so the frame before pos
 * stays ≥ 0) */
void v1_pg_adjust_back(unsigned trk, unsigned pos, int k, int n, int d)
{
	unsigned b = curve(k);
	pos--;
	int x = rb(trk + (pos & 0x7F));
	if (s16(x + d) < 0)
		d = -x;
	for (;;) {
		int f = (rb(++b) << 7) & 0xFFFF;
		if (f == 0)
			return;
		int old = n;
		n = s16(n - 1);
		if (old == 0)
			return;
		int v = v1_pg_mul15(d, f);
		unsigned a = trk + (pos & 0x7F);
		pos--;
		wb(a, rb(a) + v);
	}
}

/* F26A1: a track byte as a working value: F1 ×4, F2 ×8 + 500, F3 and F4 ×16, the bandwidths and FN ×2 */
int v1_pg_scale(int p, int v)
{
	v &= 0xFF;
	switch (p) {
	case 9: return v << 2;
	case 10: return (v << 3) + 500;
	case 11:
	case 12: return v << 4;
	case 13:
	case 14:
	case 15:
	case 16: return v << 1;
	default: return v;
	}
}

/* F2601: the generator's context and each track's current value (its value at the write position) */
void v1_pg_state_reset(void)
{
	ww(LOCUS_CUR, 0xCCC0);
	ww(LOCUS_PREV, 0xCCC0);
	for (unsigned a = 0xA162; a < 0xA180; a += 2)
		if (a != LOCUS_PREV && a != LOCUS_CUR)
			ww(a, 0);
	for (int i = 0; i < 18; i++) {
		ww(PREVB(i), rw(WRITE_MIN) - rw(LOOKAHEAD));
		ww(POS(i), rw(WRITE_MIN));
		ww(CUR(i), v1_pg_scale(i, rsb(0x23DBu + (unsigned)i)));
	}
	ww(WRITE_MAX, rw(WRITE_MIN));
}

/* F1559: the tracks' ring bookkeeping. 1: is there room for n more frames? 2: rebase all positions by 0x400 once
 * the frame builder is past it; 3: are frames ready beyond the look-ahead? 4: the frame builder moves on. */
int v1_pg_ring(int op, int n)
{
	switch (op) {
	case 1:
		count32(0xA506);
		return s16(rw(WRITE_MAX) + n + rw(ROOM_KEEP) - rw(V1_TRACK_READ)) < 0x80;
	case 2:
		count32(0xA50A);
		if (rw(V1_TRACK_READ) > 0x400) {
			for (int i = 0; i < 18; i++) {
				add(POS(i), -0x400);
				add(PREVB(i), -0x400);
			}
			v1_irq_disable_nested();
			add(V1_TRACK_READ, -0x400);
			if (rw(STOP_AT) != -1)
				add(STOP_AT, -0x400);
			add(WRITE_MIN, -0x400);
			add(WRITE_MAX, -0x400);
			v1_irq_enable_nested();
		}
		return 1;
	case 3:
		count32(0xA50E);
		return s16(rw(V1_TRACK_READ) + rw(LOOKAHEAD)) < rw(WRITE_MIN);
	case 4:
		count32(0xA512);
		add(V1_TRACK_READ, 1);
		return 1;
	default:
		v1_fatal_error(0x21);
		return 0;
	}
}

/* F34D5: write parameter p's segment into its track by the struct's type (REFERENCE §12.4): 4 hold the target,
 * 6 ramp from the onset to the target over durF, 2 blend toward the onset; the odd types first blend the frames
 * since the previous boundary back toward locB over durB (dropped when there are none) */
void v1_pg_emit(int p)
{
	count32(0xA4D2);
	unsigned s = PS(p);
	int type = rw(s), durb = rw(s + DURB), durf = rw(s + DURF), len = rw(s + LEN);
	int locb = rw(s + LOCB), onset = rw(s + ONSET), target = rw(s + TARGET);
	if (locb < 0)
		locb = 0;
	if (onset < 0)
		onset = 0;
	unsigned trk = ruw(TRK(p)), pos = ruw(POS(p));
	int gap = s16(pos - rw(PREVB(p)));
	switch (p) {
	case 9: locb >>= 2, onset >>= 2, target >>= 2; break;
	case 10:
		locb = s16(locb - 500) >> 3, onset = s16(onset - 500) >> 3, target = s16(target - 500) >> 3;
		break;
	case 11:
	case 12: locb >>= 4, onset >>= 4, target >>= 4; break;
	case 13:
	case 14:
	case 15:
	case 16: locb >>= 1, onset >>= 1, target >>= 1; break;
	}
	if ((type & 1) && gap <= 0)
		type &= 0xFFFE;
	switch (type) {
	case 2: v1_pg_blend_fwd(trk, pos, durf, len, onset); break;
	case 3:
		v1_pg_blend_back(trk, pos, durb, gap, locb);
		v1_pg_blend_fwd(trk, pos, durf, len, onset);
		break;
	case 4: v1_pg_ramp(trk, pos, 0, len, 0, target); break;
	case 5:
		v1_pg_blend_back(trk, pos, durb, gap, locb);
		v1_pg_ramp(trk, pos, 0, len, 0, target);
		break;
	case 6: v1_pg_ramp(trk, pos, durf, len, onset, target); break;
	case 7:
		v1_pg_blend_back(trk, pos, durb, gap, locb);
		v1_pg_ramp(trk, pos, durf, len, onset, target);
		break;
	}
}

/* F3418: soften a velar or palatal F2-F3 pinch: over n frames from pos, pull a toward b (F2 down, F3 up) by an amount
 * that shrinks with their distance, scaled by w and a fading table (DS:24D3) */
void v1_pg_pinch(unsigned a, unsigned b, unsigned pos, int n, int w)
{
	pos &= 0x7F;
	for (int i = 0; i < n; i++) {
		int8_t d = (int8_t)(rb(a + pos) - rb(b + pos));
		if (d < 0)
			d = (int8_t)-d;
		int u = v1_pg_mul15(100 - d / 2, w);
		u = v1_pg_mul15(u, rw(0x24D3u + 2u * (unsigned)i)) >> 3;
		wb(a + pos, rb(a + pos) - u);
		wb(b + pos, rb(b + pos) + (u >> 1));
		pos = (pos + 1) & 0x7F;
	}
}

/* F1D06: load the targets for the current segment (and the next one's F1-F3, B1-B3) and the defaults: type 7,
 * durF from DS:27A9, the segment length, the locus weights by the class pair (DS:25FD) */
void v1_pg_load_targets(void)
{
	int c = cc(), k;
	ww(LOCUS_PREV, rw(LOCUS_CUR));
	ww(CLASS_PREV, rw(CLASS_CUR));
	ww(AH_BASE, 0x33);
	ww(AF_LEVEL, 0x39);
	ww(CLASS_CUR, feat(c, 0x100) & 2 ? 0 : feat(c, 0x100) & 1 ? 3 : feat(c, 0) & 2 ? 1 : 2);
	for (int i = 0; i < 9; i++)
		sf(i, TARGET, rsb(0x279Fu + (unsigned)i));
	sf(17, TARGET, rsb(cur() + V1_NODE_ARG1) << 1);
	sf(12, TARGET, 0xCE4);
	sf(16, TARGET, 0xFA);
	for (int i = 0; i < 18; i++) {
		sf(i, DURF, rsb(0x27A9u + (unsigned)i));
		sf(i, TYPE, 7);
		sf(i, LEN, arg0_of(cur()));
		ww(WEIGHT(i), rw(0x25FDu + 2u * (unsigned)(rw(CLASS_PREV) * 4 + rw(CLASS_CUR))));
	}
	if (feat(pc(), 0x180) & 0x10)
		ww(LOCUS_PREV, rw(0x2595u + 2u * (unsigned)rsb(ruw(0x2551) + (unsigned)pc())));
	if (!(feat(c, 0) & 2) || (feat(c, 0) & 0x10)) { /* consonants and nasals: by place */
		k = rsb(ruw(0x237D) + (unsigned)c);
		ww(LOCUS_CUR, rw(0x25D7u + 2u * (unsigned)k));
		sf(9, TARGET, rb(0x261Du + (unsigned)k) << 2);
		sf(10, TARGET, rb(0x2625u + (unsigned)k) * 8 + 500);
		sf(11, TARGET, rb(0x262Du + (unsigned)k) << 4);
		sf(13, TARGET, rb(0x2635u + (unsigned)k) << 1);
		sf(14, TARGET, rb(0x263Du + (unsigned)k) << 1);
		sf(15, TARGET, rb(0x2645u + (unsigned)k) << 1);
	} else { /* vowels and glides: by vowel */
		k = rsb(ruw(0x2551) + (unsigned)c);
		ww(LOCUS_CUR, rw(0x2553u + 2u * (unsigned)k));
		sf(9, TARGET, rb(0x264Du + (unsigned)k) << 2);
		sf(10, TARGET, rb(0x266Fu + (unsigned)k) * 8 + 500);
		sf(11, TARGET, rb(0x2691u + (unsigned)k) << 4);
		sf(13, TARGET, rb(0x26B3u + (unsigned)k) << 1);
		sf(14, TARGET, rb(0x26D5u + (unsigned)k) << 1);
		sf(15, TARGET, rb(0x26F7u + (unsigned)k) << 1);
	}
	int n = nc();
	if (!(feat(n, 0) & 2) || (feat(n, 0) & 0x10)) {
		k = rsb(ruw(0x237D) + (unsigned)n);
		ww(NEXTT(9), rb(0x261Du + (unsigned)k) << 2);
		ww(NEXTT(10), rb(0x2625u + (unsigned)k) * 8 + 500);
		ww(NEXTT(11), rb(0x262Du + (unsigned)k) << 4);
		ww(NEXTT(13), rb(0x2635u + (unsigned)k) << 1);
		ww(NEXTT(14), rb(0x263Du + (unsigned)k) << 1);
		ww(NEXTT(15), rb(0x2645u + (unsigned)k) << 1);
	} else {
		k = rsb(ruw(0x2551) + (unsigned)n);
		ww(NEXTT(9), rb(0x264Du + (unsigned)k) << 2);
		ww(NEXTT(10), rb(0x266Fu + (unsigned)k) * 8 + 500);
		ww(NEXTT(11), rb(0x2691u + (unsigned)k) << 4);
	}
	if (feat(c, 0x180) & 0x10) { /* a diphthong: its second target */
		k = rsb(ruw(0x2551) + (unsigned)c);
		ww(DIPH_DUR, rsb(0x27DDu + (unsigned)k));
		ww(SECOND(9), rb(0x2719u + (unsigned)k) << 2);
		ww(SECOND(10), rb(0x273Bu + (unsigned)k) * 8 + 500);
		ww(SECOND(11), rb(0x275Du + (unsigned)k) << 4);
	}
}

/* F387D: per-segment set-up: F0 type 6, the voice-onset delay, loci after a vowel, and boundaries after stops */
void v1_pg_setup(void)
{
	ww(0xA1C6, 0);
	sf(17, TYPE, 6);
	if ((feat(cc(), 0) & 1) && bit5(cur())) {
		sf(17, LEN, gf(17, LEN) - rw(VOT));
		ww(VOT, 0);
	}
	if ((feat(nc(), 0) & 1) && bit5(ruw(NEXT_SEG))) {
		ww(VOT, 0); /* the firmware clears it before its test, so it never adds anything */
		if (rw(ROOM_KEEP) < rw(VOT))
			ww(VOT, rw(ROOM_KEEP));
		sf(17, LEN, gf(17, LEN) + rw(VOT));
	}
	if (feat(pc(), 0x180) & 2) {
		ww(WEIGHT(11), 0x7FFE);
		ww(WEIGHT(10), 0x7FFE);
		ww(WEIGHT(9), 0x7FFE);
	}
	if (feat(pc(), 0) & 0x20)
		ww(PREVB(1), rw(POS(1)));
	if (!(feat(pc(), 0) & 4)) {
		ww(PREVB(17), rw(POS(17)));
		sf(0, TYPE, 6);
	}
}

/* F3983: voiced segments: AV */
void v1_pg_voiced(void)
{
	sf(0, TARGET, 0x3C);
	if (!(feat(cc(), 0) & 0x10) && !(feat(cc(), 0x180) & 1)) {
		if (feat(cc(), 0) & 0x40) {
			sf(0, TARGET, 0x32);
			sf(13, TARGET, gf(13, TARGET) + 100);
			if ((feat(pc(), 0x100) & 2) && (feat(nc(), 0x100) & 2))
				sf(0, TARGET, 0x36);
		}
		if (feat(cc(), 0) & 0x20)
			sf(0, TARGET, 0x32);
	} else
		sf(0, TARGET, 0x39);
}

/* F3782: aspiration moved back over the end of the segment before (ASP_LEN frames) */
void v1_pg_aspirate_back(void)
{
	if (rw(ASP_LEN) <= rw(POS(2))) {
		add(POS(2), -rw(ASP_LEN));
		v1_pg_adjust_back(ruw(TRK(2)), ruw(POS(2)), 3, rw(POS(2)) - rw(PREVB(2)),
		                  rw(AH_LEVEL) - rsb(ruw(TRK(2)) + (ruw(POS(2)) & 0x7F)));
		sf(2, LEN, gf(2, LEN) + rw(ASP_LEN));
		ww(AH_LAST, rw(AH_LEVEL));
		sf(2, DURF, 3);
		if (feat(pc(), 0) & 4)
			v1_pg_adjust_back(ruw(TRK(13)), ruw(POS(2)), 6, rw(POS(2)) - rw(PREVB(13)), 0x32);
		if (cc() == ' ')
			v1_pg_adjust_back(ruw(TRK(0)), ruw(POS(2)), 10, rw(POS(2)) - rw(PREVB(0)), -4);
		if (feat(cc(), 0x100) & 1)
			sf(2, TYPE, 5);
		if (rw(ASP_LEN) <= rw(POS(0))) {
			sf(0, LEN, gf(0, LEN) + rw(ASP_LEN));
			add(POS(0), -rw(ASP_LEN));
		}
	}
}

/* F3A32: voiceless segments: the aspiration level and, after a vowel-like segment, aspiration moved back */
void v1_pg_voiceless(void)
{
	ww(AH_LEVEL, rw(AH_BASE));
	if (cc() == ' ') {
		ww(ASP_LEN, 3);
		add(AH_LEVEL, -6);
		v1_pg_aspirate_back();
	} else if (feat(pc(), 0) & 2) {
		ww(ASP_LEN, 1);
		if ((feat(cc(), 0) & 0x40) && rw(POS(1)) > 1) {
			sf(1, LEN, gf(1, LEN) + 2);
			add(POS(1), -2);
		}
		if (feat(cc(), 0x100) & 1)
			add(AH_LEVEL, -6);
		v1_pg_aspirate_back();
	}
	sf(17, TYPE, 4);
	sf(0, TYPE, 5);
}

/* F3ACA: a burst frame at the end of the stop closure before */
void v1_pg_burst_frame(void) { v1_pg_fill(ruw(TRK(8)), rw(POS(8)) - rw(STOP_LEN), 1, 0x37); }

/* F3AEE: after a stop: the formant transition lengths by the stop's place (DS:25E7), AV type 6 */
void v1_pg_after_stop(void)
{
	for (int i = 9; i < 17; i++)
		sf(i, DURF, rsb(0x25E7u + (unsigned)rsb(ruw(0x237D) + (unsigned)pc())));
	sf(9, DURF, gf(9, DURF) / 2 + 1);
	for (int i = 0; i < 9; i++)
		sf(i, TYPE, 6);
	for (int i = 9; i < 17; i++)
		sf(i, TYPE, 7);
	if ((feat(pc(), 0) & 0x10) && (feat(cc(), 0) & 4))
		sf(0, TYPE, 4);
}

/* F4F17: a stop's release into this vowel: burst length and aspiration at the vowel's start (AH, B1 widened,
 * AV off), taken from the vowel's frames */
void v1_pg_release(void)
{
	unsigned pv = ruw(PREV_SEG);
	ww(BURST_LEN, 4);
	ww(AH_LEVEL, rw(AH_BASE));
	if (!bit5(pv)) {
		ww(BURST_LEN, 2);
		add(AH_LEVEL, -3);
	}
	if (!(feat(cc(), 0x100) & 2)) {
		ww(BURST_LEN, 5);
		if ((feat(cc(), 0x100) & 0x40) && (feat(pc(), 0x100) & 0x10))
			ww(BURST_LEN, 5);
	}
	if (cc() == 'p')
		ww(BURST_LEN, 7);
	if (feat(pc(), 0) & 0x20) {
		int d = rsb(0x25EFu + (unsigned)rsb(ruw(0x237D) + (unsigned)pc()));
		add(BURST_LEN, d);
		sf(2, LEN, gf(2, LEN) + d);
		sf(0, LEN, gf(0, LEN) + d);
		sf(13, LEN, gf(13, LEN) + d);
		add(POS(2), -d);
		add(POS(0), -d);
		add(POS(13), -d);
		ww(PREVB(2), rw(POS(2)));
	}
	if (feat(pc(), 0) & 0x40) {
		ww(BURST_LEN, 1);
		if (!(feat(cc(), 0x100) & 2))
			ww(BURST_LEN, gf(2, LEN) / 2);
	}
	if (gf(2, LEN) < rw(BURST_LEN))
		ww(BURST_LEN, gf(2, LEN));
	if (gf(0, LEN) < rw(BURST_LEN))
		ww(BURST_LEN, gf(0, LEN));
	if (rw(BURST_LEN) > 0) {
		if ((feat(cc(), 0x100) & 8) && (feat(pc(), 0x100) & 4))
			add(AH_LEVEL, 9);
		if (feat(pc(), 0) & 0x40)
			v1_pg_adjust_back(ruw(TRK(2)), ruw(POS(2)), 3, rw(POS(2)) - rw(PREVB(2)),
			                  rw(AH_LEVEL) - rw(AH_LAST));
		v1_pg_fill(ruw(TRK(0)), ruw(POS(0)), rw(BURST_LEN), 0);
		v1_pg_fill(ruw(TRK(2)), ruw(POS(2)), rw(BURST_LEN), rw(AH_LEVEL));
		v1_pg_fill(ruw(TRK(13)), ruw(POS(13)), rw(BURST_LEN), 0x4B);
		ww(CUR(13), 0x96);
		ww(AH_LAST, rw(AH_LEVEL));
		sf(0, TYPE, 6);
		sf(2, LEN, gf(2, LEN) - rw(BURST_LEN));
		add(POS(2), rw(BURST_LEN));
		sf(0, LEN, gf(0, LEN) - rw(BURST_LEN));
		add(POS(0), rw(BURST_LEN));
		sf(13, LEN, gf(13, LEN) - rw(BURST_LEN));
		add(POS(13), rw(BURST_LEN));
	}
}

/* F3B9A: vowels: F4 by vowel class, the release from a stop, and loci after glides and liquids */
void v1_pg_vowel_start(void)
{
	unsigned pv = ruw(PREV_SEG);
	int c = cc(), p = pc();
	if (!(feat(c, 0x180) & 0x20)) {
		if (!(feat(c, 0x100) & 8) && !(feat(c, 0x100) & 0x40)) {
			if (c == 'E') {
				sf(15, TARGET, 0x14A);
				sf(12, TARGET, gf(12, TARGET) + 100);
			}
		} else
			sf(12, TARGET, gf(12, TARGET) + 300);
	} else
		sf(12, TARGET, gf(12, TARGET) - 100);
	if ((feat(c, 0x180) & 1) && (feat(p, 0) & 0x40) && (feat(p, 0) & 4))
		v1_pg_release();
	else if (!(feat(p, 0) & 4) && p != ' ' && p != 'H' && !bit6(pv))
		v1_pg_release();
	if ((feat(pc(), 0) & 0x20) || (feat(pc(), 0) & 0x40))
		sf(1, LEN, gf(1, LEN) - rw(GLIDE_ADJ));
	if (feat(pc(), 0x180) & 1) {
		int d = 7;
		if ((feat(cc(), 0x180) & 0x20) && pc() == 'Y')
			d = 11;
		for (int i = 9; i < 17; i++)
			sf(i, DURF, d);
		if (feat(pc(), 0x100) & 8)
			sf(11, DURF, 9);
	}
	if ((feat(pc(), 0x200) & 4) || (feat(pc(), 0x180) & 0x40)) {
		ww(WEIGHT(9), 0x2CD8);
		ww(WEIGHT(10), 0x2CD8);
		ww(WEIGHT(11), 0x2CD8);
	}
	if ((feat(cc(), 0x180) & 0x20) && (feat(pc(), 0x180) & 4))
		sf(10, DURF, gf(10, DURF) + 5);
}

/* F3DD9: vowels: coarticulation of F2/F3 with the neighbours, shortening by duration toward a neutral vowel
 * (DS:25F7-25FB, by length DS:277F) and the diphthong glide to the second target */
void v1_pg_vowel(void)
{
	int c = cc(), p;
	if (c == 'p')
		sf(13, TARGET, gf(13, TARGET) + 0x2F);
	if (!(feat(c, 0x100) & 0x20) || !(feat(c, 0x180) & 0x80) || !(feat(nc(), 0x100) & 0x80)) {
		if ((feat(nc(), 0x100) & 0x40) && ((feat(c, 0x180) & 0x40) || (feat(c, 0x100) & 0x20))) {
			ww(SECOND(10), rw(SECOND(10)) - 300);
			if (c != 'I' && c != 'y')
				sf(10, TARGET, gf(10, TARGET) - 300);
		}
	} else
		ww(SECOND(10), rb(0x266Fu + (unsigned)rsb(ruw(0x2551) + (unsigned)c)) * 8 + 500);
	p = pc();
	if ((feat(p, 0x180) & 1) && !(feat(p, 0x180) & 4) && ((feat(c, 0x100) & 0x20) || (feat(c, 0x180) & 0x80))) {
		sf(10, TARGET, gf(10, TARGET) - 0x15E);
		if (gf(10, TARGET) < gf(9, TARGET) + 0xFA)
			sf(10, TARGET, gf(9, TARGET) + 0xFA);
	}
	if (c == 'E' && (feat(p, 0x100) & 0x40))
		sf(10, TARGET, gf(10, TARGET) - 0xFA);
	else if (feat(p, 0x100) & 8) {
		sf(11, TARGET, s16(gf(11, TARGET) * 3 + 0x708) / 4);
		ww(SECOND(11), s16(rw(SECOND(11)) * 3 + 0x708) / 4);
		if (feat(c, 0x100) & 0x20) {
			sf(10, TARGET, s16(gf(10, TARGET) * 3 + 0x578) / 4);
			ww(SECOND(10), s16(rw(SECOND(10)) * 3 + 0x578) / 4);
		}
	}
	if ((feat(nc(), 0) & 0x20) && (feat(nc(), 0x100) & 4) && c == 'U')
		ww(SECOND(10), rw(SECOND(10)) + 400);
	if ((feat(p, 0x180) & 8) && (feat(c, 0x180) & 0x20))
		sf(10, DURF, gf(10, DURF) + 5);
	if ((feat(c, 0x180) & 0x40) && (feat(nc(), 0x100) & 4)) {
		ww(SECOND(10), rw(SECOND(10)) - 0x96);
		if (rw(SECOND(10)) < gf(10, TARGET))
			ww(SECOND(10), gf(10, TARGET));
	}
	if (feat(c, 0x100) & 8) { /* short vowels move toward the neutral vowel */
		unsigned len = (unsigned)(arg0_of(cur()) * 10);
		if (len > 0xFF)
			len = 0xFF;
		int w = rw(0x277Fu + 2u * (len / 16));
		sf(9, TARGET, gf(9, TARGET) + v1_pg_mul15(rw(0x25F7) - gf(9, TARGET), w));
		sf(10, TARGET, gf(10, TARGET) + v1_pg_mul15(rw(0x25F9) - gf(10, TARGET), w));
		sf(11, TARGET, gf(11, TARGET) + v1_pg_mul15(rw(0x25FB) - gf(11, TARGET), w));
		ww(SECOND(9), rw(SECOND(9)) + v1_pg_mul15(rw(0x25F7) - rw(SECOND(9)), w));
		ww(SECOND(10), rw(SECOND(10)) + v1_pg_mul15(rw(0x25F9) - rw(SECOND(10)), w));
		ww(SECOND(11), rw(SECOND(11)) + v1_pg_mul15(rw(0x25FB) - rw(SECOND(11)), w));
	}
	if (feat(c, 0x80) & 0x10) { /* F3 from the neighbours */
		sf(11, TARGET, v1_pg_mul15(gf(11, TARGET) + rw(CUR(11)) + rw(NEXTT(11)), 0x2A48));
		return;
	}
	if (feat(c, 0x180) & 8)
		sf(10, DURF, gf(10, DURF) - 2);
	if (!(feat(c, 0x180) & 0x10))
		return;
	/* a diphthong: first target, then a glide to the second */
	int inh = rsb(ruw(0x22B9) + (unsigned)c);
	ww(DIPH_DUR, s16((int16_t)((int32_t)gf(9, LEN) * rw(DIPH_DUR) / inh) + rw(DIPH_DUR)) >> 1);
	ww(DIPH_LEN, (int16_t)((int32_t)gf(9, LEN) * rsb(0x27BBu + (unsigned)rsb(ruw(0x2551) + (unsigned)c)) / inh));
	if (pc() == 'H' && !(feat(c, 0x180) & 0x80))
		ww(DIPH_LEN, v1_pg_mul15(rw(DIPH_LEN), 0x55D8));
	for (int i = 9; i < 12; i++) {
		int prevb = rw(PREVB(i)), durf = gf(i, DURF), durb = gf(i, DURB), onset = gf(i, ONSET);
		int locb = gf(i, LOCB), target = gf(i, TARGET), len = gf(i, LEN), pos = rw(POS(i));
		sf(i, TYPE, 7);
		if ((feat(c, 0x200) & 2) && !bit5(cur()))
			target = s16(target - (s16(target - rw(SECOND(i))) >> 2));
		int mid = s16(rw(SECOND(i)) + target) / 2, start = mid;
		if (c == 'I' || c == 'y') {
			if (i == 11)
				start = c == 'y' ? mid - 400 : mid - 200;
			else if (i == 10)
				start = mid + 200;
			else if (c == 'y')
				start = mid + 0x46;
		} else if (c == 'U' && i == 11)
			add(DIPH_LEN, -7);
		else if ((c == '4' || c == 'k') && i == 11)
			add(DIPH_LEN, -3);
		if (rw(DIPH_LEN) < 1)
			start = target;
		else {
			if (gf(i, LEN) <= rw(DIPH_LEN)) {
				start = rw(SECOND(i));
				sf(i, TYPE, 3);
				ww(DIPH_LEN, gf(i, LEN));
			}
			if (i == 9)
				v1_pg_fill(ruw(TRK(9)), ruw(POS(9)), rw(DIPH_LEN), target >> 2);
			else if (i == 10)
				v1_pg_fill(ruw(TRK(10)), ruw(POS(10)), rw(DIPH_LEN), s16(target - 500) >> 3);
			else if (i == 11)
				v1_pg_fill(ruw(TRK(11)), ruw(POS(11)), rw(DIPH_LEN), target >> 4);
			sf(i, LEN, gf(i, LEN) - rw(DIPH_LEN));
			add(POS(i), rw(DIPH_LEN));
		}
		ww(PREVB(i), pos);
		sf(i, TARGET, rw(SECOND(i)));
		sf(i, LOCB, start);
		sf(i, ONSET, start);
		sf(i, DURF, rw(DUR_MAX) < rw(DIPH_DUR) ? rw(DUR_MAX) : rw(DIPH_DUR));
		sf(i, DURB, gf(i, DURF));
		v1_pg_emit(i);
		sf(i, TARGET, target);
		sf(i, LEN, len);
		ww(POS(i), pos);
		sf(i, TYPE, 3);
		ww(PREVB(i), prevb);
		sf(i, DURF, durf);
		sf(i, DURB, durb);
		sf(i, ONSET, onset);
		sf(i, LOCB, locb);
	}
}

/* F45CE: glides, liquids and H: voiced H between voiced sounds, R and L formants pulled toward the next vowel */
void v1_pg_glide(void)
{
	unsigned c0 = cur();
	int p = pc();
	if ((feat(p, 0x100) & 2) ||
	    ((feat(p, 0) & 2) && stress_of(ruw(NEXT_SEG)) != 2 && stress_of(ruw(NEXT_SEG)) != 3)) {
		if (cc() == 'H')
			wb(c0 + V1_NODE_CH, 'd');
		else if (cc() == 'h')
			wb(c0 + V1_NODE_CH, 'W');
	}
	if (feat(cc(), 0x200) & 0x20) {
		sf(2, TARGET, rw(AH_BASE));
		sf(13, TARGET, 0x96);
	}
	if (cc() == 'd') {
		sf(0, TARGET, 0x30);
		sf(2, TARGET, gf(2, TARGET) + 3);
	}
	if (pc() == ' ' && (feat(cc(), 0) & 4))
		sf(0, TARGET, 0x38);
	if (cc() == 'R') {
		int nv = rb(0x266Fu + (unsigned)rsb(ruw(0x2551) + (unsigned)nc())) * 8 + 500;
		sf(10, TARGET, gf(10, TARGET) - v1_pg_mul15(gf(10, TARGET), 0x2008));
		sf(10, TARGET, gf(10, TARGET) + v1_pg_mul15(nv, 0x2008));
		sf(11, TARGET, gf(10, TARGET) + 0xFA);
	} else if (cc() == 'L') {
		int nv = rb(0x266Fu + (unsigned)rsb(ruw(0x2551) + (unsigned)nc())) * 8 + 500;
		sf(0, TARGET, gf(0, TARGET) - 3);
		sf(10, TARGET, gf(10, TARGET) - v1_pg_mul15(gf(10, TARGET), 0xCD0));
		sf(10, TARGET, gf(10, TARGET) + v1_pg_mul15(nv, 0xCD0));
	}
	if ((feat(cc(), 0x100) & 0x40) && (feat(pc(), 0) & 2) && !(feat(pc(), 0) & 0x10)) {
		ww(WEIGHT(11), 0x7350);
		ww(WEIGHT(10), 0x7350);
		ww(WEIGHT(9), 0x7350);
	}
	int d = 9;
	if (feat(cc(), 0x180) & 1) {
		d = 7;
		if (feat(pc(), 0x180) & 1)
			d = 5;
	}
	for (int i = 9; i < 17; i++)
		sf(i, DURF, d);
	if (feat(cc(), 0x100) & 8)
		sf(11, DURF, 9);
}

/* F485D: obstruents and nasals: AV off, the parallel amplitudes A2-A6 and AB by manner and place */
void v1_pg_consonant(void)
{
	int c = cc(), n = nc();
	if (!(feat(n, 0x100) & 2))
		sf(0, TARGET, gf(0, TARGET) - 0x1E);
	if (!(feat(c, 0) & 4) && !(feat(c, 0x180) & 2))
		sf(9, TARGET, gf(9, TARGET) + 100);
	if (feat(c, 0x100) & 0x10) {
		sf(7, TARGET, 0);
		sf(6, TARGET, 0);
		sf(5, TARGET, 0);
		sf(4, TARGET, 0);
		sf(3, TARGET, 0);
		sf(8, TARGET, 0x48);
		if (c == 'F' || c == 'V') {
			sf(10, TARGET, 0x46A);
			sf(8, TARGET, 0x40);
		}
	} else if (feat(c, 0x200) & 0x10) {
		sf(6, TARGET, 0);
		sf(5, TARGET, 0);
		sf(4, TARGET, 0);
		sf(3, TARGET, 0);
		sf(7, TARGET, 0x3E);
		sf(8, TARGET, 0x3A);
	} else if (feat(c, 0x100) & 4) {
		sf(5, TARGET, 0);
		sf(4, TARGET, 0);
		sf(3, TARGET, 0);
		sf(6, TARGET, 0x32);
		sf(7, TARGET, 0x52);
		sf(10, TARGET, gf(10, TARGET) - 200);
		if ((feat(c, 0) & 0x20) && !(feat(n, 0x100) & 4)) {
			sf(4, TARGET, 0x34);
			sf(5, TARGET, 0x3C);
			sf(6, TARGET, 0x46);
			sf(7, TARGET, 0x49);
			if (feat(n, 0x100) & 8) {
				sf(4, TARGET, 0x43);
				sf(7, TARGET, 0x41);
				sf(6, TARGET, 0x41);
			}
		}
	} else if (feat(c, 0x180) & 4) {
		sf(3, TARGET, 0);
		sf(4, TARGET, 0x48);
		sf(5, TARGET, 0x34);
		sf(6, TARGET, 0x3C);
		sf(7, TARGET, 0x3F);
		sf(12, TARGET, gf(12, TARGET) - 0x96);
	} else if (feat(c, 0x100) & 0x80) {
		sf(3, TARGET, 0x55 - rw(NEXTT(9)) / 0x32);
		sf(5, TARGET, 0x19);
		sf(4, TARGET, 0x19);
		sf(6, TARGET, 0x32);
		sf(7, TARGET, 10);
		if (feat(n, 0x100) & 0x20) {
			sf(3, TARGET, 0x1E);
			sf(4, TARGET, 0x3C);
			sf(5, TARGET, 0x14);
			sf(6, TARGET, 0x32);
			sf(7, TARGET, 5);
			ww(LOCUS_CUR, 0x3340);
		}
		if (!(feat(n, 0x100) & 2))
			add(AF_LEVEL, -5);
	}
	if (feat(n, 0) & 2) {
		ww(GLIDE_ADJ, 2);
		if (feat(c, 0) & 0x20)
			ww(GLIDE_ADJ, 1);
		if (arg0_of(ruw(NEXT_SEG)) < 3)
			ww(GLIDE_ADJ, arg0_of(ruw(NEXT_SEG)) - 1);
		sf(1, LEN, gf(1, LEN) + rw(GLIDE_ADJ));
	}
	if ((feat(c, 0) & 4) && (!(feat(pc(), 0) & 4) || !(feat(pc(), 0) & 2)))
		sf(0, TARGET, gf(0, TARGET) - 0x1E);
}

/* F4B9C: fricatives: AF */
void v1_pg_fricative(void)
{
	sf(9, TARGET, gf(9, TARGET) + 100);
	sf(1, TARGET, 0x3C);
	if (feat(cc(), 0) & 4)
		sf(1, TARGET, gf(1, TARGET) - 5);
	if (nc() == ' ')
		sf(1, TARGET, gf(1, TARGET) - 4);
	if (!(feat(pc(), 0) & 0x40) && !(feat(pc(), 0x100) & 1) && !(feat(nc(), 0) & 0x40) && gf(1, LEN) < 8)
		sf(1, TARGET, gf(1, TARGET) + (8 - gf(1, LEN)));
	if (!(feat(cc(), 0) & 4))
		sf(2, TARGET, rw(AH_BASE) - 0x14);
}

/* F4C4B: stops: transition types and lengths by place (DS:25E7) */
void v1_pg_stop(void)
{
	if (!(feat(pc(), 0x100) & 1)) {
		for (int i = 0; i < 10; i++)
			sf(i, TYPE, 5);
		for (int i = 10; i < 17; i++)
			sf(i, TYPE, 7);
	}
	int d = rsb(0x25E7u + (unsigned)rsb(ruw(0x237D) + (unsigned)cc()));
	if (feat(pc(), 0x200) & 1)
		d = 5;
	for (int i = 9; i < 17; i++)
		sf(i, DURF, d);
	sf(9, DURF, gf(9, DURF) / 2 + 1);
}

/* F4D00: a stop's closure: AF silent after the release length (DS:25EF by place), and the frication level */
void v1_pg_closure(void)
{
	int c = cc();
	ww(STOP_LEN, rsb(0x25EFu + (unsigned)rsb(ruw(0x237D) + (unsigned)c)));
	if (!(feat(c, 0) & 4) && !(feat(c, 0x100) & 0x10))
		add(STOP_LEN, 1);
	if (c == 'T' && (feat(nc(), 0x100) & 8))
		add(STOP_LEN, 1);
	ww(STOP_REST, gf(1, LEN));
	if (rw(STOP_REST) <= rw(STOP_LEN))
		ww(STOP_LEN, rw(STOP_REST) / 2);
	add(STOP_LEN, 1);
	if (rw(STOP_LEN) < 1)
		ww(STOP_LEN, 1);
	v1_pg_adjust_back(ruw(TRK(1)), ruw(POS(1)), 3, rw(POS(1)) - rw(PREVB(1)), -0x14);
	if (rw(STOP_LEN) < gf(1, LEN)) {
		ww(STOP_REST, gf(1, LEN) - rw(STOP_LEN));
		v1_pg_fill(ruw(TRK(1)), ruw(POS(1)), rw(STOP_REST), 0);
		add(POS(1), rw(STOP_REST));
	}
	ww(0xA182, 0);
	sf(1, LEN, rw(STOP_LEN));
	if (!(feat(cc(), 0) & 4))
		add(AF_LEVEL, 6);
	if (!bit5(cur())) {
		add(AF_LEVEL, -3);
		if (feat(pc(), 0) & 0x10)
			add(AF_LEVEL, -3);
	}
	if (nc() == 'p' || (feat(nc(), 0) & 0x40))
		add(AF_LEVEL, -3);
	sf(1, TARGET, rw(AF_LEVEL));
	sf(1, TYPE, 4);
}

/* F4EA4: nasals */
void v1_pg_nasal(void)
{
	if (cc() == '~')
		sf(11, TARGET, gf(11, TARGET) + 0x96);
	sf(9, TARGET, 0x208);
	sf(13, TARGET, 0xA0);
	sf(14, TARGET, 0x96);
	sf(16, TARGET, 0x1A4);
	sf(16, TYPE, 5);
	ww(WEIGHT(16), 0x7FFE);
	sf(0, TYPE, 4);
	sf(0, TARGET, gf(0, TARGET) - 6);
	if (cc() == 'N')
		sf(10, TARGET, 0x578);
	else if (cc() == 'M')
		sf(10, TARGET, gf(10, TARGET) + 0x96);
}

static void swap(unsigned a, unsigned b)
{
	int t = rw(a);
	ww(a, rw(b));
	ww(b, t);
}

/* the loci work on the boundary as seen from its more consonant-like side: when the current segment is the more
 * vowel-like one (by class) the current and previous F1-F3 swap roles, and the weights invert */
static void loci_flip(void)
{
	swap(CUR(9), PS(9) + TARGET);
	swap(CUR(10), PS(10) + TARGET);
	swap(CUR(11), PS(11) + TARGET);
	ww(WEIGHT(9), 0x7FFE - rw(WEIGHT(9)));
	ww(WEIGHT(10), 0x7FFE - rw(WEIGHT(10)));
	ww(WEIGHT(11), 0x7FFE - rw(WEIGHT(11)));
}

/* F2A0A: F1-F3 loci and weights at the boundary between the segment on the consonant side (con) and the vowel
 * side (vow) */
void v1_pg_loci(void)
{
	unsigned vow, con;
	int flip = rw(CLASS_PREV) < rw(CLASS_CUR);
	if (flip) {
		con = ruw(CUR_SEG);
		vow = ruw(PREV_SEG);
		loci_flip();
	} else {
		vow = ruw(CUR_SEG);
		con = ruw(PREV_SEG);
	}
	int v = ch_of(vow), k = ch_of(con);
	if (feat(v, 0x100) & 8) {
		ww(WEIGHT(11), 0x6018);
		ww(WEIGHT(10), 0x6018);
	}
	if ((feat(k, 0x180) & 4) && !(feat(k, 0) & 2)) {
		ww(WEIGHT(10), 0x19A0);
		sf(10, DURF, gf(10, DURF) + 3);
	}
	if ((feat(v, 0x180) & 4) && (feat(k, 0x80) & 8))
		sf(11, TARGET, gf(11, TARGET) + 300);
	if (!(feat(v, 0x100) & 1) && (feat(k, 0x100) & 1)) {
		ww(WEIGHT(9), 0x4010);
		ww(WEIGHT(11), 0);
		ww(WEIGHT(10), 0);
		if (feat(k, 0) & 0x10) {
			ww(WEIGHT(9), 0);
			sf(16, TYPE, 6);
		}
		if (feat(k, 0x100) & 0x10) {
			ww(WEIGHT(10), 0x19A0);
			ww(WEIGHT(11), 0x59B0);
			if (feat(v, 0x100) & 0x20) {
				ww(WEIGHT(10), 0x5348);
				ww(WEIGHT(11), 0x19A0);
			}
			if (feat(v, 0x100) & 8) {
				ww(WEIGHT(10), 0x19A0);
				ww(CUR(11), 0x6D6);
			}
		}
		if (feat(k, 0x100) & 4) {
			ww(CUR(10), 0x640);
			ww(CUR(11), 0xA3C);
			if (k == 'N')
				ww(CUR(10), 0x578);
			if (!(feat(v, 0x100) & 0x40)) {
				if (feat(v, 0x100) & 8)
					ww(CUR(11), 0x8FC);
			} else
				ww(CUR(10), 0x41A);
		}
		if (feat(k, 0x100) & 0x80) {
			ww(CUR(10), gf(9, TARGET) * 2 + gf(10, TARGET) - 600);
			ww(CUR(11), rw(CUR(10)) + 400);
			if (feat(v, 0x100) & 0x10)
				ww(CUR(11), rw(CUR(11)) + 400);
			if ((feat(v, 0x100) & 0x20) || v == 'o') {
				ww(CUR(10), rb(0x262A) * 8 + 500);
				ww(CUR(11), rb(0x2632) << 4);
			}
			if (k == '~') {
				ww(CUR(10), s16(gf(10, TARGET) + rw(CUR(10))) / 2);
				ww(CUR(11), s16(gf(11, TARGET) + rw(CUR(11))) / 2);
			}
			if (v == 'E' || v == 'i') {
				ww(CUR(10), rw(CUR(10)) + 0xFA);
				ww(CUR(11), gf(11, TARGET) + 0x32);
			}
		}
	}
	if (!(feat(v, 0) & 0x10) && (feat(k, 0) & 0x10))
		ww(WEIGHT(16), 0);
	if (flip)
		loci_flip();
}

/* F2DC5: W, R-like and nasal contexts: F2-F4 loci and targets */
void v1_pg_loci_special(void)
{
	int p = pc();
	if (cc() == 'W' && (feat(p, 0x100) & 4) && (feat(p, 0x100) & 1)) {
		ww(CUR(10), 0x4B0);
		ww(CUR(11), 0x802);
		ww(CUR(12), 0x9C4);
		ww(WEIGHT(12), 0);
		sf(12, DURF, 3);
	}
	if ((feat(p, 0x100) & 8) || (feat(p, 0x200) & 2)) {
		if (!(feat(cc(), 0x100) & 4)) {
			if (feat(cc(), 0x100) & 0x80) {
				sf(10, TARGET, 0x6A4);
				sf(11, TARGET, 0x76C);
			}
		} else {
			sf(12, TARGET, gf(12, TARGET) - 500);
			sf(10, TARGET, 0x73A);
			sf(11, TARGET, 0x898);
		}
	}
	if (!(feat(cc(), 0x100) & 8)) {
		if ((feat(p, 0x180) & 0x40) && (feat(cc(), 0x100) & 4))
			sf(10, TARGET, 0x76C);
		else if ((feat(cc(), 0x100) & 0x80) && (feat(p, 0x200) & 4)) {
			sf(10, TARGET, 900);
			sf(11, TARGET, 2000);
		} else if ((feat(cc(), 0x180) & 8) && (feat(p, 0x100) & 4) && (feat(p, 0x100) & 1))
			ww(CUR(10), 0x73A);
	} else if (!(feat(p, 0x100) & 0x10)) {
		if (!(feat(p, 0x100) & 4)) {
			if (feat(p, 0x100) & 0x80) {
				ww(CUR(10), 0x44C);
				ww(CUR(11), 0x514);
			}
		} else
			ww(CUR(11), 0x834);
	} else
		ww(CUR(11), 0x6A4);
}

/* F2FDC: finish the structs: a flap's formants between its neighbours, F(n+1) ≥ F(n) + 200 Hz for F1-F3 when voiced,
 * durations limited, the onsets (locus = current·(1 − w) + target·w), F0's transition */
void v1_pg_finish(void)
{
	if (cc() == 't') {
		sf(0, TARGET, gf(0, TARGET) - 0x1E);
		sf(9, TARGET, v1_pg_mul15(gf(9, TARGET) + rw(CUR(9)) + rw(NEXTT(9)), 0x2A48));
		sf(10, TARGET, v1_pg_mul15(gf(10, TARGET) + rw(CUR(10)) + rw(NEXTT(10)), 0x2A48));
		sf(11, TARGET, v1_pg_mul15(gf(11, TARGET) + rw(CUR(11)) + rw(NEXTT(11)), 0x2A48));
	}
	if (feat(pc(), 0x180) & 8)
		sf(10, DURF, gf(10, DURF) - 2);
	if (gf(0, TARGET) > 0 || gf(2, TARGET) > 0)
		for (int i = 9; i < 12; i++)
			if (gf(i + 1, TARGET) < gf(i, TARGET) + 200)
				sf(i + 1, TARGET, gf(i, TARGET) + 200);
	for (int i = 0; i < 18; i++) {
		sf(i, DURF, rw(DUR_MAX) < gf(i, DURF) ? rw(DUR_MAX) : gf(i, DURF));
		sf(i, DURB, gf(i, DURF));
		sf(i, LEN, gf(i, LEN) < 0x3D ? gf(i, LEN) : 0x3C);
		int a = v1_pg_mul15(rw(CUR(i)), 0x7FFE - rw(WEIGHT(i)));
		int b = v1_pg_mul15(gf(i, TARGET), rw(WEIGHT(i)));
		sf(i, ONSET, b + a);
		sf(i, LOCB, gf(i, ONSET));
	}
	if (feat(pc(), 0x100) & 0x40) {
		sf(9, LOCB, gf(9, LOCB) - 0x32);
		sf(9, ONSET, gf(9, ONSET) + 0x32);
		sf(10, LOCB, gf(10, LOCB) - 0x32);
		sf(10, ONSET, gf(10, ONSET) + 0x32);
	}
	sf(17, DURF, gf(17, LEN));
	if (gf(17, DURF) < 7)
		sf(17, DURF, 7);
	if (rw(DUR_MAX) < gf(17, DURF))
		sf(17, DURF, rw(DUR_MAX));
	sf(17, DURB, 0);
}

/* F3208: the amplitudes' onsets: halfway from the current value to the target, within k of either */
void v1_pg_amp_onsets(void)
{
	for (int i = 0; i < 9; i++) {
		sf(i, ONSET, s16(gf(i, TARGET) + rw(CUR(i))) / 2);
		int k = 9;
		if (cc() == ' ')
			k = 0xF;
		if (i == 2)
			k = 0;
		if (!(feat(pc(), 0) & 4))
			k = 10;
		if (i > 2)
			k = 0;
		if (gf(i, ONSET) < gf(i, TARGET) - k)
			sf(i, ONSET, gf(i, TARGET) - k);
		if (gf(i, ONSET) < rw(CUR(i)) - k)
			sf(i, ONSET, rw(CUR(i)) - k);
		sf(i, LOCB, gf(i, ONSET));
	}
	if (feat(pc(), 0) & 4) {
		if (!(feat(cc(), 0) & 4) || !(feat(cc(), 0) & 2))
			sf(0, LOCB, gf(0, LOCB) - 6);
		if ((feat(cc(), 0) & 2) && !(feat(cc(), 0) & 0x10) && ((feat(pc(), 0x100) & 1) || (feat(pc(), 0) & 0x40)))
			sf(0, LOCB, gf(0, LOCB) - 4);
		if ((feat(cc(), 0) & 4) && (feat(cc(), 0) & 2) && (feat(pc(), 0) & 4) && !(feat(pc(), 0) & 2))
			sf(0, ONSET, gf(0, ONSET) - 9);
	}
}

/* F21BF: generate one segment: targets, the context rules, the tracks */
void v1_pg_segment(void)
{
	v1_pg_load_targets();
	v1_pg_setup();
	if (!(feat(cc(), 0) & 4))
		v1_pg_voiceless();
	else
		v1_pg_voiced();
	if (!(feat(cc(), 0x100) & 1) && (feat(pc(), 0x100) & 1)) {
		if (feat(pc(), 0) & 0x20)
			v1_pg_burst_frame();
		v1_pg_after_stop();
	}
	if (feat(cc(), 0) & 2) {
		v1_pg_vowel_start();
		if (!(feat(cc(), 0x100) & 2))
			v1_pg_glide();
		else
			v1_pg_vowel();
	}
	if (!(feat(cc(), 0) & 2) || (feat(cc(), 0) & 0x10)) {
		if ((feat(cc(), 0) & 0x20) || (feat(cc(), 0) & 0x40)) {
			v1_pg_consonant();
			if (feat(cc(), 0) & 0x40)
				v1_pg_fricative();
		}
		if (feat(cc(), 0x100) & 1) {
			v1_pg_stop();
			if ((feat(cc(), 0) & 0x20) && !(feat(nc(), 0x100) & 1) && !bit6(cur()) && nc() != ' ')
				v1_pg_closure();
			if (feat(cc(), 0) & 0x10)
				v1_pg_nasal();
		}
	}
	v1_pg_loci();
	v1_pg_loci_special();
	v1_pg_finish();
	v1_pg_amp_onsets();
	if (!(feat(cc(), 0) & 4)) { /* voiceless: F0 off, kept for the next voiced segment */
		ww(SAVED_F0, gf(17, TARGET));
		sf(17, TARGET, 0);
	}
	if (rsb(cur() + V1_NODE_ARG1) == 0) { /* F0 0 (pitch 0): whisper, AV becomes AH */
		sf(2, TARGET, gf(0, TARGET));
		sf(0, TARGET, 0);
	}
	for (int i = 0; i < 3; i++) {
		int v = gf(i, TARGET) - rw(PG_AMPLITUDE);
		sf(i, TARGET, v < 0 ? 0 : v);
	}
	for (int i = 0; i < 18; i++)
		v1_pg_emit(i);
	if (bit6(cur())) { /* a glottal stop: an F0 dip */
		sf(17, LOCB, 0x28);
		sf(17, ONSET, 0x28);
		sf(17, DURF, 2);
		sf(17, DURB, 5);
		sf(17, TYPE, 3);
		v1_pg_emit(17);
	}
	int a = rw(LOCUS_PREV) < 0 ? -rw(LOCUS_PREV) : rw(LOCUS_PREV);
	if (a > 0x199F) {
		int b = rw(LOCUS_CUR) < 0 ? -rw(LOCUS_CUR) : rw(LOCUS_CUR);
		if (b > 0x199F) {
			int d = rw(LOCUS_CUR) / 2 - rw(LOCUS_PREV) / 2;
			if ((d >= 0 && rw(LOCUS_PREV) < 1) || (d < 0 && rw(LOCUS_CUR) < 1)) {
				int k = rw(CLASS_CUR) < rw(CLASS_PREV) ? 3 : rw(CLASS_PREV) < rw(CLASS_CUR) ? 9 : 6;
				int n = k + gf(10, LEN);
				if (n > 0xC)
					n = 0xC;
				if (d < 0)
					d = -d;
				v1_pg_pinch(ruw(TRK(10)), ruw(TRK(11)), rw(POS(10)) - k, n, d);
			}
		}
	}
}

/* F2520: a silence segment after node n: 15 frames for a C command (kind 1), else look-ahead + room + 10 frames,
 * followed by a 4-frame one; both take n's F0 */
unsigned v1_pg_insert_silence(unsigned n, int kind)
{
	n = v1_node_insert(n, 1, 4, ' ');
	if (kind == 1)
		set_arg0(n, 0xF);
	else
		set_arg0(n, rw(LOOKAHEAD) + rw(ROOM_KEEP) + 10);
	wb(n + V1_NODE_ARG1, rb(ruw(CUR_SEG) + V1_NODE_ARG1));
	ww(n + V1_NODE_FLAGS, rw(n + V1_NODE_FLAGS) & 0xFFDF);
	ww(n + V1_NODE_FLAGS, rw(n + V1_NODE_FLAGS) & 0xFFBF);
	ww(n + V1_NODE_FLAGS, rw(n + V1_NODE_FLAGS) & 0xFFE7);
	ww(n + V1_NODE_FLAGS, rw(n + V1_NODE_FLAGS) | 0x10);
	unsigned m = v1_node_insert(n, 1, 4, ' ');
	set_arg0(m, 4);
	wb(m + V1_NODE_ARG1, rb(ruw(CUR_SEG) + V1_NODE_ARG1));
	ww(m + V1_NODE_FLAGS, rw(m + V1_NODE_FLAGS) & 0xFFDF);
	ww(m + V1_NODE_FLAGS, rw(m + V1_NODE_FLAGS) & 0xFFBF);
	ww(m + V1_NODE_FLAGS, rw(m + V1_NODE_FLAGS) | 0x18);
	if (ruw(CUR_SEG) == 0) {
		ww(CUR_SEG, n);
		n = v1_node_next(n);
	}
	ww(STOP_AT, -1);
	ww(STOP_WAIT, 0);
	count32(0xA4D6);
	return n;
}

/* F27DB: find the segment after the current one, adding silence where the speech ends (a C command, an x or a
 * stopping index marker, a hold, nothing more after a while). Returns it, or 0 to wait. */
unsigned v1_pg_next_segment(void)
{
	unsigned n, p = ruw(PREV_SEG);
	if (ruw(CUR_SEG) != 0 && (p == ruw(CUR_SEG) || kind_of(p) != 4)) {
		n = v1_node_insert(ruw(CUR_SEG), 0, 4, ' ');
		ww(PREV_SEG, n);
		set_arg0(n, 4);
		wb(n + V1_NODE_ARG1, rb(ruw(CUR_SEG) + V1_NODE_ARG1));
		ww(n + V1_NODE_FLAGS, rw(n + V1_NODE_FLAGS) & 0xFFDF);
		ww(n + V1_NODE_FLAGS, rw(n + V1_NODE_FLAGS) & 0xFFBF);
		ww(n + V1_NODE_FLAGS, rw(n + V1_NODE_FLAGS) & 0xFFE7);
		ww(n + V1_NODE_FLAGS, rw(n + V1_NODE_FLAGS) | 8);
	}
	n = ruw(CUR_SEG);
	int skipped = 0;
	if (n != 0) {
		for (;;) {
			n = v1_node_next(n);
			if (n == 0)
				break;
			int k = kind_of(n);
			if (k == 4) {
				if (rw(SEG_STATE) == 3 && ch_of(n) == ' ' && stress_of(n) == 3)
					return n;
				ww(SEG_STATE, 0);
				break;
			}
			skipped++;
			if (k == 0) {
				if (ch_of(n) == 'C') {
					ww(SEG_STATE, 1);
					break;
				}
				if (ch_of(n) == 'x' || (rw(V1_END_REQUEST) != 0 && ch_of(n) == 'i')) {
					ww(n + V1_NODE_FLAGS, rw(n + V1_NODE_FLAGS) | 0x20);
					ww(SEG_STATE, 2);
					break;
				}
				continue;
			}
			if (k == 5) {
				ww(SEG_STATE, 3);
				break;
			}
			if (k == 3 && (feat(ch_of(n), 0x200) & 8)) {
				ww(STOP_AT, -1);
				ww(STOP_WAIT, 0);
				if (rw(STOP_STATE) == 4)
					ww(STOP_STATE, 5);
			}
		}
	}
	if (skipped > 9)
		ww(SEG_STATE, 4);
	int st = rw(SEG_STATE);
	if (st == 0 || (n == 0 && st != 4)) {
		if (n == 0 && s16(rw(WRITE_MIN) - rw(LOOKAHEAD) - rw(V1_TRACK_READ)) < 5 &&
		    (rw(STOP_STATE) == 3 || rw(STOP_STATE) == 5)) {
			ww(SEG_STATE, 5);
			n = v1_pg_insert_silence(ruw(PG_LAST), 5);
			ww(STOP_STATE, 0);
		}
	} else if (st == 1)
		n = v1_pg_insert_silence(n, 1);
	else if (st == 3)
		n = v1_pg_insert_silence(v1_node_prev(n), 3);
	else if (st == 4)
		n = v1_pg_insert_silence(ruw(PG_LAST), 4);
	else if (st == 2)
		n = v1_pg_insert_silence(n, 2);
	return n;
}

/* F2709: move every track's write position past its segment and take its new current value; mark the boundary */
void v1_pg_advance(void)
{
	int lo = 0x800, hi = -1;
	for (int i = 0; i < 18; i++) {
		int pos = s16(gf(i, LEN) + rw(POS(i)));
		ww(POS(i), pos);
		ww(CUR(i), v1_pg_scale(i, rb(ruw(TRK(i)) + ((unsigned)(pos - 1) & 0x7F))));
		if (pos < lo)
			lo = pos;
		if (hi < pos)
			hi = pos;
		if (rw(PREVB(i)) < s16(pos - rw(LOOKAHEAD)))
			ww(PREVB(i), pos - rw(LOOKAHEAD));
	}
	mark(MARKS, lo - 1);
	ww(WRITE_MAX, hi);
	ww(WRITE_MIN, lo);
}

/* F1B0E: write a hold (s pause, t tone, g ESC[l values) of up to n frames, as far as room allows. Returns the frames
 * still to go. */
int v1_pg_hold(int ch, int n, int room, int first)
{
	if (first) {
		ww(HOLD_INDEX, 0);
		if ((int8_t)ch == 'g')
			for (int i = 0; i < 18; i++)
				wb(HOLD_VALUES + (unsigned)i, rb(V1_L_VALUES + (unsigned)i));
		else if ((int8_t)ch == 's')
			for (int i = 0; i < 18; i++)
				wb(HOLD_VALUES + (unsigned)i, rb(0x23DBu + (unsigned)i));
		else if ((int8_t)ch == 't') {
			unsigned c0 = cur(), k = (unsigned)stress_of(c0);
			if (bit5(c0))
				k += 4;
			if (bit6(c0))
				k += 8;
			if (rsb(0x23F6u + k * 18) == -1)
				ww(HOLD_INDEX, rb(c0 + V1_NODE_ARG1));
			for (int i = 0; i < 18; i++) {
				int v = rsb(0x23EDu + k * 18 + (unsigned)i);
				if (v == -1) {
					if (i < 9 || i > 11) {
						if (i == 17)
							v = rsb(c0 + V1_NODE_ARG1);
						else if (i == 0 || i == 1) {
							v = (int8_t)(0x3C - rw(PG_AMPLITUDE));
							if (rw(HOLD_INDEX) != 0)
								v = (int8_t)(v - rsb(0x24B3u + (unsigned)(rw(HOLD_INDEX) >> 4)));
						}
					} else
						v = rw(HOLD_INDEX);
				}
				wb(HOLD_VALUES + (unsigned)i, v);
			}
		}
	}
	unsigned pos = ruw(WRITE_MIN);
	for (; n > 0 && room > 0; room--) {
		for (int i = 0; i < 18; i++)
			wb(ruw(TRK(i)) + (pos & 0x7F), rb(HOLD_VALUES + (unsigned)i));
		if (rw(HOLD_INDEX) != 0)
			mark(INDEX_MARKS, (int)pos - 1);
		n--;
		pos++;
	}
	for (int i = 0; i < 18; i++) {
		ww(PREVB(i), pos - rw(LOOKAHEAD));
		ww(POS(i), pos);
	}
	ww(WRITE_MIN, pos);
	ww(WRITE_MAX, pos);
	return n;
}

/* F1A38: a hold node (kind 5): start it, then write as much as the ring has room for. Returns the frames left. */
int v1_pg_hold_run(void)
{
	if (rw(HOLD_NEW)) {
		if (kind_of(ruw(CUR_SEG)) == 4)
			ww(CUR_SEG, v1_node_free(ruw(CUR_SEG), 1));
		wb(HOLD_CH, rb(ruw(CUR_SEG) + V1_NODE_CH));
		ww(HOLD_LEFT, arg0_of(ruw(CUR_SEG)) * 10);
		v1_pg_state_reset();
		ww(SEG_STATE, 3);
		ww(HOLD_NEW, 0);
		ww(HOLD_FIRST, 1);
	}
	int room = s16(rw(V1_TRACK_READ) + 0x80 - rw(WRITE_MAX) - rw(ROOM_KEEP));
	if (rw(HOLD_LEFT) != 0 && room != 0) {
		ww(HOLD_LEFT, v1_pg_hold(rsb(HOLD_CH), rw(HOLD_LEFT), room, rw(HOLD_FIRST)));
		if (rw(HOLD_FIRST)) {
			ww(STOP_AT, -1);
			ww(STOP_WAIT, 0);
		}
		ww(HOLD_FIRST, 0);
	}
	if (rw(HOLD_LEFT) == 0)
		ww(HOLD_NEW, 1);
	return rw(HOLD_LEFT);
}

static int is_end_pause(unsigned n)
{
	return kind_of(n) == 4 && ch_of(n) == ' ' && stress_of(n) == 3;
}

static int commit(int more) { return v1_stage_commit() ? 2 : more; }

/* F1659 */
int v1_stage_paramgen_run(void)
{
	count32(0xA4CA);
	v1_stage_begin(R);
	if (rw(PREV_SEG) == 0 && rw(CUR_SEG) == 0 && rw(NEXT_SEG) == 0)
		return commit(0);
	if (rw(PG_MODE) & 0x2000) {
		ww(DUR_MAX, 4);
		ww(ROOM_KEEP, 3);
		ww(LOOKAHEAD, 2);
	} else {
		ww(DUR_MAX, 0x14);
		ww(ROOM_KEEP, 0xF);
		ww(LOOKAHEAD, 0xC);
	}
	if (rw(STOP_AT) != -1 && s16(rw(WRITE_MIN) - rw(LOOKAHEAD) - rw(V1_TRACK_READ)) <= 4 && rw(STOP_STATE) == 0)
		ww(STOP_STATE, 1);
	unsigned s = ruw(CUR_SEG);
	if (rw(SEG_STATE) == 2 && is_end_pause(s)) {
		ww(END_PAUSE, 1);
		return commit(0);
	}
	if (rw(SEG_STATE) == 3 || (s != 0 && kind_of(s) != 4)) {
		ww(SEG_STATE, 3);
		if (s != 0 && (kind_of(s) != 4 || is_end_pause(s))) {
			int left = v1_pg_hold_run();
			v1_pg_ring(2, 0);
			ww(HOLD_FRAMES, left > 10 ? 10 : left);
			if (left == 0) {
				ww(PREV_SEG, rw(CUR_SEG));
				unsigned n = v1_node_next(ruw(CUR_SEG));
				ww(NEXT_SEG, n);
				ww(CUR_SEG, n);
				ww(SEG_STATE, 0);
			}
			ww(STOP_STATE, 5);
			return commit(1);
		}
	}
	if (rw(CUR_SEG) != 0) {
		ww(HOLD_FRAMES, arg0_of(ruw(CUR_SEG)));
		if (!v1_pg_ring(1, rw(HOLD_FRAMES))) {
			ww(STOP_AT, -1);
			ww(STOP_WAIT, 0);
			if (rw(STOP_STATE) == 4)
				ww(STOP_STATE, 5);
			return commit(1);
		}
		if (rw(PG_WORD) == 0) {
			ww(STOP_AT, -1);
			ww(STOP_WAIT, 0);
			if (rw(STOP_STATE) == 4)
				ww(STOP_STATE, 5);
		}
	}
	unsigned n = v1_pg_next_segment();
	if (n == 0)
		return commit(0);
	ww(NEXT_SEG, n);
	if (cc() != ' ') {
		if (rw(STOP_STATE) == 0)
			ww(STOP_STATE, 4);
		else if (rw(STOP_STATE) == 1)
			ww(STOP_STATE, 2);
	}
	count32(0xA502);
	v1_pg_segment();
	v1_pg_advance();
	if (!(feat(cc(), 0) & 4))
		ww(CUR(17), rw(SAVED_F0));
	unsigned p = ruw(PREV_SEG);
	if (kind_of(p) == 4 && ch_of(p) == ' ' && stress_of(p) == 1)
		v1_node_free(p, 1);
	if (rw(STOP_STATE) == 2 && s16(rw(WRITE_MIN) - rw(LOOKAHEAD) - rw(V1_TRACK_READ)) > 4) {
		ww(STOP_AT, -1);
		ww(STOP_WAIT, 0);
		ww(STOP_STATE, 3);
	}
	ww(PREV_SEG, rw(CUR_SEG));
	ww(CUR_SEG, v1_node_next(ruw(CUR_SEG)));
	ww(NEXT_SEG, v1_node_next(ruw(NEXT_SEG)));
	s = ruw(CUR_SEG);
	if (is_end_pause(s)) {
		ww(STOP_AT, rw(WRITE_MIN) - rw(LOOKAHEAD));
		ww(STOP_WAIT, 0);
		unsigned m = s;
		do
			m = v1_node_next(m);
		while (m != 0 && kind_of(m) != 4 && kind_of(m) != 5);
		if (m == 0) { /* the utterance ends here */
			v1_node_free(ruw(CUR_SEG), 0);
			ww(PREV_SEG, 0);
			ww(CUR_SEG, 0);
			ww(NEXT_SEG, 0);
			int b = rw(WRITE_MIN) - 1;
			wb(MARKS + (unsigned)((b & 0x7F) >> 3), rb(MARKS + (unsigned)((b & 0x7F) >> 3)) & ~ruw(0x48A6u + 2u * (unsigned)(b & 7)));
			mark(MARKS, rw(WRITE_MIN) - 1 - rw(LOOKAHEAD));
		}
	}
	v1_pg_ring(2, 0);
	return commit(0);
}

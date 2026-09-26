/*
 * Writing a segment into the track ring (REFERENCE §12.4). A track is a 128-byte ring of track bytes; positions
 * keep counting upwards and are masked with 0x7F on use. The ramps at DS:9B9E are 0-terminated lists of
 * fractions of 256: ramp k moves a value to its goal over about k frames.
 */
#include "pg.h"

static unsigned ramp(int k) { return ruw(RAMPS + 2u * (unsigned)k); }

static unsigned track_at(int track, int pos) { return (unsigned)(track + (pos & 0x7F)) & 0xFFFFu; }

/* x moved by the fraction r/256 of the way to v (byte arithmetic). */
static unsigned blend(unsigned x, unsigned v, unsigned r)
{
	if (v >= x)
		return x + ((v - x) * r >> 8);
	return x - ((x - v) * r >> 8);
}

/* D345F: count frames from pos: `from` moving to `to` along ramp k, then `to` held. */
void track_ramp_then_hold(int track, int pos, int k, int count, int from, int to)
{
	unsigned r, dist, R = ramp(k);
	uint16_t n = (uint16_t)(count + 1);
	from &= 0xFF;
	to &= 0xFF;
	dist = from >= to ? (unsigned)(from - to) : (unsigned)(to - from);
	while ((r = rb(R++)) != 0) {
		if (--n == 0)
			return;
		unsigned d = dist * r >> 8;
		wb(track_at(track, pos++), from >= to ? to + d : to - d);
	}
	while (--n != 0)
		wb(track_at(track, pos++), to);
}

/* D3532: the count frames before pos, walking backwards, pulled toward value along ramp k (from its second
 * entry on). */
void track_blend_back(int track, int pos, int k, int count, int value)
{
	unsigned r, R = ramp(k);
	uint16_t n = (uint16_t)(count + 1);
	for (;;) {
		unsigned a = track_at(track, --pos);
		r = rb(++R);
		if (r == 0 || --n == 0)
			return;
		wb(a, blend(rb(a), value & 0xFF, r));
	}
}

/* D357B: the count frames from pos pulled toward value along ramp k. */
void track_blend_fwd(int track, int pos, int k, int count, int value)
{
	unsigned r, R = ramp(k);
	uint16_t n = (uint16_t)(count + 1);
	for (;;) {
		unsigned a = track_at(track, pos++);
		r = rb(R++);
		if (r == 0 || --n == 0)
			return;
		wb(a, blend(rb(a), value & 0xFF, r));
	}
}

/* D3F36 */
void track_fill(int track, int pos, int count, int value)
{
	for (uint16_t n = (uint16_t)count; n != 0; n--)
		wb(track_at(track, pos++), value);
}

/* D3F59: add delta, scaled down along ramp k (from its second entry), to the count frames before pos. The
 * delta is limited so the frame just before pos does not go below 0; amplitudes (p < 3) that would are set to 0. */
void track_decay_back(int track, int p, int pos, int k, int count, int delta)
{
	unsigned R = ramp(k);
	int v;
	pos--;
	v = rb(track_at(track, pos));
	if (v + s16(delta) < 0)
		delta = -v;
	for (;;) {
		unsigned r = rb(++R) << 7;
		if (r == 0 || count-- == 0)
			return;
		v = s16(fx_mul_q15(delta, r) + rsb(track_at(track, pos)));
		if (v <= 0 && p < 3)
			v = 0;
		wb(track_at(track, pos), v);
		pos--;
	}
}

/* D3D2F: convert parameter p's segment to track bytes and write it by transition type:
 * 2 blend toward the onset, 4 hold the target, 6 ramp from onset to target; the odd types first blend the end of
 * the previous segment toward locB. */
void param_emit_segment(int p)
{
	unsigned s = PARAM(p, 0);
	int type = ruw(s + F_TYPE), dur_b = rw(s + F_DURB), dur_f = rw(s + F_DURF);
	int locb = rw(s + F_LOCB), onset = rw(s + F_ONSET), target = rw(s + F_TARGET);
	int track = rw(TRK_BASE(p)), pos = rw(TRK_POS(p)), gap = s16(pos - rw(TRK_PREV(p)));
	if (locb < 0)
		locb = 0;
	if (onset < 0)
		onset = 0;
	switch (p) {
	case P_F1:
		locb >>= 2, onset >>= 2, target >>= 2;
		break;
	case P_F2:
		locb = s16(locb - 500) >> 3, onset = s16(onset - 500) >> 3, target = s16(target - 500) >> 3;
		break;
	case P_F3:
	case P_F4:
		locb >>= 4, onset >>= 4, target >>= 4;
		break;
	case P_B1:
	case P_B2:
	case P_B3:
		locb >>= 1, onset >>= 1, target >>= 1;
		if (target <= 0)
			fatal_error(0x30);
		break;
	case P_FN:
		locb = s16(locb - 192) >> 2, onset = s16(onset - 192) >> 2, target = s16(target - 192) >> 2;
		break;
	}
	if ((type & 1) && gap <= 0)
		type &= ~1;
	switch (type) {
	case 3:
		track_blend_back(track, pos, dur_b, gap, locb);
		/* fall through */
	case 2:
		track_blend_fwd(track, pos, dur_f, rw(s + F_LEN), onset);
		break;
	case 5:
		track_blend_back(track, pos, dur_b, gap, locb);
		/* fall through */
	case 4:
		track_ramp_then_hold(track, pos, 0, rw(s + F_LEN), 0, target);
		break;
	case 7:
		track_blend_back(track, pos, dur_b, gap, locb);
		/* fall through */
	case 6:
		track_ramp_then_hold(track, pos, dur_f, rw(s + F_LEN), onset, target);
		break;
	}
}

/* D3FFB: move the start of the aspiration back by [EBB2] frames into the previous segment: AH's write position
 * goes back, AH/AV/AF of those frames decay toward the new values, and AH's length grows to match. */
void pg_shift_aspiration(void)
{
	int back = rw(0xEBB2), pos;
	if (rw(TRK_POS(P_AH)) < back)
		return;
	ww(TRK_POS(P_AH), rw(TRK_POS(P_AH)) - back);
	pos = rw(TRK_POS(P_AH));
	track_decay_back(rw(TRK_BASE(P_AH)), P_AH, pos, 3, s16(pos - rw(TRK_PREV(P_AH))),
	                 s16(rw(0xEBC4) - rsb(track_at(rw(TRK_BASE(P_AH)), pos))));
	ww(PARAM(P_AH, F_LEN), rw(PARAM(P_AH, F_LEN)) + rw(0xEBB2));
	ww(LOCUS(P_AH), rw(0xEBC4));
	ww(PARAM(P_AH, F_DURF), 3);
	if (feature(node_char(rw(NODE_PREV)), 0) & 4) /* after a voiced sound: widen B1 over those frames */
		track_decay_back(rw(TRK_BASE(P_B1)), P_B1, rw(TRK_POS(P_AH)), 6,
		                 s16(rw(TRK_POS(P_AH)) - rw(TRK_PREV(P_B1))), 0x32);
	if (node_char(rw(NODE_CUR)) == ' ')
		track_decay_back(rw(TRK_BASE(P_AV)), P_AV, rw(TRK_POS(P_AH)), 10,
		                 s16(rw(TRK_POS(P_AH)) - rw(TRK_PREV(P_AV))), -4);
	if (feature(node_char(rw(NODE_CUR)), 0x100) & 1)
		ww(PARAM(P_AH, F_TYPE), 5);
	if (rw(TRK_POS(P_AV)) >= rw(0xEBB2)) {
		ww(PARAM(P_AV, F_LEN), rw(PARAM(P_AV, F_LEN)) + rw(0xEBB2));
		ww(TRK_POS(P_AV), rw(TRK_POS(P_AV)) - rw(0xEBB2));
	}
}

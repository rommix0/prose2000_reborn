/*
 * The top of the parameter generator: one call of paramgen_run per pass of the synthesis loop moves the phoneme
 * window on by at most one segment, keeping the track ring (§11.4) filled ahead of the frame being played.
 */
#include "pg.h"

/* Variables of the run loop whose meaning is only partly known. */
#define V_DD80 0xDD80
#define V_DD9C 0xDD9C     /* frames just written */
#define V_DC92 0xDC92
#define V_C27C 0xC27C
#define V_C27E 0xC27E
#define END_KIND 0xEAFE   /* what ends the current stretch: 0 none, 1 'C', 2 'x'/'i', 3 pause, 4 too long, 5 underrun */
#define FLOW 0xEAFC       /* flow state 0-5 */
#define PAUSE_DONE 0xEAF8
#define NEW_UTT 0xEAE6    /* reset the previous-boundary pointers before the next segment */
#define LAST_CHAR 0xEADE
#define END_CHAR 0xEADF
#define HOLD_READY 0xEB00
#define HOLD_CHAR 0xEBE6
#define HOLD_FRAMES 0xEBE4
#define HOLD_LOAD 0xEBE8
#define HOLD_ROW 0xEBCC   /* 22 bytes written each held frame */
#define HOLD_F0 0xEBE2    /* test mode: F0 from the node, 0 = off */
#define HOST_PARAMS 0xEB02 /* the host `l` values (§8.2) */
#define LOG_POS 0xEA5C
#define LOG 0xEA5E        /* 128-byte phoneme log */
#define V_EAFA 0xEAFA
#define V_EBA0 0xEBA0

static int is_utterance_end(int n)
{
	return node_kind(n) == NODE_SEGMENT && node_char(n) == ' ' && node_stress(n) == 3;
}

static int ring_ahead(void) { return rw(RING_LOW) - rw(RING_LAG) - rw(RING_READ); }

static void release_hold(void)
{
	ww(RING_HOLD, -1);
	ww(V_DD80, 0);
}

static void set_frame_bit(unsigned bits, int frame, int on)
{
	unsigned a = bits + ((unsigned)(frame & 0x7F) >> 3);
	unsigned m = rb(BITMASK + 2u * (unsigned)(frame & 7));
	wb(a, on ? rb(a) | m : rb(a) & ~m);
}

int paramgen_run(void)
{
	stage_window_update(PG_STAGE);
	return paramgen_step();
}

/* DCB0B-DCEEB. Returns 0 when idle, 1 or 2 (2 = the stage handed nodes on). */
int paramgen_step(void)
{
	int n, r, kind;
	if (rw(NODE_PREV) == 0 && rw(NODE_CUR) == 0 && rw(NODE_NEXT) == 0)
		goto done;
	if (!(ruw(MODE) & 0x2000) && rw(V_C27E) < 0x13) {
		ww(LOOKAHEAD, 20);
		ww(RING_SLACK, 15);
		ww(RING_LAG, 12);
	} else {
		ww(LOOKAHEAD, 4);
		ww(RING_SLACK, 3);
		ww(RING_LAG, 2);
	}
	if (rw(RING_HOLD) != -1 && ring_ahead() <= 8 && rw(FLOW) == 0)
		ww(FLOW, 1);

	n = rw(NODE_CUR);
	if (rw(END_KIND) == 2 && is_utterance_end(n)) {
		ww(PAUSE_DONE, 1);
		goto done;
	}
	n = rw(NODE_CUR);
	if (rw(END_KIND) == 3 || (n != 0 && node_kind(n) != NODE_SEGMENT)) {
		ww(END_KIND, 3);
		if (n != 0 && (is_utterance_end(n) || node_kind(n) != NODE_SEGMENT)) {
			/* a held stretch (`g`, `s`, test mode) */
			r = paramgen_hold_step();
			param_ring_ctl(2, 0);
			ww(NEW_UTT, 1);
			ww(V_DD9C, r > 1 ? 1 : r);
			if (r == 0) {
				ww(NODE_PREV, rw(NODE_CUR));
				n = node_next(rw(NODE_CUR));
				ww(NODE_NEXT, n);
				ww(NODE_CUR, n);
				ww(END_KIND, 0);
			}
			ww(FLOW, 5);
			goto commit;
		}
	}
	if (rw(NODE_CUR) != 0) {
		ww(V_DD9C, node_dur(rw(NODE_CUR)));
		if (!param_ring_ctl(1, rw(V_DD9C))) {
			release_hold();
			if (rw(FLOW) == 4)
				ww(FLOW, 5);
			goto commit;
		}
		if (ring_ahead() > 0x40) {
			release_hold();
			ww(V_DC92, 0);
			if (rw(FLOW) == 4)
				ww(FLOW, 5);
		}
		if (rw(V_C27C) == 0) {
			release_hold();
			if (rw(FLOW) == 4)
				ww(FLOW, 5);
		}
	}

	n = find_segment_end();
	if (n == 0)
		goto done;
	ww(NODE_NEXT, n);
	for (;;) {
		n = node_next(n);
		if (n == 0)
			break;
		kind = node_kind(n);
		if (kind == NODE_SEGMENT) {
			wb(END_CHAR, rb(n + N_CHAR));
			break;
		}
		if (kind != NODE_SYMBOL) {
			wb(END_CHAR, ' ');
			break;
		}
	}
	if (node_char(rw(NODE_CUR)) != ' ') {
		if (rw(FLOW) == 0)
			ww(FLOW, 4);
		else if (rw(FLOW) == 1)
			ww(FLOW, 2);
	}
	paramgen_segment();
	paramgen_advance();
	if (!(feature(node_char(rw(NODE_CUR)), 0) & 4))
		ww(V_EBA0, rw(V_EAFA));
	n = rw(NODE_PREV);
	if (node_kind(n) == NODE_SEGMENT && node_char(n) == ' ' && node_stress(n) == 1)
		node_delete(n, 1);
	if (rw(FLOW) == 2 && ring_ahead() > 8) {
		release_hold();
		ww(FLOW, 3);
	}
	wb(LAST_CHAR, rb(rw(NODE_PREV) + N_CHAR));
	ww(NODE_PREV, rw(NODE_CUR));
	ww(NODE_CUR, node_next(rw(NODE_CUR)));
	ww(NODE_NEXT, node_next(rw(NODE_NEXT)));
	n = rw(NODE_CUR);
	if (is_utterance_end(n)) {
		ww(RING_HOLD, rw(RING_LOW) - rw(RING_LAG));
		ww(V_DD80, 0);
		do
			n = node_next(n);
		while (n != 0 && node_kind(n) != NODE_SEGMENT && node_kind(n) != NODE_HOLD);
		if (n == 0) {
			/* nothing follows: end of the utterance */
			node_delete(rw(NODE_CUR), 0);
			ww(NODE_PREV, 0);
			ww(NODE_CUR, 0);
			ww(NODE_NEXT, 0);
			set_frame_bit(RING_MARK, rw(RING_LOW) - 1, 0);
			set_frame_bit(RING_MARK, rw(RING_LOW) - 1 - rw(RING_LAG), 1);
		}
	}
	param_ring_ctl(2, 0);
done:
	return stage_commit() ? 2 : 0;
commit:
	return stage_commit() ? 2 : 1;
}

/* DCA52: 1 = is there room for `frames` more, 2 = rebase all positions by 0x400 when they grow large,
 * 3 = has the look-ahead been written, 4 = advance the read index. */
int param_ring_ctl(int op, int frames)
{
	switch (op) {
	case 1:
		if (rw(RING_HIGH) + frames + rw(RING_SLACK) - rw(RING_READ) > 0x7F)
			return 0;
		break;
	case 2:
		if (rw(RING_READ) > 0x400) {
			for (int p = 0; p < NPARAM; p++) {
				ww(TRK_POS(p), rw(TRK_POS(p)) - 0x400);
				ww(TRK_PREV(p), rw(TRK_PREV(p)) - 0x400);
			}
			/* D63E5 / D6576: interrupts off (nested count EE74, latch bit 2) */
			if (ruw(0xEE74) == 0) {
				wb(0xDB82, rb(0xDB82) | 4);
				wb(ruw(0xDB84), rb(0xDB82));
			}
			ww(0xEE74, rw(0xEE74) + 1);
			ww(RING_READ, rw(RING_READ) - 0x400);
			if (rw(RING_HOLD) != -1)
				ww(RING_HOLD, rw(RING_HOLD) - 0x400);
			ww(RING_LOW, rw(RING_LOW) - 0x400);
			ww(RING_HIGH, rw(RING_HIGH) - 0x400);
			ww(0xEE74, rw(0xEE74) - 1);
			if (rw(0xEE74) == 0) {
				wb(0xDB82, rb(0xDB82) & ~4);
				wb(ruw(0xDB84), rb(0xDB82));
			}
		}
		break;
	case 3:
		if (rw(RING_LOW) <= rw(RING_READ) + rw(RING_LAG))
			return 0;
		break;
	case 4:
		ww(RING_READ, rw(RING_READ) + 1);
		break;
	default:
		fatal_error(0x21);
		return 0;
	}
	return 1;
}

/* DCEEC: continue a held stretch. Returns the frames still to hold. */
int paramgen_hold_step(void)
{
	int room;
	if (rw(HOLD_READY)) {
		if (node_kind(rw(NODE_CUR)) == NODE_SEGMENT)
			ww(NODE_CUR, node_delete(rw(NODE_CUR), 1));
		wb(HOLD_CHAR, rb(rw(NODE_CUR) + N_CHAR));
		ww(HOLD_FRAMES, node_dur(rw(NODE_CUR)));
		paramgen_clear_state();
		ww(END_KIND, 3);
		ww(HOLD_READY, 0);
		ww(HOLD_LOAD, 1);
	}
	room = s16(rw(RING_READ) + 0x80 - rw(RING_HIGH) - rw(RING_SLACK));
	if (rw(HOLD_FRAMES) != 0 && room != 0) {
		ww(HOLD_FRAMES, paramgen_hold(rsb(HOLD_CHAR), rw(HOLD_FRAMES), room, rw(HOLD_LOAD)));
		if (rw(HOLD_LOAD))
			release_hold();
		ww(HOLD_LOAD, 0);
	}
	if (rw(HOLD_FRAMES) == 0)
		ww(HOLD_READY, 1);
	return rw(HOLD_FRAMES);
}

/* DCFA2: write up to `room` held frames. mode 'g' holds the host `l` values, 's' silence, 't' a test row
 * (DS:6122, 22 bytes per stress/flag combination). load = 1 on the first call of a stretch.
 * Returns the frames still to write. */
int paramgen_hold(int mode, int frames, int room, int load)
{
	int p, pos;
	if (load) {
		ww(HOLD_F0, 0);
		if (mode == 'g') {
			for (p = 0; p < NPARAM; p++)
				wb(HOLD_ROW + p, rb(HOST_PARAMS + p));
		} else if (mode == 's') {
			for (p = 0; p < NPARAM; p++)
				wb(HOLD_ROW + p, rb(TRACK_DEFAULT + p));
		} else if (mode == 't') {
			int n = rw(NODE_CUR), row = node_stress(n), hi = 0;
			if (node_bit(n, 5))
				row += 4;
			if (node_bit(n, 6))
				row += 8;
			row *= 22;
			if (rb(0x6122 + row + 9) == 0xFF)
				ww(HOLD_F0, rb(n + N_F0));
			for (p = 0; p < NPARAM; p++) {
				int v = rb(0x6122 + row + p);
				/* hi is a stack slot the firmware leaves uninitialised; it is only read after F1 set it */
				if (rb(0x6122 + row + 9) == 0xFF && p >= 13 && p <= 15 && hi)
					v = 0x2D;
				if (v == 0xFF) {
					if (p >= 9 && p <= 11) {
						hi = rw(HOLD_F0) > 0xB4;
						v = rw(HOLD_F0);
					} else if (p == P_F0) {
						v = rb(n + N_F0);
					} else if (p == P_AV || p == P_AF) {
						v = 60 - rw(ATTEN);
						if (rw(HOLD_F0))
							v -= rsb(0x6214 + (rw(HOLD_F0) >> 4));
						if (s16(v) <= 0)
							v = 1;
					}
				}
				wb(HOLD_ROW + p, v);
			}
		}
	}
	pos = rw(RING_LOW);
	for (; frames > 0 && room > 0; room--, frames--, pos++) {
		for (p = 0; p < NPARAM; p++)
			wb(rw(TRK_BASE(p)) + (pos & 0x7F), rb(HOLD_ROW + p));
		if (rw(HOLD_F0))
			set_frame_bit(RING_ALT, pos - 1, 1);
	}
	for (p = 0; p < NPARAM; p++) {
		ww(TRK_PREV(p), pos - rw(RING_LAG));
		ww(TRK_POS(p), pos);
	}
	ww(RING_LOW, pos);
	ww(RING_HIGH, pos);
	return frames;
}

/* DDE1F: look ahead from the current node for the end of the stretch that may be synthesised now, inserting a
 * pause pair where the stretch has to break. Returns the node after the segment, or 0 to wait. */
int find_segment_end(void)
{
	int n, kind, count = 0;
	if (rw(NODE_CUR) != 0 && (rw(NODE_PREV) == rw(NODE_CUR) || node_kind(rw(NODE_PREV)) != NODE_SEGMENT)) {
		/* make sure the current phoneme has a pause before it */
		n = node_insert(rw(NODE_CUR), 0, NODE_SEGMENT, ' ');
		ww(NODE_PREV, n);
		ww(n + N_DUR, (rw(n + N_DUR) & 0xFF00) | 4);
		wb(n + N_F0, rb(rw(NODE_CUR) + N_F0));
		ww(n + N_FLAGS, (rw(n + N_FLAGS) & 0xFF87) | 8); /* stress 1 */
	}
	n = rw(NODE_CUR);
	if (n != 0) {
		for (;;) {
			n = node_next(n);
			if (n == 0)
				break;
			kind = node_kind(n);
			if (kind == NODE_SEGMENT) {
				if (rw(END_KIND) == 3 && is_utterance_end(n))
					return n;
				ww(END_KIND, 0);
				break;
			}
			count++;
			if (kind == NODE_COMMAND) {
				if (node_char(n) == 'C') {
					ww(END_KIND, 1);
					break;
				}
				if (node_char(n) == 'x' || (rw(0xEE70) != 0 && node_char(n) == 'i')) {
					ww(n + N_FLAGS, rw(n + N_FLAGS) | 0x20);
					ww(END_KIND, 2);
					break;
				}
			} else if (kind == NODE_HOLD) {
				ww(END_KIND, 3);
				break;
			} else if (kind == NODE_SYMBOL && (feature(node_char(n), 0x200) & 8) && node_char(n) != ']') {
				release_hold();
				if (rw(FLOW) == 4)
					ww(FLOW, 5);
			}
		}
	}
	if (count >= 10)
		ww(END_KIND, 4);
	if (rw(END_KIND) != 0 && (n != 0 || rw(END_KIND) == 4)) {
		switch (rw(END_KIND)) {
		case 1:
			n = insert_pause_pair(n, 1);
			break;
		case 4:
			n = insert_pause_pair(rw(NODE_LAST), 4);
			break;
		case 2:
			n = insert_pause_pair(n, 2);
			break;
		}
	} else if (n == 0 && ring_ahead() <= 8 && (rw(FLOW) == 3 || rw(FLOW) == 5)) {
		ww(END_KIND, 5);
		n = insert_pause_pair(rw(NODE_LAST), 5);
		ww(FLOW, 0);
	}
	return n;
}

/* DDAA2: insert two pause nodes after `at` (stress 2 then 3); why = 1 ('C') makes the first 4 frames long, the
 * others fill the ring look-ahead. */
int insert_pause_pair(int at, int why)
{
	int a = node_insert(at, 1, NODE_SEGMENT, ' '), b;
	if (why == 1)
		ww(a + N_DUR, (rw(a + N_DUR) & 0xFF00) | 4);
	else
		ww(a + N_DUR, (rw(a + N_DUR) & 0xFF00) | ((rw(RING_LAG) + rw(RING_SLACK) + 10) & 0xFF));
	wb(a + N_F0, rw(NODE_CUR) == 0 ? 0x32 : rb(rw(NODE_CUR) + N_F0));
	ww(a + N_FLAGS, (rw(a + N_FLAGS) & 0xFF87) | 0x10);
	b = node_insert(a, 1, NODE_SEGMENT, ' ');
	ww(b + N_DUR, (rw(b + N_DUR) & 0xFF00) | 4);
	wb(b + N_F0, rw(NODE_CUR) == 0 ? 0x32 : rb(rw(NODE_CUR) + N_F0));
	ww(b + N_FLAGS, (rw(b + N_FLAGS) & 0xFF87) | 0x18);
	if (rw(NODE_CUR) == 0) {
		ww(NODE_CUR, a);
		a = node_next(a);
	}
	release_hold();
	return a;
}

/* DDB81: forget the context of the previous segment. */
void paramgen_clear_state(void)
{
	static const uint16_t zero[] = {0xEBB2, 0xEBAA, 0xEBAC, 0xEBB8, 0xEBB6, 0xEBBC, 0xEBBA, 0xEBC0, 0xEBB4,
	                                0xEBBE, 0xEBCA, 0xEBC8, 0xEBC6, 0xEBC4, 0xEBC2};
	ww(0xEBB0, 0xCCC0);
	ww(0xEBAE, 0xCCC0);
	for (unsigned i = 0; i < sizeof zero / sizeof zero[0]; i++)
		ww(zero[i], 0);
	for (int p = 0; p < NPARAM; p++) {
		ww(TRK_PREV(p), rw(RING_LOW) - rw(RING_LAG));
		ww(TRK_POS(p), rw(RING_LOW));
		ww(LOCUS(p), track_byte_to_value(p, rsb(TRACK_DEFAULT + p)));
	}
	ww(RING_HIGH, rw(RING_LOW));
	wb(LAST_CHAR, ' ');
}

/* DDC0A: a track byte back to natural units (the inverse of the §11.4 codings). */
int track_byte_to_value(int p, int byte)
{
	int v = byte & 0xFF;
	switch (p) {
	case P_F1: return v * 4;
	case P_F2: return v * 8 + 500;
	case P_F3: case P_F4: return v * 16;
	case P_B1: case P_B2: case P_B3: return v * 2;
	case P_FN: return v * 4 + 192;
	default: return v;
	}
}

/* DDC70: after a segment, move every track's write position on by its length, set the locus of each parameter to
 * the last value written, and mark the segment boundary. With MODE bit 15, log the phoneme and its stress. */
void paramgen_advance(void)
{
	int lo = 0x800, hi = -1, n;
	for (int p = 0; p < NPARAM; p++) {
		int pos = s16(rw(TRK_POS(p)) + rw(PARAM(p, F_LEN)));
		ww(TRK_POS(p), pos);
		ww(LOCUS(p), track_byte_to_value(p, rsb(rw(TRK_BASE(p)) + ((pos - 1) & 0x7F))));
		if (pos < lo)
			lo = pos;
		if (pos > hi)
			hi = pos;
		if (s16(pos - rw(RING_LAG)) > rw(TRK_PREV(p)))
			ww(TRK_PREV(p), pos - rw(RING_LAG));
	}
	set_frame_bit(RING_MARK, lo - 1, 1);
	ww(RING_HIGH, hi);
	ww(RING_LOW, lo);
	if (!(ruw(MODE) & 0x8000))
		return;
	n = rw(NODE_CUR);
	wb(LOG + rw(LOG_POS), rb(n + N_CHAR));
	ww(LOG_POS, (rw(LOG_POS) + 1) & 0x7F);
	if (node_stress(n) != 0 && node_char(n) != ' ') {
		static const char mark[] = {'2', '1', '"'};
		wb(LOG + rw(LOG_POS), mark[node_stress(n) - 1]);
		ww(LOG_POS, (rw(LOG_POS) + 1) & 0x7F);
	}
	for (n = rw(NODE_CUR); (n = node_next(n)) != 0 && node_kind(n) != NODE_SEGMENT;) {
		if (node_kind(n) == NODE_SYMBOL && (feature(node_char(n), 0x200) & 8)) {
			wb(LOG + rw(LOG_POS), rb(n + N_CHAR));
			ww(LOG_POS, (rw(LOG_POS) + 1) & 0x7F);
		}
	}
}

/* DD6A0 */
void paramgen_reset(void)
{
	ww(LOOKAHEAD, 4);
	ww(RING_SLACK, 3);
	ww(RING_LAG, 2);
	ww(0xC28A, 0x28);
	ww(END_KIND, 0);
	ww(PAUSE_DONE, 0);
	ww(HOLD_READY, 1);
	ww(FLOW, 1);
	ww(NEW_UTT, 0);
	for (int p = 0; p < NPARAM; p++)
		wb(HOST_PARAMS + p, rb(0xEE46 + p));
	paramgen_clear_state();
	ww(0xEA5A, 0);
	ww(LOG_POS, 0);
	ww(0xEA56, 0);
	ww(0xEA58, 0);
}

/* DC9B9: fill each track with its default and start writing at frame 12. */
void param_tracks_init(void)
{
	for (int p = 0; p < NPARAM; p++) {
		ww(TRK_PREV(p), 10);
		ww(TRK_POS(p), 12);
		ww(TRK_BASE(p), 0xDF56 + 0x80 * p);
		for (int i = 0; i < 12; i++)
			wb(rw(TRK_BASE(p)) + i, rb(TRACK_DEFAULT + p));
	}
	ww(RING_READ, 9);
	ww(RING_HIGH, 12);
	ww(RING_LOW, 12);
	ww(V_DD9C, 0);
	ww(0xDD9A, 0);
	for (int i = 0; i < 16; i++) {
		wb(RING_MARK + i, 0);
		wb(RING_ALT + i, 0);
	}
	ww(RING_HOLD, rw(RING_LOW) - 2);
	ww(V_DD80, 0);
}

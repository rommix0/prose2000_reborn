/* The phoneme node list (doubly linked, with a free list) and the stage window it is walked through. */
#include "pg.h"

void (*pg_segment_hook)(int ch);

/* D3C25: unlink n. */
static int list_unlink(int n)
{
	ww(rw(n + 2), rw(n));
	ww(rw(n) + 2, rw(n + 2));
	return n;
}

/* D38E1: link n in front of at (between at and its +2 neighbour). */
static int list_link(int n, int at)
{
	int other = rw(at + 2);
	ww(n + 2, other);
	ww(other, n);
	ww(n, at);
	ww(at + 2, n);
	return n;
}

/* D38B8: the node after n, or 0 when n is the last node of the current stage's window. */
int node_next(int n)
{
	if (n == 0)
		fatal_error(0x2A);
	if (n == rw(rw(STAGE_CUR) + 8))
		return 0;
	return rw(n);
}

/* D36D2: the node before n, or 0 when n is the first node of the current stage's window. */
int node_prev(int n)
{
	if (n == 0)
		fatal_error(0x2B);
	if (n == rw(rw(STAGE_CUR)))
		return 0;
	return rw(n + 2);
}

/* D3614: take a node from the free list and link it next to at (after = 1: after it, on the +0 side). */
int node_insert(int at, int after, int kind, int ch)
{
	int n, stage = ruw(STAGE_CUR);
	if (rw(FREE_COUNT) < 1)
		fatal_error(0x1E);
	if (at == 0)
		fatal_error(0x1F);
	n = list_unlink(rw(rw(FREE_HEAD)));
	ww(FREE_COUNT, rw(FREE_COUNT) - 1);
	list_link(n, after == 1 ? rw(at) : at);
	ww(n + N_FLAGS, (rw(n + N_FLAGS) & 0xFFF8) | (kind & 7));
	wb(n + N_CHAR, ch);
	ww(n + N_DUR, rw(n + N_DUR) & 0xFF00);
	wb(n + N_F0, 0);
	ww(n + N_FLAGS, rw(n + N_FLAGS) & 0xFF07); /* stress and bits 5-7 cleared */
	if (stage) {
		if (at == rw(stage + 8) && after == 1)
			ww(stage + 8, n);
		else if (at == rw(stage) && after == 0)
			ww(stage, n);
	}
	return n;
}

/* D3805: return n to the free list. Returns its neighbour on the given side (0 when that leaves the window). */
int node_delete(int n, int forward)
{
	int next, stage = ruw(STAGE_CUR);
	if (n == 0)
		fatal_error(0x20);
	next = forward == 1 ? rw(n) : rw(n + 2);
	if (stage) {
		if (n == rw(stage + 8)) {
			if (rw(stage + 8) == rw(stage)) {
				ww(stage + 8, 0);
				ww(stage, 0);
			} else {
				ww(stage + 8, rw(rw(stage + 8) + 2));
			}
			if (forward == 1)
				next = 0;
		}
		if (n == rw(stage)) {
			ww(stage, rw(rw(stage)));
			if (forward == 0)
				next = 0;
		}
	}
	list_unlink(n);
	list_link(n, rw(FREE_TAIL));
	ww(FREE_COUNT, rw(FREE_COUNT) + 1);
	ww(n + N_FLAGS, (rw(n + N_FLAGS) & 0xFFF8) | NODE_FREE);
	return next;
}

/* D36FB: hand the nodes this stage has finished with to the next stage. Returns 1 if there were any. */
int stage_commit(void)
{
	int stage = ruw(STAGE_CUR), next_stage = stage + 0x22, handed = 0, edge;
	if (rw(stage) != 0 && rw(stage) != rw(stage + 2)) {
		handed = 1;
		edge = rw(stage + 2) ? rw(rw(stage + 2) + 2) : rw(stage + 8);
		if (stage == 0xC28E) { /* the last stage frees its nodes */
			while (rw(rw(STAGE_CUR)) != rw(rw(STAGE_CUR) + 2))
				ww(rw(STAGE_CUR), node_delete(rw(rw(STAGE_CUR)), 1));
		} else {
			ww(next_stage + 8, edge);
			for (int f = 6; f >= 0; f -= 2)
				if (rw(next_stage + f) == 0)
					ww(next_stage + f, rw(rw(STAGE_CUR)));
		}
		stage = ruw(STAGE_CUR);
		if (rw(stage + 2) == 0) {
			ww(stage, 0);
			ww(stage + 4, 0);
			ww(stage + 6, 0);
			ww(stage + 8, 0);
		} else {
			ww(stage, rw(stage + 2));
		}
	}
	ww(STAGE_CUR, 0);
	return handed;
}

/* D9D2B: apply the command node at the current stage's cursor to the stage's copy of the host settings. Returns 0
 * when the stage must stop here (a hold reaching prosody, or an index marker the host link cannot take yet). */
int stage_run_command(void)
{
	int stage = ruw(STAGE_CUR), n = rw(stage + 4), v1, v2;
	if (stage == 0xC24A && node_kind(n) == NODE_HOLD)
		return 0;
	if (node_kind(n) != NODE_COMMAND)
		return 1;
	v1 = node_dur(n);
	v2 = rb(n + N_F0);
	switch (node_char(n)) {
	case 0:
		break;
	case 'i': /* index marker: reported when the last stage reaches it */
		if (stage != 0xC28E)
			break;
		if (rw(stage + REC_MODE) & 0x400) {
			int param = v1;
			if (pg_host_send_hook && !pg_host_send_hook(1, 'i', 1, &param))
				return 0;
		}
		if (!node_bit(n, 5))
			break;
		ww(0xEE72, v1);
		ww(0xEE6E, 1);
		break;
	case 'x':
		if (stage == 0xC28E) {
			ww(0xEE72, 0);
			ww(0xEE6E, 1);
		} else if (stage == 0xC206) {
			node_insert(n, 1, NODE_COMMAND, 'C');
		}
		break;
	case 'C':
		if (stage == 0xC206)
			node_insert(rw(stage + 4), 0, NODE_SYMBOL, ']');
		break;
	case 'I':
		ww(stage + REC_INPUT, v1);
		break;
	case 'P':
		ww(stage + REC_PROSODY, (v2 << 8) + v1);
		break;
	case 'a':
		ww(stage + REC_ATTEN, v1);
		break;
	case 'V':
		ww(stage + REC_VOICE, v1);
		ww(stage + REC_PITCH, rw(0x5328 + 2 * rw(stage + REC_VOICE)));
		break;
	case 'f':
		if (rw(stage + REC_FAST) == 0)
			ww(0xDD20, rw(stage + REC_SPEED));
		ww(stage + REC_FAST, v1);
		ww(stage + REC_SPEED, v2);
		if (rw(stage + REC_FAST) == 0)
			ww(stage + REC_SPEED, rw(0xDD20));
		break;
	case 'r':
	case 'v':
		ww(stage + REC_SPEED, v1);
		break;
	case 'p':
		ww(stage + REC_PITCH, v1);
		break;
	case 's':
	case 't':
	case 'g':
		if (stage == 0xC24A)
			ww(n + N_FLAGS, (rw(n + N_FLAGS) & 0xFFF8) | NODE_HOLD);
		break;
	case 'l':
		wb(0xEB02 + v1, v2);
		break;
	case 'N':
		ww(stage + REC_MODE, (v2 << 8) | v1);
		break;
	case 'A':
		ww(stage + REC_AFLAGS, (v2 << 8) | v1);
		break;
	default:
		fatal_error(1);
	}
	return 1;
}

/* D3B66: make `stage` the current stage and bring its cursors onto nodes it handles. Each stage accepts a set of
 * node kinds (the mask at +0x1E, tested against the kind bits at DS:0020); the next-cursor (+6) skips the others,
 * and the current cursor (+4) runs the command nodes it passes. Returns 1 if the stage has a node ahead. */
int stage_window_update(int stage)
{
	int n, cur;
	ww(STAGE_CUR, stage);
	stage = ruw(STAGE_CUR);
	for (n = rw(stage + 6); n != 0 && !(rw(0x20 + 2 * node_kind(n)) & rw(stage + 0x1E)); n = node_next(n))
		;
	ww(stage + 6, n);
	while ((cur = rw(stage + 4)) != 0 && !(rw(0x20 + 2 * node_kind(cur)) & rw(stage + 0x1E))) {
		if (!stage_run_command())
			break;
		ww(stage + 4, node_next(cur));
		if (rw(stage + 2) == cur)
			ww(stage + 2, rw(stage + 4));
	}
	return n != 0;
}

/* D40F4: the last stage record (C28E) follows playback. It runs the command nodes playback has reached (index
 * markers, end of sentence) and frees the timed segments whose frames have been played ([DD9A] counts them).
 * Returns 0 at a segment still playing, 1 at a command that must wait, 2 when its window is used up. */
int stage_playback_run(void)
{
	int n;
	if (stage_window_update(0xC28E)) {
		while ((n = rw(0xC292)) != 0) {
			if (node_kind(n) == NODE_COMMAND) {
				if (!stage_run_command()) {
					ww(0xC294, n);
					ww(0xC290, n);
					stage_commit();
					return 1;
				}
			} else if (node_kind(n) == NODE_SEGMENT) {
				if (rw(SEGMENTS_PLAYED) == 0) {
					ww(0xC294, n);
					ww(0xC290, n);
					stage_commit();
					return 0;
				}
				ww(SEGMENTS_PLAYED, rw(SEGMENTS_PLAYED) - 1);
				if (pg_segment_hook)
					pg_segment_hook(node_char(n));
			}
			ww(0xC292, node_next(rw(0xC292)));
		}
		ww(0xC294, rw(0xC292));
		ww(0xC290, rw(0xC292));
	}
	stage_commit();
	return 2;
}

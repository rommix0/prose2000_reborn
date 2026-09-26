/* v1.1 node list, stage windows and in-band commands.
 *
 * The design is v3.4.1's (REFERENCE §15.1): one doubly linked list of 8-byte nodes that the stages pass along,
 * each stage working on a window of it described by its stage record (V1_REC_*). A stage opens its window with
 * v1_stage_begin, which also runs the command nodes at its cursor, and hands the finished part on to the next stage
 * with v1_stage_commit. The node pool (250 nodes at DS:8D5E) and the list are set up by v1_nodes_reset. */
#include "v1.h"

/* EC756: link node in front of before */
static void link_before(unsigned node, unsigned before)
{
	unsigned prev = ruw(before + V1_NODE_PREV);
	ww(node + V1_NODE_PREV, prev);
	ww(prev + V1_NODE_NEXT, node);
	ww(node + V1_NODE_NEXT, before);
	ww(before + V1_NODE_PREV, node);
}

/* ECA94: unlink a node */
static unsigned unlink_node(unsigned node)
{
	ww(ruw(node + V1_NODE_PREV) + V1_NODE_NEXT, rw(node + V1_NODE_NEXT));
	ww(ruw(node + V1_NODE_NEXT) + V1_NODE_PREV, rw(node + V1_NODE_PREV));
	return node;
}

static unsigned stage(void) { return ruw(V1_STAGE); }

/* EC783: 250 free nodes, the empty list (two sentinels at DS:8D3A and 8D42) and the five stage records, each with
 * a copy of the host settings */
void v1_nodes_reset(void)
{
	ww(V1_STAGE, 0);
	ww(V1_FREE_COUNT, V1_NODE_COUNT);
	ww(V1_FREE_START, 0x8D56);
	ww(0x8D56 + V1_NODE_PREV, 0);
	ww(0x8D56 + V1_NODE_FLAGS, rw(0x8D56 + V1_NODE_FLAGS) | 7);
	ww(V1_FREE_END, 0x8D4E);
	ww(0x8D4E + V1_NODE_NEXT, 0);
	ww(0x8D4E + V1_NODE_FLAGS, rw(0x8D4E + V1_NODE_FLAGS) | 7);
	unsigned prev = 0x8D56;
	for (unsigned i = 0; i < V1_NODE_COUNT; i++) {
		unsigned node = V1_NODE_POOL + 8 * i;
		ww(prev + V1_NODE_NEXT, node);
		ww(node + V1_NODE_PREV, prev);
		ww(node + V1_NODE_FLAGS, (rw(node + V1_NODE_FLAGS) & 0xFFF8) | 6);
		prev = node;
	}
	ww(0x8D4E + V1_NODE_PREV, V1_NODE_POOL + 8 * (V1_NODE_COUNT - 1));
	ww(V1_LIST_END, 0x8D3A);
	ww(V1_LIST_START, 0x8D42);
	ww(0x8D3A + V1_NODE_NEXT, 0);
	ww(0x8D3A + V1_NODE_PREV, 0x8D42);
	ww(0x8D3A + V1_NODE_FLAGS, rw(0x8D3A + V1_NODE_FLAGS) | 7);
	ww(0x8D42 + V1_NODE_NEXT, 0x8D3A);
	ww(0x8D42 + V1_NODE_PREV, 0);
	ww(0x8D42 + V1_NODE_FLAGS, rw(0x8D42 + V1_NODE_FLAGS) | 7);
	for (unsigned k = 0; k < 5; k++) {
		unsigned r = V1_REC_TEXTRULES + V1_REC_SIZE * k;
		for (unsigned i = 0; i < 14; i += 2)
			ww(r + i, 0);
		ww(r + V1_REC_INPUT, rw(V1_SET_INPUT));
		ww(r + V1_REC_WORD, rw(V1_SET_WORD));
		ww(r + V1_REC_RATE, rw(V1_SET_RATE));
		ww(r + V1_REC_PITCH, rw(V1_SET_PITCH));
		ww(r + V1_REC_AMPLITUDE, rw(V1_SET_AMPLITUDE));
		ww(r + V1_REC_MODE, rw(V1_HOST_MODE));
	}
}

/* EC417: take a free node, link it after (where 1) or in front of (where 0) ref, and make it a node of the given kind
 * holding ch. It extends the current stage's window when ref is the window's edge on that side. */
unsigned v1_node_insert(unsigned ref, int where, int kind, int ch)
{
	int free_count = rw(V1_FREE_COUNT);
	count32(0xA4AA);
	if (free_count < 1)
		v1_fatal_error(0x1E);
	if (ref == 0)
		v1_fatal_error(0x1F);
	unsigned node = unlink_node(ruw(ruw(V1_FREE_START) + V1_NODE_NEXT));
	ww(V1_FREE_COUNT, rw(V1_FREE_COUNT) - 1);
	link_before(node, where == 1 ? ruw(ref + V1_NODE_NEXT) : ref);
	ww(node + V1_NODE_FLAGS, kind & 7); /* no parameter byte, flag bits 3-7 clear */
	wb(node + V1_NODE_ARG1, 0);
	wb(node + V1_NODE_CH, ch);
	unsigned s = stage();
	if (s != 0) {
		if (ruw(s + V1_REC_LAST) == ref && where == 1)
			ww(s + V1_REC_LAST, node);
		else if (ruw(s + V1_REC_FIRST) == ref && where == 0)
			ww(s + V1_REC_FIRST, node);
	}
	return node;
}

/* EC3B4: append a node to the list for the text-rules stage (the input stage's output) */
unsigned v1_node_append(int kind, int ch)
{
	count32(0xA4A6);
	unsigned node = v1_node_insert(ruw(V1_LIST_END), 0, kind, ch);
	unsigned r = V1_REC_TEXTRULES;
	ww(r + V1_REC_LAST, node);
	for (int i = 6; i >= 0; i -= 2)
		if (rw(r + (unsigned)i) == 0)
			ww(r + (unsigned)i, node);
	return node;
}

/* EC4F1: the node before this one in the current window, or 0 */
unsigned v1_node_prev(unsigned node)
{
	return ruw(stage() + V1_REC_FIRST) == node ? 0 : ruw(node + V1_NODE_PREV);
}

/* EC735: the node after this one in the current window, or 0 */
unsigned v1_node_next(unsigned node)
{
	return ruw(stage() + V1_REC_LAST) == node ? 0 : ruw(node + V1_NODE_NEXT);
}

/* EC658: return a node to the free list. Returns its neighbour (after it for dir 1, before it for dir 0), or 0 at the
 * end of the current window. */
unsigned v1_node_free(unsigned node, int dir)
{
	count32(0xA4AE);
	if (node == 0)
		v1_fatal_error(0x20);
	unsigned res = dir == 1 ? ruw(node + V1_NODE_NEXT) : ruw(node + V1_NODE_PREV);
	unsigned s = stage();
	if (s != 0) {
		if (ruw(s + V1_REC_LAST) == node) {
			if (ruw(s + V1_REC_LAST) == ruw(s + V1_REC_FIRST)) {
				ww(s + V1_REC_LAST, 0);
				ww(s + V1_REC_FIRST, 0);
			} else {
				ww(s + V1_REC_LAST, rw(ruw(s + V1_REC_LAST) + V1_NODE_PREV));
			}
			if (dir == 1)
				res = 0;
		}
		if (ruw(s + V1_REC_FIRST) == node) {
			ww(s + V1_REC_FIRST, rw(ruw(s + V1_REC_FIRST) + V1_NODE_NEXT));
			if (dir == 0)
				res = 0;
		}
	}
	unlink_node(node);
	link_before(node, ruw(V1_FREE_END));
	ww(V1_FREE_COUNT, rw(V1_FREE_COUNT) + 1);
	ww(node + V1_NODE_FLAGS, (rw(node + V1_NODE_FLAGS) & 0xFFF8) | 6);
	return res;
}

/* EC512: hand the finished part of the window (from its first node up to the one before DONE, or all of it) to the
 * next stage, whose window grows to its end; the playback stage frees it instead. Returns 1 if there was any. */
int v1_stage_commit(void)
{
	int res = 0;
	unsigned s = stage(), next = s + V1_REC_SIZE;
	count32(0xA4B6);
	if (rw(s + V1_REC_FIRST) != 0 && rw(s + V1_REC_FIRST) != rw(s + V1_REC_DONE)) {
		res = 1;
		unsigned last = rw(s + V1_REC_DONE) != 0 ? ruw(ruw(s + V1_REC_DONE) + V1_NODE_PREV) : ruw(s + V1_REC_LAST);
		if (s == V1_REC_PLAYBACK) {
			while (rw(s + V1_REC_FIRST) != rw(s + V1_REC_DONE))
				ww(s + V1_REC_FIRST, v1_node_free(ruw(s + V1_REC_FIRST), 1));
		} else {
			ww(next + V1_REC_LAST, last);
			for (int i = 6; i >= 0; i -= 2)
				if (rw(next + (unsigned)i) == 0)
					ww(next + (unsigned)i, rw(s + V1_REC_FIRST));
		}
		if (rw(s + V1_REC_DONE) == 0) {
			ww(s + V1_REC_FIRST, 0);
			ww(s + V1_REC_CURSOR, 0);
			ww(s + V1_REC_AHEAD, 0);
			ww(s + V1_REC_LAST, 0);
		} else {
			ww(s + V1_REC_FIRST, rw(s + V1_REC_DONE));
		}
	}
	ww(V1_STAGE, 0);
	return res;
}

/* EC917: whether the current stage works on this node: the kind's bit in the record's mask (kind 0 = 0x10, 1-3 =
 * 1/2/4, 5 = 0x20; kind 4 needs mask bit 8 and a phoneme with feature bit 0x80 in the table at DS:0090) */
int v1_stage_accepts(unsigned node)
{
	if (node == 0)
		return 0;
	unsigned mask = ruw(stage() + V1_REC_MASK);
	switch (rw(node + V1_NODE_FLAGS) & 7) {
	case 0: return (int)(mask & 0x10);
	case 1: return (int)(mask & 1);
	case 2: return (int)(mask & 2);
	case 3: return (int)(mask & 4);
	case 4: return (mask & 8) && (rsb((unsigned)(0x90 + rsb(node + V1_NODE_CH))) & 0x80) ? 1 : 0;
	case 5: return (int)(mask & 0x20);
	default: return 0;
	}
}

/* F12F5: the command node at the current stage's cursor. Settings commands update the stage record; s, t and g
 * become kind 5 (hold) in the prosody stage; i and x reach the host from the playback stage. Returns 0 if the index
 * reply did not fit in the output ring (retry later), else 1. */
int v1_command_apply(void)
{
	unsigned s = stage(), node = ruw(s + V1_REC_CURSOR);
	if (rw(node + V1_NODE_FLAGS) & 7)
		return 1;
	if (s == V1_REC_PLAYBACK)
		count32(0xA4DA);
	int p0 = (rw(node + V1_NODE_FLAGS) >> 8) & 0xFF, p1 = rb(node + V1_NODE_ARG1);
	switch (rsb(node + V1_NODE_CH)) {
	case 'i':
		if (s == V1_REC_PLAYBACK) {
			if (rw(s + V1_REC_MODE) & 0x400) {
				uint8_t b[1] = {(uint8_t)p0};
				if (!v1_host_send(1, 'i', 1, b))
					return 0;
			}
			if ((rw(node + V1_NODE_FLAGS) >> 5) & 1) {
				ww(V1_STOP_INDEX, p0);
				ww(V1_STOP_REQUEST, 1);
			}
		}
		break;
	case 'x':
		if (s == V1_REC_PLAYBACK) {
			ww(V1_STOP_INDEX, 0);
			ww(V1_STOP_REQUEST, 1);
		}
		break;
	case 'C':
		if (s == V1_REC_LEXICAL)
			v1_node_insert(ruw(s + V1_REC_CURSOR), 0, 3, ')');
		break;
	case 'I': ww(s + V1_REC_INPUT, p0); break;
	case 'P': ww(s + V1_REC_WORD, p0); break;
	case 'a': ww(s + V1_REC_AMPLITUDE, p0); break;
	case 'r': ww(s + V1_REC_RATE, p0); break;
	case 'p': ww(s + V1_REC_PITCH, p0); break;
	case 's':
	case 't':
	case 'g':
		if (s == V1_REC_PROSODY)
			ww(node + V1_NODE_FLAGS, (rw(node + V1_NODE_FLAGS) & 0xFFF8) | 5);
		break;
	case 'l': wb(V1_L_VALUES + (unsigned)p0, p1); break;
	case 'N': ww(s + V1_REC_MODE, p1 << 8 | p0); break;
	default: v1_fatal_error(1); break;
	}
	return 1;
}

/* EC9D6: make rec the current stage, move its look-ahead cursor to a node it works on, and run the command nodes at
 * its cursor (moving DONE along with it). Returns whether the look-ahead node is one it works on. */
int v1_stage_begin(unsigned rec)
{
	count32(0xA4B2);
	ww(V1_STAGE, rec);
	while (rw(rec + V1_REC_AHEAD) != 0 && !v1_stage_accepts(ruw(rec + V1_REC_AHEAD)))
		ww(rec + V1_REC_AHEAD, v1_node_next(ruw(rec + V1_REC_AHEAD)));
	while (!v1_stage_accepts(ruw(rec + V1_REC_CURSOR)) && rw(rec + V1_REC_CURSOR) != 0 && v1_command_apply()) {
		unsigned node = ruw(rec + V1_REC_CURSOR);
		ww(rec + V1_REC_CURSOR, v1_node_next(node));
		if (ruw(rec + V1_REC_DONE) == node)
			ww(rec + V1_REC_DONE, rw(rec + V1_REC_CURSOR));
	}
	return v1_stage_accepts(ruw(rec + V1_REC_AHEAD));
}

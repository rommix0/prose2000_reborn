/* v1.1 input stage (host text into nodes) and playback stage (frees played nodes, reports index markers and the end
 * of a sentence). Both work like v3.4.1's (REFERENCE §15.5). */
#include "v1.h"

/* F53A0: move up to 10 input bytes into nodes: text as kind 1, and the escape parser's in-band commands (ESC, letter,
 * count, values) as kind 0 nodes. It stops after a run of spaces, so it hands on about a word at a time. The byte
 * that ends a run is kept in V1_INPUT_AHEAD. Returns 1 if it made any node. */
int v1_stage_input_run(void)
{
	count32(0xA4BA);
	int n = 0, space = 0, made = 0;
	if (rw(V1_INPUT_AHEAD) == -1) {
		ww(V1_INPUT_AHEAD, v1_input_getc());
		if (rw(V1_INPUT_AHEAD) == -1)
			return 0;
	}
	for (;;) {
		if (rw(V1_INPUT_AHEAD) == ' ')
			space = 1;
		else if (space)
			return made;
		if (n++ >= 10)
			return made;
		if (rw(V1_INPUT_AHEAD) == 0x1B) {
			unsigned node = v1_node_append(0, rw(V1_INPUT_AHEAD));
			wb(node + V1_NODE_CH, v1_input_getc());
			int count = v1_input_getc();
			if (count == 1) {
				wb(node + V1_NODE_ARG0, v1_input_getc());
				wb(node + V1_NODE_ARG1, 0);
			} else if (count == 2) {
				wb(node + V1_NODE_ARG0, v1_input_getc());
				wb(node + V1_NODE_ARG1, v1_input_getc());
			} else if (count == 3) {
				/* ESC[t: the first value (0-9) goes into flag bits 3-7 */
				int v = v1_input_getc();
				unsigned f = (ruw(node + V1_NODE_FLAGS) & 0xFFE7) | (v & 3) << 3;
				if (v & 4)
					f |= 0x20;
				if (v & 8)
					f |= 0x40;
				if (v & 0x10)
					f |= 0x80;
				ww(node + V1_NODE_FLAGS, f);
				wb(node + V1_NODE_ARG0, v1_input_getc());
				wb(node + V1_NODE_ARG1, v1_input_getc());
			} else {
				wb(node + V1_NODE_ARG0, 0);
				wb(node + V1_NODE_ARG1, 0);
			}
		} else {
			v1_node_append(1, rw(V1_INPUT_AHEAD));
		}
		ww(V1_INPUT_AHEAD, -1);
		made = 1;
		ww(V1_INPUT_AHEAD, v1_input_getc());
		if (rw(V1_INPUT_AHEAD) == -1)
			return 1;
	}
}

/* ECACC: run the command nodes that have reached playback and free the nodes behind them. It stops at a timed
 * segment the frame builder has not finished (returns 0) or at an index reply that did not fit (1); otherwise 2. */
int v1_stage_playback_run(void)
{
	unsigned r = V1_REC_PLAYBACK;
	count32(0xA4CE);
	if (!v1_stage_begin(r)) {
		v1_stage_commit();
		return 2;
	}
	int res = 2;
	for (; rw(r + V1_REC_CURSOR) != 0; ww(r + V1_REC_CURSOR, v1_node_next(ruw(r + V1_REC_CURSOR)))) {
		int kind = rw(ruw(r + V1_REC_CURSOR) + V1_NODE_FLAGS) & 7;
		if (kind == 0 && !v1_command_apply()) {
			res = 1;
			break;
		}
		if (kind == 4) {
			if (rw(V1_SEGMENTS_PLAYED) == 0) {
				res = 0;
				break;
			}
			ww(V1_SEGMENTS_PLAYED, rw(V1_SEGMENTS_PLAYED) - 1);
		}
	}
	ww(r + V1_REC_AHEAD, rw(r + V1_REC_CURSOR));
	ww(r + V1_REC_DONE, rw(r + V1_REC_CURSOR));
	v1_stage_commit();
	return res;
}

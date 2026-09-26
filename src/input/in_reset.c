/* The synthesis loop's resets: after an end of sentence or ESC[S (loop_reset), ESC[q (quit_reset), and at its entry
 * (loop_entry, after ^R or ESC[W). They empty the node list and set every stage back to its start. */
#include "input.h"
#include "prosody.h"

/* D641F: the receive side */
static void rx_reset(void)
{
	ww(ESC_STATE, 1);
	ww(RX_BUSY, 0);
	ww(0xDB92, 0); /* frames the DSP asked for while none was ready */
	latch_set(0x89);
}

/* the settings into the five stage records (their other fields cleared) */
static void records_reset(void)
{
	for (int r = 0xC206; r < 0xC206 + 5 * 0x22; r += 0x22) {
		for (int i = 0; i < 0x0E; i += 2)
			ww(r + i, 0);
		ww(r + 0x0E, rw(SET_INPUT));
		ww(r + 0x10, rw(SET_PROSODY));
		ww(r + 0x12, rw(SET_SPEED));
		ww(r + 0x14, rw(SET_PITCH));
		ww(r + 0x16, rw(SET_AMPLITUDE));
		ww(r + 0x18, rw(SET_FAST));
		ww(r + 0x1A, rw(HOST_MODE));
		ww(r + 0x1C, rw(HOST_AFLAGS));
		ww(r + 0x20, rw(SET_VOICE));
	}
}

static void set_kind7(int n)
{
	ww(n + N_FLAGS, (rw(n + N_FLAGS) & 0xFFF8) | 7);
}

/* D3900: all nodes free, an empty list (head C2B4, tail C2BE), fresh stage records */
void node_list_init(void)
{
	int prev;
	ww(0xC202, 0);
	ww(FREE_COUNT, NODE_COUNT);
	ww(FREE_HEAD, 0xC2D6); /* the free list runs between two sentinels, C2D6 and C2CC */
	ww(0xC2D6 + 2, 0);
	set_kind7(0xC2D6);
	ww(FREE_TAIL, 0xC2CC);
	ww(0xC2CC, 0);
	set_kind7(0xC2CC);
	prev = rw(FREE_HEAD);
	for (int i = 0; i < NODE_COUNT; i++) {
		int n = NODE_BASE + 10 * i;
		ww(prev, n);
		ww(n + 2, prev);
		ww(n + N_FLAGS, (rw(n + N_FLAGS) & 0xFFF8) | 6);
		prev = n;
	}
	ww(prev, rw(FREE_TAIL));
	ww(rw(FREE_TAIL) + 2, NODE_BASE + 10 * (NODE_COUNT - 1));
	ww(LIST_HEAD, 0xC2B4);
	ww(LIST_TAIL, 0xC2BE);
	ww(rw(LIST_HEAD), 0);
	ww(rw(LIST_HEAD) + 2, rw(LIST_TAIL));
	set_kind7(rw(LIST_HEAD));
	ww(rw(LIST_TAIL), rw(LIST_HEAD));
	ww(rw(LIST_TAIL) + 2, 0);
	set_kind7(rw(LIST_TAIL));
	records_reset();
}

/* D3A97: delete every node of the list, fresh stage records */
static void node_list_flush(void)
{
	ww(0xC202, 0);
	for (int n = rw(rw(LIST_TAIL)); n != rw(LIST_HEAD);)
		n = node_delete(n, 1);
	records_reset();
}

/* D8918: the frame sender: no hold, and the frame buffer from the ROM's silence frame (frame_builder_reset_frame
 * keeps its own copy of this state) */
static void frame_reset(void)
{
	ww(0xDBCE, 0);
	ww(0xDBD0, 0);
	ww(OUTPUT_HOLD, 0);
	for (int i = 0; i < 40; i++)
		ww(0xDBD2 + 2 * i, rw(0x5656 + 2 * i));
}

/* DE024: both host rings empty, flow control and the transmitter idle; XOFF counts as sent, so the input stage
 * sends XON once it next reads */
static void host_link_reset(void)
{
	ww(RX_WRITE, 0);
	ww(RX_READ, 0);
	ww(INPUT_WRITE, 0);
	ww(INPUT_READ, 0);
	ww(INPUT_AHEAD, -1);
	ww(INPUT_XOFF, 1);
	ww(OUTPUT_WRITE, 0);
	ww(OUTPUT_READ, 0);
	wb(FLOW_PENDING, 0);
	ww(TX_PENDING, 0);
	ww(TX_STOPPED, 0);
	ww(TX_RUNNING, 0);
	ww(0xEE1A, 0);
	if (dip_switches() & 0x20)
		uart_command_clear(0x20);
	else
		uart_command_set(0x20);
	wb(BEL_COUNT, 0);
	ww(DEMO_PTR, rw(DEMO_TEXT_PTR));
	ww(INPUT_NFLAGS, 0x17C0);
	ww(INPUT_AFLAGS, 0x41);
}

/* E3D94 */
static void lexical_reset(void)
{
	ww(0xEE9A, 0);
	ww(0xEE98, 0);
	ww(0xEE96, 0);
	ww(0xEEA0, 0);
	ww(0xEE9E, 0);
	ww(0xC246, 6);
	ww(0xEEAE, 0);
}

/* D4185 */
static void text_rules_reset(void)
{
	ww(0xC224, 0x17);
	ww(0xDB72, 0x146E);
	ww(0xDB70, 1);
	ww(0xDB18, 0xDB1A);
	ww(0xDB6E, 0);
}

static void loop_flags_reset(void)
{
	ww(0xEE5C, 0);
	ww(INPUT_IDLE, 1);
	for (int a = 0xEE60; a <= 0xEE68; a += 2)
		ww(a, 0);
	ww(PLAYBACK_STATE, 2);
	ww(K_MODE, 0);
	ww(STOP_REQUEST, 0);
	ww(END_REQUEST, 0);
	ww(QUIT_REQUEST, 0);
}

/* the stages after the node list (host: loop_reset also resets the host link, in this place) */
static void stages_reset(int host)
{
	param_tracks_init();
	frame_reset();
	if (host)
		host_link_reset();
	ww(0xC2AC, 0x3F); /* D40ED */
	ww(0xDC22, 0);    /* D89B8 (frame_builder_reset_source) */
	ww(0xDC26, 0x55);
	ww(0xDC24, 0);
	paramgen_reset();
	prosody_reset();
	lexical_reset();
	text_rules_reset();
}

/* E38FF: everything back to the start, including the host rings (text not yet taken in is lost). Returns the marker
 * for the ESC[n x reply (the index marker that ended the sentence, 0 for x), or -1 when there was no x (ESC[S). */
int loop_reset(void)
{
	int marker = rw(END_REQUEST) ? s16(rw(0xEE72)) : -1;
	ww(IRQ_NEST, 1);
	loop_flags_reset();
	rx_reset();
	node_list_init();
	stages_reset(1);
	ww(IRQ_NEST, rw(IRQ_NEST) - 1);
	return marker;
}

/* E3CE7: ESC[q: stop speaking now; the host rings and the settings are kept */
void quit_reset(void)
{
	loop_flags_reset();
	node_list_flush();
	ww(IRQ_NEST, 1);
	stages_reset(0);
	ww(IRQ_NEST, rw(IRQ_NEST) - 1);
}

/* E37F0: the synthesis loop's entry: default settings, the hardware set-up (code 0x12: power-up and ^R; in_boot.c),
 * the pipeline reset, and the reply ESC[nR (code 0x12, n = the self-test result at [EE78]) or ESC[W. The firmware
 * waits for the output ring to empty before the XON (output_wait). */
void loop_entry(int code)
{
	ww(SET_INPUT, 0);
	ww(SET_PROSODY, 1);
	ww(SET_SPEED, 13);
	ww(SET_RATE, 150);
	ww(SET_PITCH, 85);
	ww(SET_AMPLITUDE, 0);
	ww(SET_FAST, 0);
	ww(HOST_MODE, 0x17C0);
	ww(HOST_AFLAGS, 0x41);
	ww(SET_VOICE, 0);
	for (int i = 0; i < 22; i++)
		wb(LOW_VALUES + i, rb(LOW_DEFAULT + i));
	if (rw(0xDB8C) != 0) /* E3D71: clear the error counters */
		for (int a = 0xEE7A; a <= 0xEE8E; a += 4) {
			ww(a + 2, 0);
			ww(a, 0);
		}
	ww(END_REQUEST, 0);
	if ((code & 0xFF) == 0x12)
		boot_hardware_init();
	loop_reset();
	if ((code & 0xFF) == 0x12) {
		int r[1] = {rb(SELF_TEST)};
		if (rb(SELF_TEST) == 0)
			latch_clear(0x10);
		host_send(1, 'R', 1, r);
	} else {
		host_send(1, 'W', 0, NULL);
	}
	output_wait();
	host_send(0, 0x11, 0, NULL);
	latch_set(2);
	latch_clear(0x20);
}

/* the part of the loop that follows ESC[q, and ESC[S or an end of sentence reaching playback (E39B5); returns 1 if
 * it acted. */
int loop_check_stop(void)
{
	if (rw(QUIT_REQUEST)) {
		quit_reset();
		return 1;
	}
	if (!rw(STOP_REQUEST))
		return 0;
	output_wait();
	int marker = loop_reset();
	if (marker < 0) {
		host_send(1, 'S', 0, NULL);
	} else {
		int p[1] = {marker};
		host_send(1, 'x', 1, p);
	}
	output_wait();
	host_send(0, 0x11, 0, NULL);
	return 1;
}

/* v1.1 power-up and the synthesis loop's resets (REFERENCE §16).
 *
 * loop_restart (EC021) sets up the interrupt vectors, the latch and the 8259 and calls synthesis_main (F7204), which
 * starts with loop_entry (F7083): default host settings, the 8251, the DSP boot block, loop_reset and the reset reply
 * ESC[1R. loop_reset (F716E) runs again at the end of each utterance and on ESC[S; it resets every stage. */
#include "v1.h"

void (*v1_dsp_write_hook)(unsigned word);

static void count32_dec(unsigned a)
{
	unsigned lo = ruw(a);
	ww(a, lo - 1);
	if (lo == 0)
		ww(a + 2, rw(a + 2) - 1);
}

/* EC12B: the peripheral latch's power-up value */
static void latch_init(void) { wb(V1_LATCH, 0x5E); }

/* EC181: 8251 reset and mode (from the DIP switches, which the C does not model), then command 04 (RxEN) */
static void uart_init(void) { wb(V1_UART_CMD, 4); }

/* EC135: pulse the DSP's reset (latch bit 6) with its clock on (bit 5), then send the 15-word boot block; the
 * handshake (RQM, and USF0 after the first word) is the board's business */
static void dsp_boot(unsigned block)
{
	wb(V1_LATCH, rb(V1_LATCH) | 0x20);
	for (unsigned i = 0; i < 15; i++)
		if (v1_dsp_write_hook)
			v1_dsp_write_hook(ruw(block + 2 * i));
}

/* EC02F: all 40 interrupt vectors at linear 0000 point to EC00:00C7 (fatal_error 0x25), except IR0/IR7 (8259 vectors
 * 20h/27h) = dsp_irq_service EC1E0, IR1 = uart_rx_isr EC2C8, IR2/IR3 = uart_tx_isr EC341. The latch goes to 0 and the
 * 8259 mask to 7C. */
static void restart_setup(void)
{
	for (unsigned v = 0; v < 0xA0; v += 4) {
		ww(V1_DS_RAM_LO + v, 0xC7);
		ww(V1_DS_RAM_LO + v + 2, 0xEC00);
	}
	ww(V1_DS_RAM_LO + 0x80, 0x1E0);
	ww(V1_DS_RAM_LO + 0x84, 0x2C8);
	ww(V1_DS_RAM_LO + 0x88, 0x341);
	ww(V1_DS_RAM_LO + 0x8C, 0x341);
	ww(V1_DS_RAM_LO + 0x9C, 0x1E0);
	wb(V1_LATCH, 0);
	wb(V1_DSP_SPURIOUS, 0);
	wb(V1_PIC_MASK, 0x7C);
}

/* EC021 (the RAM part), after ^R, ESC[T or a fatal error */
void v1_loop_restart(void)
{
	ww(V1_POWERED_UP, 0);
	restart_setup();
}

/* EC000 (the RAM part), at reset: RAM filled with 0x55 and V1_POWERED_UP set, so loop_entry also clears the error
 * counters */
void v1_boot_reset(void)
{
	for (unsigned i = 0; i < 0x3000; i++)
		wb(V1_DS_RAM_LO + i, 0x55);
	ww(V1_POWERED_UP, 1);
	restart_setup();
}

/* ED9D8: the escape parser back to text; receive, transmit and frame interrupts on (latch bits 0, 7, 3) */
static void escape_reset(void)
{
	ww(V1_ESC_STATE, 1);
	ww(V1_RX_BUSY, 0);
	ww(0x959A, 0);
	v1_latch_set(1);
	v1_latch_set(0x80);
	v1_latch_set(8);
}

/* F14AA: the 18 parameter tracks (128-byte rings at DS:9862, pointers at 9742) filled with 12 frames of their rest
 * values (DS:23DB), and the frame builder's ring positions */
static void frame_reset(void)
{
	for (unsigned i = 0; i < 18; i++) {
		ww(0x971E + 2 * i, 10);
		ww(0x96FA + 2 * i, 12);
		ww(V1_TRACKS + 2 * i, 0x9862 + 0x80 * i);
		int rest = rb(0x23DB + i);
		for (unsigned j = 0; j < 12; j++)
			wb(ruw(V1_TRACKS + 2 * i) + j, rest);
	}
	ww(V1_TRACK_READ, 0);
	ww(0x96E4, 12);
	ww(0x96E2, 12);
	ww(0x96F8, 0);
	ww(V1_SEGMENTS_PLAYED, 0);
	for (unsigned i = 0; i < 16; i++) {
		wb(0x96E6 + i, 0);
		wb(0xA20E + i, 0);
	}
	ww(0x96E0, rw(0x96E2) - 2);
	ww(0x96DC, 0);
}

/* EE466: output hold off and the frame buffer (37 words at DS:95C8) from its silent default (DS:1823) */
static void dsp_reset(void)
{
	ww(0x95C4, 0);
	ww(0x95C6, 0);
	ww(V1_OUTPUT_HOLD, 0);
	for (unsigned i = 0; i < 37; i++)
		ww(0x95C8 + 2 * i, rw(0x1823 + 2 * i));
}

/* F2170: the parameter generator's state and the ESC[l values (from the host settings); F2601 sets each track's
 * current value (its rest value) */
static void paramgen_reset(void)
{
	ww(0xA222, 4);
	ww(0xA220, 3);
	ww(0xA21E, 2);
	ww(V1_REC_PARAMGEN + V1_REC_MASK, 0x28);
	ww(0xA206, 0);
	ww(0xA20C, 0);
	ww(0xA204, 1);
	ww(0xA208, 1);
	for (unsigned i = 0; i < 18; i++)
		wb(V1_L_VALUES + i, rb(V1_L_TABLE + i));
	v1_pg_state_reset();
}

/* EF343: the prosody stage's state */
static void prosody_reset(void)
{
	ww(V1_REC_PROSODY + V1_REC_MASK, 0x1C);
	ww(0x9684, 1);
	ww(0x966C, 0);
	ww(0x9672, 0);
	ww(0x9674, 0);
	ww(0x968A, 0xA0);
	ww(0x967C, 0);
	ww(0x9680, 5);
	ww(0x9658, 7);
	ww(0x9678, 0x14);
	ww(0x9676, 0);
	ww(0x9656, 0);
}

/* ECB85: the text-rules stage's state */
static void textrules_reset(void)
{
	ww(V1_REC_TEXTRULES + V1_REC_MASK, 0x17);
	ww(V1_TR_PC, V1_TR_PROGRAM);
	ww(V1_TR_TEST, 1);
	ww(V1_TR_CALLS, V1_TR_STACK);
	ww(V1_TR_YIELDED, 0);
}

/* F716E: reset every stage. Returns the index that ended the utterance (V1_STOP_INDEX) after ESC[x or an index
 * stop, or -1 (a plain stop). */
int v1_loop_reset(void)
{
	v1_irq_disable();
	ww(V1_IRQ_NEST, 1);
	int res = rw(V1_END_REQUEST) == 0 ? -1 : rw(V1_STOP_INDEX);
	ww(V1_IDLE, 0);
	ww(V1_INPUT_EMPTY, 1);
	for (unsigned a = V1_RUN_TEXTRULES; a <= V1_TRACKS_FULL; a += 2)
		ww(a, 0);
	ww(V1_PLAYBACK_STATE, 2);
	ww(V1_STOP_REQUEST, 0);
	ww(V1_END_REQUEST, 0);
	escape_reset();
	v1_nodes_reset();
	frame_reset();
	dsp_reset();
	v1_host_rings_reset();
	ww(V1_REC_PLAYBACK + V1_REC_MASK, 0x3F); /* ECABD */
	paramgen_reset();                         /* after EE55B, which does nothing */
	prosody_reset();
	ww(V1_REC_LEXICAL + V1_REC_MASK, 2); /* F5A6C */
	textrules_reset();
	ww(V1_IRQ_NEST, rw(V1_IRQ_NEST) - 1);
	v1_irq_enable();
	return res;
}

/* F5A2E: clear the event counters A416-A541, and after a reset (V1_POWERED_UP) also the error counters A3EA-A415 */
static void counters_reset(void)
{
	for (unsigned a = 0xA416; a <= 0xA53E; a += 4)
		ww(a, 0), ww(a + 2, 0);
	if (rw(V1_POWERED_UP))
		for (unsigned a = 0xA3EA; a <= 0xA412; a += 4)
			ww(a, 0), ww(a + 2, 0);
}

/* the transmit interrupt while the firmware waits for the output ring to empty; on the board the transmitter's next
 * interrupt finds the ring empty and stops it before the firmware queues anything else */
static void drain_output(void)
{
	while (rw(V1_TX_RUNNING))
		v1_uart_tx_isr();
}

/* F7083: the start of synthesis_main. The reply is ESC[1R: the self-test (EC277) always passes, and the value comes
 * from DS:48C8 (48C6 on a failure). */
void v1_loop_entry(void)
{
	v1_irq_disable();
	ww(V1_SET_INPUT, 0);
	ww(V1_SET_WORD, 1);
	ww(V1_SET_RATE, 0xA0);
	ww(V1_SET_PITCH, 0x4B);
	ww(V1_SET_AMPLITUDE, 0);
	ww(V1_HOST_MODE, 0x7C0);
	for (unsigned i = 0; i < 18; i++)
		wb(V1_L_TABLE + i, rb(0x24A1 + i));
	counters_reset();
	ww(V1_END_REQUEST, 0);
	uart_init();
	latch_init();
	dsp_boot(0x1805);
	v1_loop_reset();
	v1_latch_clear(0x10);
	uint8_t v[1] = {(uint8_t)rb(0x48C8)};
	v1_host_send(1, 'R', 1, v);
	drain_output();
	v1_host_send(0, 0x11, 0, NULL);
	count32_dec(0xA43E); /* the XON is not counted */
	v1_uart_command_set(2);
	v1_latch_clear(2);
}

/* EC000 and F7083: the state synthesis_main starts its loop with after a reset */
void v1_power_up(void)
{
	v1_boot_reset();
	v1_loop_entry();
}

/* the transmitter's start from the loop and the idle routine; it needs DIP S4-5 (off on the emulated board) */
static void tx_kick_switch(void)
{
	if (!rw(V1_TX_RUNNING) && rw(V1_TX_PENDING) && !rw(V1_TX_STOPPED) && v1_dip_switch_test(0x10) &&
	    v1_uart_status_test(0x80)) {
		v1_irq_disable_nested();
		v1_uart_tx_start();
		ww(V1_TX_RUNNING, 1);
		v1_irq_enable_nested();
	}
}

/* F7204 (the stop request): once the output ring has drained, reset every stage and reply ESC[S (a plain stop) or
 * ESC[n x with the index that ended the utterance, then XON (not counted). The waits are the transmitter's: the
 * caller drains the ring through v1_uart_tx_isr. Returns 1 if there was a stop to handle. */
int v1_loop_check_stop(void)
{
	if (!rw(V1_STOP_REQUEST))
		return 0;
	drain_output();
	int r = v1_loop_reset();
	if (r < 0) {
		v1_host_send(1, 'S', 0, NULL);
	} else {
		uint8_t v[2] = {(uint8_t)r, (uint8_t)(r >> 8)};
		v1_host_send(1, 'x', 1, v);
	}
	drain_output();
	v1_host_send(0, 0x11, 0, NULL);
	count32_dec(0xA43E);
	return 1;
}

/* One pass of synthesis_main's loop (F7204) after the stop check: the playback stage, then the first of the
 * generator, prosody, lexical, text-rules and input stages that has work and enough free nodes. Returns 0 where the
 * firmware calls loop_idle (F73F0) instead: see v1_idle_has_work. */
int v1_loop_pass(void)
{
	count32(0xA416);
	tx_kick_switch();
	if (rw(V1_PLAYBACK_STATE) != 2)
		ww(V1_PLAYBACK_STATE, v1_stage_playback_run());
	if (rw(V1_TRACKS_FULL) && v1_pg_ring(1, rw(0x96F8)))
		ww(V1_TRACKS_FULL, 0);
	if (!rw(V1_TRACKS_FULL) && rw(V1_RUN_PARAMGEN)) {
		int r = v1_stage_paramgen_run();
		if (r == 0)
			ww(V1_RUN_PARAMGEN, 0);
		else if (r == 1)
			ww(V1_TRACKS_FULL, 1);
		ww(V1_PLAYBACK_STATE, v1_stage_playback_run());
		return 1;
	}
	int (*stage)(void);
	unsigned run, next;
	int min_free;
	if (rw(V1_RUN_PROSODY))
		stage = v1_stage_prosody_run, run = V1_RUN_PROSODY, next = V1_RUN_PARAMGEN, min_free = 0x14;
	else if (rw(V1_RUN_LEXICAL))
		stage = v1_stage_lexical_run, run = V1_RUN_LEXICAL, next = V1_RUN_PROSODY, min_free = 0x25;
	else if (rw(V1_RUN_TEXTRULES))
		stage = v1_stage_text_rules_run, run = V1_RUN_TEXTRULES, next = V1_RUN_LEXICAL, min_free = 0x25;
	else if (!rw(V1_INPUT_EMPTY))
		stage = v1_stage_input_run, run = V1_INPUT_EMPTY, next = V1_RUN_TEXTRULES, min_free = 0x0F;
	else
		return 0;
	if (rw(V1_FREE_COUNT) <= min_free)
		return 0;
	if (stage())
		ww(next, 1);
	else
		ww(run, stage == v1_stage_input_run); /* the input stage sets V1_INPUT_EMPTY, the others clear their flag */
	return 1;
}

/* One spin of loop_idle (F73F0): returns 1 when the loop has work again, 0 while it must wait for an interrupt.
 * It wakes the input stage when the input ring has bytes (or on the DIP S4-7 self-test), lets the generator go on
 * when the tracks have room, and restarts it when the frame builder nears the written frames. */
int v1_idle_has_work(void)
{
	if (!rw(V1_INPUT_EMPTY) && rw(V1_FREE_COUNT) > 0x0F)
		return 1;
	if (!rw(V1_TRACKS_FULL) && rw(V1_RUN_PARAMGEN))
		return 1;
	count32(0xA41A);
	if (rw(V1_STOP_REQUEST))
		return 1;
	if (rw(V1_PLAYBACK_STATE) == 0 && rw(0x96F6))
		return 1;
	int room = rw(V1_OUTPUT_READ) - rw(V1_OUTPUT_WRITE) - 1;
	if (room < 0)
		room += 0x80;
	if (rw(V1_PLAYBACK_STATE) == 1 && room > 6)
		return 1;
	if (v1_dip_switch_test(0x40) || rw(V1_INPUT_READ) != rw(V1_INPUT_WRITE))
		ww(V1_INPUT_EMPTY, 0);
	if (rw(V1_TRACKS_FULL) && s16(rw(0x96E4) + rw(0x96F8) + rw(0xA220) - rw(V1_TRACK_READ)) < 0x80)
		ww(V1_TRACKS_FULL, 0);
	if (rw(0x96E0) == -1 && s16(rw(0x96E2) - rw(0xA21E) - rw(V1_TRACK_READ)) <= 4) {
		ww(V1_RUN_PARAMGEN, 1);
		return 1;
	}
	tx_kick_switch();
	return 0;
}

/* v1.1 host link: the 256-byte input ring (host text for the input stage), the 128-byte output ring (replies to the
 * host), host_send and the transmitter. The same design as v3.4.1's (REFERENCE §15.5), with event counters. */
#include "v1.h"

void (*v1_tx_hook)(int c);

/* F5513: free bytes in the input ring (one is always left empty) */
int v1_input_ring_free(void)
{
	int n = s16(rw(V1_INPUT_READ) - rw(V1_INPUT_WRITE) - 1);
	if (n < 0)
		n = s16(rw(V1_INPUT_READ) - rw(V1_INPUT_WRITE) + 0xFF);
	return n;
}

/* F553E: the next input byte, or -1. After an XOFF, XON goes out once 110 bytes are free. With DIP S4-7 on it reads
 * the built-in self-test text (DS:291B) in a loop instead. */
int v1_input_getc(void)
{
	int c;
	v1_irq_disable_nested();
	count32(0xA496);
	if (rw(V1_INPUT_XOFF) && v1_input_ring_free() > 0x6D)
		v1_host_send(0, 0x11, 0, NULL);
	if (!v1_dip_switch_test(0x40)) {
		if (rw(V1_INPUT_READ) == rw(V1_INPUT_WRITE)) {
			c = -1;
		} else {
			c = rsb(V1_INPUT_RING + ruw(V1_INPUT_READ));
			ww(V1_INPUT_READ, rw(V1_INPUT_READ) + 1);
			if (rw(V1_INPUT_READ) > 0xFF)
				ww(V1_INPUT_READ, 0);
		}
	} else {
		if (rb(ruw(V1_SELFTEST_PTR)) == 0)
			ww(V1_SELFTEST_PTR, rw(0x291B));
		c = rsb(ruw(V1_SELFTEST_PTR));
		ww(V1_SELFTEST_PTR, rw(V1_SELFTEST_PTR) + 1);
	}
	v1_irq_enable_nested();
	return c;
}

/* F55DF: one byte into the input ring; XOFF goes out when fewer than 80 bytes are free. Returns 0 if the ring is
 * full (counted in A412). */
int v1_input_put(int c)
{
	count32(0xA49A);
	int n = v1_input_ring_free();
	if (n < 0x50 && !rw(V1_INPUT_XOFF))
		v1_host_send(0, 0x13, 0, NULL);
	if (n < 1) {
		count32(0xA412);
		return 0;
	}
	wb(V1_INPUT_RING + ruw(V1_INPUT_WRITE), c);
	ww(V1_INPUT_WRITE, rw(V1_INPUT_WRITE) + 1);
	if (rw(V1_INPUT_WRITE) >= 0x100)
		ww(V1_INPUT_WRITE, 0);
	return 1;
}

/* F5655: free bytes in the output ring */
int v1_output_ring_free(void)
{
	int n = s16(rw(V1_OUTPUT_READ) - rw(V1_OUTPUT_WRITE) - 1);
	if (n < 0)
		n = s16(rw(V1_OUTPUT_READ) - rw(V1_OUTPUT_WRITE) + 0x7F);
	return n;
}

static void output_put(int c)
{
	int w = rw(V1_OUTPUT_WRITE);
	wb(V1_OUTPUT_RING + (unsigned)w, c);
	if (++w > 0x7F)
		w = 0;
	ww(V1_OUTPUT_WRITE, w);
}

/* start the transmitter unless it runs, the host sent XOFF, or (DIP S4-5) the host holds DSR low */
static void tx_kick(void)
{
	ww(V1_TX_PENDING, 1);
	if (!rw(V1_TX_RUNNING) && !rw(V1_TX_STOPPED) && (!v1_dip_switch_test(0x10) || v1_uart_status_test(0x80))) {
		v1_uart_tx_start();
		ww(V1_TX_RUNNING, 1);
	}
}

/* F5765: kind 0 sends one character, kind 1 the reply ESC [ p0 ; p1 ... letter with count byte parameters (at most
 * 2). XON / XOFF are queued for the transmitter with DIP S4-2 (a queued pair cancels out) and set the 8251's RTS; BEL
 * goes out only with N-flag 10. Returns 0 if the output ring is full. */
int v1_host_send(int kind, int ch, int count, const uint8_t *params)
{
	int ok = 1;
	ch = (int8_t)ch;
	v1_irq_disable_nested();
	count32(0xA4A2);
	if (kind == 0) {
		if (ch == 0x11 || ch == 0x13) {
			if (v1_dip_switch_test(2))
				wb(V1_FLOW_PENDING, rb(V1_FLOW_PENDING) | (ch == 0x11 ? 1 : 2));
			ww(V1_INPUT_XOFF, ch == 0x13);
			if (!(rb(V1_FLOW_PENDING) & 1) || !(rb(V1_FLOW_PENDING) & 2)) {
				ww(V1_TX_PENDING, 1);
				if (!rw(V1_TX_RUNNING)) {
					v1_uart_tx_start();
					ww(V1_TX_RUNNING, 1);
				}
			} else {
				wb(V1_FLOW_PENDING, rb(V1_FLOW_PENDING) & 0xFC);
			}
			if (ch == 0x11)
				v1_uart_command_set(0x20);
			else
				v1_uart_command_clear(0x20);
		} else if (ch == 7) {
			if (rw(V1_HOST_MODE) & 0x200) {
				wb(V1_BEL_COUNT, rb(V1_BEL_COUNT) + 1);
				ww(V1_TX_PENDING, 1);
				if (!rw(V1_TX_RUNNING)) {
					v1_uart_tx_start();
					ww(V1_TX_RUNNING, 1);
				}
			}
		} else if (v1_output_ring_free() == 0) {
			ok = 0;
		} else {
			output_put(ch);
			tx_kick();
		}
	} else if (v1_output_ring_free() < 0x0D) {
		ok = 0;
	} else {
		char buf[14];
		int n = 2;
		buf[0] = 0x1B;
		buf[1] = '[';
		if (count > 2)
			v1_fatal_error(0x1D);
		for (int i = 0; i < count; i++) {
			if (i != 0)
				buf[n++] = ';';
			unsigned v = params[i];
			int div = v < 100 ? (v < 10 ? -1 : 10) : 100;
			for (; div > 0; div -= 0x5A) { /* 100, then 10 */
				char d = '/';
				int r = (int)v;
				do {
					d++;
					r -= div;
				} while (r >= 0);
				v = (unsigned)(r + div);
				buf[n++] = d;
			}
			buf[n++] = (char)(v + '0');
		}
		buf[n] = (char)ch;
		for (int i = 0; i < n + 1; i++)
			output_put(buf[i]);
		tx_kick();
	}
	v1_irq_enable_nested();
	return ok;
}

/* F5680: the next byte to send: a queued XON, then XOFF, a BEL, then the output ring unless the host sent XOFF (with
 * DIP S4-5, also while DSR is low). -1 when there is nothing, and the transmitter stops. */
int v1_tx_next_byte(void)
{
	int c = -1;
	v1_irq_disable_nested();
	count32(0xA49E);
	if (rb(V1_FLOW_PENDING)) {
		if (rb(V1_FLOW_PENDING) & 1) {
			c = 0x11;
			wb(V1_FLOW_PENDING, rb(V1_FLOW_PENDING) & 0xFE);
		} else if (rb(V1_FLOW_PENDING) & 2) {
			c = 0x13;
			wb(V1_FLOW_PENDING, rb(V1_FLOW_PENDING) & 0xFD);
		} else {
			wb(V1_FLOW_PENDING, 0);
		}
	}
	if (c == -1 && rb(V1_BEL_COUNT)) {
		c = 7;
		wb(V1_BEL_COUNT, rb(V1_BEL_COUNT) - 1);
	}
	if (c == -1) {
		if (rw(V1_OUTPUT_READ) == rw(V1_OUTPUT_WRITE)) {
			ww(V1_TX_PENDING, 0);
		} else if (!rw(V1_TX_STOPPED) && (!v1_dip_switch_test(0x10) || v1_uart_status_test(0x80))) {
			c = rsb(V1_OUTPUT_RING + ruw(V1_OUTPUT_READ));
			ww(V1_OUTPUT_READ, rw(V1_OUTPUT_READ) + 1);
			if (rw(V1_OUTPUT_READ) > 0x7F)
				ww(V1_OUTPUT_READ, 0);
		}
	}
	if (c == -1) {
		v1_uart_tx_stop();
		ww(V1_TX_RUNNING, 0);
	}
	v1_irq_enable_nested();
	return (int8_t)c;
}

/* EE3A3, from the transmit interrupt: send the next byte (EC373 waits for TxRDY and writes it) and count it by kind
 * (the table at DS:17E1). Sending XOFF also releases the output hold. Returns the byte, or -1. */
int v1_tx_send(void)
{
	ww(V1_IRQ_NEST, rw(V1_IRQ_NEST) + 1);
	v1_latch_clear(0x80);
	int c = (int8_t)v1_tx_next_byte();
	if (c < 0) {
		v1_latch_set(0x80);
		ww(V1_IRQ_NEST, rw(V1_IRQ_NEST) - 1);
		return -1;
	}
	switch (c) {
	case 0x07: count32(0xA40E); break;
	case 0x11: count32(0xA43E); break;
	case 0x13:
		count32(0xA43A);
		ww(V1_OUTPUT_HOLD, 0);
		break;
	case 0x1B: count32(0xA436); break;
	case 'R': count32(0xA406); break;
	case 'S': count32(0xA44A); break;
	case 'i': count32(0xA442); break;
	case 'x': count32(0xA446); break;
	default: break;
	}
	v1_uart_command_set(1); /* EC373 */
	if (v1_tx_hook)
		v1_tx_hook(c);
	count32(0xA432);
	v1_latch_set(0x80);
	ww(V1_IRQ_NEST, rw(V1_IRQ_NEST) - 1);
	return c;
}

/* F5346: empty the receive, input and output rings and the transmitter state. XOFF counts as sent, so the input
 * stage's first read sends XON. */
void v1_host_rings_reset(void)
{
	ww(V1_RX_WRITE, 0);
	ww(V1_RX_READ, 0);
	ww(V1_INPUT_WRITE, 0);
	ww(V1_INPUT_READ, 0);
	ww(V1_INPUT_AHEAD, -1);
	ww(V1_INPUT_XOFF, 1);
	ww(V1_OUTPUT_WRITE, 0);
	ww(V1_OUTPUT_READ, 0);
	wb(V1_FLOW_PENDING, 0);
	ww(V1_TX_PENDING, 0);
	ww(V1_TX_STOPPED, 0);
	ww(V1_TX_RUNNING, 0);
	wb(V1_BEL_COUNT, 0);
	ww(V1_SELFTEST_PTR, rw(0x291B));
}

/* EC341: the transmit interrupt (IR2/IR3): latch bit 2 up while it runs, and tx_send if the 8251 has TxRDY */
void v1_uart_tx_isr(void)
{
	v1_latch_set(4);
	if (v1_uart_status & 1)
		v1_tx_send();
	v1_latch_clear(4);
}

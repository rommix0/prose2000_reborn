/* The host link's send side: replies and index markers into the output ring, and XON/XOFF flow control. */
#include "input.h"

unsigned host_uart_status = 0x85;
void (*host_tx_hook)(int c);

/* D63C6 / D63AF: set / clear 8251 command bits (copy at UART_CMD) */
void uart_command_set(int bits)
{
	wb(UART_CMD, rb(UART_CMD) | bits);
	wb(ruw(UART_CMD_PTR), rb(UART_CMD));
}

void uart_command_clear(int bits)
{
	wb(UART_CMD, rb(UART_CMD) & ~bits);
	wb(ruw(UART_CMD_PTR), rb(UART_CMD));
}

/* D63DB */
int uart_status(void)
{
	return host_uart_status & 0xFF;
}

/* D62FE: unmask the transmit interrupt and enable the transmitter */
void uart_tx_start(void)
{
	wb(PIC_MASK, rb(PIC_MASK) & 0xF3);
	wb(ruw(PIC_MASK_PTR), rb(PIC_MASK));
	uart_command_set(1);
}

/* DE398: free bytes in the output ring */
int output_ring_free(void)
{
	int n = s16(rw(OUTPUT_READ) - rw(OUTPUT_WRITE) - 1);
	if (n < 0)
		n += 0x100;
	return n;
}

static void output_put(int c)
{
	int w = ruw(OUTPUT_WRITE);
	wb(OUTPUT_RING + w, c);
	ww(OUTPUT_WRITE, w + 1 >= 0x100 ? 0 : w + 1);
}

/* start the transmitter if it is idle */
static void tx_kick(void)
{
	ww(TX_PENDING, 1);
	if (rw(TX_RUNNING) == 0) {
		ww(TX_RUNNING, 1);
		uart_tx_start();
		if (dip_switches() & 0x20)
			uart_command_set(0x20);
	}
}

/* the same after data: not while the host holds DSR low (with DIP 5 and 6), nor while output is stopped */
static void tx_kick_data(void)
{
	ww(TX_PENDING, 1);
	if (rw(TX_RUNNING) != 0 || rw(TX_STOPPED) != 0)
		return;
	if ((dip_switches() & 0x10) && (dip_switches() & 0x20) && !(uart_status() & 0x80))
		return;
	ww(TX_RUNNING, 1);
	uart_tx_start();
	if (dip_switches() & 0x20)
		uart_command_set(0x20);
}

/* DE4C6: kind 0 sends the character ch (XON / XOFF also change the flow control, BEL only with N-flag 10); kind 1
 * sends the report ESC [ p1 ; p2 ... ch with count parameters (bytes, 0-255), and a CR with N-flag 15. Returns 0 when
 * the output ring has no room. */
int host_send(int kind, int ch, int count, const int *params)
{
	int ok = 1;
	irq_disable_nested();
	ch = (int8_t)ch;
	if (kind == 0) {
		if (ch == 0x11 ||
		    ch == 0x13) { /* XON / XOFF: queued for the transmitter with DIP 2; a queued pair cancels out */
			if (dip_switches() & 2)
				wb(FLOW_PENDING, rb(FLOW_PENDING) | (ch == 0x11 ? 1 : 2));
			ww(INPUT_XOFF, ch == 0x13);
			if ((rb(FLOW_PENDING) & 1) && (rb(FLOW_PENDING) & 2))
				wb(FLOW_PENDING, rb(FLOW_PENDING) & 0xFC);
			else
				tx_kick();
			if (ch == 0x11)
				uart_command_set(2); /* RTS on */
			else if ((dip_switches() & 0x10) || (dip_switches() & 0x20))
				uart_command_clear(2);
		} else if (ch == 7) {
			if (rw(HOST_MODE) & 0x200) {
				wb(BEL_COUNT, rb(BEL_COUNT) + 1);
				tx_kick();
			}
		} else if (output_ring_free() == 0) {
			ok = 0;
		} else {
			output_put(ch);
			tx_kick_data();
		}
	} else if (kind == 1) {
		char buf[0x46];
		int len = 2;
		if (count > 16)
			fatal_error(0x1D);
		if (output_ring_free() < count * 4 + 3) {
			ok = 0;
		} else {
			buf[0] = 0x1B;
			buf[1] = '[';
			for (int i = 0; i < count; i++) {
				int v = params[i] & 0xFF, d = v > 99 ? 100 : v > 9 ? 10 : -1;
				if (i)
					buf[len++] = ';';
				for (; d > 0; d -= 90) {
					int q = '0' - 1;
					do {
						q++;
						v -= d;
					} while (v >= 0);
					v += d;
					buf[len++] = (char)q;
				}
				buf[len++] = (char)(v + '0');
			}
			buf[len++] = (char)ch;
			for (int i = 0; i < len; i++)
				output_put(buf[i]);
			if (rw(HOST_MODE) & 0x4000)
				output_put('\r');
			tx_kick_data();
		}
	}
	irq_enable_nested();
	return ok;
}

/* D6387: transmitter off: 8251 TxEN cleared, transmit interrupts masked */
static void tx_stop(void)
{
	uart_command_clear(1);
	wb(PIC_MASK, rb(PIC_MASK) | 0x0C);
	wb(ruw(PIC_MASK_PTR), rb(PIC_MASK));
}

/* DE3AE: the next byte to send: a CR after a control character (N-flag 15), a queued XON or XOFF, a BEL, then the
 * output ring (unless the host stopped output). -1 when there is nothing: the transmitter is switched off. */
int tx_next_byte(void)
{
	int c = -1;
	irq_disable_nested();
	if ((dip_switches() & 0x10) && (dip_switches() & 0x20) && !(uart_status() & 0x80)) {
		tx_stop(); /* DIP 5 and 6: the host holds DSR low */
		ww(TX_RUNNING, 0);
		uart_command_clear(0x20);
		irq_enable_nested();
		return -1;
	}
	if (rw(CR_PENDING) == 1) {
		ww(CR_PENDING, 0);
		c = '\r';
	}
	if (rb(FLOW_PENDING) != 0) {
		if (rb(FLOW_PENDING) & 1) {
			c = 0x11;
			if (rw(HOST_MODE) & 0x4000)
				ww(CR_PENDING, 1);
			wb(FLOW_PENDING, rb(FLOW_PENDING) & 0xFE);
		} else if (rb(FLOW_PENDING) & 2) {
			c = 0x13;
			if (rw(HOST_MODE) & 0x4000)
				ww(CR_PENDING, 1);
			wb(FLOW_PENDING, rb(FLOW_PENDING) & 0xFD);
		} else {
			wb(FLOW_PENDING, 0);
		}
	}
	if (c == -1 && rb(BEL_COUNT) != 0) {
		c = 7;
		wb(BEL_COUNT, rb(BEL_COUNT) - 1);
		if (rw(HOST_MODE) & 0x4000)
			ww(CR_PENDING, 1);
	}
	if (c == -1) {
		if (rw(OUTPUT_READ) == rw(OUTPUT_WRITE)) {
			ww(TX_PENDING, 0);
		} else if (rw(TX_STOPPED) == 0) {
			c = rsb(OUTPUT_RING +
			        ruw(OUTPUT_READ)); /* a byte >= 0x80 reads as negative and ends the sending */
			ww(OUTPUT_READ, rw(OUTPUT_READ) + 1);
			if (rw(OUTPUT_READ) >= 0x100)
				ww(OUTPUT_READ, 0);
		}
	}
	if (c == -1) {
		tx_stop();
		ww(TX_RUNNING, 0);
		if (dip_switches() & 0x20)
			uart_command_clear(0x20);
	}
	irq_enable_nested();
	return c;
}

/* D734B (from the transmit interrupt D631B when the 8251 has room): send the next byte. Sending XOFF also releases
 * an output hold. Returns the byte, or -1. */
int tx_send(void)
{
	int c;
	ww(IRQ_NEST, rw(IRQ_NEST) + 1);
	latch_clear(0x80);
	c = tx_next_byte();
	if (c >= 0) {
		if (c == 0x13)
			ww(OUTPUT_HOLD, 0);
		wb(ruw(UART_DATA_PTR), c); /* D6379 */
		if (host_tx_hook)
			host_tx_hook(c);
	}
	latch_set(0x80);
	ww(IRQ_NEST, rw(IRQ_NEST) - 1);
	return c;
}

/* The firmware's "wait until the output ring is empty" (E38D1, E39C6, E3A06): the transmit interrupt sends meanwhile.
 * If the host has stopped output the firmware waits for its XON; the C gives up instead. */
void output_wait(void)
{
	while (rw(OUTPUT_READ) != rw(OUTPUT_WRITE))
		if (tx_send() < 0 && (rw(TX_RUNNING) == 0 || rw(TX_STOPPED) != 0))
			break;
}

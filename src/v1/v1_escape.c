/* v1.1 receive side: host_rx_char (the serial interrupt's handler) and the escape parser.
 *
 * The design is v3.4.1's (REFERENCE §8.1): a 16-byte receive ring drained by host_escape_parser. Plain text goes on
 * into the input ring; ESC [ p0 ; p1 ... letter collects up to 16 values and then acts at once or goes in-band into
 * the input ring as ESC, letter, n, values. v1.1 knows 18 commands (REFERENCE §16). */
#include "v1.h"

void (*v1_restart_hook)(int code);
static int restarted;

/* EC3AC: restart the synthesis loop (loop_restart EC021). It does not return on the board. */
static void restart(int code)
{
	if (v1_restart_hook) {
		v1_restart_hook(code);
		restarted = 1;
	}
}

static int param(int i) { return rw(V1_ESC_PARAMS + 2 * (unsigned)i); }
static void set_param(int i, int v) { ww(V1_ESC_PARAMS + 2 * (unsigned)i, v); }
static int clamp(int v, int dflt, int lo, int hi) { return v < 0 ? dflt : v < lo ? lo : v > hi ? hi : v; }

/* the command letter ch with n values: returns the number of values to pass in-band, -1 (BEL: not understood) or -2
 * (done, nothing to pass) */
static int command(int *ch, int n)
{
	int res = -1;
	switch (*ch) {
	case 'T': /* restart with code p0 (1 without a value) */
		count32(0xA466);
		if (n <= 1) {
			if (param(0) < 0)
				restart(1);
			else if (param(0) < 7)
				restart(param(0));
		}
		break;
	case 'D':
		count32(0xA462);
		if (n == 0)
			res = -2; /* calls EC10E, an empty routine */
		break;
	case 'i': /* index marker, 1-255 (1 without a value) */
		count32(0xA456);
		if (n <= 1) {
			if (param(0) < 0) {
				set_param(0, 1);
				res = 1;
			} else if (param(0) != 0) {
				res = 1;
			}
		}
		break;
	case 'S': /* stop */
		count32(0xA45E);
		if (n == 0) {
			res = -2;
			ww(V1_STOP_REQUEST, 1);
		}
		break;
	case 'x': /* end of sentence */
		count32(0xA45A);
		if (n == 0) {
			res = 0;
			ww(V1_END_REQUEST, 1);
			ww(V1_OUTPUT_HOLD, 0);
		}
		break;
	case 'C': /* release the output hold */
		count32(0xA46A);
		if (n == 0) {
			res = 0;
			ww(V1_OUTPUT_HOLD, 0);
		}
		break;
	case 'H': /* hold the output at once */
		count32(0xA46E);
		if (n == 0) {
			res = -2;
			ww(V1_OUTPUT_HOLD, 1);
		}
		break;
	case 'I': /* phoneme input, 0-1 (0 without a value) */
	case 'P': /* word mode, 0-1 (1 without a value) */
		count32(*ch == 'I' ? 0xA476 : 0xA47A);
		if (n <= 1) {
			unsigned at = *ch == 'I' ? V1_SET_INPUT : V1_SET_WORD;
			if (param(0) < 0) {
				set_param(0, *ch == 'I' ? 0 : 1);
				ww(at, param(0));
				res = 1;
			} else if (param(0) <= 1) {
				ww(at, param(0));
				res = 1;
			}
		}
		break;
	case 'a': /* amplitude, 0-15 */
		count32(0xA47E);
		if (n <= 1) {
			set_param(0, clamp(param(0), 0, 0, 15));
			ww(V1_SET_AMPLITUDE, param(0));
			res = 1;
		}
		break;
	case 'r': /* rate, 50-250 (160) */
		count32(0xA486);
		if (n <= 1) {
			set_param(0, clamp(param(0), 0xA0, 0x32, 0xFA));
			ww(V1_SET_RATE, param(0));
			res = 1;
		}
		break;
	case 'p': /* pitch, 0 or 50-200 (75) */
		count32(0xA482);
		if (n <= 1) {
			int v = param(0);
			if (v < 0)
				v = 0x4B;
			else if (v < 0x32 && v != 0)
				v = 0x32;
			else if (v > 0xC8)
				v = 0xC8;
			set_param(0, v);
			ww(V1_SET_PITCH, v);
			res = 1;
		}
		break;
	case 's': /* 1-255 (10), in-band only */
	case 'g':
		count32(*ch == 's' ? 0xA48A : 0xA492);
		if (n <= 1) {
			set_param(0, clamp(param(0), 10, 1, 0xFF));
			res = 1;
		}
		break;
	case 't': /* three values: 0-9, 1-255 (10), 30-255 (50) */
		count32(0xA48E);
		if (n <= 3) {
			res = 3;
			if (param(0) < 0)
				set_param(0, 0);
			else if (param(0) >= 10)
				res = -1;
			set_param(1, clamp(param(1), 10, 1, 0xFF));
			set_param(2, clamp(param(2), 0x32, 0x1E, 0xFF));
		}
		break;
	case 'l': /* entry p0 (below 18) of the table at A57C: p1, at most its maximum (DS:23B7), or its default (DS:24A1) */
		if (param(0) != -1 && n <= 2 && param(0) < 0x12) {
			if (param(1) < 0)
				set_param(1, rb(0x24A1 + (unsigned)param(0)));
			else if (rw(0x23B7 + 2 * (unsigned)param(0)) < param(1))
				set_param(1, rw(0x23B7 + 2 * (unsigned)param(0)));
			wb(V1_L_TABLE + (unsigned)param(0), param(1));
			res = 2;
		}
		break;
	case 'F': /* clear / set N flags 1-14 (bit masks from DS:48A4); passes the new flags as two bytes */
	case 'N': {
		count32(0xA472);
		int mask = 0;
		for (int i = 0; i < n; i++)
			if (param(i) >= 1 && param(i) <= 14)
				mask |= rw(0x48A4 + 2 * (unsigned)param(i));
		if (*ch == 'N')
			ww(V1_HOST_MODE, rw(V1_HOST_MODE) | mask);
		else
			ww(V1_HOST_MODE, rw(V1_HOST_MODE) & ~mask);
		set_param(0, rw(V1_HOST_MODE) & 0xFF);
		set_param(1, (rw(V1_HOST_MODE) >> 8) & 0xFF);
		res = 2;
		*ch = 'N';
		break;
	}
	default:
		break;
	}
	return res;
}

/* EDB32: one byte from the receive ring. State 1 is text, 2 after ESC, 3 in the parameters. */
void v1_host_escape_parser(void)
{
	int ch = rsb(V1_RX_RING + ruw(V1_RX_READ));
	ww(V1_RX_READ, (rw(V1_RX_READ) + 1) & 0xF);
	if (ch == 0x1B) {
		ww(V1_ESC_STATE, 2);
		return;
	}
	switch (rw(V1_ESC_STATE)) {
	case 1:
		v1_input_put(ch);
		return;
	case 2:
		if (ch == '[') {
			ww(V1_ESC_STATE, 3);
			ww(V1_ESC_OVERFLOW, 0);
			wb(V1_ESC_INDEX, 0x10);
			while (rsb(V1_ESC_INDEX) > 0) {
				wb(V1_ESC_INDEX, rb(V1_ESC_INDEX) - 1);
				set_param(rsb(V1_ESC_INDEX), -1);
			}
		} else {
			v1_host_send(0, 7, 0, NULL);
			ww(V1_ESC_STATE, 1);
		}
		return;
	case 3:
		break;
	default:
		return;
	}
	int idx = rsb(V1_ESC_INDEX);
	if (ch >= '0' && ch <= '9') {
		if (param(idx) == -1)
			set_param(idx, 0);
		if (idx < 0x10) {
			set_param(idx, s16(10 * param(idx) + ch - '0'));
			if (param(idx) > 0xFF)
				ww(V1_ESC_OVERFLOW, 1);
		}
		return;
	}
	if (ch == ';') {
		wb(V1_ESC_INDEX, rb(V1_ESC_INDEX) + 1);
		if (rsb(V1_ESC_INDEX) < 0x10)
			set_param(rsb(V1_ESC_INDEX), 0);
		return;
	}
	int n = idx;
	if (n != 0 || param(0) != -1)
		n++;
	if (rw(V1_ESC_OVERFLOW) || n > 0x10)
		ch = 0;
	restarted = 0;
	int res = command(&ch, n);
	if (restarted)
		return;
	if (res == -1) {
		v1_host_send(0, 7, 0, NULL);
	} else if (res != -2) {
		if (v1_input_ring_free() >= res + 3) {
			v1_input_put(0x1B);
			v1_input_put(ch);
			v1_input_put(res);
			for (int i = 0; i < res; i++)
				v1_input_put(param(i));
		} else {
			count32(0xA412);
		}
	}
	ww(V1_ESC_STATE, 1);
}

/* EE1BB: one received byte with the 8251's error bits (8 parity, 0x10 overrun, 0x20 framing, 0x40 break: the byte
 * becomes 0). CR, LF and TAB become spaces, ^R restarts, XOFF / XON stop and start the transmitter, other control
 * characters get BEL. The rest goes into the receive ring, which the parser drains unless it is already running. */
void v1_host_rx_char(int ch, int status)
{
	ww(V1_IRQ_NEST, rw(V1_IRQ_NEST) + 1);
	v1_latch_clear(1);
	count32(0xA42E);
	if (status) {
		if (status & 8)
			count32(0xA3F2);
		if (status & 0x10)
			count32(0xA3F6);
		if (status & 0x20)
			count32(0xA3FA);
		if (status & 0x40) {
			count32(0xA3FE);
			ch = 0;
		}
	}
	ch &= 0x7F;
	if (ch == '\r' || ch == '\n' || ch == '\t')
		ch = ' ';
	if (ch == 0x12) {
		count32(0xA40A);
		restart(0);
	} else if (ch != 0 && ch != 0x7F) {
		if (ch == 0x13) {
			count32(0xA44E);
			ww(V1_TX_STOPPED, 1);
		} else if (ch == 0x11) {
			count32(0xA452);
			ww(V1_TX_STOPPED, 0);
			if (!rw(V1_TX_RUNNING) && rw(V1_TX_PENDING) && v1_dip_switch_test(0x10) && v1_uart_status_test(0x80)) {
				v1_uart_tx_start();
				ww(V1_TX_RUNNING, 1);
			}
		} else if (ch < 0x20 && ch != 0x1B) {
			v1_host_send(0, 7, 0, NULL);
		} else {
			int n = s16(rw(V1_RX_READ) - rw(V1_RX_WRITE) - 1);
			if (n < 0)
				n += 0x10;
			if (n > 0) {
				wb(V1_RX_RING + ruw(V1_RX_WRITE), ch);
				ww(V1_RX_WRITE, (rw(V1_RX_WRITE) + 1) & 0xF);
				if (!rw(V1_RX_BUSY)) {
					ww(V1_RX_BUSY, 1);
					v1_latch_set(1);
					while (rw(V1_RX_WRITE) != rw(V1_RX_READ)) {
						ww(V1_IRQ_NEST, rw(V1_IRQ_NEST) - 1);
						v1_irq_enable();
						v1_host_escape_parser();
						v1_irq_disable();
						ww(V1_IRQ_NEST, rw(V1_IRQ_NEST) + 1);
					}
					ww(V1_RX_BUSY, 0);
				}
			} else {
				count32(0xA402);
			}
		}
	}
	ww(V1_IRQ_NEST, rw(V1_IRQ_NEST) - 1);
	v1_latch_set(1);
}

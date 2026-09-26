/* The serial receive side: control characters, the 16-byte receive ring, and the ESC [ command parser. Plain text
 * and in-band commands go on into the input stage's ring (INPUT_RING); the other commands act at once. */
#include "input.h"

void (*input_restart_hook)(int code);
void (*input_int8_hook)(int n);
static int restarted; /* a hook restarted the firmware: the parser must not touch RAM any more */

/* D31B5 (^R, code 0x12) / D31DA (ESC[W, code 0x77): reset the stack and start the synthesis loop again. Returns 1
 * when a hook did that: the caller must then stop, as the firmware does not come back. */
static int restart(int code)
{
	if (!input_restart_hook)
		return 0;
	input_restart_hook(code);
	restarted = 1;
	return 1;
}

static uint32_t rw32(int a)
{
	return ruw(a) | (uint32_t)ruw(a + 2) << 16;
}

static void ww32(int a, uint32_t v)
{
	ww(a, v & 0xFFFF);
	ww(a + 2, v >> 16);
}

static int param(int i)
{
	return rw(ESC_PARAMS + 2 * i);
}

static void set_param(int i, int v)
{
	ww(ESC_PARAMS + 2 * i, v);
}

static void bel(void)
{
	host_send(0, 7, 0, NULL);
}

/* DE345: one byte into the input ring; sends XOFF when fewer than 0x50 bytes are free. Returns 0 when full. */
int input_put(int c)
{
	int n = input_ring_free();
	if (n < 0x50 && rw(INPUT_XOFF) == 0)
		host_send(0, 0x13, 0, NULL);
	if (n < 1)
		return 0;
	wb(INPUT_RING + ruw(INPUT_WRITE), c);
	ww(INPUT_WRITE, rw(INPUT_WRITE) + 1);
	if (rw(INPUT_WRITE) >= 0x100)
		ww(INPUT_WRITE, 0);
	return 1;
}

/* D6D8C: answer ESC[nQ with reports. 0 = all settings, then ESC[0Q; 255 = ESC[w and only the settings that differ
 * from the defaults, then ESC[255Q; 1-12 = one setting. */
void host_status_report(int n)
{
	int all = 0, changed = 0, list[16], k;
	if (n == 0) {
		all = 1;
	} else if (n == 255) {
		all = 1;
		changed = 1;
		host_send(1, 'w', 0, NULL);
	}
	switch (n) {
	case 0:
	case 1:
	case 255:
		k = 0;
		for (int i = 0; i < 16; i++)
			if ((rw(HOST_MODE) & rw(BITMASK + 2 * i)) && (!changed || !(rw(BITMASK + 2 * i) & 0x17C0)))
				list[k++] = i + 1;
		if (!changed || k > 0)
			host_send(1, 'N', k, list);
		if (!all)
			break;
		/* fall through */
	case 2:
		k = 0;
		for (int i = 0; i < 16; i++)
			if (!(rw(HOST_MODE) & rw(BITMASK + 2 * i)) && (!changed || (rw(BITMASK + 2 * i) & 0x17C0)))
				list[k++] = i + 1;
		if (!changed || k > 0)
			host_send(1, 'F', k, list);
		if (!all)
			break;
		/* fall through */
	case 3:
		k = 0;
		for (int i = 0; i < 7; i++)
			if ((rw(HOST_AFLAGS) & rw(BITMASK + 2 * i)) && (!changed || !(rw(BITMASK + 2 * i) & 0x41)))
				list[k++] = i + 1;
		if (!changed || k > 0)
			host_send(1, 'A', k, list);
		if (!all)
			break;
		/* fall through */
	case 4:
		k = 0;
		for (int i = 0; i < 7; i++)
			if (!(rw(HOST_AFLAGS) & rw(BITMASK + 2 * i)) && (!changed || (rw(BITMASK + 2 * i) & 0x41)))
				list[k++] = i + 1;
		if (!changed || k > 0)
			host_send(1, 'D', k, list);
		if (!all)
			break;
		/* fall through */
	case 5:
		list[0] = rb(SET_INPUT);
		if (!changed || rw(SET_INPUT) != 0)
			host_send(1, 'I', 1, list);
		if (!all)
			break;
		/* fall through */
	case 6:
		list[0] = rb(SET_AMPLITUDE);
		if (!changed || rw(SET_AMPLITUDE) != 0)
			host_send(1, 'a', 1, list);
		if (!all)
			break;
		/* fall through */
	case 7:
		list[0] = rb(SET_PITCH);
		if (!changed || rw(SET_PITCH) != 85)
			host_send(1, 'p', 1, list);
		if (!all)
			break;
		/* fall through */
	case 8:
		list[0] = rb(SET_FAST);
		if (!changed || rw(SET_FAST) != 0)
			host_send(1, 'f', 1, list);
		if (!all)
			break;
		/* fall through */
	case 9:
		list[0] = rb(SET_SPEED);
		if (!changed || rw(SET_SPEED) != 13)
			host_send(1, 'v', 1, list);
		if (!all)
			break;
		/* fall through */
	case 10:
		list[0] = rb(SET_PROSODY);
		if (!changed || rw(SET_PROSODY) != 1)
			host_send(1, 'P', 1, list);
		if (!all)
			break;
		/* fall through */
	case 11:
		list[0] = rb(SET_RATE);
		if (!changed) /* the rate follows from v */
			host_send(1, 'r', 1, list);
		if (!all)
			break;
		/* fall through */
	case 12:
		list[0] = rb(SET_VOICE);
		if (!changed || rw(SET_VOICE) != 0)
			host_send(1, 'V', 1, list);
		break;
	}
	if (all) {
		list[0] = n & 0xFF;
		host_send(1, 'Q', 1, list);
	}
}

/* w: all settings to their defaults, also in the stage records (not the rate, which only Q reports) */
static void settings_reset(void)
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
	for (int k = 0, r = 0xC206; k < 5; k++, r += 0x22) {
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

static int clamp(int v, int lo, int hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

/* N / F, A / D: the flags word after turning the listed flags (1..max) on or off */
static int flags_command(int n, int reg, int max, int on)
{
	int mask = 0;
	for (int i = 0; i < n; i++)
		if (s16(param(i)) >= 1 && s16(param(i)) <= max)
			mask |= rw(BITMASK + 2 * (param(i) - 1));
	ww(reg, on ? rw(reg) | mask : rw(reg) & ~mask);
	set_param(1, rw(reg) >> 8 & 0xFF);
	return rw(reg) & 0xFF;
}

/* The command letter c with n parameters. Returns the number of values to pass on in-band, -1 for an error (BEL)
 * or -2 when it is done; p0 is the (adjusted) first value. The letter to pass on is left in *c. */
static int command(int *c, int n, int *p0)
{
	int si = s16(param(0)), r = -1;
	switch (*c) {
	case 'i': /* index marker, 1-255 */
		if (n > 1)
			break;
		if (si < 0)
			si = 1;
		else if (si == 0)
			break;
		r = 1;
		break;
	case 'E': /* identify */
		if (n != 0)
			break;
		r = -2;
		{
			int id[1] = {rb(IDENTITY)};
			host_send(1, 'E', 1, id);
		}
		break;
	case 'Q':
		if (n > 1)
			break;
		if (si < 0)
			si = 0;
		if ((si >= 0 && si <= 12) || si == 255) {
			r = -2;
			host_status_report(si);
		}
		break;
	case 'w':
		if (n != 0)
			break;
		settings_reset();
		r = -2;
		break;
	case 'S':
		if (n != 0)
			break;
		r = -2;
		ww(STOP_REQUEST, 1);
		break;
	case 'q':
		if (n != 0)
			break;
		r = -2;
		ww(QUIT_REQUEST, 1);
		break;
	case 'W':
		if (n == 0 && restart(0x77)) {
			r = -2;
			break;
		}
		/* fall through */
	case 'x': /* end of sentence */
		if (n != 0)
			break;
		r = 0;
		ww(END_REQUEST, 1);
		ww(OUTPUT_HOLD, 0);
		break;
	case 'C': /* continue */
		if (n != 0)
			break;
		r = 0;
		ww(OUTPUT_HOLD, 0);
		break;
	case 'H': /* hold: stop sending frames */
		if (n != 0)
			break;
		r = -2;
		ww(OUTPUT_HOLD, 1);
		break;
	case 'I':
		if (n > 1)
			break;
		if (si < 0) {
			si = 0;
			ww(SET_INPUT, 0);
		} else if (si > 1) {
			break;
		} else {
			ww(SET_INPUT, si);
		}
		r = 1;
		break;
	case 'P':
		if (n > 1)
			break;
		if (si < 0) {
			si = 1;
			ww(SET_PROSODY, 1);
		} else if (si > 1) {
			break;
		} else {
			ww(SET_PROSODY, si);
		}
		r = 1;
		break;
	case 'K': /* K1: drop text and in-band commands */
		if (n > 1)
			break;
		if (si < 0)
			si = 0;
		if (si <= 1)
			ww(K_MODE, si);
		r = -2;
		break;
	case 'a':
		if (n > 1)
			break;
		si = clamp(si, 0, 15);
		ww(SET_AMPLITUDE, si);
		r = 1;
		break;
	case 'L': /* INT 8 with the value; the vector holds no code on the Prose 2000 */
		if (n > 1)
			break;
		si = clamp(si, 0, 5);
		if (input_int8_hook) {
			input_int8_hook(si);
			restarted = 1;
			r = -2; /* the firmware does not come back */
		}
		break;
	case 'V':
		if (n > 1)
			break;
		si = clamp(si, 0, 2);
		ww(SET_VOICE, si);
		r = 1;
		break;
	case 'f': /* fast read: caps the speed */
		if (n > 1)
			break;
		si = clamp(si, 0, 9);
		ww(SET_FAST, si);
		if (s16(rw(SET_SPEED)) >= s16(rw(SPEED_CAP + 2 * si)))
			ww(SET_SPEED, rw(SPEED_CAP + 2 * si));
		set_param(1, rw(SET_SPEED));
		r = 2;
		break;
	case 'r': /* words per minute, turned into a speed */
		if (n <= 1) {
			if (si < 50 && si > 0)
				si = 50;
			ww(SET_RATE, si > 0 ? si : 150);
			if (si > 0)
				si = (si - 46) >> 3;
		}
		/* fall through */
	case 'v':
		if (n > 1)
			break;
		if (si < 0)
			si = 13;
		else if (si > 25)
			si = 25;
		if (rw(SET_FAST) != 0 && si > s16(rw(SPEED_CAP + 2 * rw(SET_FAST))))
			si = s16(rw(SPEED_CAP + 2 * rw(SET_FAST)));
		ww(SET_SPEED, si);
		r = 1;
		if (*c == 'v')
			ww(SET_RATE, si * 8 + 50);
		break;
	case 'p':
		if (n > 1)
			break;
		if (si < 0)
			si = 85;
		else if (si < 50 && si != 0)
			si = 50;
		else if (si > 200)
			si = 200;
		ww(SET_PITCH, si);
		r = 1;
		break;
	case 's': /* pause */
	case 'g': /* hold the l frame */
		if (n > 1)
			break;
		si = si < 0 ? 100 : clamp(si, 1, 255);
		r = 1;
		break;
	case 't': /* test frames: row 0-9, then two values */
		if (n > 3)
			break;
		r = 3;
		if (si < 0)
			si = 0;
		else if (si >= 10)
			r = -1;
		set_param(1, s16(param(1)) < 0 ? 100 : clamp(s16(param(1)), 1, 255));
		set_param(2, s16(param(2)) < 0 ? 50 : clamp(s16(param(2)), 30, 243));
		break;
	case 'l': /* l n;value: a low-level parameter for g */
		if (si == -1 || n > 2 || si >= 22)
			break;
		if (s16(param(1)) < 0)
			set_param(1, rb(LOW_DEFAULT + si));
		else if (s16(param(1)) > s16(rw(LOW_MAX + 2 * si)))
			set_param(1, rw(LOW_MAX + 2 * si));
		wb(LOW_VALUES + si, param(1));
		r = 2;
		break;
	case 'N':
	case 'F':
		si = flags_command(n, HOST_MODE, 16, *c == 'N');
		r = 2;
		*c = 'N';
		break;
	case 'A':
	case 'D':
		si = flags_command(n, HOST_AFLAGS, 7, *c == 'A');
		r = 2;
		*c = 'A';
		break;
	}
	*p0 = si;
	return r;
}

/* D658C: one character from the receive ring through the ESC [ p1 ; p2 ... letter state machine */
void host_escape_parser(void)
{
	int c = rsb(RX_RING + ruw(RX_READ));
	ww(RX_READ, (rw(RX_READ) + 1) & 0xF);
	if (c == 0x1B) {
		ww(ESC_STATE, 2);
		return;
	}
	switch (rw(ESC_STATE)) {
	case 1: /* text */
		if (rw(K_MODE) == 0)
			input_put(c);
		return;
	case 2: /* after ESC */
		if (c == '[') {
			ww(ESC_STATE, 3);
			ww(ESC_OVERFLOW, 0);
			for (int i = 15; i >= 0; i--)
				set_param(i, 0xFFFF);
			wb(ESC_INDEX, 0);
		} else {
			bel();
			ww(ESC_STATE, 1);
		}
		return;
	case 3:
		break;
	default:
		return;
	}
	int i = (int8_t)rb(ESC_INDEX);
	if (c >= '0' && c <= '9') {
		if (s16(param(i)) == -1)
			set_param(i, 0);
		if (i >= 16)
			return;
		set_param(i, param(i) * 10 + c - '0');
		if (s16(param(i)) > 0xFF)
			ww(ESC_OVERFLOW, 1);
		return;
	}
	if (c == ';') {
		wb(ESC_INDEX, i + 1);
		if ((int8_t)rb(ESC_INDEX) < 16)
			set_param((int8_t)rb(ESC_INDEX), 0);
		return;
	}
	/* the command letter */
	int n = i, p0, r;
	if (n != 0 || s16(param(0)) != -1)
		n++;
	if (rw(ESC_OVERFLOW) != 0 || n > 16)
		c = 0;
	r = command(&c, n, &p0);
	if (restarted) {
		restarted = 0;
		return;
	}
	set_param(0, p0);
	if (r != -2 && rw(K_MODE) == 0) {
		if (r == -1) {
			bel();
		} else if (r + 3 <= input_ring_free()) { /* passed on in-band: ESC, letter, count, values */
			input_put(0x1B);
			input_put(c);
			input_put(r);
			for (int k = 0; k < r; k++)
				input_put(param(k));
		}
	}
	ww(ESC_STATE, 1);
}

/* D713F: a received character (and the 8251 error bits) from the serial interrupt */
void host_rx_char(int ch, int status)
{
	ww(IRQ_NEST, rw(IRQ_NEST) + 1);
	latch_clear(1);
	if ((dip_switches() & 0x10) && (dip_switches() & 0x20) && !(uart_status() & 0x80))
		goto out; /* DIP 5 and 6: ignored while the host holds DSR low */
	if (status) {
		if (status & 8)
			ww32(RX_PARITY_ERRORS, rw32(RX_PARITY_ERRORS) + 1);
		if (status & 0x10)
			ww32(RX_OVERRUN_ERRORS, rw32(RX_OVERRUN_ERRORS) + 1);
		if (status & 0x20)
			ww32(RX_FRAMING_ERRORS, rw32(RX_FRAMING_ERRORS) + 1);
		if (status & 0x40) {
			ww32(RX_BREAKS, rw32(RX_BREAKS) + 1);
			ch = 0;
		}
	}
	if (rw(RX_FAST_START) == 0 && s16(rw(FREE_COUNT)) > 0x260)
		ww(RX_FIRST_PHRASE, 1);
	ch &= 0x7F;
	if (ch == '\r' || ch == '\n' || ch == '\t')
		ch = ' ';
	if (ch == 0x12) {
		if (restart(0x12)) {
			restarted = 0;
			return; /* the restart set the nesting count and the latch */
		}
		goto out;
	}
	if (ch == 0 || ch == 0x7F)
		goto out;
	if (ch == 0x13) { /* XOFF: stop output */
		ww(TX_STOPPED, 1);
		goto out;
	}
	if (ch == 0x11) { /* XON: restart output if it has something to send */
		ww(TX_STOPPED, 0);
		if (rw(TX_RUNNING) != 0 || rw(TX_PENDING) == 0)
			goto out;
		if ((dip_switches() & 0x10) && (dip_switches() & 0x20) && !(uart_status() & 0x80))
			goto out;
		ww(TX_RUNNING, 1);
		uart_tx_start();
		if (dip_switches() & 0x20)
			uart_command_set(0x20);
		goto out;
	}
	if (ch < 0x20 && ch != 0x1B) {
		bel();
		goto out;
	}
	int n = s16(rw(RX_READ) - rw(RX_WRITE) - 1);
	if (n < 0)
		n += 0x10;
	if (n <= 0)
		goto out; /* the ring is full: the character is lost */
	wb(RX_RING + ruw(RX_WRITE), ch);
	ww(RX_WRITE, (rw(RX_WRITE) + 1) & 0xF);
	if (rw(RX_BUSY) == 0) { /* not already inside the parser (interrupts are on while it runs) */
		ww(RX_BUSY, 1);
		latch_set(1);
		while (rw(RX_WRITE) != rw(RX_READ)) {
			ww(IRQ_NEST, rw(IRQ_NEST) - 1);
			host_escape_parser();
			ww(IRQ_NEST, rw(IRQ_NEST) + 1);
		}
		ww(RX_BUSY, 0);
	}
out:
	ww(IRQ_NEST, rw(IRQ_NEST) - 1);
	latch_set(1);
}

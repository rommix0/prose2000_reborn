/* The input stage: host text from the receive ring into the node list. */
#include "input.h"

unsigned input_dip_port = 0xFC; /* the emulator's (and MAME's) default: S4-1 and S4-2 on */

/* D5D68 */
int dip_switches(void)
{
	return ~input_dip_port & 0xFF;
}

/* D5D8B */
void latch_set(int bits)
{
	wb(LATCH, rb(LATCH) | bits);
	wb(ruw(LATCH_PTR), rb(LATCH));
}

/* D5D74 */
void latch_clear(int bits)
{
	wb(LATCH, rb(LATCH) & ~bits);
	wb(ruw(LATCH_PTR), rb(LATCH));
}

/* D63E5: interrupts off (cli), and latch bit 2 set on the outermost call */
void irq_disable_nested(void)
{
	int old = rw(IRQ_NEST);
	ww(IRQ_NEST, old + 1);
	if (old == 0)
		latch_set(4);
}

/* D6576 */
void irq_enable_nested(void)
{
	ww(IRQ_NEST, rw(IRQ_NEST) - 1);
	if (rw(IRQ_NEST) == 0)
		latch_clear(4); /* then sti */
}

/* DE2AF: free bytes in the receive ring */
int input_ring_free(void)
{
	int n = s16(rw(INPUT_READ) - rw(INPUT_WRITE) - 1);
	if (n < 0)
		n += 0x100;
	return n;
}

/* DE2C5: the next character from the receive ring (or from the demo text in self-test), -1 when there is none.
 * Sends XON once a ring that was stopped with XOFF has room again. */
int input_getc(void)
{
	int c;
	irq_disable_nested();
	if (rw(INPUT_XOFF) != 0 && input_ring_free() >= 0x87)
		host_send(0, 0x11, 0, NULL); /* XON */
	if (dip_switches() & 0x40) {
		if (rb(ruw(DEMO_PTR)) == 0)
			ww(DEMO_PTR, rw(DEMO_TEXT_PTR));
		c = rsb(ruw(DEMO_PTR));
		ww(DEMO_PTR, rw(DEMO_PTR) + 1);
	} else if (rw(INPUT_READ) == rw(INPUT_WRITE)) {
		c = -1;
	} else {
		c = rsb(INPUT_RING + ruw(INPUT_READ));
		ww(INPUT_READ, rw(INPUT_READ) + 1);
		if (rw(INPUT_READ) >= 0x100)
			ww(INPUT_READ, 0);
	}
	irq_enable_nested();
	return c;
}

/* D35C4: a node at the end of the list; it starts the text-rules window if that is empty */
int node_append(int kind, int ch)
{
	int n = node_insert(rw(LIST_HEAD), 0, kind, ch);
	ww(0xC20E, n);
	for (int f = 0xC20C; f >= 0xC206; f -= 2)
		if (rw(f) == 0)
			ww(f, n);
	return n;
}

/* the parameters of a command from the ring: a count byte (1-3), then the values */
static void command_values(int n)
{
	int v;
	switch (input_getc() - 1) {
	case 0: /* one value */
		ww(n + N_DUR, (rw(n + N_DUR) & 0xFF00) | (input_getc() & 0xFF));
		wb(n + N_F0, 0);
		break;
	case 1: /* two values; A and N are remembered for the [ ] brackets */
		ww(n + N_DUR, (rw(n + N_DUR) & 0xFF00) | (input_getc() & 0xFF));
		wb(n + N_F0, input_getc());
		if (node_char(n) == 'A')
			ww(INPUT_AFLAGS, rb(n + N_F0) << 8 | rb(n + N_DUR));
		if (node_char(n) == 'N')
			ww(INPUT_NFLAGS, rb(n + N_F0) << 8 | rb(n + N_DUR));
		break;
	case 2: /* node flags (stress, bits 5 and 6), then two values */
		v = input_getc();
		ww(n + N_FLAGS, (rw(n + N_FLAGS) & 0xFFE7) | (v & 3) << 3);
		if (v & 4)
			ww(n + N_FLAGS, rw(n + N_FLAGS) | 0x20);
		if (v & 8)
			ww(n + N_FLAGS, rw(n + N_FLAGS) | 0x40);
		ww(n + N_DUR, (rw(n + N_DUR) & 0xFF00) | (input_getc() & 0xFF));
		wb(n + N_F0, input_getc());
		break;
	default:
		ww(n + N_DUR, rw(n + N_DUR) & 0xFF00);
		wb(n + N_F0, 0);
		break;
	}
}

/* DE09D: take characters until the end of the next word (a space and then something else), 10 at most. Returns 1
 * if it took any. */
int stage_input_run(void)
{
	int taken = 0, space = 0, ret, c, n;
	if (rw(INPUT_AHEAD) == -1) {
		ww(INPUT_AHEAD, input_getc());
		if (rw(INPUT_AHEAD) == -1)
			return 0;
	}
	ret = 0;
	do {
		c = rw(INPUT_AHEAD);
		if (c == ' ')
			space = 1;
		else if (space)
			break;
		if (taken++ >= 10)
			break;
		if (c == 0x1B) {
			n = node_append(NODE_COMMAND, c);
			wb(n + N_CHAR, input_getc());
			command_values(n);
		} else if ((c == '[' || c == ']') && (rw(INPUT_AFLAGS) & 0x20) && !(rw(INPUT_NFLAGS) & 3)) {
			/* A-flag 6: [ ] switch phoneme input on (1, or 2 = phoneme names with A-flag 7) and off */
			n = node_append(NODE_COMMAND, 'I');
			if (c == ']')
				ww(n + N_DUR, rw(n + N_DUR) & 0xFF00);
			else
				ww(n + N_DUR, (rw(n + N_DUR) & 0xFF00) | ((rw(INPUT_AFLAGS) & 0x40) ? 2 : 1));
		} else {
			node_append(1, c);
		}
		ww(INPUT_AHEAD, -1);
		ret = 1;
		ww(INPUT_AHEAD, input_getc());
	} while (rw(INPUT_AHEAD) != -1);
	return ret;
}

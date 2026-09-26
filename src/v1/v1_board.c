/* v1.1 board helpers: the DIP switches, the peripheral latch, the 8251 command register and the nested interrupt
 * lock. The firmware reaches the I/O page through ES = 0300 (offset 0x400 = port 3400, and so on); the C keeps the
 * copies it holds in RAM, and the ports the board returns are variables. */
#include "v1.h"

unsigned v1_dip_port = 0xFC;    /* the emulator's default: S4-1 and S4-2 on (0 = on) */
unsigned v1_uart_status = 0x85; /* TxRDY, TxEMPTY, DSR */

/* EC0CE: 1 if every switch in mask is on */
int v1_dip_switch_test(int mask) { return (v1_dip_port & (unsigned)mask & 0xFF) == 0; }

/* EC0FB */
void v1_latch_set(int bits) { wb(V1_LATCH, rb(V1_LATCH) | bits); }

/* EC0E6 */
void v1_latch_clear(int bits) { wb(V1_LATCH, rb(V1_LATCH) & ~bits); }

/* EC10F: cli, latch bit 2 up */
void v1_irq_disable(void) { v1_latch_set(4); }

/* EC11D: latch bit 2 down, sti */
void v1_irq_enable(void) { v1_latch_clear(4); }

/* EC290 / EC27B: 8251 command register bits */
void v1_uart_command_set(int bits) { wb(V1_UART_CMD, rb(V1_UART_CMD) | bits); }
void v1_uart_command_clear(int bits) { wb(V1_UART_CMD, rb(V1_UART_CMD) & ~bits); }

/* EC2A3: 1 if any of the 8251 status bits in mask is set */
int v1_uart_status_test(int mask) { return (v1_uart_status & (unsigned)mask) != 0; }

/* EC328: the transmitter on: IR2/IR3 unmasked, 8251 TxEN */
void v1_uart_tx_start(void)
{
	wb(V1_PIC_MASK, rb(V1_PIC_MASK) & 0xF3);
	v1_uart_command_set(1);
}

/* EC393: the transmitter off */
void v1_uart_tx_stop(void)
{
	v1_uart_command_clear(1);
	wb(V1_PIC_MASK, rb(V1_PIC_MASK) | 0x0C);
}

/* ED99C: interrupts off, counted in V1_IRQ_NEST (each call counts in A426) */
void v1_irq_disable_nested(void)
{
	count32(0xA426);
	v1_irq_disable();
	ww(V1_IRQ_NEST, rw(V1_IRQ_NEST) + 1);
}

/* EDB16: back on when the count reaches 0 (each call counts in A42A) */
void v1_irq_enable_nested(void)
{
	count32(0xA42A);
	ww(V1_IRQ_NEST, rw(V1_IRQ_NEST) - 1);
	if (rw(V1_IRQ_NEST) == 0)
		v1_irq_enable();
}

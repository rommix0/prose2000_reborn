/* Power-up: the boot code at the reset vector, and the hardware part of loop_entry (code 0x12): self-test, interrupt
 * vectors, 8259, peripheral latch, 8251 and the DSP boot. The board's ports are memory-mapped into the data segment
 * (DS:EF00 = linear 03000); writes to them land in the image above the RAM. */
#include "input.h"

#include <string.h>

static const prose_rom *boot_rom;

/* the ROM image for the self-test's checksums (power_up sets it too) */
void boot_load_rom(const prose_rom *rom)
{
	boot_rom = rom;
	dsp_load_rom(rom);
}

/* the interrupt vector table and the rest of linear 0-3FF are at DS:BF00 */
static void ivt_word(int off, int v)
{
	ww(DS_RAM_LO + off, v);
}

/* D3207: busy-wait n x 0x170 loops (no effect on RAM) */
static void delay(int n)
{
	(void)n;
}

/* D32B0: RAM test over linear 0-2BFF: each byte is walked from FF down to 0, then from the top back up to FF.
 * Returns 0, or 1 when a byte did not read back (the firmware then lights the error LED and reports it). */
static int ram_test(void)
{
	uint8_t *ram = pg_ds + DS_RAM_LO;
	for (int i = 0; i < RAM_TESTED; i++) {
		unsigned ax = 0xFF;
		ram[i] = 0xFF;
		for (;;) {
			if (ram[i] != (ax & 0xFF))
				return 1;
			int carry = ax & 1;
			ax >>= 1;
			if (!carry)
				break;
			ram[i] = (uint8_t)ax;
		}
	}
	for (int i = RAM_TESTED - 1; i >= 0; i--) {
		unsigned ax = 0;
		for (;;) {
			if (ram[i] != (ax & 0xFF))
				return 1;
			int carry = ax & 1;
			ax = (ax >> 1) | 0x8000;
			if (carry)
				break;
			ram[i] = (uint8_t)ax;
		}
	}
	return 0;
}

/* D33B3: ROM checksums: for each of the six 32 KB byte lanes of D0000-FFFFF, the byte sum must equal the word at
 * DS:000C + 2 * lane (lanes 2-7). Returns 0, or the first failing lane (then an error LED). The sums come with the
 * ROM image (prose_rom.lane_sum), since the built-in image has no code to sum. */
static int rom_checksum(void)
{
	for (int lane = 2; lane <= 7; lane++) {
		if (!boot_rom)
			return 0; /* no image to check */
		if (boot_rom->lane_sum[lane - 2] != (unsigned)ruw(0x000C + 2 * lane)) {
			wb(LATCH_PORT, 0x10); /* LEDs: ROM error */
			return lane;
		}
	}
	wb(LATCH_PORT, 0x08);
	return 0;
}

/* D3377: the self-test result for ESC[nR: 0x10 when the RAM test failed, else the ROM checksum's */
static int self_test(void)
{
	if (rw(BOOT_STATUS) == 0x10)
		wb(LATCH_PORT, 0x12); /* LEDs: RAM error */
	else {
		int r = rom_checksum();
		if (r != 0)
			ww(BOOT_STATUS, r);
	}
	return rw(BOOT_STATUS);
}

/* D5E14: the 8259 (edge triggered, vector base 0x20, all masked), every vector to the default handler D5D6:0644
 * (fatal_error 0x25), and vector 8 to a stub copied to 0000:0024 from DS:03AA. The stub writes AL to port 3402 and
 * jumps to the reset vector: ESC[nL. */
static void pic_init(void)
{
	int icw1 = rw(IO_BASE) + 0x200, mask = rw(IO_BASE) + 0x202;
	ww(BOOT_CS, 0xD5D6);
	ww(PIC_MASK_PTR, mask);
	wb(icw1, 0x17);
	wb(mask, 0x20);
	wb(mask, 0x0F);
	wb(PIC_MASK, 0xFF);
	wb(mask, 0xFF);
	wb(icw1, 0xC0);
	for (int v = 0; v < 0xA0; v += 4) {
		ivt_word(v, 0x0644);
		ivt_word(v + 2, rw(BOOT_CS));
	}
	ivt_word(0x20, 0x24);
	ivt_word(0x22, 0);
	for (int i = 0; i < 15; i++)
		wb(DS_RAM_LO + 0x24 + i, rb(INT8_STUB + i));
}

/* D5DF6: the DIP port and the peripheral latch (5E: the DSP held in reset, LEDs) */
static void latch_init(void)
{
	ww(DIP_PTR, rw(IO_BASE) + 0x400);
	ww(LATCH_PTR, rw(IO_BASE) + 0x401);
	wb(LATCH, 0x5E);
	wb(ruw(LATCH_PTR), 0x5E);
}

/* D6006: the 8251 (internal reset, then mode: 8 data bits, the stop and parity bits from DIP S4-1/3/4, x16 clock) and
 * its interrupt vectors: 0x21 receive (D5D6:0500), 0x22 and 0x23 transmit (D5D6:05BB) */
static void uart_init(void)
{
	int ctl = rw(IO_BASE) + 2;
	ww(UART_DATA_PTR, rw(IO_BASE));
	ww(UART_CMD_PTR, ctl);
	wb(ctl, 0);
	delay(1);
	wb(ctl, 0);
	delay(1);
	wb(ctl, 0);
	delay(1);
	wb(ctl, 0x40);
	delay(1);
	wb(ctl, ((~input_dip_port & 0x0D) << 2 | 0x4A) & 0xFF);
	delay(1);
	wb(UART_CMD, (dip_switches() & 0x10) || (dip_switches() & 0x20) ? 4 : 6); /* receiver on, RTS unless DIP 5/6 */
	wb(ctl, rb(UART_CMD));
	delay(2);
	ivt_word(0x84, 0x0500);
	ivt_word(0x86, rw(BOOT_CS));
	ivt_word(0x88, 0x05BB);
	ivt_word(0x8A, rw(BOOT_CS));
	ivt_word(0x8C, 0x05BB);
	ivt_word(0x8E, rw(BOOT_CS));
	wb(PIC_MASK, rb(PIC_MASK) & 0xFD);
	wb(ruw(PIC_MASK_PTR), rb(PIC_MASK));
}

/* D5EE3: reset the DSP and send it n words from DS:src, each when it asks (status bit 0x20 first, then RQM 0x80); its
 * interrupt vectors 0x20 and 0x27 (D5D6:03FF), unmasked. Returns 0 if the DSP did not answer; the C has no DSP to
 * wait for, so it always answers (the caller sends the boot block to its DSP model itself). */
static int dsp_boot(int src, int n)
{
	(void)src;
	(void)n;
	ww(DSP_DATA_PTR, rw(IO_BASE) + 0x600);
	ww(DSP_STATUS_PTR, rw(IO_BASE) + 0x602);
	wb(ruw(LATCH_PTR), rb(LATCH) & 0xBF); /* reset pulse */
	delay(1);
	wb(LATCH, rb(LATCH) | 0x40);
	wb(ruw(LATCH_PTR), rb(LATCH));
	delay(1);
	wb(LATCH, rb(LATCH) | 0x20);
	wb(ruw(LATCH_PTR), rb(LATCH));
	ww(0xDB76, 0);
	ivt_word(0x80, 0x03FF);
	ivt_word(0x82, rw(BOOT_CS));
	ivt_word(0x9C, 0x03FF);
	ivt_word(0x9E, rw(BOOT_CS));
	wb(PIC_MASK, rb(PIC_MASK) & 0x7E);
	wb(ruw(PIC_MASK_PTR), rb(PIC_MASK));
	return 1;
}

/* E37F0, code 0x12: the self-test result into [EE78], then the hardware set-up */
void boot_hardware_init(void)
{
	wb(SELF_TEST, self_test());
	pic_init();
	latch_init();
	uart_init();
	if (!dsp_boot(0x5644, 9))
		fatal_error(0x29);
}

/* D3100 with DIP S4-8 off: RAM test, RAM 0-2BFF filled with 0x55, then synthesis_loop(0x12), which runs loop_entry.
 * (With S4-8 on the board loops in a RAM or ROM test for the factory, D3113-D318D.) The RAM from 2C00 up is not
 * cleared: it keeps what it held, which the caller sets. Returns after loop_entry, before the loop's first pass. */
void power_up(const prose_rom *rom)
{
	boot_rom = rom;
	dsp_load_rom(rom);
	wb(LATCH_PORT, 0x1C);
	int failed = ram_test();
	wb(LATCH_PORT, failed ? 0x12 : 0x0A);
	memset(pg_ds + DS_RAM_LO, 0x55, RAM_TESTED);
	ww(IO_BASE, 0xEF00); /* 0300:0000 seen from DS F410 */
	ww(0xDB8C, 1);       /* powered up (not restarted): loop_entry clears the error counters */
	ww(BOOT_STATUS, failed ? 0x10 : 0);
	loop_entry(0x12);
}

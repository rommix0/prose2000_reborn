/*
 * Checks the v1.1 C power-up (v1_power_up: loop_restart's RAM set-up and loop_entry) against the emulator's RAM when
 * synthesis_main returns from loop_entry (F7210).
 *
 *   v1_boot_check ENTRY.ram
 *
 * ENTRY.ram (written by a scratch harness on native/ with the DSP stub, not committed) is the 12 KB of RAM, followed
 * by SP. The C starts from cleared RAM (the emulator's power-on contents), runs v1_power_up and lets the transmitter
 * send what is queued, then compares all of RAM except the stack (linear 0100-02FF: SP starts at 0300). Differences
 * are printed as DS offsets.
 *
 * The emulated DSP stub asks for one frame while loop_entry waits for ESC[1R to go out, so the C runs one frame
 * interrupt (v1_dsp_irq_service) as well: it builds a frame from the rest values, which changes the frame buffer
 * (DS:95C4-963F), the track position 96DE and the counters.
 */
#include "v1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sent[64], n_sent, boot_words;
static void on_tx(int c)
{
	if (n_sent < 64)
		sent[n_sent++] = c;
}
static void on_dsp(unsigned w)
{
	(void)w;
	boot_words++;
}
static void on_fatal(int code)
{
	printf("fatal_error(0x%X)\n", code);
	exit(1);
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: v1_boot_check ENTRY.ram\n");
		return 2;
	}
	static uint8_t want[0x3000];
	FILE *f = fopen(argv[1], "rb");
	if (!f || fread(want, 1, sizeof want, f) != sizeof want)
		return 2;
	fclose(f);

	v1_load_rom();
	v1_fatal_hook = on_fatal;
	v1_tx_hook = on_tx;
	v1_dsp_write_hook = on_dsp;
	memset(v1_ds + V1_DS_RAM_LO, 0, 0x3000);
	v1_power_up();
	while (rw(V1_TX_RUNNING))
		v1_uart_tx_isr();
	v1_dsp_status = 0xA0; /* RQM, USF0 */
	v1_dsp_irq_service();

	printf("%d boot words; sent:", boot_words);
	for (int i = 0; i < n_sent; i++)
		printf(sent[i] == 0x1B ? " ESC" : sent[i] < 0x20 ? " %02X" : " %c", sent[i]);
	printf("\n");
	int bad = 0;
	for (int i = 0; i < 0x3000; i++) {
		if (i >= 0x100 && i < 0x300)
			continue;
		if (v1_ds[V1_DS_RAM_LO + i] != want[i]) {
			if (bad++ < 40)
				printf("  DS:%04X: C %02X, emulator %02X\n", V1_DS_RAM_LO + i, v1_ds[V1_DS_RAM_LO + i],
				       want[i]);
		}
	}
	printf("%d bytes differ (stack excluded)\n", bad);
	return bad != 0;
}

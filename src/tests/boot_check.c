/*
 * Checks the C power-up (power_up: boot, self-test, hardware set-up, loop_entry) against the emulator's RAM when the
 * synthesis loop first reaches its idle routine.
 *
 *   boot_check IDLE.ram
 *
 * IDLE.ram (written by a scratch harness on native/, not committed) is the 12 KB of RAM at the first entry to
 * loop_idle E3B9D, followed by SP. The C starts from RAM 2C00-2FFF cleared (the emulator's power-on contents), runs
 * power_up and lets the transmitter send what loop_entry queued, then compares all of RAM except the stack
 * (linear 0100-02FF: SP starts at 0300). Differences are printed as DS offsets. The emulated DSP raises one interrupt
 * after its reset without asking for a frame; the C runs dsp_irq_service once with the status port showing no request,
 * which counts it in [DB76] as the firmware does.
 */
#include "input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sent[64], n_sent;
static void on_tx(int c)
{
	if (n_sent < 64)
		sent[n_sent++] = c;
}

static void on_fatal(int code)
{
	printf("fatal_error(0x%X)\n", code);
	exit(1);
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: boot_check IDLE.ram\n");
		return 2;
	}
	static prose_rom rom;
	prose_rom_builtin(&rom);
	static uint8_t want[0x3000];
	FILE *f = fopen(argv[1], "rb");
	if (!f || fread(want, 1, sizeof want, f) != sizeof want)
		return 2;
	fclose(f);

	pg_load_rom(&rom);
	pg_fatal_hook = on_fatal;
	pg_host_send_hook = host_send;
	host_tx_hook = on_tx;
	memset(pg_ds + DS_RAM_LO, 0, 0x3000);
	power_up(&rom);
	dsp_status = 0x80; /* RQM only: no frame request */
	dsp_irq_service();
	while (rw(TX_RUNNING))
		if (tx_send() < 0)
			break;

	printf("sent:");
	for (int i = 0; i < n_sent; i++)
		printf(sent[i] == 0x1B ? " ESC" : sent[i] < 0x20 ? " %02X" : " %c", sent[i]);
	printf("\n");
	int bad = 0;
	for (int i = 0; i < 0x3000; i++) {
		if (i >= 0x100 && i < 0x300)
			continue;
		if (pg_ds[DS_RAM_LO + i] != want[i]) {
			if (bad++ < 40)
				printf("  DS:%04X: C %02X, emulator %02X\n", DS_RAM_LO + i, pg_ds[DS_RAM_LO + i],
				       want[i]);
		}
	}
	printf("%d bytes differ (stack excluded)\n", bad);
	return bad != 0;
}

/* The v1.1 data-segment image and the fatal-error path. */
#include "v1.h"

#include "prose_v1_data.h"

#include <stdlib.h>
#include <string.h>

uint8_t v1_ds[0x10000];

void (*v1_fatal_hook)(int code);

void v1_load_rom(void)
{
	memset(v1_ds, 0xFF, V1_DS_RAM_LO);
	memcpy(v1_ds, prose_v1_ds_data, sizeof prose_v1_ds_data);
	memset(v1_ds + V1_DS_RAM_HI, 0xFF, sizeof v1_ds - V1_DS_RAM_HI);
}

void v1_load_ram(const uint8_t ram[0x3000]) { memcpy(v1_ds + V1_DS_RAM_LO, ram, 0x3000); }

void v1_save_ram(uint8_t ram[0x3000]) { memcpy(ram, v1_ds + V1_DS_RAM_LO, 0x3000); }

/* ED9B6: count the error (32-bit counter A3EA), keep a value (A3EE, sign-extended into A3F0) and restart the
 * synthesis loop (loop_restart EC021). The restart is the caller's business, so the hook must not return. The
 * firmware means to keep the code but stores the caller's SI (xchg ax,si where ax,di was meant); the C keeps the
 * code. */
void v1_fatal_error(int code)
{
	count32(0xA3EA);
	ww(0xA3EE, code);
	ww(0xA3F0, s16(code) >> 15);
	if (v1_fatal_hook)
		v1_fatal_hook(code);
	abort();
}

/* The data segment image and the fatal-error path. */
#include "pg.h"

#include <stdlib.h>
#include <string.h>

uint8_t pg_ds[0x10000];

void (*pg_fatal_hook)(int code);
int (*pg_host_send_hook)(int kind, int ch, int count, const int *params);

void pg_load_rom(const prose_rom *rom)
{
	memcpy(pg_ds, rom->image + (PROSE_DS - 0xC0000u), DS_RAM_LO);
	memset(pg_ds + DS_RAM_HI, 0xFF, sizeof pg_ds - DS_RAM_HI);
}

void pg_load_ram(const uint8_t ram[0x3000]) { memcpy(pg_ds + DS_RAM_LO, ram, 0x3000); }

void pg_save_ram(uint8_t ram[0x3000]) { memcpy(ram, pg_ds + DS_RAM_LO, 0x3000); }

/* D6400: count the error (32-bit counter EE7A), keep its code (EE7E, sign-extended into EE80) and restart the
 * synthesis loop (D31B5). The restart is the caller's business, so the hook must not return. */
void fatal_error(int code)
{
	unsigned lo = ruw(0xEE7A) + 1;
	ww(0xEE7A, lo);
	ww(0xEE7C, rw(0xEE7C) + (lo > 0xFFFF || (lo & 0xFFFF) == 0));
	ww(0xEE80, s16(code) >> 15);
	ww(0xEE7E, code);
	if (pg_fatal_hook)
		pg_fatal_hook(code);
	abort();
}

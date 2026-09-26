/* The Prose 2000 v3.4.1 8086 firmware image (linear C0000-FFFFF), built from the data in src/data. */
#ifndef PROSE_ROM_H
#define PROSE_ROM_H

#include <stdint.h>

typedef struct {
	uint8_t image[0x40000]; /* linear C0000-FFFFF */
	uint16_t lane_sum[6];   /* byte sums of the 32 KB lanes 2-7 of D0000-FFFFF, for the boot ROM check */
} prose_rom;

/* The image from the data extracted into src/data (tools/rom_extract.py): the lexicon segment and the data
   segment, with the lane sums of the full ROM. The code areas read as FF; the C never reads them. */
void prose_rom_builtin(prose_rom *rom);

/* The firmware's data segment is F410, so DS:x is linear F4100 + x. Tables are read from the ROM image. */
#define PROSE_DS 0xF4100u

static inline uint8_t prose_ds_byte(const prose_rom *rom, uint16_t off)
{
	return rom->image[PROSE_DS + off - 0xC0000u];
}

static inline int16_t prose_ds_word(const prose_rom *rom, uint16_t off)
{
	return (int16_t)(prose_ds_byte(rom, off) | prose_ds_byte(rom, (uint16_t)(off + 1)) << 8);
}

/* Word idx of a DS table. */
static inline int16_t prose_ds_table(const prose_rom *rom, uint16_t table, int idx)
{
	return prose_ds_word(rom, (uint16_t)(table + 2 * idx));
}

#endif

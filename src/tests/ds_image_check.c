/* ds_image_check [OUT.bin]
 *
 * The data segment DS:0000-AEA9 is built from the tables in src/data/ds_*.c (prose_ds_tables.h), which may be edited
 * by hand. This test checks that the layout list covers DS:0000-AEA9 in order without gaps or overlaps, and that the
 * image prose_rom_builtin builds from it is the ROM's, byte for byte (FNV-1a hash of the dump, PROSE_DS_FNV1A64).
 * OUT.bin receives the built DS:0000-AEA9. Returns 0 if both checks pass.
 */
#include "prose_ds_tables.h"
#include "prose_rom.h"

#include <stdio.h>

static prose_rom rom;

int main(int argc, char **argv)
{
	unsigned i, at = 0, bad = 0;
	uint64_t h = 0xCBF29CE484222325ull;
	for (i = 0; i < prose_ds_layout_count; i++) {
		const prose_ds_span *s = &prose_ds_layout[i];
		if (s->off != at) {
			printf("%s starts at DS:%04X, expected DS:%04X\n", s->name, s->off, at);
			bad++;
		}
		at = s->off + s->size;
	}
	if (at != PROSE_DS_SIZE) {
		printf("the tables end at DS:%04X, not DS:%04X\n", at, PROSE_DS_SIZE);
		bad++;
	}
	prose_rom_builtin(&rom);
	for (i = 0; i < PROSE_DS_SIZE; i++) {
		h ^= prose_ds_byte(&rom, (uint16_t)i);
		h *= 0x100000001B3ull;
	}
	if (h != PROSE_DS_FNV1A64) {
		printf("DS:0000-%04X differs from the ROM (FNV-1a %016llX, expected %016llX)\n", PROSE_DS_SIZE - 1,
		       (unsigned long long)h, (unsigned long long)PROSE_DS_FNV1A64);
		bad++;
	}
	if (argc > 1) {
		FILE *f = fopen(argv[1], "wb");
		if (!f || fwrite(rom.image + (PROSE_DS_DATA_BASE - 0xC0000u), 1, PROSE_DS_SIZE, f) != PROSE_DS_SIZE) {
			printf("cannot write %s\n", argv[1]);
			bad++;
		}
		if (f)
			fclose(f);
	}
	printf("%u tables, DS:0000-%04X: %s\n", prose_ds_layout_count, PROSE_DS_SIZE - 1,
	       bad ? "DIFFERENT" : "equal to the ROM");
	return bad != 0;
}

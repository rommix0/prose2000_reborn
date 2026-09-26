#include "prose_rom.h"

#include "prose_data.h"

#include <string.h>

void prose_rom_builtin(prose_rom *rom)
{
	memset(rom->image, 0xFF, sizeof rom->image);
	memcpy(rom->image + (PROSE_LEXICON_BASE - 0xC0000u), prose_lexicon, sizeof prose_lexicon);
	memcpy(rom->image + (PROSE_DS_DATA_BASE - 0xC0000u), prose_ds_data, sizeof prose_ds_data);
	memcpy(rom->lane_sum, prose_rom_lane_sums, sizeof rom->lane_sum);
}

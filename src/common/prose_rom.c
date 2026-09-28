#include "prose_rom.h"

#include "prose_data.h"
#include "prose_ds_tables.h"

#include <string.h>

void prose_rom_builtin(prose_rom *rom)
{
	unsigned i;
	memset(rom->image, 0xFF, sizeof rom->image);
	memcpy(rom->image + (PROSE_LEXICON_BASE - 0xC0000u), prose_lexicon, sizeof prose_lexicon);
	for (i = 0; i < prose_ds_layout_count; i++)
		memcpy(rom->image + (PROSE_DS_DATA_BASE - 0xC0000u) + prose_ds_layout[i].off, prose_ds_layout[i].data,
		       prose_ds_layout[i].size);
	memcpy(rom->lane_sum, prose_rom_lane_sums, sizeof rom->lane_sum);
}

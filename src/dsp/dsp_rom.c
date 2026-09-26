#include "dsp_rom.h"

#include "prose_data.h"

#include <string.h>

void prose_dsp_builtin_data_rom(uint16_t rom[512])
{
	memcpy(rom, prose_dsp_data_rom, sizeof prose_dsp_data_rom);
}

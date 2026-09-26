/* The Prose DSP data ROM (v3.12, 8/9/88). */
#ifndef DSP_ROM_H
#define DSP_ROM_H

#include <stdint.h>

/* The 512 words in address order, from the data extracted into src/data (tools/rom_extract.py). */
void prose_dsp_builtin_data_rom(uint16_t rom[512]);

#endif

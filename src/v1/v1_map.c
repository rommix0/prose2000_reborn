/* v1.1's 37-word frame -> v3.12's 40-word frame. See v1_map.h.
 *
 * Word correspondence (v1.1 word -> v3.12 word; src/v1/v1_frame.c and src/frame/frame_build.c):
 *   F4:  w11 -> 6 (r cos); gain 23 = 0x3C40 - (w11 >> 1): v1.1 sends it as w28 = gain * 9/8
 *   F3:  w13, w14, w30 -> 8, 9, 25     F2: w15, w16, w32 -> 10, 11, 27
 *   F1:  w19, w20 -> 14, 15; w24 -> 36, which v3.12 takes at twice the scale
 *   nasal zero: w3 -> 32, w34 -> 29    AH: w21 -> 16
 *   parallel: w31, w29, w27, w23, w33 -> 26, 24, 22, 18, 28; w25 -> 20, rescaled from v1.1's fixed F5 gain 0x63E0
 *   to v3.12 voice 0's
 * The two builders' cosine, bandwidth, dB and spacing tables agree (a few entries differ by one); v1.1's nasal-zero
 * gain table is v3.4.1's indexed by the same frequency. v1.1's own words 0, 1, 4-10, 12, 17, 18, 22, 26 and 35 are
 * constants whose use by its DSP is unknown; they are dropped.
 *
 * The source: F0 = w2 bits 1-8, and AV is the smallest dB index whose value is -w36 (the table is monotonic), so
 * v1.1's AV 0 (still a small level on its DSP) becomes an unvoiced frame. AH likewise, and AF > 0 when any parallel
 * amplitude is set. v3.4.1's builder then makes the source, the silence flag and the voice words from these, with
 * p18-p21 = 17, 8, 0, 0 (voice 0, no jitter or shimmer). */
#include "v1_map.h"

#include <string.h>

#define T_DB 0x5CCA
#define T_BOOT 0x5644
#define T_VOICE_F5G 0x53D8
#define V1_F5_GAIN 0x63E0

enum { V1_DB_SIZE = 213 }; /* v1.1's dB table, DS:1E4F-1FF8 */

/* the smallest index from base whose dB value is v (or the first above it) */
static int db_index(const prose_rom *rom, int base, int v)
{
	for (int i = base; i < V1_DB_SIZE; i++)
		if (prose_ds_table(rom, T_DB, i) >= v)
			return i - base;
	return V1_DB_SIZE - 1 - base;
}

void v1_map_init(v1_map *m, const prose_rom *rom) { frame_builder_init(&m->fb, rom, 0); }

void v1_map_boot_block(const v1_map *m, uint16_t out[9])
{
	for (int i = 0; i < 9; i++)
		out[i] = (uint16_t)prose_ds_table(m->fb.rom, T_BOOT, i);
}

void v1_map_frame(v1_map *m, const uint16_t v1[37], uint16_t out[40])
{
	const prose_rom *rom = m->fb.rom;
	uint8_t t[22] = {0};
	t[0] = (uint8_t)db_index(rom, 0x8C, -(int16_t)v1[36]);
	t[2] = (uint8_t)db_index(rom, 0x69, (int16_t)v1[21]);
	t[1] = v1[23] || v1[25] || v1[27] || v1[29] || v1[31] || v1[33];
	t[17] = (uint8_t)(v1[2] >> 1);
	t[18] = 17;
	t[19] = 8;
	frame_build(&m->fb, t, 0, 0);
	uint16_t *w = m->fb.w;

	w[6] = v1[11];
	w[23] = (uint16_t)(0x3C40 - ((int16_t)v1[11] >> 1));
	w[8] = v1[13], w[9] = v1[14], w[25] = v1[30];
	w[10] = v1[15], w[11] = v1[16], w[27] = v1[32];
	w[14] = v1[19], w[15] = v1[20], w[36] = (uint16_t)(v1[24] << 1);
	w[32] = v1[3], w[29] = v1[34];
	w[16] = v1[21];
	if (t[1]) {
		w[26] = v1[31], w[24] = v1[29], w[22] = v1[27], w[18] = v1[23], w[28] = v1[33];
		w[20] = (uint16_t)((int32_t)(int16_t)v1[25] * prose_ds_table(rom, T_VOICE_F5G, 0) / V1_F5_GAIN);
	}
	memcpy(out, w, 40 * sizeof *w);
}

/* v1.1's 37-word DSP frame on the v3.12 DSP model (REFERENCE §16).
 *
 * v1.1's DSP program was never dumped, so its frames are played on v3.12's, the only Prose DSP program known to
 * survive. The two frame builders share their resonator, nasal-zero and parallel-branch arithmetic and nearly all of
 * their tables, so most of v1.1's words go straight to the v3.12 word that holds the same coefficient. The voicing
 * source is v3.12's: v3.4.1's frame builder makes it from v1.1's F0 and AV with voice 0's settings, without jitter or
 * shimmer. An approximation: how v1.1's DSP used its source words is unknown. */
#ifndef V1_MAP_H
#define V1_MAP_H

#include "frame_build.h"

#include <stdint.h>

typedef struct {
	frame_builder fb; /* v3.4.1's builder, for the source words and the fixed voice words */
} v1_map;

void v1_map_init(v1_map *m, const prose_rom *rom);

/* one v1.1 frame (DS:95C8, 37 words) -> one v3.12 frame (40 words) */
void v1_map_frame(v1_map *m, const uint16_t v1[37], uint16_t out[40]);

/* the v3.12 DSP's boot block (9 words), which the DSP needs before the first frame */
void v1_map_boot_block(const v1_map *m, uint16_t out[9]);

#endif

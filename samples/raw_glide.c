/* Raw synthesis (API.md): an "ah" gliding to "ee", set frame by frame, saved as glide.wav.
 *
 *   raw_glide [v11] */
#include "prose.h"

#include <stdio.h>
#include <string.h>

#define FRAMES 30 /* 30 x 10 ms = 0.3 s */

typedef struct {
	int16_t pcm[FRAMES * 100]; /* 100 samples per frame */
} glide;

static int lerp(int a, int b, int k) { return a + (b - a) * k / FRAMES; }

/* after each frame: keep its audio, then set up the next frame */
static int on_frame(prose_h h, int frame, const int16_t *pcm, int count, void *user)
{
	glide *g = user;
	memcpy(g->pcm + frame * 100, pcm, count * sizeof *pcm);
	int k = frame + 1;
	prose_set_frame_param(h, PROSE_F1, lerp(175, 75, k));  /* 700 -> 300 Hz */
	prose_set_frame_param(h, PROSE_F2, lerp(87, 212, k));  /* 1200 -> 2200 Hz */
	prose_set_frame_param(h, PROSE_F3, lerp(156, 181, k)); /* 2500 -> 2900 Hz */
	prose_set_frame_param(h, PROSE_F0, lerp(130, 100, k)); /* pitch 130 -> 100 Hz */
	return 0;
}

static glide g;

int main(int argc, char **argv)
{
	int version = argc > 1 && !strcmp(argv[1], "v11") ? PROSE_V11 : PROSE_V341;
	prose_h h;
	if (prose_open(&h, version))
		return 1;
	prose_set_frame_param(h, PROSE_AV, 60); /* frame 0: voiced "ah" */
	prose_set_frame_param(h, PROSE_F1, 175);
	prose_set_frame_param(h, PROSE_F2, 87);
	prose_set_frame_param(h, PROSE_F3, 156);
	prose_set_frame_param(h, PROSE_F0, 130);
	int n = prose_render_frames(h, FRAMES, on_frame, &g);
	int err = n < 0 ? n : prose_save_wave("glide.wav", g.pcm, FRAMES * 100);
	printf("%d frames rendered; glide.wav: %s\n", n, prose_error_string(err));
	prose_close(h);
	return err ? 1 : 0;
}

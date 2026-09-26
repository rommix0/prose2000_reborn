/* A custom glottal pulse (API.md): makes one Rosenberg period as pulse.wav, loads it, and speaks a sentence with it
 * (custom.wav) and with the Prose's own pulse (original.wav).
 *
 *   glottal_pulse [v11] */
#include "prose.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int main(int argc, char **argv)
{
	int version = argc > 1 && !strcmp(argv[1], "v11") ? PROSE_V11 : PROSE_V341;

	/* a period for testing: a Rosenberg pulse, 40 % rising, 16 % falling, then closed */
	enum { N = 100 }; /* one 10 ms period at 10 kHz */
	int16_t period[N];
	for (int i = 0; i < N; i++) {
		double t = (double)i / N, open = 0.40, close = 0.16;
		double g = t < open ? 0.5 * (1 - cos(M_PI * t / open)) : t < open + close ? cos(M_PI / 2 * (t - open) / close) : 0;
		period[i] = (int16_t)(g * 32000);
	}
	int err = prose_save_wave("pulse.wav", period, N);
	if (err) {
		printf("pulse.wav: %s\n", prose_error_string(err));
		return 1;
	}

	prose_h h;
	if (prose_open(&h, version))
		return 1;
	err = prose_load_glottal_wave(h, "pulse.wav", PROSE_GLOTTAL_FLOW);
	printf("load pulse.wav: %s\n", prose_error_string(err));
	if (err == 0)
		prose_use_custom_glottal(h, 1);
	err = prose_speak_to_wave(h, "custom.wav", "This voice uses a custom glottal pulse.");
	printf("custom.wav: %s\n", prose_error_string(err));

	prose_use_custom_glottal(h, 0); /* back to the Prose's own pulse */
	err = prose_speak_to_wave(h, "original.wav", "This one uses the original.");
	printf("original.wav: %s\n", prose_error_string(err));
	prose_close(h);
	return err ? 1 : 0;
}

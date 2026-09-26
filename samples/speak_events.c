/* Speaking with events (API.md): prose_speak with an index marker, then a WAV file and the phonemes.
 *
 *   speak_events [v11]
 *
 * Writes out.wav. The two sentences play on the sound device; without one they are skipped. */
#ifdef _WIN32
#include <windows.h>
#define sleep_ms(ms) Sleep(ms)
#else
#define _POSIX_C_SOURCE 200809L
#include <time.h>
static void sleep_ms(int ms)
{
	struct timespec t = {0, ms * 1000000L};
	nanosleep(&t, NULL);
}
#endif

#include "prose.h"

#include <stdio.h>
#include <string.h>

typedef struct {
	int last_index;
	volatile int done;
} app_state;

static void my_index(prose_h h, int n, uint32_t pos, void *user)
{
	app_state *s = user;
	(void)h;
	s->last_index = n;
	printf("  marker %d at %.2f s\n", n, pos / 10000.0);
}

static void my_done(prose_h h, int last_index, uint32_t total, void *user)
{
	app_state *s = user;
	(void)h;
	printf("  done: last marker %d, %.2f s of audio\n", last_index, total / 10000.0);
	s->done++;
}

static app_state state; /* must outlive the callbacks */

int main(int argc, char **argv)
{
	int version = argc > 1 && !strcmp(argv[1], "v11") ? PROSE_V11 : PROSE_V341;
	prose_h h;
	int err = prose_open(&h, version);
	if (err) {
		printf("prose_open: %s\n", prose_error_string(err));
		return 1;
	}
	prose_callbacks cb = {0};
	cb.on_index = my_index;
	cb.on_done = my_done;
	prose_set_callbacks(h, &cb, &state);

	prose_set_rate(h, 180);
	prose_set_volume(h, 12);
	printf("speaking two sentences\n");
	err = prose_speak(h, "Hello there.");
	if (!err) {
		prose_index(h, 5);
		prose_speak(h, "Second sentence.");
		while (state.done < 2) /* prose_speak returns at once; wait for both */
			sleep_ms(20);
	} else {
		printf("  prose_speak: %s\n", prose_error_string(err));
	}

	printf("writing out.wav\n");
	err = prose_speak_to_wave(h, "out.wav", "Saved to a file.");
	if (err)
		printf("  prose_speak_to_wave: %s\n", prose_error_string(err));

	char ph[256];
	int need = prose_text_to_phoneme(h, "Hello.", ph, sizeof ph);
	printf("phonemes of \"Hello.\": \"%s\" (%d characters)\n", need >= 0 ? ph : "", need);

	prose_close(h);
	return err ? 1 : 0;
}

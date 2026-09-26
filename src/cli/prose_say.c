/*
 * prose_say: speaks text with the Prose library (prose.dll / libprose.so), to the speaker or to a WAV file.
 *
 *   prose_say [-1] [-w OUT.wav] [-r WPM] [-p PITCH] [-a VOLUME] [-v VOICE] [-t] [TEXT...]
 *
 * With no TEXT it reads the text from standard input. -1 uses the v1.1 firmware (default v3.4.1). -t prints the
 * phonemes instead of speaking. Escape sequences in the text are passed on: write them as \e (e.g. "\e[5i").
 */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#endif

#include "prose.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile int done;

static void on_done(prose_h h, int last_index, uint32_t total, void *user)
{
	(void)h;
	(void)last_index;
	(void)total;
	(void)user;
	done = 1;
}

static void sleep_ms(int ms)
{
#ifdef _WIN32
	Sleep((DWORD)ms);
#else
	struct timespec t = {0, ms * 1000000L};
	nanosleep(&t, NULL);
#endif
}

static void usage(void)
{
	fprintf(stderr, "usage: prose_say [-1] [-w OUT.wav] [-r WPM] [-p PITCH] [-a VOLUME] [-v VOICE] [-t] [TEXT...]\n"
	                "  -1         the v1.1 firmware (default v3.4.1)\n"
	                "  -w FILE    write a 10 kHz WAV file instead of playing\n"
	                "  -r WPM     rate, 50-250\n"
	                "  -p PITCH   pitch, 50-200\n"
	                "  -a VOLUME  volume, 0-15 (larger is louder)\n"
	                "  -v VOICE   voice, 0-2 (v3.4.1)\n"
	                "  -t         print the phonemes instead of speaking\n"
	                "Without TEXT the text is read from standard input. \\e in the text is ESC.\n");
}

/* appends s to the growing text, with \e as ESC */
static int append(char **text, size_t *len, size_t *cap, const char *s)
{
	size_t n = strlen(s);
	if (*len + n + 2 > *cap) {
		size_t c = *cap ? *cap * 2 : 256;
		while (c < *len + n + 2)
			c *= 2;
		char *p = realloc(*text, c);
		if (!p)
			return -1;
		*text = p;
		*cap = c;
	}
	for (; *s; s++)
		if (s[0] == '\\' && s[1] == 'e') {
			(*text)[(*len)++] = 0x1B;
			s++;
		} else {
			(*text)[(*len)++] = *s;
		}
	(*text)[*len] = 0;
	return 0;
}

int main(int argc, char **argv)
{
	const char *wav = NULL;
	int version = PROSE_V341, rate = -1, pitch = -1, volume = -1, voice = -1, phonemes = 0, i;
	for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
		const char *o = argv[i];
		if (!strcmp(o, "--")) {
			i++;
			break;
		} else if (!strcmp(o, "-1")) {
			version = PROSE_V11;
		} else if (!strcmp(o, "-t")) {
			phonemes = 1;
		} else if (i + 1 < argc && (!strcmp(o, "-w") || !strcmp(o, "-r") || !strcmp(o, "-p") || !strcmp(o, "-a") ||
		                            !strcmp(o, "-v"))) {
			const char *v = argv[++i];
			switch (o[1]) {
			case 'w': wav = v; break;
			case 'r': rate = atoi(v); break;
			case 'p': pitch = atoi(v); break;
			case 'a': volume = atoi(v); break;
			default: voice = atoi(v); break;
			}
		} else {
			usage();
			return 2;
		}
	}

	char *text = NULL;
	size_t len = 0, cap = 0;
	if (i < argc) {
		for (; i < argc; i++)
			if (append(&text, &len, &cap, argv[i]) || (i + 1 < argc && append(&text, &len, &cap, " ")))
				return 1;
	} else {
		char line[4096];
		while (fgets(line, sizeof line, stdin))
			if (append(&text, &len, &cap, line))
				return 1;
	}
	if (!text || !*text) {
		usage();
		return 2;
	}

	prose_h h;
	int err = prose_open(&h, version);
	if (err) {
		fprintf(stderr, "prose_open: %s\n", prose_error_string(err));
		return 1;
	}
	if (rate >= 0)
		prose_set_rate(h, rate);
	if (pitch >= 0)
		prose_set_pitch(h, pitch);
	if (volume >= 0)
		prose_set_volume(h, volume);
	if (voice >= 0)
		prose_set_voice(h, voice);

	if (phonemes) {
		int n = prose_text_to_phoneme(h, text, NULL, 0);
		char *out = n >= 0 ? malloc((size_t)n + 1) : NULL;
		if (n < 0 || !out || (err = prose_text_to_phoneme(h, text, out, (size_t)n + 1)) < 0) {
			fprintf(stderr, "prose_text_to_phoneme: %s\n", prose_error_string(n < 0 ? n : err));
			err = 1;
		} else {
			printf("%s\n", out);
			err = 0;
		}
		free(out);
	} else if (wav) {
		if ((err = prose_speak_to_wave(h, wav, text)) != 0)
			fprintf(stderr, "prose_speak_to_wave: %s\n", prose_error_string(err));
	} else {
		prose_callbacks cb = {0};
		cb.on_done = on_done;
		prose_set_callbacks(h, &cb, NULL);
		if ((err = prose_speak(h, text)) != 0)
			fprintf(stderr, "prose_speak: %s\n", prose_error_string(err));
		else
			while (!done)
				sleep_ms(20);
	}
	prose_close(h);
	free(text);
	return err ? 1 : 0;
}

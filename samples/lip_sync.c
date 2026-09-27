/* Lip sync with on_phoneme (API.md): a mouth drawn in the terminal changes shape with each phoneme as it is spoken.
 *
 *   lip_sync [v11] [TEXT]
 *
 * on_phoneme fires as each phoneme starts playing on the sound device. The callback maps the Prose phoneme to one of
 * ten mouth shapes (the Preston Blair visemes used by lip-sync tools such as Papagayo) and redraws the mouth. With no
 * sound device it prints the timeline instead: when each mouth shape starts, as an animation program would take it. */
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

enum { REST, MBP, FV, L, WQ, U, O, E, AI, ETC, VISEMES };

static const char *const viseme_name[VISEMES] = {"rest", "MBP", "FV", "L", "WQ", "U", "O", "E", "AI", "etc"};

/* Each shape is 5 lines of 13 characters. */
static const char *const mouth[VISEMES][5] = {
    {"             ", "             ", "  (-------)  ", "             ", "             "}, /* rest */
    {"             ", "             ", " (=========) ", "             ", "             "}, /* MBP: lips pressed */
    {"             ", "  .-------.  ", " ( VVVVVVV ) ", "  '~~~~~~~'  ", "             "}, /* FV: lip under teeth */
    {"             ", "  .-------.  ", " (  ##^##  ) ", "  '-------'  ", "             "}, /* L: tongue up */
    {"             ", "      .      ", "     (o)     ", "      '      ", "             "}, /* WQ: tight round */
    {"             ", "     .-.     ", "    (   )    ", "     '-'     ", "             "}, /* U: pursed */
    {"    .---.    ", "   /     \\   ", "  |       |  ", "   \\     /   ", "    '---'    "}, /* O: round */
    {"             ", " .---------. ", "( ######### )", " '---------' ", "             "}, /* E: wide */
    {"   .-----.   ", "  /       \\  ", " |         | ", "  \\       /  ", "   '-----'   "}, /* AI: open */
    {"             ", "  .-------.  ", " ( ####### ) ", "  '-------'  ", "             "}, /* etc: teeth */
};

/* The Prose's phoneme codes (API.md, Phonemes) as mouth shapes, after Papagayo's table for the ARPABET phonemes. */
static int viseme_of(char ph)
{
	switch (ph) {
	case 'P': case 'B': case 'M': case 'm':
		return MBP;
	case 'F': case 'V':
		return FV;
	case 'L': case 'j': case 'l':
		return L;
	case 'W': case 'h':
		return WQ;
	case 'u': case 'b': case 'U': case 'c':
		return U;
	case 'O': case 'w': case 'g': case 'y': case 'f':
		return O;
	case 'E': case 'A': case 'e': case 'k': case '3':
		return E;
	case 'a': case 'o': case 'v': case '@': case 'i': case '|': case 'I': case 'r': case '4':
		return AI;
	case ' ':
		return REST;
	default: /* T D t q K G C s J z X x S Z H d N n ~ R Y Q, and the release vocoid p */
		return ETC;
	}
}

typedef struct {
	volatile int done;
	int drawn;
} app_state;

/* Redraw the mouth in place: back up over the last drawing with ANSI cursor movement. */
static void show_mouth(app_state *s, int v, char ph)
{
	if (s->drawn)
		printf("\x1b[6A");
	for (int i = 0; i < 5; i++)
		printf("\r\x1b[2K        %s\n", mouth[v][i]);
	printf("\r\x1b[2K        %-4s  %c\n", viseme_name[v], ph == ' ' ? '_' : ph);
	fflush(stdout);
	s->drawn = 1;
}

/* prose_speak's callbacks run on the library's audio thread. A GUI program would post the shape to its UI thread
 * here instead of drawing. */
static void on_phoneme(prose_h h, char ph, int ms, uint32_t pos, void *user)
{
	(void)h, (void)ms, (void)pos;
	show_mouth(user, viseme_of(ph), ph);
}

static void on_done(prose_h h, int last_index, uint32_t total, void *user)
{
	app_state *s = user;
	(void)h, (void)last_index, (void)total;
	show_mouth(s, REST, ' ');
	s->done = 1;
}

/* without a sound device: the timeline, one line per change of shape */
static int last_viseme = -1;
static void print_timeline(prose_h h, char ph, int ms, uint32_t pos, void *user)
{
	(void)h, (void)user;
	int v = viseme_of(ph);
	if (v != last_viseme)
		printf("  %6.3f s  %-4s  (%c, %d ms)\n", pos / (double)PROSE_SAMPLE_RATE, viseme_name[v], ph == ' ' ? '_' : ph,
		       ms);
	last_viseme = v;
}

static int no_audio(prose_h h, const int16_t *pcm, size_t count, uint32_t pos, void *user)
{
	(void)h, (void)pcm, (void)count, (void)pos, (void)user;
	return 0;
}

static app_state state; /* must outlive the callbacks */

int main(int argc, char **argv)
{
	int version = PROSE_V341, arg = 1;
	if (argc > arg && !strcmp(argv[arg], "v11")) {
		version = PROSE_V11;
		arg++;
	}
	const char *text = argc > arg ? argv[arg] : "Hello, my friend. How are you today?";
#ifdef _WIN32
	/* ANSI cursor movement in the Windows console */
	HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD mode;
	if (GetConsoleMode(out, &mode))
		SetConsoleMode(out, mode | 0x0004); /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */
#endif
	prose_h h;
	int err = prose_open(&h, version);
	if (err) {
		printf("prose_open: %s\n", prose_error_string(err));
		return 1;
	}
	prose_callbacks cb = {0};
	cb.on_phoneme = on_phoneme;
	cb.on_done = on_done;
	prose_set_callbacks(h, &cb, &state);

	printf("\"%s\"\n\n", text);
	err = prose_speak(h, text);
	if (!err) {
		while (!state.done) /* prose_speak returns at once */
			sleep_ms(20);
	} else {
		printf("prose_speak: %s\nthe mouth shapes instead:\n", prose_error_string(err));
		prose_callbacks timeline = {0};
		timeline.on_phoneme = print_timeline;
		prose_set_callbacks(h, &timeline, NULL);
		prose_speak_to_buffer(h, text, NULL, 0, no_audio, NULL);
	}
	prose_close(h);
	return 0;
}

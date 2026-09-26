/*
 * Speaks text with the decompiled v1.1 firmware, entirely in C, and plays the result.
 *
 *   v1_pipeline_play -s TEXT [-p N] [-w OUT.wav] [-f FRAMES.txt] [-q]
 *
 * The run starts from the C power-up (v1_power_up). The program plays the host: it sends TEXT and a CR to
 * host_rx_char, pausing while the firmware has sent XOFF. TEXT may hold escape sequences (\e = ESC); ^R and ESC[nT
 * restart the synthesis loop. Between frame requests it runs synthesis_main's loop (F7204) until the loop would idle,
 * and at each request the frame interrupt (dsp_irq_service EC1E0), which sends the frame built at the previous
 * request and builds the next. v1.1's DSP program is lost, so each 37-word frame is mapped onto the v3.12 DSP model
 * (v1_map) and prose_synth makes the audio: an approximation of v1.1's sound (REFERENCE §16).
 *
 * -p N paces the host at N characters per 10 ms frame (a serial line; 10 is about 9600 baud); by default all of the
 * text is sent at once, as fast as the firmware takes it. -w saves the audio, -f writes each 37-word frame sent (hex, one line per frame), -q skips playback.
 */
#include "dsp_rom.h"
#include "prose_synth.h"
#include "prose_wave.h"
#include "v1.h"
#include "v1_map.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static v1_map map;
static uint16_t queue[64];
static int q_len, q_pos, booted, finished, missed;
static size_t produced_now, last_request = (size_t)-1, end_sample;
static FILE *frames_out;
static const char *host_text; /* text not yet in the receive ring */
static int pace, budget;      /* -p N: the host sends at most N characters per frame (0: all at once) */

/* The transmit interrupt: what the firmware sends the host (replies, index markers, XON/XOFF). */
static char host_out[4096];
static size_t host_out_len;
static void on_tx(int c)
{
	if (host_out_len < sizeof host_out - 1)
		host_out[host_out_len++] = (char)c;
}

static void drain_output(void)
{
	while (rw(V1_TX_RUNNING))
		v1_uart_tx_isr();
}

/* The host: send text to the serial interrupt's handler until the firmware sends XOFF. */
static void feed_host_text(void)
{
	while (host_text && *host_text && !rw(V1_INPUT_XOFF) && (!pace || budget > 0)) {
		v1_host_rx_char((unsigned char)*host_text++, 0);
		budget--;
	}
}

/* ^R and ESC[nT: loop_restart, then synthesis_main starts again */
static void on_restart(int code)
{
	(void)code;
	v1_loop_restart();
	v1_loop_entry();
	drain_output();
}

static void on_fatal(int code)
{
	fprintf(stderr, "fatal_error(0x%X)\n", code);
	exit(1);
}

/* synthesis_main's loop between two frame requests, until it would idle */
static void schedule(void)
{
	for (int k = 0; k < 4096; k++) {
		feed_host_text();
		drain_output();
		if (v1_loop_check_stop())
			drain_output();
		if (!v1_loop_pass() && !v1_idle_has_work())
			break;
	}
	drain_output();
}

/* dsp_write_frame: the frame goes to the DSP */
static void on_frame(const uint16_t *frame, int words)
{
	(void)words;
	if (frames_out) {
		for (int i = 0; i < 37; i++)
			fprintf(frames_out, "%04X%c", frame[i], i < 36 ? ' ' : '\n');
	}
	v1_map_frame(&map, frame, queue);
	q_len = 40;
	q_pos = 0;
	missed = 0;
	end_sample = produced_now;
}

static int idle(void)
{
	return (!host_text || !*host_text) && rw(V1_INPUT_EMPTY) && !rw(V1_RUN_TEXTRULES) && !rw(V1_RUN_LEXICAL) &&
	       !rw(V1_RUN_PROSODY) && !rw(V1_RUN_PARAMGEN) && !rw(V1_TRACKS_FULL);
}

static int poll(void *user, size_t produced, uint16_t *word)
{
	(void)user;
	if (q_pos == q_len) {
		q_len = q_pos = 0;
		if (!booted) {
			v1_map_boot_block(&map, queue);
			q_len = 9;
			booted = 1;
		} else {
			/* one interrupt per frame request (the DSP polls again once after a miss, without a new one) */
			if (finished || produced == last_request)
				return 0;
			last_request = produced_now = produced;
			budget = pace;
			schedule();
			v1_dsp_status = 0xA0; /* USF0 and RQM */
			v1_dsp_irq_service();
			if (!q_len)
				return 0;
		}
	}
	*word = queue[q_pos++];
	return 1;
}

static void timeout(void *user, size_t produced)
{
	(void)user;
	(void)produced;
	if (++missed >= 30 && idle())
		finished = 1;
}

int main(int argc, char **argv)
{
	const char *wav = NULL, *frames = NULL, *say = NULL;
	int quiet = 0;
	for (int i = 1; i < argc; i++)
		if (!strcmp(argv[i], "-w") && i + 1 < argc)
			wav = argv[++i];
		else if (!strcmp(argv[i], "-f") && i + 1 < argc)
			frames = argv[++i];
		else if (!strcmp(argv[i], "-p") && i + 1 < argc)
			pace = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-q"))
			quiet = 1;
		else if (!strcmp(argv[i], "-s") && i + 1 < argc)
			say = argv[++i];
	if (!say) {
		fprintf(stderr, "usage: v1_pipeline_play -s TEXT [-p N] [-w OUT.wav] [-f FRAMES.txt] [-q]\n");
		return 2;
	}
	static prose_rom rom;
	uint16_t dsp_rom[512];
	prose_rom_builtin(&rom);
	prose_dsp_builtin_data_rom(dsp_rom);
	v1_map_init(&map, &rom);

	v1_load_rom();
	v1_fatal_hook = on_fatal;
	v1_tx_hook = on_tx;
	v1_restart_hook = on_restart;
	v1_frame_hook = on_frame;
	memset(v1_ds + V1_DS_RAM_LO, 0, 0x3000); /* power-on RAM as in the emulator */
	v1_power_up();
	drain_output();

	static char text[4096];
	size_t n = 0;
	const char *c = say;
	for (; *c && n < sizeof text - 2; c++) /* \e = ESC */
		if (c[0] == '\\' && c[1] == 'e') {
			text[n++] = 0x1B;
			c++;
		} else {
			text[n++] = *c;
		}
	if (*c) {
		fprintf(stderr, "the text is too long (at most %u characters)\n", (unsigned)sizeof text - 2);
		return 2;
	}
	text[n++] = '\r';
	text[n] = 0;
	host_text = text;
	if (frames && !(frames_out = fopen(frames, "w")))
		return 2;

	size_t total = 600 * 10000; /* 10 minutes at most */
	int16_t *out = calloc(total, 2);
	prose_synth *s = prose_synth_create(dsp_rom);
	prose_synth_set_host(s, poll, timeout, NULL);
	prose_synth_run(s, out, total);
	prose_synth_destroy(s);
	if (frames_out)
		fclose(frames_out);
	n = end_sample + 3000 < total ? end_sample + 3000 : total;
	printf("%zu samples (%.2f s)\n", n, n / 10000.0);
	drain_output();
	if (host_out_len) { /* what the firmware sent to the host, escapes shown as \e */
		printf("sent: ");
		for (size_t i = 0; i < host_out_len; i++)
			if (host_out[i] == 0x1B)
				printf("\\e");
			else if ((unsigned char)host_out[i] < 0x20)
				printf("\\x%02X", (unsigned char)host_out[i]);
			else
				putchar(host_out[i]);
		putchar('\n');
	}
	if (wav) {
		int16_t *pcm = malloc(n * 2);
		for (size_t i = 0; i < n; i++)
			pcm[i] = prose_dac_to_pcm((uint16_t)out[i]);
		if (prose_wav_save(wav, pcm, n, 10000))
			fprintf(stderr, "cannot write %s\n", wav);
		free(pcm);
	}
	if (!quiet && prose_wave_play_dsp(out, n))
		fprintf(stderr, "cannot open the audio output\n");
	free(out);
	return 0;
}

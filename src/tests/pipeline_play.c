/*
 * Runs the decompiled pipeline down to DAC samples and plays the result.
 *
 *   pipeline_play [CAPTURE.bin] [-s TEXT | -t] [-w OUT.wav] [-f FRAMES.txt] [-q]
 *
 * CAPTURE.bin is a stage capture (see stage_replay.c); only the RAM of one record is used.
 * - -s TEXT speaks TEXT from the start: from the C power-up, or, given a capture of the input stage, from the RAM of
 *   its first record (the idle firmware). The program plays the host: it sends TEXT and a CR to host_rx_char, the
 *   serial interrupt's handler, pausing while the firmware has sent XOFF. TEXT may hold escape sequences (\e or ESC).
 *   ^R and ESC[W restart the synthesis loop (loop_entry).
 * - -t starts from a capture of the text-rules stage, at the first text-rules step with the receive ring empty.
 * - Otherwise CAPTURE.bin is a capture of the prosody stage and the run starts from the RAM before the first step
 *   that begins timing a phrase. Only text that had reached the node list is spoken: when the text arrived slowly, a
 *   long phrase that hits the phoneme limit or fast-start mode (N14) leaves the rest out.
 * From there everything is C: the input, text-rules, lexical, prosody and parameter-generator stages scheduled as the
 * synthesis loop (E39A7) does, the frame interrupt (dsp_irq_service: it sends the frame built at the previous request
 * and builds the next) each time the DSP asks for a frame, and prose_synth for the audio.
 *
 * -w saves the audio, -f writes one line per frame ("dd82 mark alt t0..t21") for comparison with a frame capture of
 * the same text, -q skips playback.
 */
#include "capture.h"
#include "dsp_rom.h"
#include "frame_build.h"
#include "input.h"
#include "lexical.h"
#include "pg.h"
#include "prose_synth.h"
#include "prose_wave.h"
#include "prosody.h"
#include "textrules.h"

#include <stdlib.h>

/* the synthesis loop's pending flags */
#define TEXT_PENDING 0xEE60
#define LEXICAL_PENDING 0xEE62
#define PROSODY_PENDING 0xEE64
#define PARAMGEN_PENDING 0xEE66
#define PARAMGEN_WAIT 0xEE68 /* paramgen_run returned 1: wait for ring room */
#define FRAMES_JUST_WRITTEN 0xDD9C

static unsigned ram_word(const uint8_t *ram, unsigned ds)
{
	return ram[ds - DS_RAM_LO] | ram[ds - DS_RAM_LO + 1] << 8;
}

static const prose_rom *rom_ptr;
static uint16_t queue[64];
static int q_len, q_pos, booted, finished;
static size_t produced_now, last_request = (size_t)-1;
static int missed;
static size_t end_sample;
static FILE *frames_out;
static const char *host_text; /* -s: text not yet in the receive ring */

/* The transmit interrupt: send what the firmware has for the host (replies, index markers, XON/XOFF). */
static char host_out[4096];
static size_t host_out_len;
static void on_tx(int c)
{
	if (host_out_len < sizeof host_out - 1)
		host_out[host_out_len++] = (char)c;
}

static void drain_output(void)
{
	while (rw(TX_RUNNING))
		if (tx_send() < 0)
			break;
}

/* The host: send text to the serial interrupt's handler until the firmware sends XOFF. */
static void feed_host_text(void)
{
	while (host_text && *host_text && !rw(INPUT_XOFF))
		host_rx_char((unsigned char)*host_text++, 0);
}

/* ^R and ESC[W restart the synthesis loop */
static void on_restart(int code)
{
	loop_entry(code);
	drain_output();
}

/* ESC[nL: the stub at 0000:0024 writes n to port 3402 and jumps to the reset vector: a cold boot */
static void on_int8(int n)
{
	wb(0xF302, n);
	power_up(rom_ptr);
	drain_output();
}

/* One stage of E39A7: run it if enough nodes are free. Returns 0 when the loop must idle instead. */
static int run_stage(int (*stage)(void), int pending, int next_pending, int min_free)
{
	if (rw(FREE_COUNT) <= min_free)
		return 0;
	if (stage())
		ww(next_pending, 1);
	else
		ww(pending, 0);
	return 1;
}

/* The passes of E39A7 until there is nothing to do (the host link's transmitter is left out). */
static void schedule_pass(void)
{
	feed_host_text();
	for (int k = 0; k < 1024; k++) {
		drain_output();
		if (loop_check_stop()) { /* ESC[q, ESC[S, or an end of sentence reached playback: back to the start */
			drain_output();
			feed_host_text();
			continue;
		}
		if (rw(PLAYBACK_STATE) != 2)
			ww(PLAYBACK_STATE, stage_playback_run());
		if (rw(PARAMGEN_WAIT) && param_ring_ctl(1, rw(FRAMES_JUST_WRITTEN)))
			ww(PARAMGEN_WAIT, 0);
		if (!rw(PARAMGEN_WAIT) && rw(PARAMGEN_PENDING)) {
			int r = paramgen_run();
			if (r == 0)
				ww(PARAMGEN_PENDING, 0);
			else if (r == 1)
				ww(PARAMGEN_WAIT, 1);
			ww(PLAYBACK_STATE, stage_playback_run());
			continue;
		}
		if (rw(PROSODY_PENDING)) {
			if (run_stage(prosody_run, PROSODY_PENDING, PARAMGEN_PENDING, 0x69))
				continue;
		} else if (rw(LEXICAL_PENDING)) {
			if (run_stage(stage_lexical_run, LEXICAL_PENDING, PROSODY_PENDING, 0x25))
				continue;
		} else if (rw(TEXT_PENDING)) {
			if (run_stage(stage_text_rules_run, TEXT_PENDING, LEXICAL_PENDING, 0x25))
				continue;
		} else if (!rw(INPUT_IDLE) && rw(FREE_COUNT) > 0x0F) {
			if (stage_input_run())
				ww(TEXT_PENDING, 1);
			else
				ww(INPUT_IDLE, 1);
			feed_host_text();
			continue;
		}
		break;
	}
}

/* The idle routine (E3B9D): it spins until the loop has work again. Returns 1 if it would return now; otherwise the
 * loop waits for the next frame request. */
static int idle_has_work(void)
{
	for (int spin = 0; spin < 2; spin++) {
		if (!rw(INPUT_IDLE) && rw(FREE_COUNT) > 0x0F)
			return 1;
		if (!rw(PARAMGEN_WAIT) && rw(PARAMGEN_PENDING))
			return 1;
		if (rw(STOP_REQUEST) || rw(QUIT_REQUEST))
			return 1;
		if (rw(PLAYBACK_STATE) == 0 && rw(SEGMENTS_PLAYED)) /* a played segment to free */
			return 1;
		if (rw(PLAYBACK_STATE) == 1 && output_ring_free() > 6) /* a marker can be sent now */
			return 1;
		if (rw(INPUT_READ) != rw(INPUT_WRITE)) /* wake the input stage */
			ww(INPUT_IDLE, 0);
		/* release the wait once the ring has room, restart the generator when it runs low */
		if (rw(PARAMGEN_WAIT) &&
		    s16(rw(RING_HIGH) + rw(FRAMES_JUST_WRITTEN) + rw(RING_SLACK) - rw(RING_READ)) < 0x80)
			ww(PARAMGEN_WAIT, 0);
		if (rw(RING_HOLD) == -1 && s16(rw(RING_LOW) - rw(RING_LAG) - rw(RING_READ)) <= 8) {
			ww(PARAMGEN_PENDING, 1);
			return 1;
		}
	}
	return 0;
}

/* E39A7 between two frame requests */
static void schedule(void)
{
	for (int round = 0; round < 64; round++) {
		schedule_pass();
		if (!idle_has_work())
			break;
	}
}

/* dsp_write_frame: the frame goes to the DSP */
static int on_frame(const uint16_t *frame, int words)
{
	memcpy(queue, frame, 2 * (size_t)words);
	q_len = words;
	q_pos = 0;
	missed = 0;
	end_sample = produced_now;
	return 1;
}

static void on_frame_built(unsigned pos, int mark, int alt, const uint8_t t[22])
{
	if (!frames_out)
		return;
	fprintf(frames_out, "%u %d %d", pos, mark, alt);
	for (int p = 0; p < 22; p++)
		fprintf(frames_out, " %u", t[p]);
	fputc('\n', frames_out);
}

static int idle(void)
{
	return (!host_text || !*host_text) && rw(INPUT_IDLE) && !rw(TEXT_PENDING) && !rw(LEXICAL_PENDING) &&
	       !rw(PROSODY_PENDING) && !rw(PARAMGEN_PENDING) && !rw(PARAMGEN_WAIT);
}

static int poll(void *user, size_t produced, uint16_t *word)
{
	(void)user;
	if (q_pos == q_len) {
		q_len = q_pos = 0;
		if (!booted) {
			for (int i = 0; i < 9; i++) /* boot block */
				queue[q_len++] = (uint16_t)prose_ds_table(rom_ptr, 0x5644, i);
			booted = 1;
		} else {
			/* one interrupt per frame request (the DSP polls again once after a miss, without a new one) */
			if (finished || produced == last_request)
				return 0;
			last_request = produced_now = produced;
			schedule();
			dsp_status = 0xA0; /* USF0 and RQM */
			dsp_irq_service();
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

static void on_fatal(int code)
{
	fprintf(stderr, "fatal_error(0x%X)\n", code);
	exit(1);
}

int main(int argc, char **argv)
{
	const char *wav = NULL, *frames = NULL;
	int quiet = 0, from_text = 0;
	const char *say = NULL;
	const char *capture = argc > 1 && argv[1][0] != '-' ? argv[1] : NULL;
	for (int i = capture ? 2 : 1; i < argc; i++)
		if (!strcmp(argv[i], "-w") && i + 1 < argc)
			wav = argv[++i];
		else if (!strcmp(argv[i], "-f") && i + 1 < argc)
			frames = argv[++i];
		else if (!strcmp(argv[i], "-q"))
			quiet = 1;
		else if (!strcmp(argv[i], "-t"))
			from_text = 1;
		else if (!strcmp(argv[i], "-s") && i + 1 < argc)
			say = argv[++i];
	if (!capture && !say) {
		fprintf(stderr,
		        "usage: pipeline_play [CAPTURE.bin] [-s TEXT | -t] [-w OUT.wav] [-f FRAMES.txt] [-q]\n");
		return 2;
	}
	static prose_rom rom;
	uint16_t dsp_rom[512];
	prose_rom_builtin(&rom);
	prose_dsp_builtin_data_rom(dsp_rom);
	rom_ptr = &rom;
	boot_load_rom(&rom);
	pg_load_rom(&rom);
	lexical_load_rom(&rom);
	pg_fatal_hook = on_fatal;
	pg_host_send_hook = host_send;

	input_restart_hook = on_restart;
	input_int8_hook = on_int8;
	host_tx_hook = on_tx;
	dsp_write_hook = on_frame;
	frame_trace_hook = on_frame_built;

	/* the start: (-s) the first input step, (-t) the first text-rules step with the receive ring empty, or the
	 * first prosody step that starts walking a phrase */
	FILE *f = capture ? fopen(capture, "rb") : NULL;
	static record r;
	int found = !capture;
	while (f && read_record(f, &r))
		if (say ? r.func == 0xDE09D
		    : from_text
		        ? r.func == 0xD41A4 && ram_word(r.before, INPUT_READ) == ram_word(r.before, INPUT_WRITE) &&
		              ram_word(r.before, INPUT_AHEAD) == 0xFFFF
		        : (r.func == 0xD9274 || r.func == 0xD9281) && r.before[SCAN_STATE - DS_RAM_LO] != 2 &&
		              r.after[SCAN_STATE - DS_RAM_LO] == 2) {
			found = 1;
			break;
		}
	if (f)
		fclose(f);
	if (!found) {
		fprintf(stderr, "no %s in %s\n",
		        say         ? "input step"
		        : from_text ? "text-rules step with all of the text taken in"
		                    : "prosody step that starts a phrase",
		        capture);
		return 2;
	}
	if (!capture) {
		memset(pg_ds + DS_RAM_LO, 0, 0x3000); /* power-on RAM as in the emulator; the boot code fills 0-2BFF */
		power_up(&rom);
		drain_output();
	} else {
		pg_load_ram(r.before);
	}
	if (say) { /* the idle firmware; the text follows as the host sends it */
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
		if (capture) { /* empty the receive rings of the captured RAM */
			ww(INPUT_READ, 0);
			ww(INPUT_WRITE, 0);
			ww(INPUT_AHEAD, -1);
			ww(RX_READ, 0);
			ww(RX_WRITE, 0);
			ww(ESC_STATE, 1);
			ww(INPUT_IDLE, 1);
		}
	} else {
		ww(from_text ? TEXT_PENDING : PROSODY_PENDING, 1);
	}
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
	size_t n = end_sample + 3000 < total ? end_sample + 3000 : total;
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

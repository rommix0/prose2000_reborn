/* The DLL's internals: the handle, and the engine that runs a handle's copy of the firmware.
 *
 * The decompiled firmware keeps its state in one data-segment image per version (pg_ds, v1_ds), so the library has
 * one of each. A handle keeps its own copy of the RAM part (0x3000 bytes) and its DSP model; before the engine runs a
 * handle it swaps that handle's RAM in (engine_lock). Only one handle runs firmware code at a time, a few
 * milliseconds per call, so several handles can speak at once. Not part of the firmware. */
#ifndef PROSE_INT_H
#define PROSE_INT_H

#include "prose_sys.h" /* first: it sets the system API level */

#include "frame_build.h"
#include "prose.h"
#include "prose_synth.h"

#include <stdio.h>

#define PROSE_MAGIC 0x50524F53u /* "PROS" */

enum { EV_INDEX, EV_PHONEME, EV_PARAMS };

typedef struct {
	int type;
	uint32_t pos; /* samples from the start of the utterance */
	int n;        /* EV_INDEX: the marker */
	int ms;       /* EV_PHONEME: its length, -1 until the next phoneme starts */
	char ph;      /* EV_PHONEME */
	uint8_t p[PROSE_PARAM_MAX];
} prose_event;

typedef struct job {
	char *text;
	unsigned gen; /* the stop generation it was queued in */
	struct job *next;
} job;

struct prose_handle {
	uint32_t magic;
	int version;
	int nparams;
	sys_mutex lock;
	sys_cond cond;

	/* ---- set by the program (under lock) ---- */
	prose_callbacks cb;
	void *user;
	char *pending; /* escape commands for the settings and markers, sent ahead of the next text */
	size_t pending_len, pending_cap;
	uint8_t raw[PROSE_PARAM_MAX]; /* raw synthesis: the parameters of the next frame */
	int16_t custom[PROSE_SYNTH_PULSE_LEN];
	int has_custom, use_custom;
	unsigned custom_gen, custom_applied; /* custom_gen changes with the pulse or the switch */
	int fw_error;

	/* ---- jobs (under lock) ---- */
	job *jobs, **jobs_tail;
	int job_active;    /* a job is running (the audio thread's or a blocking call's) */
	int blocking;      /* that job is a blocking call */
	unsigned stop_gen; /* prose_stop and prose_reset add 1; a job from an older generation stops */
	int paused;
	int closing, reset_pending;
	sys_thread thread;
	int thread_started;
	sys_thread_id cb_thread; /* the thread running a callback of this handle, while cb_depth > 0 */
	int cb_depth;

	/* ---- the engine (under engine_lock) ---- */
	uint8_t ram[0x3000];
	unsigned dsp_status;
	int echo_on;       /* v3.4.1's phoneme echo (N-flag 16) as the DLL last set it */
	void *map;         /* v1.1: its v1_map */
	prose_synth *synth;
	uint16_t q[64];
	int q_len, q_pos, booted, misses;
	size_t last_request, req_produced, utt_start;
	int in_utt, finished, want_params, want_phoneme, capture;
	/* the utterance's text: segments end at each ESC[...x, and each waits for the reply to the one before */
	char *text;
	size_t text_len, text_pos, seg_end;
	int x_sent, x_replies, last_index; /* last_index: the last marker reached */
	int flushed;                       /* the end flush: 0 not yet, 1 ESC[C sent, 2 ESC[x sent */
	/* the host side of the serial line: replies from the firmware */
	int tx_state, tx_val, tx_first, tx_count;
	char *cap; /* text_to_phoneme: what the firmware echoed */
	size_t cap_len, cap_cap;
	prose_event *ev; /* events not delivered yet, in order */
	size_t ev_head, ev_len, ev_cap;
	int ev_phoneme;  /* index of the last phoneme event, whose length is still open, or -1 */
	int ev_failed;   /* out of memory: events were lost */

	/* ---- raw synthesis (only the thread running the job) ---- */
	frame_builder raw_fb;
	prose_synth *raw_synth;
	uint16_t raw_q[40];
	int raw_q_len, raw_q_pos, raw_have_frame, raw_booted;
	size_t raw_last;
	unsigned raw_custom_applied;
	sys_audio *dev; /* prose_speak's sound device, while the audio thread has jobs */
};

/* ---- the engine (prose_engine.c) ---- */
int engine_init(void);                    /* once: the ROM image and the hooks; 0 or PROSE_ERR_MEMORY */
int engine_open(prose_h h);               /* power-up of a new handle's firmware; 0 or an error */
void engine_close(prose_h h);
void engine_reboot(prose_h h);            /* power-up again (prose_reset, after a fatal error) */
/* Starts an utterance: text (with the DLL's escapes) goes to the firmware, and ESC[x is added at the end if the text
 * does not end with it. want_params / want_phoneme: collect those events. capture: keep the echoed phonemes. */
int engine_start(prose_h h, const char *text, int want_params, int want_phoneme, int capture);
/* Runs `count` samples (PCM) of the utterance; events go to h->ev. 0, or PROSE_ERR_FIRMWARE after a fatal error
 * (the firmware has been restarted). */
int engine_run(prose_h h, int16_t *pcm, size_t count);
int engine_finished(prose_h h);
void engine_abort(prose_h h);             /* stop the utterance (ESC[S) and run until the firmware is idle */
void engine_end(prose_h h);               /* the utterance is over: forget its text and events */
uint32_t engine_position(prose_h h);      /* samples of the utterance so far */
const uint16_t *engine_boot_block(void);  /* the v3.12 DSP's 9 setup words */
const prose_rom *engine_rom(void);
const uint16_t *engine_dsp_rom(void);    /* the v3.12 DSP's data ROM */

/* the version drivers (prose_eng3.c, prose_eng1.c), called with the engine lock held and h swapped in */
typedef struct {
	void (*init)(const prose_rom *rom);
	void (*power_up)(prose_h h);
	void (*save)(prose_h h);
	void (*load)(prose_h h);
	void (*send_char)(int c);  /* one byte from the host, as the serial interrupt receives it */
	int (*xoff)(void);         /* the firmware has sent XOFF */
	void (*request)(prose_h h); /* a frame request: the loop until it would idle, then the frame interrupt */
	int (*idle)(void);         /* the pipeline has nothing to do */
	void (*drain)(void);       /* send what the firmware has for the host */
	void (*boot_block)(prose_h h, uint16_t out[9]);
} engine_driver;
extern const engine_driver eng3_driver, eng1_driver;

/* called by the drivers' hooks */
void engine_tx(int c);                           /* a byte the firmware sends the host */
void engine_frame(const uint16_t frame[40]);     /* a frame for the DSP */
void engine_params(const uint8_t *p, int count); /* the parameter bytes of a frame just built */
void engine_segment(int ch);                     /* a segment starts playing */
void engine_fatal(int code);                     /* the firmware's fatal error; does not return */
void engine_feed(void);                          /* send the utterance's text while the firmware takes it */
prose_h engine_current(void);                    /* the handle whose firmware is running */

/* ---- parameters (prose_params.c) ---- */
void params_defaults(int version, uint8_t out[PROSE_PARAM_MAX]);
int params_max(int version, int p);
/* one raw frame (p0-p21, or v1.1's p0-p17) -> the 40-word DSP frame */
void params_build_frame(prose_h h, const uint8_t *p, uint16_t out[40]);
int glottal_load(const char *filename, int kind, int16_t out[PROSE_SYNTH_PULSE_LEN]);
int wav_write(const char *filename, const int16_t *pcm, size_t count);
void wav_header(uint8_t hdr[44], uint32_t samples);
int wav_put_samples(FILE *f, const int16_t *pcm, size_t count);

#endif

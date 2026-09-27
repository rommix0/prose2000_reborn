/*
 * Parameter generator (REFERENCE §12): the phoneme node list after prosody -> the 22 parameter tracks of §11.4.
 *
 * The firmware keeps all of its state in the data segment DS = F410, and the rule tables address that state by
 * DS offset (a rule action is {op, voice mask, value, DS address}). The C code therefore works on an image of the
 * 64 KB data segment: DS:0000-BEFF is ROM (linear F4100-FFFFF), DS:BF00-EEFF is the 12 KB SRAM (linear
 * 00000-02FFF), and everything above is I/O, which this code never needs to read. Writes outside the RAM window
 * are dropped, as on the board.
 *
 * Names follow REFERENCE.md. Variables whose role is still unclear keep their DS offset in the name (V_EAFE).
 * All arithmetic is 16-bit as on the 8086: values are stored with ww(), which truncates, and read back signed.
 */
#ifndef PG_H
#define PG_H

#include <stddef.h>
#include <stdint.h>

#include "prose_rom.h"

/* ---- data segment image ---- */
extern uint8_t pg_ds[0x10000];

#define DS_RAM_LO 0xBF00u /* DS:BF00 = linear 00000 */
#define DS_RAM_HI 0xEF00u /* DS:EF00 = linear 03000 (I/O) */

void pg_load_rom(const prose_rom *rom);       /* fills DS:0000-BEFF */
void pg_load_ram(const uint8_t ram[0x3000]);  /* fills DS:BF00-EEFF from a RAM snapshot */
void pg_save_ram(uint8_t ram[0x3000]);

static inline unsigned rb(unsigned a) { return pg_ds[a & 0xFFFFu]; }
static inline int rsb(unsigned a) { return (int8_t)pg_ds[a & 0xFFFFu]; }
static inline int rw(unsigned a) { return (int16_t)(pg_ds[a & 0xFFFFu] | pg_ds[(a + 1) & 0xFFFFu] << 8); }
static inline unsigned ruw(unsigned a) { return (uint16_t)rw(a); }
static inline void wb(unsigned a, int v)
{
	a &= 0xFFFFu;
	if (a >= DS_RAM_LO && a < DS_RAM_HI)
		pg_ds[a] = (uint8_t)v;
}
static inline void ww(unsigned a, int v)
{
	wb(a, v);
	wb(a + 1, v >> 8);
}
static inline int s16(int v) { return (int16_t)v; } /* wrap an intermediate to 16 bits */

/* ---- the node list (REFERENCE §12.1) ---- */
/* Node layout: +0 link to the next (later) node, +2 link to the previous node, +4 flags (bits 0-2 kind, 3-4 stress,
 * 5 in a stressed syllable (the vowel and its onset), 6 glottal onset (lexical allophone rules), 7 given values relative (input) / the phrase's accent
 * (after prosody)), +6 low byte duration in frames (a command's value
 * before prosody), +8 F0/2, +9 phoneme char. */
#define N_FLAGS 4
#define N_DUR 6
#define N_F0 8
#define N_CHAR 9

/* Kinds: 0 an in-band command (char = command letter, +6 = its value), 2 a text character (a letter, or the word
 * record that heads a word, before the lexical stage), 3 a symbol (a phoneme before prosody, a word boundary % &,
 * punctuation), 4 a timed segment (a phoneme after prosody, or a pause ' '), 5 a hold (the g, s and t commands,
 * converted by the prosody stage), 6 free. */
enum { NODE_COMMAND = 0, NODE_TEXT = 2, NODE_SYMBOL = 3, NODE_SEGMENT = 4, NODE_HOLD = 5, NODE_FREE = 6 };

static inline int node_kind(int n) { return rw(n + N_FLAGS) & 7; }
static inline int node_char(int n) { return rsb(n + N_CHAR); }
static inline int node_stress(int n) { return (ruw(n + N_FLAGS) >> 3) & 3; }
static inline int node_bit(int n, int bit) { return (ruw(n + N_FLAGS) >> bit) & 1; }
static inline int node_dur(int n) { return rw(n + N_DUR) & 0xFF; }

/* The pipeline stage that owns the window being walked (set by the stage update, D3B66). */
#define STAGE_CUR 0xC202
#define FREE_COUNT 0xC204
#define OUTPUT_HOLD 0xDBCC /* frames are held back while set: ESC[H, '<' in phoneme input; ESC[C, x, '>' clear it */
#define FREE_TAIL 0xC2C8
#define FREE_HEAD 0xC2CA
#define LIST_HEAD 0xC2B0 /* points to the node list's head node */

/* A stage record (0x22 bytes, one per pipeline stage from DS:C1E4): +0 first node of the stage's window, +2 prev,
 * +4 cur, +6 next (the stage's own cursors), +8 last node of the window. The parameter generator's is DS:C26C. */
#define PG_STAGE 0xC26C
#define NODE_PREV 0xC26E
#define NODE_CUR 0xC270
#define NODE_NEXT 0xC272
#define NODE_LAST 0xC274

/* Each stage record also carries the host settings as they stand at the stage's position in the text; in-band
 * command nodes update them as the stage reaches them (stage_run_command). */
enum {
	REC_INPUT = 0x0E,   /* I: phoneme input */
	REC_PROSODY = 0x10, /* P: low byte 0 word mode / 1 prosody, high byte a second parameter */
	REC_SPEED = 0x12,   /* v / r */
	REC_PITCH = 0x14,   /* p, or the voice's default pitch after V */
	REC_ATTEN = 0x16,   /* a */
	REC_FAST = 0x18,    /* f */
	REC_MODE = 0x1A,    /* N / F mode flags */
	REC_AFLAGS = 0x1C,  /* A / D flags */
	REC_VOICE = 0x20    /* V */
};

int node_next(int n);                                /* D38B8: follows +0; 0 past the window's last node */
int node_prev(int n);                                /* D36D2: follows +2; 0 before the window's first node */
int node_insert(int at, int after, int kind, int ch); /* D3614 */
int node_delete(int n, int forward);                  /* D3805 */
int stage_commit(void);                               /* D36FB */
int stage_run_command(void);                          /* D9D2B */
int stage_window_update(int stage);                   /* D3B66 */
int stage_playback_run(void);                         /* D40F4 */
#define SEGMENTS_PLAYED 0xDD9A /* segments whose last frame has been played (dsp_build_frame counts them) */

/* ---- host settings ---- */
#define ATTEN 0xC282   /* ESC[a */
#define VOICE 0xC28C   /* ESC[V */
#define MODE 0xC286    /* bit 13 fast response, bit 15 phoneme log */

/* ---- tables in ROM ---- */
#define PHONEME_MAP_PTR 0x944A /* -> DS:93CA, phoneme char -> index 0-57 */
#define FEATURES 0x00A8        /* feature table, planes at +0, +80, +100, +180, +200 */
#define RAMPS 0x9B9E           /* k-frame smoothing ramps, k = 0-20 */
#define BITMASK 0x9BC8         /* word table: 1 << i, i = 0..15 */
#define TRACK_DEFAULT 0x610C   /* default track byte per parameter */

static inline int phoneme_index(int ch) { return rsb(rw(PHONEME_MAP_PTR) + ch); }
static inline unsigned feature(int ch, int plane) { return rb(FEATURES + (unsigned)(ch | plane)); }
/* DS:98C2: phoneme index -> its class as a following sound: vowels 0 high front (4 E U), 1 other front (A a e i k |),
   2 back rounded (O b c g u w y), 3 central/low (I f r 3 @ o v); 4 glide/liquid, 5 p, 6 fricative, 7 d h H,
   8 stop/nasal/t Q q, 9 pause. */
static inline int next_class(int idx) { return rsb(0x98C2 + idx); }

/* ---- per-parameter segment structs (§12.2) ---- */
enum {
	P_AV, P_AF, P_AH, P_A2, P_A3, P_A4, P_A5, P_A6, P_AB, P_F1, P_F2, P_F3, P_F4, P_B1, P_B2, P_B3, P_FN, P_F0,
	P_SRC18, P_SRC19, P_SRC20, P_SRC21, NPARAM
};
enum { F_TYPE = 0, F_DURB = 2, F_DURF = 4, F_LEN = 6, F_LOCB = 8, F_ONSET = 10, F_TARGET = 12 };
#define PARAM(p, f) (0xDE22u + 14u * (unsigned)(p) + (f))

/* ---- the track ring (§11.4, §12.4) ---- */
#define RING_READ 0xDD82   /* frame being played */
#define RING_HOLD 0xDD84   /* -1, or the frame where playback must wait */
#define RING_LOW 0xDD86    /* lowest write position over all tracks */
#define RING_HIGH 0xDD88   /* highest write position */
#define RING_MARK 0xDD8A   /* one bit per ring frame: segment boundary */
#define RING_ALT 0xEAE8    /* one bit per ring frame: 10 Hz formant coding */
#define TRK_POS(p) (0xDD9Eu + 2u * (unsigned)(p))  /* write position */
#define TRK_PREV(p) (0xDDCAu + 2u * (unsigned)(p)) /* previous segment boundary */
#define TRK_BASE(p) (0xDDF6u + 2u * (unsigned)(p)) /* -> 128-byte track */

#define LOOKAHEAD 0xEAE0   /* frames of look-ahead: 20, or 4 in fast-response mode */
#define RING_SLACK 0xEAE2
#define RING_LAG 0xEAE4

/* ---- locus model (§12.5a) ---- */
/* Phoneme classes of prev and cur: 0 vowel, 1 voiced, 2 other, 3 closure. */
#define CLASS_PREV 0xEBAA
#define CLASS_CUR 0xEBAC
#define WEIGHT(p) (0xEB52u + 2u * (unsigned)(p))
#define LOCUS(p) (0xEB7Eu + 2u * (unsigned)(p))

/* ---- external hooks ---- */
/* fatal_error (D6400): the firmware counts the error, records its code and restarts the synthesis loop. */
extern void (*pg_fatal_hook)(int code);
void fatal_error(int code);
/* host_send (DE4C6), used by stage_run_command for index markers in the last stage. NULL counts as sent. */
extern int (*pg_host_send_hook)(int kind, int ch, int count, const int *params);
/* Not in the firmware (for the DLL): called with the phoneme of each timed segment the playback stage passes, which
   is when the frame builder has started the segment. NULL: nothing. */
extern void (*pg_segment_hook)(int ch);
/* Not in the firmware (for tools such as formant_trace, FORMANT_SYSTEM.md): what the generator does while it builds a
 * segment. The hook reads the rest (positions, structs, loci) from the data segment. NULL in normal use. */
enum {
	PG_TR_SEGMENT,    /* a: the node, at the start of paramgen_segment */
	PG_TR_RULE,       /* a: the rule group, b: the index of the rule applied in it */
	PG_TR_ROUTINE,    /* a: a routine of the rule's list, by linear address */
	PG_TR_REDUCTION,  /* pg_vowel: a = the pull toward the neutral vowel, q15 */
	PG_TR_ONGLIDE,    /* pg_vowel: a diphthong's onglide is held a frames, then moves to the offglide over b */
	PG_TR_ASPIRATION, /* pg_shift_aspiration: AH starts a frames early, at TRK_POS(P_AH) */
	PG_TR_CLOSURE,    /* pg_stop_burst: a closure frames from TRK_POS(P_AF), then b frames of burst */
	PG_TR_RELEASE,    /* pg_release_onset: a frames of AV 0 from TRK_POS(P_AV), with aspiration b dB */
	PG_TR_EMIT,       /* the structs, weights and loci are final; the 22 tracks are written next */
};
extern void (*pg_trace_hook)(int event, int a, int b);
#define PG_TRACE(event, a, b)                                                                                          \
	do {                                                                                                           \
		if (pg_trace_hook)                                                                                     \
			pg_trace_hook((event), (a), (b));                                                              \
	} while (0)

/* ---- helpers ---- */
static inline int fx_mul_q15(int a, int b) { return (int16_t)(((int32_t)(int16_t)a * (int16_t)b) >> 15); } /* D3521 */

/* ---- functions ---- */
int paramgen_run(void);                      /* DCB00 */
int paramgen_step(void);                     /* DCB00 after its stage update */
void paramgen_reset(void);                   /* DD6A0 */
void param_tracks_init(void);                /* DC9B9 */
int param_ring_ctl(int op, int frames);      /* DCA52 */
int paramgen_hold(int mode, int frames, int room, int load); /* DCFA2 */
int paramgen_hold_step(void);                /* DCEEC */
int find_segment_end(void);                  /* DDE1F */
int insert_pause_pair(int at, int why);      /* DDAA2 */
void paramgen_clear_state(void);             /* DDB81 */
int track_byte_to_value(int p, int byte);    /* DDC0A */
void paramgen_advance(void);                 /* DDC70 */

void paramgen_segment(void);                 /* DD703 */
void paramgen_load_targets(void);            /* DD1C0 */
void paramgen_segment_setup(void);           /* DEB3A */
void paramgen_apply_rules(void);             /* DE96E */
void paramgen_rule_action(int list, int voice_mask); /* DE81F */

void param_emit_segment(int p);              /* D3D2F */
void track_ramp_then_hold(int track, int pos, int k, int count, int from, int to); /* D345F */
void track_blend_back(int track, int pos, int k, int count, int value); /* D3532 */
void track_blend_fwd(int track, int pos, int k, int count, int value);  /* D357B */
void track_fill(int track, int pos, int count, int value);              /* D3F36 */
void track_decay_back(int track, int p, int pos, int k, int count, int delta); /* D3F59 */

/* context rules (§12.5) and the locus model (§12.5a); called from the rule lists */
void pg_shift_aspiration(void);   /* D3FFB */
void pg_voiceless_onset(void);    /* DEBFA */
void pg_after_closure(void);      /* DEC87 */
void pg_sonorant_onset(void);     /* DEE85 */
void pg_vowel(void);              /* DF245 */
void pg_sonorant_consonant(void); /* DFB5B */
void pg_obstruent_voicing(void);  /* DFE87 */
void pg_fricative_amps(void);     /* E01C0 */
void pg_closure_types(void);      /* E067F */
void pg_stop_burst(void);         /* E08EA */
void pg_locus_weights(void);      /* E1024 */
void pg_consonant_loci(void);     /* E1A49 */
void pg_apply_loci(void);         /* E1C1A */
void pg_amplitude_boundaries(void); /* E2535 */
void pg_finalize(void);           /* E2FB8 */
void pg_release_onset(void);          /* E30C1 */

#endif

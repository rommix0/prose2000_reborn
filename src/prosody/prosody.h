/*
 * The prosody stage (REFERENCE §12.8): the stage just above the parameter generator. It walks the phoneme symbols
 * the lexical stages produced and, for each one, fixes a duration in frames (node +6) and an F0 target (node +8,
 * Hz / 2), turning it into a timed segment. On the way it splits long sentences into phrases, inserts the pauses
 * for punctuation and phrase breaks, merges geminate consonants and runs the in-band commands it meets.
 *
 * The durations follow Klatt's rule system (MITalk 1987, ch. 9): each phoneme has an inherent and a minimum
 * duration, the context rules multiply a percentage, and DUR = MINDUR + (INHDUR - MINDUR) * PRCNT / 100.
 * F0 is a declining phrase line with accents on stressed syllables.
 *
 * Like the parameter generator, this code works on the data-segment image of pg.h.
 */
#ifndef PROSODY_H
#define PROSODY_H

#include "pg.h"

/* ---- the stage record at DS:C24A (see pg.h) ---- */
#define PR_STAGE 0xC24A
#define PR_FIRST 0xC24C                     /* +2: first node of the phrase being timed */
#define PR_CUR 0xC24E                       /* +4: node being timed */
#define PR_SCAN 0xC250                      /* +6: phrase scan position */
#define PR_PROSODY (PR_STAGE + REC_PROSODY) /* low byte 0 = word mode; high byte = contour choice */
#define PR_SPEED (PR_STAGE + REC_SPEED)
#define PR_PITCH (PR_STAGE + REC_PITCH)
#define PR_MODE (PR_STAGE + REC_MODE)
#define PR_AFLAGS (PR_STAGE + REC_AFLAGS)
#define PR_VOICE (PR_STAGE + REC_VOICE)
#define PR_1E (PR_STAGE + 0x1E)

/* ---- phrase scan (prosody_scan_phrase) ---- */
#define POS_17 0xDC7A /* phoneme position of a class-17 word in the phrase (0 = none) */
#define POS_8 0xDC7C  /* ... class 8, class 7, class 6 (-1 = none) */
#define POS_7 0xDC7E
#define POS_6 0xDC80
#define CONTOUR 0xDC82     /* F0 contour type chosen from the positions above */
#define WORD_MODE 0xDC84   /* 1 after "P" with 0 (word mode) */
#define SCAN_STATE 0xDC86  /* 0 scanning, 1 C command, 2 timing, 3 sentence end, 4 length limit, 5 comma */
#define CARRY 0xDC88       /* phonemes carried over to the next phrase */
#define PHRASE_LEN 0xDC8A  /* phonemes in the phrase */
#define PHRASE_MAX 0xDC8C  /* scan limit in phonemes: 180, 70 or 5 */
#define STEP_BUDGET 0xDC8E /* nodes timed per call before the ring is checked */
#define FAST_START 0xDC90  /* mode flag 14 or N command bit 5: start speaking early */
#define FIRST_PHRASE 0xDC92
#define MIN_DUR 0xDC94 /* Klatt MINDUR x 10 */
#define INH_DUR 0xDC96 /* Klatt INHDUR x 10 */
#define DUR_SET 0xDC98 /* a duration given with the phoneme (phoneme input), else 0 */
#define GIVEN_PREV 0xDC9A
#define GIVEN 0xDC9C /* bit 0 duration given, 1 F0 given, 2 relative */
#define NEW_SENTENCE 0xDC9E
#define RATE_BREAKS 0xDCA0 /* r / v command below 9 seen: allow extra phrase breaks */
#define WORDS 0xDCA2       /* word-boundary nodes of the phrase, 50 max */
#define WORD_COUNT_PREV 0xDD06
#define WORD_COUNT 0xDD08
#define GIVEN_F0 0xDD0C
#define GIVEN_NODE 0xDD0E
#define GIVEN_CHAR 0xDD0A
#define BREAK_SEEN 0xDD10
#define IN_WORD 0xDD12
#define SCANNED 0xDD14 /* nodes scanned */
#define SCAN_PHONEMES 0xDD16
#define RUN 0xDD18 /* phonemes of the current word */
#define SCAN_LAST 0xDD1A
#define SAVED_STATE 0xDD1C
#define WALK 0xDD1E /* 1 start, 2 walking, 3 end of the phrase reached */
#define SAVED_SPEED 0xDD20

/* ---- the phoneme being timed (context_load) ---- */
#define NEXT_CH 0xDD22
#define NEXT_SYL 0xDD24
#define NEXT_B5 0xDD26
#define PREV_CH 0xDD28
#define PREV_SYL 0xDD2A
#define CUR_CH 0xDD2C
#define CUR_VOWEL 0xDD2E   /* +100 & 02 */
#define CUR_SYL 0xDD30     /* +0 & 01 */
#define CUR_B5 0xDD32      /* node bit 5 */
#define CUR_CLOSURE 0xDD34 /* +100 & 01 */
#define CUR_SON 0xDD36     /* +0 & 02 */
#define CUR_PHONEME 0xDD38 /* +0 & 80 */
#define CUR_NASAL 0xDD3A   /* +0 & 10 */
#define CUR_FRIC 0xDD3C    /* +0 & 40 */
#define CUR_AFFR 0xDD3E    /* +80 & 08 */
#define LOWER_F0 0xDD40
#define WORD_CLASS 0xDD42 /* 1 or 2 for a function word, else 0 */
#define NEXT_PLACE 0xDD44
#define ACCENT_NODE 0xDD46
#define ACCENT_DONE 0xDD48
#define ACCENT_HERE 0xDD4A
#define PHRASE_FINAL 0xDD4C
#define CLAUSE_FINAL 0xDD4E
#define F0_HOLD 0xDD50
#define PEAK 0xDD52
#define F0_POS 0xDD54
#define NEXT_NODE 0xDD56
#define PREV_NODE 0xDD58
#define NEXT_SEG 0xDD5A
#define PREV_SEG 0xDD5C
#define PRCNT 0xDD5E /* the duration percentages, multiplied together; PRCNT(0) is the result */
#define DECL_LOW 0xDD7C
#define BREAK_COUNT 0xDD7E

#define PRCNT_AT(i) (PRCNT + 2u * (unsigned)(i))

/* ---- tables in ROM ---- */
#define INH_TABLE 0xADE4   /* -> inherent duration per phoneme char, in 10 ms */
#define MIN_TABLE 0xAE46   /* -> minimum duration per phoneme char */
#define PLACE_TABLE 0xAEA8 /* -> place of articulation per phoneme char */
#define PAUSE_SCALE 0x6052 /* per speed: pause scale */
#define SPEED_PCT 0x6086   /* per speed: duration percentage */
#define CLUSTER_PCT 0x60D8 /* by NEXT_PLACE */
#define BREAK_MASK 0x60BA  /* per speed: bit mask of the phrase breaks to keep */
#define F0_SCALE 0x5378    /* per voice: Q15 accent scale */

/* ---- 16-bit arithmetic as the 8086 does it ---- */
/* mul; xor dx, dx; div: the product's low word divided unsigned */
static inline int mul_div_u(int a, int b, int d)
{
	return (uint16_t)((uint16_t)a * (uint16_t)b) / (uint16_t)d;
}
/* mul; cwd; idiv: the product's low word divided signed */
static inline int mul_div_s(int a, int b, int d)
{
	return (int16_t)((uint16_t)a * (uint16_t)b) / (int16_t)d;
}
static inline int percent(int a, int p)
{
	return mul_div_u(a, p, 100);
}

/* ---- functions ---- */
int prosody_run(void);           /* D9274 */
int prosody_step(void);          /* D9274 after its stage update */
void prosody_reset(void);        /* D9900 */
int prosody_scan_phrase(void);   /* D9492 */
int prosody_walk(void);          /* D9336 */
void prosody_time_segment(void); /* D9A20 */

int prev_symbol(int n);                                                        /* D9945 */
int node_has(int n, int test, int negate);                                     /* D997E */
int next_symbol(int n);                                                        /* D99F2 */
int context_search(int forward, int count, int want, int stop, int by_stress); /* D9A9D */
void given_values(int restore);                                                /* D9BE6 */
void context_load(void);                                                       /* D9F9A */

void phrase_breaks(int punct);                      /* DA101 */
void insert_phrase_break(int from, int ch, int at); /* DA770 */
void mark_break_neighbour(int n, int side);         /* DA811 */

void merge_geminate(void);     /* DA8AB */
int duration_rules(void);      /* DA9DC */
void klatt_duration(void);     /* DB4A6 */
void accent_context(void);     /* DBA35 */
void accent_reset(void);       /* DBC51 */
void f0_target(void);          /* DBC84 */
void segment_prosody(void);    /* DC1EA */
void insert_pause(int length); /* DC332 */
void cluster_duration(void);   /* DC57B */

#endif

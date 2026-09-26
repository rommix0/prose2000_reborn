/*
 * The lexical stage (REFERENCE §15.3): turns the words the text-rules stage delivers into phoneme symbols.
 *
 * A word arrives as a word record (a text node whose +6 low byte is the word type the text rules gave it) followed
 * by its letters (text nodes). The stage looks the word up in the pronunciation lexicon, stripping suffixes and
 * prefixes and repairing the stem if the whole word is not there (lex_lookup_with_affixes). Whatever the lexicon
 * does not cover goes through the letter-to-sound rules (lts_rules), which also place the stress. The finished
 * word then gets its allophone rules (allophone_rules) and the word record becomes a boundary symbol.
 *
 * Like the other stages, this code works on the data-segment image of pg.h. The lexicon itself is in its own
 * segment (E900), read through lex_read_byte / lex_index_word.
 */
#ifndef LEXICAL_H
#define LEXICAL_H

#include "pg.h"

/* ---- the stage record at DS:C228 (see pg.h) ---- */
#define LX_STAGE 0xC228
#define LX_DONE 0xC22A /* +2: next node to hand on */
#define LX_DONE2 0xC22C
#define LX_SCAN 0xC22E  /* +6: scan position (start of the next word) */
#define LX_LAST 0xC230  /* +8: last node of the window */
#define LX_FIRST 0xC232 /* +0A: first letter of the part being looked up */
#define LX_END 0xC234   /* +0C: its last letter */
#define LX_INPUT (LX_STAGE + REC_INPUT)
#define LX_PROSODY (LX_STAGE + REC_PROSODY)
#define LX_SPEED (LX_STAGE + REC_SPEED)
#define LX_PITCH (LX_STAGE + REC_PITCH)
#define LX_FAST (LX_STAGE + REC_FAST)
#define LX_MODE (LX_STAGE + REC_MODE)
#define LX_KINDS (LX_STAGE + 0x1E)

/* ---- the word being worked on ---- */
#define SCAN_STEPS 0xEE92 /* nodes skipped by scan_ahead */
#define SCAN_POS 0xEE94
#define WORD_END 0xEE96    /* last letter (later: last phoneme) of the word */
#define WORD_START 0xEE98  /* first letter */
#define WORD_REC 0xEE9A    /* the word record */
#define PREV_CHAR 0xEE9C   /* last symbol of the previous word, for allophone_rules */
#define SPAN_END 0xEE9E    /* the word taken over by take_span: its last node ... */
#define SPAN_REC 0xEEA0    /* ... and its word record */
#define AFFIX_CODE 0xEEA2  /* byte: a stripped affix's code >= 2 (right-context class for the LTS rules) */
#define LEX_CLASS 0xEEA3   /* byte: the word class from the lexicon or the affixes (REFERENCE §9.6) */
#define SECONDARY 0xEEA4   /* '`' before the word, or word types 1, 2, 5: stress the word one level down */
#define EMPHASIS 0xEEA6    /* '~' before the word, or word type 9: stress it one level up */
#define STRESS_BYTE 0xEEA8 /* byte: stress pattern of a stress-only lexicon entry (2 bits per syllable) */
#define ADDED_E 0xEEAA     /* stem_respell_and_lookup added an E that did not help */
#define AFFIX 0xEEAC       /* the affix record last matched */
#define FAST_COUNT 0xEEAE  /* content-word counter for ESC[f */

/* ---- the letter-to-sound rules (lts_rules) ---- */
#define LTS_SECOND 0xDBBC /* 1 while the suffix part is done */
#define LTS_CONS 0xDBBE   /* consonants since the last vowel */
#define LTS_OPEN 0xDBC0   /* the last vowel may be reduced (open syllable) */
#define LTS_CLASS 0xDBC2  /* class bits passed on by the rule to the right (rule +8, second word), affix bits */
#define LTS_STATE 0xDBC4  /* state bits passed on by the rule to the right (rule +8, first word) */
#define LTS_LETTER 0xDBC6 /* letter being converted (the walk goes right to left) */
#define LTS_AT 0xDBC8     /* match position, then the last phoneme written */
#define LTS_VOWEL 0xDBCA  /* last vowel written, not yet finished by vowel_reduce */

/* ---- tables ---- */
#define LETTER_STATES 0x212E  /* 12-byte records: the letter packing of lexicon keys */
#define PHONEME_STATES 0x2152 /* 8-byte records: 6-bit phoneme codes */
#define ENTRY_ADJUST 0x2172   /* by state * 17 + phoneme count: entry size minus key bytes */
#define SUFFIXES 0xA696       /* by last letter: -> list of affix records */
#define PREFIXES 0xAD50       /* by first letter */
#define LTS_RULES 0x52C0      /* by letter - '@': -> 10-byte rules */
#define CLASS_MASKS 0x48      /* context-pattern classes: low byte mask, high byte plane << 1 */

void lexical_load_rom(const prose_rom *rom); /* the lexicon segment E900 */

/* ---- functions ---- */
int stage_lexical_run(void);        /* E4A81 */
int lexical_step(void);             /* E4A81 after its stage update */
int finish_span(void);              /* E4AFB */
void take_span(void);               /* E4C8D */
int scan_ahead(void);               /* E4CAB */
int take_word(void);                /* E4D16 */
void word_stress_marks(void);       /* E4E90 */
void mark_stressed_syllable(int n); /* E546F */
void function_word_rules(void);     /* E5542 */
void word_boundary(void);           /* E5736 */
int allophone_rules(int n);         /* E3DBF */

int lex_index_word(int a, int b);                        /* D3225 */
int lex_read_byte(unsigned off);                         /* D3245 */
int lex_lookup(void);                                    /* D738A */
int lex_lookup_with_affixes(void);                       /* E4F57 */
int affix_match(int from, int to, int list, int prefix); /* E5152 */
int stem_respell_and_lookup(int suffix);                 /* E5264 */

/* shared with the prosody stage (prosody.h, whose names clash with these) */
int prev_symbol(int n); /* D9945 */
int next_symbol(int n); /* D99F2 */

void lts_rules(void);                                        /* D78AE */
int lts_class_match(int rule);                               /* D80F3 */
int lts_letters_match(int rule);                             /* D8165 */
int match_context_pattern(unsigned pat, int n, int forward); /* D81AB */
void vowel_reduce(int final);                                /* D8551 */

#endif

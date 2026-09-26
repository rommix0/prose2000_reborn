/*
 * The text-rules stage (REFERENCE §15.4): normalizes the host text (kind 1 nodes from the input stage) into words
 * for the lexical stage.
 *
 * The stage is a byte-code interpreter over a rule program in the data segment (from DS:146E). The program is a
 * tree of rules: a condition byte, a byte whose nibbles give the length of its "then" and "else" action lists,
 * and the actions. An action either changes the node list (convert, insert, delete, move a cursor), a register
 * or the program counter (goto / call a rule, return), or yields back to the synthesis loop. The interpreter keeps
 * its state in globals, so the program runs on from where it stopped each time the stage is called.
 *
 * With phoneme input (ESC[2I) the two-letter phoneme names are first converted to the one-letter alphabet
 * (phoneme_spelling).
 *
 * Like the other stages, this code works on the data-segment image of pg.h.
 */
#ifndef TEXTRULES_H
#define TEXTRULES_H

#include "pg.h"

/* ---- the stage record at DS:C206 (see pg.h) ---- */
#define TR_STAGE 0xC206 /* +0: first node of the window */
#define TR_DONE 0xC208  /* +2: next node to hand on (text_rules_commit) */
#define TR_START 0xC20A /* +4: first node of the text being worked on */
#define TR_CUR 0xC20C   /* +6: the node the rules look at */
#define TR_LAST 0xC20E  /* +8: last node of the window */
#define TR_MARK 0xC210  /* +0A: where match_list starts matching */
#define TR_SCAN 0xC212  /* +0C: scratch cursor */
#define TR_INPUT (TR_STAGE + REC_INPUT)
#define TR_PROSODY (TR_STAGE + REC_PROSODY)
#define TR_MODE (TR_STAGE + REC_MODE)
#define TR_AFLAGS (TR_STAGE + REC_AFLAGS)

/* ---- the interpreter ---- */
#define TR_CALLS 0xDB18     /* call stack pointer; 4-byte entries (pc, count, skip) from DB1A up to DB6A */
#define TR_SKIP 0xDB6A      /* byte: bytes to skip when the current action list is done */
#define TR_COUNT 0xDB6B     /* byte: bytes left in the current action list */
#define TR_REPL 0xDB6C      /* the replacement text after the pattern match_list matched */
#define TR_YIELDED 0xDB6E   /* the yield action has returned to the synthesis loop once */
#define TR_TEST 0xDB70      /* 1: the program counter is at a condition, 0: in an action list */
#define TR_PC 0xDB72        /* program counter */
#define TR_REG 0xDB0E       /* byte registers of the rule program */
#define TR_ATTR_PTR 0x03A8  /* ROM: points to TR_ATTR */
#define TR_ATTR 0xDB04      /* a node-shaped record: stress and timing marks waiting for the next phoneme */

/* ---- tables in ROM ---- */
#define TR_PROGRAM 0x146E    /* rule program; goto / call targets are relative to it */
#define TR_STRINGS 0x2053    /* 0-terminated strings: character sets and inserted text */
#define TR_LISTS 0x1434      /* match_list: per list, pointers to (pattern, replacement) pairs, 0-terminated */
#define TR_WORD_CHARS 0x145E /* characters that end a word besides letters and '1'-'4' */
#define BIT_MASKS 0x9BC8     /* 1 << n, n = 0..15 */

int stage_text_rules_run(void);         /* D41A4 */
int text_rules_step(void);              /* D41A4 after its stage update */
int text_rules_commit(int all);         /* D4B18 */
void text_rules_emit(int kind, int ch); /* D4B81 */
int match_list(int list, int fold);     /* D4C3D */
int char_test(int type, int ch);        /* D4CDB */
int not_stage_command(int ch);          /* D4D76 */
void phoneme_spelling(void);            /* D4DB0 */

#endif

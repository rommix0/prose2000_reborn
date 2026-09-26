/* v1.1 text-rules stage (ECBAC): normalizes the host text (kind 1 nodes) into words for the lexical stage.
 *
 * The same byte-code interpreter as v3.4.1's (REFERENCE §15.4) over a rule program at DS:057E: a condition byte, a
 * byte whose nibbles give the lengths of its "then" and "else" action lists, and the actions. 10xxxxxx xxxxxxxx is a
 * goto, 11xxxxxx xxxxxxxx a call (a 20-entry stack at DS:953A). v1.1 has 18 conditions and 22 actions; compared with
 * v3.4.1 it lacks the A flags, the exact-case list match, phoneme spelling, the stress marks carried over to phonemes
 * and the "!d;p" phoneme numbers. The interpreter keeps its state in globals, so the program runs on from where it
 * stopped each time the stage is called. */
#include "v1.h"

#define R V1_REC_TEXTRULES
#define TR_FIRST (R + V1_REC_FIRST)
#define TR_DONE (R + V1_REC_DONE)
#define TR_START (R + V1_REC_CURSOR) /* first node of the text being worked on */
#define TR_CUR (R + V1_REC_AHEAD)    /* the node the rules look at */
#define TR_LAST (R + V1_REC_LAST)
#define TR_MARK (R + 0x0A) /* where match_list starts matching */
#define TR_SCAN (R + 0x0C) /* scratch cursor */

static int node_kind(unsigned n) { return rw(n + V1_NODE_FLAGS) & 7; }
static void set_kind(unsigned n, int kind) { ww(n + V1_NODE_FLAGS, (rw(n + V1_NODE_FLAGS) & 0xFFF8) | kind); }

static int fetch(void) /* the next program byte (the program counter moves onto it) */
{
	ww(V1_TR_PC, rw(V1_TR_PC) + 1);
	return rsb(ruw(V1_TR_PC));
}

static int program(int k) { return rsb(ruw(V1_TR_PC) + (unsigned)k); }
static unsigned reg(int r) { return V1_TR_REG + (unsigned)r; }
static void count_sub(int n) { wb(V1_TR_COUNT, rb(V1_TR_COUNT) - n); }

/* ED735: hand on the finished nodes: all before TR_START, or (all = 0) only the commands at the start of the window.
 * Returns 1 if all is set or anything was handed on. */
static int commit(int all)
{
	if (all) {
		ww(TR_DONE, rw(TR_START));
	} else {
		ww(TR_DONE, rw(TR_FIRST));
		while (rw(TR_DONE) != rw(TR_START) && node_kind(ruw(TR_DONE)) == 0)
			ww(TR_DONE, v1_node_next(ruw(TR_DONE)));
	}
	if (v1_stage_commit())
		all = 1;
	return all;
}

/* ED78D: insert a node before TR_START, or at the end of the list when there is none */
static void emit(int kind, int ch)
{
	if (rw(TR_START) != 0) {
		v1_node_insert(ruw(TR_START), 0, kind, ch);
	} else {
		ww(TR_LAST, v1_node_insert(ruw(ruw(V1_LIST_END) + V1_NODE_PREV), 1, kind, ch));
		if (rw(TR_FIRST) == 0)
			ww(TR_FIRST, rw(TR_LAST));
	}
}

/* ED7E0: is the input text from TR_MARK to TR_CUR (letters folded to lower case) one of the patterns of list
 * (DS:0552)? Each entry points to a 0-terminated pattern followed by its 0-terminated replacement, which goes to
 * V1_TR_REPL. Only kind 1 (input text) nodes match. */
static int match_list(int list)
{
	unsigned p = ruw(0x552 + 2 * (unsigned)list);
	while (rw(p) != 0) {
		unsigned s = ruw(p);
		p += 2;
		ww(TR_SCAN, rw(TR_MARK));
		for (;;) {
			unsigned n = ruw(TR_SCAN);
			int ch = rsb(n + V1_NODE_CH);
			if (ch >= 'A' && ch <= 'Z')
				ch += 0x20;
			if (node_kind(n) != 1 || ch != rsb(s))
				break;
			s++;
			if (rb(s) == 0 || n == ruw(TR_CUR))
				break;
			ww(TR_SCAN, rw(n + V1_NODE_NEXT));
		}
		if (rb(s++) == 0 && rw(TR_SCAN) == rw(TR_CUR)) {
			ww(V1_TR_REPL, s);
			return 1;
		}
	}
	return 0;
}

/* ED89E: character types 4 letter or '\'', 5 digit, 6 lower case, 7 upper case */
static int char_test(int type, int ch)
{
	ch = (int8_t)ch;
	switch (type) {
	case 4: return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '\'';
	case 5: return ch >= '0' && ch <= '9';
	case 6: return ch >= 'a' && ch <= 'z';
	case 7: return ch >= 'A' && ch <= 'Z';
	default: v1_fatal_error(0x1A); return 0;
	}
}

/* ED96F: 0 for the commands that concern the text rules (C, F, I, N, x), 1 otherwise */
static int not_stage_command(int ch)
{
	switch ((int8_t)ch) {
	case 'C':
	case 'F':
	case 'I':
	case 'N':
	case 'x': return 0;
	default: return 1;
	}
}

static void need(unsigned cursor, int code)
{
	if (rw(cursor) == 0)
		v1_fatal_error(code);
}

/* The conditions. The operands follow the condition byte. Returns 1 or 0, or -1 when the rule needs a node the
 * window does not have yet (the stage then hands on what it has and returns). */
static int test_condition(void)
{
	int op = program(0), ch, v, k;
	unsigned s;
	switch (op) {
	case 0: return rw(R + V1_REC_INPUT) == 1; /* phoneme input */
	case 1: return rw(R + V1_REC_WORD) == 0;  /* not word mode */
	case 2:                                   /* N flag n + 1 */
		v = fetch();
		return (rw(0x48A6 + 2 * (unsigned)v) & rw(R + V1_REC_MODE)) != 0;
	case 3: /* the current node is a letter, '1'-'4' or one of the characters at DS:056E */
		need(TR_CUR, 2);
		ch = rsb(ruw(TR_CUR) + V1_NODE_CH);
		if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '1' && ch <= '4'))
			return 1;
		for (k = 0; rsb(0x56E + (unsigned)k) != 0; k++)
			if (rsb(0x56E + (unsigned)k) == ch)
				return 1;
		return 0;
	case 4:
	case 5:
	case 6:
	case 7: /* character type of the current node */
		need(TR_CUR, op - 1);
		return char_test(op, rsb(ruw(TR_CUR) + V1_NODE_CH));
	case 8: /* the next node of a type, skipping blanks and commands that do not concern this stage (20 at most) */
		ww(TR_SCAN, rw(TR_CUR));
		v = 0;
		for (k = 0; k < 20; k++) {
			if (rw(TR_SCAN) == rw(TR_LAST))
				return -1;
			ww(TR_SCAN, rw(ruw(TR_SCAN) + V1_NODE_NEXT));
			s = ruw(TR_SCAN);
			ch = rsb(s + V1_NODE_CH);
			if (node_kind(s) == 0) {
				if (not_stage_command(ch))
					continue;
				v = char_test(program(1), 'a') != 0;
				break;
			}
			if (char_test(program(1), ch)) {
				v = 1;
				break;
			}
			if (ch != ' ')
				break;
		}
		fetch();
		return v;
	case 9: /* register == value */
		v = rsb(reg(fetch()));
		return fetch() == v;
	case 10: /* register > value */
		v = rsb(reg(fetch()));
		return fetch() < v;
	case 11: /* the current node is this character */
		need(TR_CUR, 7);
		return rsb(ruw(TR_CUR) + V1_NODE_CH) == fetch();
	case 12: /* the text from TR_MARK to the current node is in a list */
		need(TR_MARK, 8);
		return match_list(fetch());
	case 13: /* step to the next node; it is a command */
		need(TR_CUR, 9);
		if (rw(TR_CUR) == rw(TR_LAST))
			return -1;
		ww(TR_CUR, v1_node_next(ruw(TR_CUR)));
		return node_kind(ruw(TR_CUR)) == 0;
	case 14: /* the current node is a command */
		if (rw(TR_CUR) == 0)
			return -1;
		return node_kind(ruw(TR_CUR)) == 0;
	case 15: return 1;
	case 16:
		need(TR_CUR, 0xA);
		return not_stage_command(rsb(ruw(TR_CUR) + V1_NODE_CH));
	case 17: /* the current node's character is in a string (DS:0F56 + offset) */
		need(TR_CUR, 0x27);
		k = fetch() << 8;
		k |= fetch() & 0xFF;
		for (s = 0xF56 + (unsigned)k; rb(s) != 0; s++)
			if (rsb(ruw(TR_CUR) + V1_NODE_CH) == rsb(s))
				return 1;
		return 0;
	default: v1_fatal_error(0xB); return 0;
	}
}

/* action 20: the numbers after a phoneme, "/d;p" (pitch; duration), from phoneme input */
static void phoneme_numbers(void)
{
	if (rw(TR_START) == 0 || rw(TR_START) != rw(TR_MARK))
		v1_fatal_error(0x28);
	unsigned n = ruw(TR_START);
	int v[2] = {-1, -1}, k = 0, ch;
	ww(TR_SCAN, rw(n + V1_NODE_NEXT));
	for (;;) {
		ww(TR_SCAN, rw(ruw(TR_SCAN) + V1_NODE_NEXT));
		ch = rsb(ruw(TR_SCAN) + V1_NODE_CH);
		if (ch >= '0' && ch <= '9') {
			v[k] = v[k] < 0 ? ch - '0' : s16(v[k] * 10 + ch - '0');
			if (v[k] <= 0xFF)
				continue;
		}
		if (ch != ';' || ++k >= 2)
			break;
	}
	if (v[0] < 0)
		v[0] = 0;
	else if (v[0] == 0)
		v[0] = 2;
	if (v[1] < 0)
		v[1] = 0;
	if (ch == '/' && (v[0] >= 50 || v[0] <= 2) && v[0] <= 200 && v[1] <= 60 &&
	    (rsb((unsigned)(0x90 + rsb(n + V1_NODE_CH))) & 0x80)) {
		wb(n + V1_NODE_ARG1, v[0] >> 1);
		wb(n + V1_NODE_ARG0, v[1]);
	}
}

/* TR_START..TR_CUR become kind (2: a word, letters upper case and '\'' as '@'; 3: symbols); then go on after it */
static void convert(int kind, int code)
{
	if (rw(TR_MARK) == 0 || rw(TR_START) != rw(TR_MARK))
		v1_fatal_error(code);
	for (;;) {
		unsigned n = ruw(TR_START);
		if (kind == 2) {
			int ch = rsb(n + V1_NODE_CH);
			if (ch == '\'')
				wb(n + V1_NODE_CH, '@');
			else if (ch >= 'a' && ch <= 'z')
				wb(n + V1_NODE_CH, ch - 0x20);
		}
		set_kind(n, kind);
		if (rw(TR_START) == rw(TR_CUR))
			break;
		ww(TR_START, rw(n + V1_NODE_NEXT));
	}
}

static void next_text(void)
{
	ww(TR_CUR, v1_node_next(ruw(TR_CUR)));
	ww(TR_MARK, rw(TR_CUR));
	ww(TR_START, rw(TR_CUR));
}

/* The actions. Each ends with the program counter on its last byte and the count not yet reduced for it. Returns 0
 * when the action yields to the synthesis loop. */
static int action(int op)
{
	int v;
	unsigned s;
	switch (op) {
	case 0:
	case 1: /* insert a string before TR_START: text (0) or symbols (1) */
		v = fetch() << 8;
		v |= fetch() & 0xFF;
		for (s = 0xF56 + (unsigned)v; rb(s) != 0; s++)
			emit(program(-2) == 0 ? 2 : 3, rsb(s));
		count_sub(2);
		break;
	case 2: /* go on after the current node */
		need(TR_CUR, 0xD);
		next_text();
		break;
	case 3:
		convert(2, 0xE);
		next_text();
		break;
	case 4:
		convert(3, 0xF);
		next_text();
		break;
	case 5: /* insert the replacement match_list found, as symbols */
		for (; rb(ruw(V1_TR_REPL)) != 0; ww(V1_TR_REPL, rw(V1_TR_REPL) + 1))
			emit(3, rsb(ruw(V1_TR_REPL)));
		break;
	case 6: /* register = value */
		v = fetch();
		wb(reg(v), fetch());
		count_sub(2);
		break;
	case 7:
		v = fetch();
		wb(reg(v), rb(reg(v)) + 1);
		count_sub(1);
		break;
	case 8:
		v = fetch();
		wb(reg(v), rb(reg(v)) - 1);
		count_sub(1);
		break;
	case 9: /* back to TR_START */
		ww(TR_MARK, rw(TR_START));
		ww(TR_CUR, rw(TR_START));
		break;
	case 10:
		need(TR_CUR, 0x10);
		ww(TR_CUR, v1_node_next(ruw(TR_CUR)));
		break;
	case 11: /* back one node */
		if (rw(TR_CUR) == rw(TR_START))
			v1_fatal_error(0x11);
		else
			ww(TR_CUR, rw(TR_CUR) == 0 ? rw(TR_LAST) : rw(ruw(TR_CUR) + V1_NODE_PREV));
		break;
	case 12: /* delete the current node */
		need(TR_CUR, 0x12);
		if (rw(TR_START) == rw(TR_CUR)) {
			v = (int)v1_node_next(ruw(TR_START));
			ww(TR_MARK, v);
			ww(TR_START, v);
		}
		ww(TR_CUR, v1_node_free(ruw(TR_CUR), 1));
		break;
	case 13: /* delete TR_START..TR_CUR */
		if (rw(TR_START) == 0 || rw(TR_START) != rw(TR_MARK))
			v1_fatal_error(0x13);
		while (rw(TR_START) != rw(TR_CUR))
			ww(TR_START, v1_node_free(ruw(TR_START), 1));
		ww(TR_START, v1_node_free(ruw(TR_START), 1));
		ww(TR_MARK, rw(TR_START));
		ww(TR_CUR, rw(TR_START));
		break;
	case 14: /* insert a text node after the current one and move onto it */
		need(TR_CUR, 0x14);
		v1_node_insert(ruw(TR_CUR), 1, 2, program(1));
		ww(TR_CUR, rw(ruw(TR_CUR) + V1_NODE_NEXT));
		ww(V1_TR_PC, rw(V1_TR_PC) + 1);
		count_sub(1);
		break;
	case 15: /* replace the current node's character */
		need(TR_CUR, 0x15);
		wb(ruw(TR_CUR) + V1_NODE_CH, fetch());
		count_sub(1);
		break;
	case 16:
		need(TR_MARK, 0x16);
		match_list(fetch());
		count_sub(1);
		break;
	case 17: /* yield to the synthesis loop once */
		if (rw(V1_TR_YIELDED) == 0) {
			ww(V1_TR_YIELDED, 1);
			return 0;
		}
		ww(V1_TR_YIELDED, 0);
		break;
	case 18: /* run the command node at TR_START */
		if (rw(TR_START) == 0 || rw(TR_START) != rw(TR_CUR))
			v1_fatal_error(0x17);
		v1_command_apply();
		break;
	case 19: /* return */
		if (ruw(V1_TR_CALLS) == V1_TR_STACK)
			v1_fatal_error(0x18);
		ww(V1_TR_CALLS, rw(V1_TR_CALLS) - 4);
		s = ruw(V1_TR_CALLS);
		ww(V1_TR_PC, rw(s));
		wb(V1_TR_COUNT, rb(s + 2));
		wb(V1_TR_SKIP, rb(s + 3));
		ww(V1_TR_PC, rw(V1_TR_PC) + 1);
		count_sub(1);
		break;
	case 20: phoneme_numbers(); break;
	case 21: ww(TR_MARK, rw(TR_CUR)); break;
	default: v1_fatal_error(0x19); break;
	}
	ww(V1_TR_PC, rw(V1_TR_PC) + 1);
	count_sub(1);
	return 1;
}

/* ECBAC. Runs the rule program on from where it stopped until a rule needs a node the window does not have yet, or
 * the program yields. Returns 1 if it handed anything on (or yielded). */
int v1_stage_text_rules_run(void)
{
	count32(0xA4BE);
	v1_stage_begin(R);
	if (rw(TR_MARK) == 0)
		ww(TR_MARK, rw(TR_START));
	for (;;) {
		if (rw(V1_TR_TEST)) {
			int r = test_condition(), b;
			if (r < 0)
				return commit(0);
			ww(V1_TR_TEST, 0);
			ww(V1_TR_PC, rw(V1_TR_PC) + 1);
			b = rsb(ruw(V1_TR_PC));
			if (r) { /* run the first (high nibble) bytes, then skip the rest */
				wb(V1_TR_COUNT, (b >> 4) & 0xF);
				wb(V1_TR_SKIP, b & 0xF);
				ww(V1_TR_PC, rw(V1_TR_PC) + 1);
			} else { /* skip the first bytes, run the rest */
				wb(V1_TR_COUNT, b & 0xF);
				wb(V1_TR_SKIP, 0);
				ww(V1_TR_PC, rw(V1_TR_PC) + ((b >> 4) & 0xF) + 1);
			}
		}
		while (rb(V1_TR_COUNT) != 0) {
			unsigned pc = ruw(V1_TR_PC);
			int op = rsb(pc);
			if (op & 0x80) { /* 10xxxxxx: goto, 11xxxxxx: call a rule */
				if ((op & 0xC0) != 0x80) {
					unsigned sp = ruw(V1_TR_CALLS);
					ww(sp, pc);
					wb(sp + 2, rb(V1_TR_COUNT));
					wb(sp + 3, rb(V1_TR_SKIP));
					ww(V1_TR_CALLS, sp + 4);
					if (ruw(V1_TR_CALLS) == V1_TR_CALLS)
						v1_fatal_error(0xC);
				}
				ww(V1_TR_PC, (((op & 0x3F) << 8) | rb(pc + 1)) + V1_TR_PROGRAM);
				wb(V1_TR_COUNT, 1);
				wb(V1_TR_SKIP, 0);
				break;
			}
			if (!action(op))
				return commit(1);
		}
		ww(V1_TR_PC, rw(V1_TR_PC) + rsb(V1_TR_SKIP));
		ww(V1_TR_TEST, 1);
	}
}

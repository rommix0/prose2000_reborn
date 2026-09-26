/* The text-rules stage driver: the rule-program interpreter and the node operations its actions use. */
#include "textrules.h"

static int fetch(void) /* the next program byte (the program counter moves onto it) */
{
	ww(TR_PC, rw(TR_PC) + 1);
	return rsb(ruw(TR_PC));
}

static int program(int k)
{
	return rsb(ruw(TR_PC) + k);
}

static void set_kind(int n, int kind)
{
	ww(n + N_FLAGS, (rw(n + N_FLAGS) & 0xFFF8) | kind);
}

/* D41A4 */
int stage_text_rules_run(void)
{
	stage_window_update(TR_STAGE);
	return text_rules_step();
}

/* The conditions. The operands follow the condition byte. Returns 1 or 0, or -1 when the rule needs a node the
 * window does not have yet. */
static int test_condition(void)
{
	int op = program(0), ch, v, k, s, r = 0;
	switch (op) {
	case 0: /* phoneme input */
		return rw(TR_INPUT) != 0;
	case 1: /* word mode */
		return rw(TR_PROSODY) == 0;
	case 2: /* mode flag n (N flags 0-15, then A flags) */
		v = fetch();
		if (v < 16)
			return (rw(TR_MODE) & rw(BIT_MASKS + 2 * v)) != 0;
		return (rw(TR_AFLAGS) & rw(BIT_MASKS + 2 * (v - 16))) != 0;
	case 3: /* the current node is a letter, '1'-'4' or a character that ends a word */
		if (rw(TR_INPUT) == 2) {
			if (rw(TR_CUR) == rw(TR_LAST))
				return -1;
			phoneme_spelling();
		}
		ch = node_char(rw(TR_CUR));
		if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '1' && ch <= '4'))
			return 1;
		for (k = 0; rb(TR_WORD_CHARS + k) != 0; k++) {
			if (rsb(TR_WORD_CHARS + k) == ch)
				return 1;
			if (ch == '\\')
				r = 1;
		}
		return r;
	case 4:
	case 5:
	case 6:
	case 7: /* character type of the current node */
		return char_test(op, node_char(rw(TR_CUR)));
	case 8: /* the next node of a type, skipping blanks and commands that do not concern this stage (20 at most) */
		ww(TR_SCAN, rw(TR_CUR));
		for (k = 0; k < 20; k++) {
			if (rw(TR_SCAN) == rw(TR_LAST))
				return -1;
			ww(TR_SCAN, rw(rw(TR_SCAN)));
			s = rw(TR_SCAN);
			ch = node_char(s);
			if (node_kind(s) == NODE_COMMAND) {
				if (not_stage_command(ch))
					continue;
				r = char_test(program(1), 'a');
				break;
			}
			if (char_test(program(1), ch)) {
				r = 1;
				break;
			}
			if (ch != ' ' && ch != '~' && ch != '`')
				break;
		}
		fetch();
		return r;
	case 9: /* register == value */
		v = rsb(TR_REG + fetch());
		return v == fetch();
	case 10: /* register > value */
		v = rsb(TR_REG + fetch());
		return v > fetch();
	case 11: /* the current node is this character */
		ch = node_char(rw(TR_CUR));
		return ch == fetch();
	case 12: /* the text from TR_MARK to the current node is in a list (letters folded to lower case) */
		return match_list(fetch(), 1);
	case 18: /* the same, exact case */
		return match_list(fetch(), 0);
	case 13: /* step to the next node; it is a command */
		if (rw(TR_CUR) == rw(TR_LAST))
			return -1;
		ww(TR_CUR, node_next(rw(TR_CUR)));
		return node_kind(rw(TR_CUR)) == NODE_COMMAND;
	case 14: /* the current node is a command */
		if (rw(TR_CUR) == 0)
			return -1;
		return node_kind(rw(TR_CUR)) == NODE_COMMAND;
	case 15:
		return 1;
	case 16:
		return not_stage_command(node_char(rw(TR_CUR)));
	case 17: /* the current node's character is in a string */
		k = (fetch() & 0xFF) << 8;
		k |= fetch() & 0xFF;
		for (s = TR_STRINGS + k; rb(s) != 0; s++)
			if (node_char(rw(TR_CUR)) == rsb(s))
				return 1;
		return 0;
	}
	return 0;
}

/* The number after a phoneme: "!d;p" or "/d;p" (duration; pitch), from phoneme input. */
static void phoneme_numbers(void)
{
	int n = rw(TR_START), v[2] = {-1, -1}, k = 0, ch;
	ww(TR_SCAN, rw(n));
	for (;;) {
		ww(TR_SCAN, rw(rw(TR_SCAN)));
		ch = node_char(rw(TR_SCAN));
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
	if (ch == '!' && v[0] <= 300 && ((v[1] <= 160 && v[1] >= 40) || v[1] == 0) &&
	    (feature(node_char(n), 0) & 0x80)) {
		if (v[0] != 0)
			wb(n + N_F0, s16(v[0] - 100) >> 1);
		if (v[1] != 0)
			ww(n + N_DUR, (rw(n + N_DUR) & 0xFF00) | ((v[1] - 100) & 0xFF));
	} else if (ch == '/' && (v[0] >= 50 || v[0] <= 2) && v[0] <= 200 && v[1] <= 60 &&
	           (feature(node_char(n), 0) & 0x80)) {
		wb(n + N_F0, v[0] >> 1);
		ww(n + N_DUR, (rw(n + N_DUR) & 0xFF00) | (v[1] & 0xFF));
	}
}

/* The actions. Each ends with the program counter on its last byte and the count not yet reduced for it.
 * Returns 0 when the action yields to the synthesis loop. */
static int action(int op)
{
	int n, s, v;
	switch (op) {
	case 0:
	case 1: /* insert a string before TR_START: text (0) or symbols (1) */
		v = (fetch() & 0xFF) << 8;
		v |= fetch() & 0xFF;
		for (s = TR_STRINGS + v; rb(s) != 0; s++)
			text_rules_emit(program(-2) == 0 ? NODE_TEXT : NODE_SYMBOL, rsb(s));
		wb(TR_COUNT, rb(TR_COUNT) - 2);
		break;
	case 3: /* TR_START..TR_CUR become a word: text nodes, letters upper case, '\'' as '@' */
		if (rb(TR_REG + 2))
			ww(rw(TR_STAGE) + N_FLAGS, rw(rw(TR_STAGE) + N_FLAGS) | 0x40);
		for (;;) {
			n = rw(TR_START);
			v = node_char(n);
			if (v == '\'')
				wb(n + N_CHAR, '@');
			else if (v >= 'a' && v <= 'z')
				wb(n + N_CHAR, v - 0x20);
			set_kind(n, NODE_TEXT);
			if (rw(TR_START) == rw(TR_CUR))
				break;
			ww(TR_START, rw(rw(TR_START)));
		}
		goto next_text;
	case 4: /* TR_START..TR_CUR become symbols (commands stay) */
		for (;;) {
			n = rw(TR_START);
			if (node_kind(n) != NODE_COMMAND)
				set_kind(n, NODE_SYMBOL);
			if (rw(TR_START) == rw(TR_CUR))
				break;
			ww(TR_START, rw(rw(TR_START)));
		}
		/* fall through */
	case 2: /* go on after the current node */
	next_text:
		ww(TR_CUR, node_next(rw(TR_CUR)));
		ww(TR_MARK, rw(TR_CUR));
		ww(TR_START, rw(TR_CUR));
		break;
	case 5: /* insert the replacement match_list found, as symbols */
		for (; rb(ruw(TR_REPL)) != 0; ww(TR_REPL, rw(TR_REPL) + 1))
			text_rules_emit(NODE_SYMBOL, rsb(ruw(TR_REPL)));
		break;
	case 6: /* register = value */
		v = fetch();
		wb(TR_REG + v, fetch());
		wb(TR_COUNT, rb(TR_COUNT) - 2);
		break;
	case 7:
		v = fetch();
		wb(TR_REG + v, rb(TR_REG + v) + 1);
		wb(TR_COUNT, rb(TR_COUNT) - 1);
		break;
	case 8:
		v = fetch();
		wb(TR_REG + v, rb(TR_REG + v) - 1);
		wb(TR_COUNT, rb(TR_COUNT) - 1);
		break;
	case 9: /* back to TR_START */
	restart:
		ww(TR_MARK, rw(TR_START));
		ww(TR_CUR, rw(TR_START));
		break;
	case 10:
		ww(TR_CUR, node_next(rw(TR_CUR)));
		break;
	case 11:
		ww(TR_CUR, rw(TR_CUR) == 0 ? rw(TR_LAST) : rw(rw(TR_CUR) + 2));
		break;
	case 12: /* delete the current node */
		if (rw(TR_START) == rw(TR_CUR)) {
			n = node_next(rw(TR_START));
			ww(TR_MARK, n);
			ww(TR_START, n);
		}
		ww(TR_CUR, node_delete(rw(TR_CUR), 1));
		break;
	case 13: /* delete TR_START..TR_CUR */
		while (rw(TR_START) != rw(TR_CUR))
			ww(TR_START, node_delete(rw(TR_START), 1));
		ww(TR_START, node_delete(rw(TR_START), 1));
		goto restart;
	case 14: /* insert a text node after the current one and move onto it */
		node_insert(rw(TR_CUR), 1, NODE_TEXT, program(1));
		ww(TR_CUR, rw(rw(TR_CUR)));
		ww(TR_PC, rw(TR_PC) + 1);
		wb(TR_COUNT, rb(TR_COUNT) - 1);
		break;
	case 15: /* replace the current node's character */
		n = rw(TR_CUR);
		wb(n + N_CHAR, fetch());
		wb(TR_COUNT, rb(TR_COUNT) - 1);
		break;
	case 16:
		match_list(fetch(), 1);
		wb(TR_COUNT, rb(TR_COUNT) - 1);
		break;
	case 17: /* yield to the synthesis loop once */
		if (rw(TR_YIELDED) == 0) {
			ww(TR_YIELDED, 1);
			return 0;
		}
		ww(TR_YIELDED, 0);
		break;
	case 18:
		stage_run_command();
		break;
	case 19: /* return */
		if (ruw(TR_CALLS) == 0xDB1A)
			fatal_error(0x18);
		ww(TR_CALLS, rw(TR_CALLS) - 4);
		s = ruw(TR_CALLS);
		ww(TR_PC, rw(s));
		wb(TR_COUNT, rb(s + 2));
		wb(TR_SKIP, rb(s + 3));
		ww(TR_PC, rw(TR_PC) + 1);
		wb(TR_COUNT, rb(TR_COUNT) - 1);
		break;
	case 20:
	case 21: /* the numbers after a phoneme; 20 also marks it (flag bit 7) */
		n = rw(TR_START);
		ww(n + N_FLAGS, rw(n + N_FLAGS) | 0x80);
		if (op == 21)
			ww(n + N_FLAGS, rw(n + N_FLAGS) & ~0x80);
		phoneme_numbers();
		break;
	case 22:
		ww(TR_MARK, rw(TR_CUR));
		break;
	}
	ww(TR_PC, rw(TR_PC) + 1);
	wb(TR_COUNT, rb(TR_COUNT) - 1);
	return 1;
}

/* D41A4 after its stage update. Runs the rule program on from where it stopped until a rule needs a node the window
 * does not have yet, or the program yields. */
int text_rules_step(void)
{
	if (rw(TR_MARK) == 0)
		ww(TR_MARK, rw(TR_START));
	for (;;) {
		if (rw(TR_TEST)) {
			int r = test_condition(), b;
			if (r < 0)
				return text_rules_commit(0);
			ww(TR_TEST, 0);
			ww(TR_PC, rw(TR_PC) + 1);
			b = rsb(ruw(TR_PC));
			if (r) { /* run the first (high nibble) bytes, then skip the rest */
				wb(TR_COUNT, (b >> 4) & 0xF);
				wb(TR_SKIP, b & 0xF);
				ww(TR_PC, rw(TR_PC) + 1);
			} else { /* skip the first bytes, run the rest */
				wb(TR_COUNT, b & 0xF);
				wb(TR_SKIP, 0);
				ww(TR_PC, rw(TR_PC) + ((b >> 4) & 0xF) + 1);
			}
		}
		while (rb(TR_COUNT) != 0) {
			int pc = ruw(TR_PC), op = rsb(pc);
			if (op & 0x80) { /* 10xxxxxx: goto, 11xxxxxx: call a rule */
				if ((op & 0xC0) != 0x80) {
					int sp = ruw(TR_CALLS);
					ww(sp, pc);
					wb(sp + 2, rb(TR_COUNT));
					wb(sp + 3, rb(TR_SKIP));
					ww(TR_CALLS, sp + 4);
					if (ruw(TR_CALLS) == 0xDB6A)
						fatal_error(0xC);
				}
				ww(TR_PC, (((op & 0x3F) << 8) | rb(pc + 1)) + TR_PROGRAM);
				wb(TR_COUNT, 1);
				wb(TR_SKIP, 0);
				break;
			}
			if (!action(op))
				return text_rules_commit(1);
		}
		ww(TR_PC, rw(TR_PC) + rsb(TR_SKIP));
		ww(TR_TEST, 1);
	}
}

/* D4B18: hand on the finished nodes: all before TR_START, or (all = 0) only the commands and ']' at the start of
 * the window. Returns 1 if all is set or anything was handed on. */
int text_rules_commit(int all)
{
	int r = all, n;
	if (all) {
		ww(TR_DONE, rw(TR_START));
	} else {
		ww(TR_DONE, rw(TR_STAGE));
		while (rw(TR_DONE) != rw(TR_START)) {
			n = rw(TR_DONE);
			if (node_kind(n) != NODE_COMMAND && !(node_kind(n) == NODE_SYMBOL && node_char(n) == ']'))
				break;
			ww(TR_DONE, node_next(n));
		}
	}
	if (stage_commit())
		r = 1;
	return r;
}

/* D4B81: insert a node before TR_START (or at the end of the list when there is none). A phoneme takes over the
 * stress and timing marks waiting in TR_ATTR. */
void text_rules_emit(int kind, int ch)
{
	int n, a;
	if (rw(TR_START) != 0) {
		n = node_insert(rw(TR_START), 0, kind, ch);
	} else {
		n = node_insert(rw(rw(LIST_HEAD) + 2), 1, kind, ch);
		ww(TR_LAST, n);
		if (rw(TR_STAGE) == 0)
			ww(TR_STAGE, rw(TR_LAST));
	}
	a = ruw(TR_ATTR_PTR);
	if ((feature(node_char(n), 0) & 8) && ((ruw(a + N_FLAGS) >> 5) & 1)) {
		ww(n + N_FLAGS, rw(n + N_FLAGS) | 0x20);
		wb(n + N_F0, rb(a + N_F0));
		wb(a + N_F0, 0);
		ww(n + N_DUR, (rw(n + N_DUR) & 0xFF00) | (rw(a + N_DUR) & 0xFF));
		ww(a + N_DUR, rw(a + N_DUR) & 0xFF00);
		ww(a + N_FLAGS, rw(a + N_FLAGS) & ~0x20);
	}
}

/* D4C3D: is the input text from TR_MARK to TR_CUR one of the patterns of the list? Each entry points to a pattern
 * and its replacement, both 0-terminated; TR_REPL gets the replacement. Only kind 1 (input text) nodes match. */
int match_list(int list, int fold)
{
	int p = rw(TR_LISTS + 2 * list), s, n, ch;
	while (rw(p) != 0) {
		s = ruw(p);
		p += 2;
		ww(TR_SCAN, rw(TR_MARK));
		for (;;) {
			n = rw(TR_SCAN);
			ch = rsb(n + N_CHAR);
			if (fold == 1 && ch >= 'A' && ch <= 'Z')
				ch += 0x20;
			if (node_kind(n) != 1 || (int8_t)ch != rsb(s))
				break;
			s++;
			if (rb(s) == 0 || n == rw(TR_CUR))
				break;
			ww(TR_SCAN, rw(n));
		}
		if (rb(s++) == 0 && rw(TR_SCAN) == rw(TR_CUR)) {
			ww(TR_REPL, s);
			return 1;
		}
	}
	return 0;
}

/* D4CDB: character types 4 letter or '\'', 5 digit, 6 lower case, 7 upper case, 14 control, 19 '.' */
int char_test(int type, int ch)
{
	ch = (int8_t)ch;
	switch (type) {
	case 4:
		return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '\'';
	case 5:
		return ch >= '0' && ch <= '9';
	case 6:
		return ch >= 'a' && ch <= 'z';
	case 7:
		return ch >= 'A' && ch <= 'Z';
	case 14:
		return ch < 0x20;
	case 19:
		return ch == '.';
	}
	return 0;
}

/* D4D76: 0 for the commands that concern the text rules (C, F, I, N, x), 1 otherwise */
int not_stage_command(int ch)
{
	switch ((int8_t)ch) {
	case 'C':
	case 'F':
	case 'I':
	case 'N':
	case 'x':
		return 0;
	}
	return 1;
}

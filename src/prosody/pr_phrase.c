/* Phrase breaks: where a long stretch of words is split, and the P command that starts each phrase. */
#include "prosody.h"

static int word(int i)
{
	return rw(WORDS + 2 * i);
}
static int word_class(int i)
{
	return rb(word(i) + N_F0);
} /* a word boundary keeps its word's class in +8 */

/* DA811: after a break is inserted, the phoneme before it (side 0) may get a closing 'p' release, and the
 * phoneme after it (side 1) a glottal onset flag (bit 6). */
void mark_break_neighbour(int n, int side)
{
	int ch = node_char(n);
	if (side == 0 && ((feature(ch, 0x100) & 1) || (feature(ch, 0x200) & 0x10) || ch == 'V' || ch == 'z')) {
		if (rw(PR_SPEED) < 19 && !(feature(ch, 0x80) & 8))
			node_insert(n, 1, NODE_SYMBOL, 'p');
	} else if (side == 1 && (feature(ch, 0x100) & 2) && ch != 'U' && rw(PR_PITCH) == 0) {
		ww(n + N_FLAGS, rw(n + N_FLAGS) | 0x40);
	}
}

static void set_first_phrase(int p)
{
	if (rw(NEW_SENTENCE) != 0) {
		wb(p + N_F0, 0);
		ww(NEW_SENTENCE, 0);
	} else {
		wb(p + N_F0, 1);
	}
}

/* the P command node that opens a phrase: +6 = its kind (1 '.', 2 other, 3 ',', 4 '?'), +8 = 0 for the first
 * phrase of a sentence. The stages below read it as ESC[kind;flag P. */
static int phrase_start(int at, int kind)
{
	int p = node_insert(at, 0, NODE_COMMAND, 'P');
	ww(p + N_DUR, (rw(p + N_DUR) & 0xFF00) | kind);
	set_first_phrase(p);
	return p;
}

/* DA770: break the phrase before word `at` with the symbol ch (',' or '\\') and start the phrase at word `from`. */
void insert_phrase_break(int from, int ch, int at)
{
	int n, p;
	if (rw(FREE_COUNT) < 3)
		return;
	n = node_insert(word(at), 0, NODE_SYMBOL, ch);
	mark_break_neighbour(node_prev(n), 0);
	mark_break_neighbour(node_next(word(at)), 1);
	p = phrase_start(word(from), 3);
	if (rw(p) == rw(PR_FIRST))
		ww(PR_FIRST, p);
}

/* the first break rule: a function word (class 5, or 14 before a class-5 word) after a content word */
static int break_before_function_word(int i)
{
	return node_char(word(i)) == '%' && !node_bit(word(i), 7) && node_char(word(i - 1)) == '&' &&
	       word_class(i - 1) != 6 && (word_class(i) == 5 || (word_class(i) == 14 && word_class(i + 1) == 5));
}

/* The second rule can never fire: it requires the next boundary's class to be 37 ('%', probably meant for its
 * char at +9) and then to be one of 13, 12, 11, 2, 15, or 4, 10, 3 before a class-13 word. */
static int break_second_rule(int i)
{
	int c1;
	if (!(node_char(word(i)) == '%' && word_class(i) == 14 && !node_bit(word(i), 7) &&
	      node_char(word(i - 1)) == '&' && word_class(i - 1) != 6 && word_class(i - 1) != 15))
		return 0;
	if (word_class(i + 1) != '%')
		return 0;
	c1 = word_class(i + 1);
	if (c1 == 13 || c1 == 12 || c1 == 11 || c1 == 2 || c1 == 15)
		return 1;
	return (c1 == 4 || c1 == 10 || c1 == 3) && word_class(i + 2) == 13;
}

static int break_third_rule(int i)
{
	int c = word_class(i), c0, nx;
	if (!(node_char(word(i)) == '%' && !node_bit(word(i), 7) && node_char(word(i - 1)) == '&'))
		return 0;
	if (c == 13)
		return 1;
	c0 = word_class(i - 1);
	if (c0 == 6 || c0 == 15)
		return 0;
	if (c == 12 || c == 11 || c == 4 || c == 10)
		return 1;
	nx = node_char(rw(word(i))); /* the symbol after the boundary */
	if (c == 2 && nx != 'x' && nx != 'B')
		return 1;
	return c == 3 && (nx == 'F' || nx == 's');
}

/* after a '?': a question that starts with a class-13 word gets a falling phrase and a stressed first syllable */
static void question_start(int p, int w)
{
	int s;
	ww(p + N_DUR, (rw(p + N_DUR) & 0xFF00) | 1);
	s = rw(rw(w));
	if (node_kind(s) == NODE_SYMBOL && (feature(node_char(s), 0) & 0x80)) {
		ww(s + N_FLAGS, rw(s + N_FLAGS) | 0x20);
		ww(s + N_FLAGS, (rw(s + N_FLAGS) & 0xFFE7) | 8);
	}
}

/* DA101: split the words scanned since the last break (WORDS[0 .. WORD_COUNT-1]) into phrases, working from the
 * middle outwards, then open the last phrase with a P command whose kind comes from the punctuation. With a slow
 * rate (RATE_BREAKS), extra ')' breaks go between content and function words, thinned by BREAK_MASK. */
void phrase_breaks(int punct)
{
	int from = 0, level, mid = 0, spread, found, i, lo, p;
	int count = rw(WORD_COUNT);
	if (count >= 25) {
		level = 2;
		mid = s16(count * 4) / 10;
	} else if (count >= 14) {
		level = 1;
		mid = (count + 1) >> 1;
	} else if (count <= 0) {
		return;
	} else {
		level = 0;
	}
	spread = (count >> 3) + 1;
	for (; level > 0; level--) {
		found = 0;
		for (i = mid + spread;; i--) {
			lo = mid - spread < from + 5 ? from + 5 : mid - spread;
			if (i < lo)
				break;
			if (break_before_function_word(i)) {
				insert_phrase_break(from, ',', i);
				from = i;
				found = 1;
				break;
			}
		}
		if (!found) {
			for (i = mid + spread;; i--) {
				lo = mid - spread < from + 5 ? from + 5 : mid - spread;
				if (i < lo)
					break;
				if (break_second_rule(i)) {
					insert_phrase_break(from, ',', i);
					from = i;
					found = 1;
					break;
				}
			}
		}
		if (!found) {
			for (i = mid + spread; i >= mid - spread; i--) {
				if (break_third_rule(i)) {
					insert_phrase_break(from, '\\', i);
					from = i;
					found = 1;
					break;
				}
			}
		}
		if (found) {
			mid = mul_div_s(rw(WORD_COUNT), 7, 10);
		} else {
			mid = (rw(WORD_COUNT) + 1) >> 1;
			spread += 2;
		}
	}

	if (rw(FREE_COUNT) < 3)
		return;
	p = node_insert(word(from), 0, NODE_COMMAND, 'P');
	if (rw(p) == rw(PR_FIRST))
		ww(PR_FIRST, p);
	if (punct == '.') {
		ww(p + N_DUR, (rw(p + N_DUR) & 0xFF00) | 1);
	} else if (punct == ',') {
		ww(p + N_DUR, (rw(p + N_DUR) & 0xFF00) | 3);
	} else if (punct == '?') {
		if (word_class(0) == 13)
			question_start(p, word(0));
		else if (word_class(from) == 13)
			question_start(p, word(from));
		else
			ww(p + N_DUR, (rw(p + N_DUR) & 0xFF00) | 4);
	} else {
		ww(p + N_DUR, (rw(p + N_DUR) & 0xFF00) | 2);
	}
	set_first_phrase(p);

	if (rw(RATE_BREAKS) == 0)
		return;
	int speed = rw(PR_SPEED), prev_ch = 0, n;
	for (n = word(0); n != word(rw(WORD_COUNT) - 1) && rw(FREE_COUNT) > 3; n = node_next(n)) {
		if (node_kind(n) == NODE_COMMAND && node_char(n) == 'r') {
			speed = s16(node_dur(n) - 46) >> 3;
		} else if (node_kind(n) == NODE_COMMAND && node_char(n) == 'v') {
			speed = node_dur(n);
		} else if ((node_char(n) == '%' || node_char(n) == '&') && node_kind(n) == NODE_SYMBOL) {
			if (node_char(n) == '%' && prev_ch == '&' && speed < 9 && node_char(node_prev(n)) != ',') {
				int k = rw(BREAK_COUNT);
				ww(BREAK_COUNT, k + 1);
				if (rw(BREAK_MASK + 2 * speed) & rw(BITMASK + 2 * (k & 15)))
					node_insert(n, 0, NODE_SYMBOL, ')');
			}
			prev_ch = node_char(n);
		}
	}
}

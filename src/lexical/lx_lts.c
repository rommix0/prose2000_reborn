/* The letter-to-sound rules, the context patterns they and the affix tables use, and the vowel reduction. */
#include "lexical.h"

static void set_stress(int n, int level)
{
	ww(n + N_FLAGS, (rw(n + N_FLAGS) & 0xFFE7) | (level & 3) << 3);
}

static int is_consonant_letter(int ch)
{
	return (feature(ch, 0x80) & 0x20) != 0;
}

/* a class byte (bit 7 set) of a context pattern: DS:0048 gives a feature mask and plane */
static int class_has(int cls, int ch)
{
	unsigned v = ruw(CLASS_MASKS + 2 * (cls & 0x7F));
	return (v & 0xFF & (unsigned)(int8_t)rb(FEATURES + ((((v & 0xFF00) >> 1) | (unsigned)ch) & 0xFFFF))) != 0;
}

/* D81AB: match a context pattern next to node n, looking left (forward = 0) or right. A pattern is a list of
 * elements, each a header byte and a 0-terminated tail, ended by an empty element; it matches as soon as one
 * element decides it (an element that matches, or a negated one that does not). Header bits 7-5 select:
 *   010 / 011  (no) vowel letter to the left before a syllable boundary (Y after a consonant letter ends it)
 *   101 / 110  (not) exactly one vowel to the left in the word
 *   001        more than one consonant since the last vowel (LTS_CONS > 1)
 *   100        a doubled consonant letter next to n
 *   other      letters or classes (bytes with bit 7) in the tail, tested on the next 1 node, or on 2-5 nodes
 *              (bit 4 set, count = (bits 0-1) + 2); bit 2 inverts a test, bit 3 negates the element, bits 2-4 all
 *              set stop at the first node that matches. */
int match_context_pattern(unsigned pat, int n, int forward)
{
	int r = 0, neg, stop_first = 0;
	if (rb(pat) == 0)
		return 1;
	for (;;) {
		int kind, ch, si = n;
		r = 0;
		neg = 0;
		kind = rsb(pat) & 0xE0;
		if (kind == 0x40 || kind == 0x60) {
			for (;;) {
				si = node_prev(si);
				if (si == 0)
					break;
				ch = node_char(si);
				if (feature(ch, 0x100) & 2)
					break;
				if (feature(ch, 0) & 8)
					r = 1;
				if (ch == 'Y' && is_consonant_letter(node_char(rw(si + 2))))
					break;
			}
			if (kind == 0x60) {
				neg = 1;
				r = !r;
			}
		} else if (kind == 0xA0 || kind == 0xC0) {
			int vowels = 0;
			while ((si = node_prev(si)) != 0) {
				ch = node_char(si);
				if (feature(ch, 0x100) & 2)
					vowels++;
				if (feature(ch, 0) & 8)
					break;
				if (ch == 'Y' && is_consonant_letter(node_char(rw(si + 2))))
					vowels++;
			}
			if (vowels == 1)
				r = 1;
			if (kind == 0xC0) {
				neg = 1;
				r = !r;
			}
		} else if (kind == 0x20) {
			if (s16(rw(LTS_CONS)) <= 1)
				return 0;
			r = 1;
		} else if (kind == 0x80) {
			if (forward == 1) {
				si = node_next(si);
				if (si != 0) {
					ch = node_char(si);
					if (is_consonant_letter(ch) && node_char(rw(si)) == ch)
						r = 1;
				}
			} else {
				si = node_prev(si);
				if (si != 0) {
					ch = node_char(si);
					if (is_consonant_letter(ch) && node_char(rw(si + 2)) == ch)
						r = 1;
				}
			}
		} else {
			unsigned start = pat;
			int hdr = rsb(pat);
			int multi = hdr & 0x10, mode = hdr & 0xC, miss = 0, count, i;
			neg = mode & 8;
			stop_first = (hdr & 0x1C) == 0x1C;
			count = multi ? (hdr & 3) + 2 : 1;
			for (i = 1; i <= count; i++) {
				si = forward == 0 ? node_prev(si) : node_next(si);
				if (si == 0) {
					r = 0;
					break;
				}
				ch = node_char(si);
				if (i == 1 || !multi) {
					pat = start + 1;
					if (mode & 4) {
						r = 0;
						miss = 1;
					} else {
						r = 1;
						miss = 0;
					}
				}
				if (mode == 0 || mode == 0xC) {
					/* every byte must match (with multi: the first byte decides) */
					for (;;) {
						int c = rsb(pat++);
						if (c == 0)
							break;
						if (c & 0x80 ? !class_has(c, ch) : c != ch) {
							r = miss;
							break;
						}
						if (multi)
							break;
					}
				} else {
					/* no byte may match (with multi: only the first byte is tested) */
					for (;;) {
						int c = rsb(pat++);
						if (c == 0)
							break;
						if (c & 0x80 ? class_has(c, ch) : c == node_char(si)) {
							r = miss;
							break;
						}
						if (multi)
							break;
					}
				}
				if (r == 0 && !multi)
					break;
				if (stop_first && r != 0)
					break;
			}
		}
		if (r != 0 && neg == 0)
			return r;
		if (r == 0 && neg != 0)
			return r;
		while (rb(pat++) != 0)
			;
		if (rb(pat) == 0)
			return r;
	}
}

/* D80F3: a rule's class condition (+6 -> two words) against LTS_STATE / LTS_CLASS: all bits present; with bit 15
 * of the first word none present; with bit 15 of the second any present. */
int lts_class_match(int rule)
{
	unsigned left = ruw(rw(rule + 6)), right = ruw(rw(rule + 6) + 2);
	if (!(left & 0x8000) && !(right & 0x8000))
		return (ruw(LTS_STATE) & left) == left && (ruw(LTS_CLASS) & right) == right;
	if (left & 0x8000)
		return !(ruw(LTS_STATE) & left) && !(ruw(LTS_CLASS) & right);
	return (ruw(LTS_STATE) & left) || (ruw(LTS_CLASS) & right);
}

/* D8165: the rule's letters (+0), read leftwards from LTS_AT; LTS_AT ends on the leftmost one. */
int lts_letters_match(int rule)
{
	for (unsigned s = ruw(rule); rb(s) != 0; s++) {
		ww(LTS_AT, node_prev(rw(LTS_AT)));
		int n = rw(LTS_AT);
		if (n == 0 || rb(n + N_CHAR) != rb(s) || node_kind(n) != NODE_TEXT)
			return 0;
	}
	return 1;
}

/* D8551: finish the vowel LTS_VOWEL now that the letters to its left are converted: U (/ju/) becomes Y + b (/j/ +
 * /u/), or just b after some consonants, which may palatalize before it (D T Z S -> J C z s); and an unstressed
 * vowel in an open syllable (LTS_OPEN) reduces to schwa ('@'), the reduced high vowel ('|'), or '3' for the
 * r-coloured ones. `final` = 1 at the end of the word part. */
void vowel_reduce(int final)
{
	int v = rw(LTS_VOWEL), si = rw(v + 2), pc = node_char(si);
	int vc, n1, n2, t, ch;
	if (pc == '[') {
		si = rw(rw(LTS_VOWEL) + 2);
		pc = node_char(si);
	}
	if (node_char(v) == 'U') {
		wb(v + N_CHAR, 'b');
		if ((feature(pc, 0x100) & 4) || (feature(pc, 0x180) & 1)) {
			if (pc == 'S' && rw(v) != 0) {
				n1 = node_char(rw(v));
				if (n1 == 'R' || (n1 != 'L' && final == 0))
					wb(si + N_CHAR, 's');
			} else if (node_stress(v) == 0 && final == 0) {
				switch (pc) {
				case 'D':
					wb(si + N_CHAR, 'J');
					break;
				case 'N':
					node_insert(v, 0, NODE_SYMBOL, 'Y');
					break;
				case 'T':
					wb(si + N_CHAR, 'C');
					break;
				case 'Z':
					wb(si + N_CHAR, 'z');
					break;
				}
			}
		} else if (!(feature(pc, 0x180) & 4) && pc != 'X') {
			node_insert(v, 0, NODE_SYMBOL, 'Y');
			t = rw(v + 2);
			ww(t + N_FLAGS, (rw(t + N_FLAGS) & 0xFFDF) | (node_bit(v, 5) << 5));
		}
		if (pc == 'H')
			ww(LTS_OPEN, 0);
	}
	if (rw(LTS_OPEN) == 1) {
		vc = node_char(rw(LTS_VOWEL));
		if (!match_context_pattern(0x2E3E, si, 0) && pc == 'E' && node_char(rw(si + 2)) == 'L' && vc != 'v' &&
		    vc != 'a' && vc != 'e')
			wb(si + N_CHAR, 'Y');
		n1 = n2 = 0;
		t = node_next(rw(LTS_VOWEL));
		if (t != 0) {
			n1 = node_char(t);
			t = node_next(t);
			if (t != 0)
				n2 = node_char(t);
		}
		if (!match_context_pattern(0x2E3E, rw(LTS_VOWEL), 0) && vc == 'O' && is_consonant_letter(n1) &&
		    ((feature(n2, 0) & 8) || n2 == 0) && rb(STRESS_BYTE) == 0)
			goto done;
		if (match_context_pattern(0x2E3E, rw(LTS_VOWEL), 0) && rw(LTS_SECOND) == 0 && rb(STRESS_BYTE) == 0 &&
		    is_consonant_letter(n1)) {
			int keep;
			if (!is_consonant_letter(n2) || ((feature(n1, 0) & 0x20) && n2 == 'R') ||
			    (n1 == 'K' && n2 == 'W') || n1 == n2)
				keep = vc == 'b' || (vc == 'O' && n2 != '[');
			else if ((feature(node_char(rw(si + 2)), 0) & 8) && pc == 'K' && vc == 'o' && n1 == 'N')
				keep = vc == 'b' || (vc == 'O' && n2 != '[');
			else
				keep = 1;
			if (keep) {
				ww(LTS_STATE, rw(LTS_STATE) | 0x4000);
				goto done;
			}
		}
		if (feature(vc, 0x200) & 0x40)
			goto done;
		switch (vc) {
		case 'E':
		case 'I':
		case 'i':
			ch = '|';
			break;
		case 'e':
			ch = n1 == 'L' || n1 == 'R' ? '@' : '|';
			break;
		case 'g':
		case 'k':
		case 'r':
			ch = '3';
			break;
		default:
			if (((feature(n1, 0x180) & 4) || (feature(n1, 0x100) & 0x80)) && !(feature(pc, 0) & 8))
				ch = '|';
			else if ((feature(pc, 0x180) & 4) && n1 == 'S')
				ch = '|';
			else
				ch = '@';
			break;
		}
		wb(rw(LTS_VOWEL) + N_CHAR, ch);
	}
done:
	ww(LTS_VOWEL, 0);
}

/* D78AE: convert the letters of the word that are still letters, right to left: the stem up to LX_END, then (if
 * a suffix was split off) the suffix from WORD_END. For each letter the rules of LTS_RULES[letter - '@'] are tried
 * in order; a rule (10 bytes) is +0 the letters to its left that it also consumes, +2 its output, reversed
 * (optionally led by control bytes: 0x10 = drop a '[' before a stem found in the lexicon, < 0x10 = extra
 * syllables), +4 a context pattern matched leftwards from the leftmost consumed letter (match_context_pattern),
 * +6 its condition on the bits the rules to the right passed on (lts_class_match), +8 the bits it passes on.
 * On the way the rules count syllables and place the stress (third syllable from the end, or the pattern of a
 * stress-only lexicon entry), and vowel_reduce reduces the unstressed vowels. */
void lts_rules(void)
{
	int8_t last = 0, pattern_left = 4, pattern;
	int syllables = 0, extra = 0, carry = 0, first = 1, stressed = 0, just = 0;
	int stop = rw(WORD_REC), pattern_used = 0, has_suffix = 0, boundary = 0, boundary_prev = 0;
	int right_keep = 0, affix_bits = 0, level = 2, rule = 0, c, t;
	unsigned out = 0;

	ww(LTS_VOWEL, 0);
	ww(LTS_AT, 0);
	ww(LTS_LETTER, 0);
	ww(LTS_SECOND, 0);
	if (rw(EMPHASIS) == 1)
		level = 3;
	else if (rw(SECONDARY) == 1)
		level = 1;
	ww(LTS_CONS, 1);
	ww(LTS_STATE, 1);
	ww(LTS_CLASS, 0);
	ww(LTS_OPEN, 0);
	ww(LTS_LETTER, rw(LX_END));
	if (rw(LX_END) != rw(WORD_END))
		has_suffix = 1;
	pattern = (int8_t)rb(STRESS_BYTE);
	if (rb(AFFIX_CODE) != 0) {
		switch (rsb(AFFIX_CODE)) {
		case 2:
			affix_bits = 0x80;
			break;
		case 3:
			affix_bits = 0x100;
			break;
		case 4:
			affix_bits = 0x200;
			break;
		case 5:
			affix_bits = 0x400;
			break;
		case 6:
			affix_bits = 0x1000;
			break;
		case 7:
			affix_bits = 0x800;
			break;
		}
		ww(LTS_CLASS, rw(LTS_CLASS) | affix_bits);
	}
	do {
		int cur = rw(LTS_LETTER);
		if (node_kind(cur) != NODE_TEXT) {
			/* phonemes from the lexicon (a stem found after stripping an affix) */
			boundary = 1;
			if (rw(LTS_SECOND) == 1) {
				if (rw(LTS_VOWEL) != 0)
					vowel_reduce(0);
				return;
			}
			int s = node_stress(cur);
			if (s != 0) {
				just = 1;
				if (s != 1 || rw(SECONDARY) != 0)
					stressed = 1;
			} else if (feature(node_char(cur), 0x100) & 2)
				just = 0;
			ww(LTS_STATE, 0);
			t = node_prev(cur);
			if (t != 0 && node_char(t) == '[') {
				if (is_consonant_letter(node_char(cur)))
					ww(LTS_CONS, rw(LTS_CONS) + 1);
				else {
					ww(LTS_STATE, 2);
					if (!(feature(node_char(cur), 0x200) & 0x40))
						ww(LTS_STATE, rw(LTS_STATE) | 0x4000);
				}
				ww(LTS_CLASS, rw(LTS_CLASS) | 4);
			}
			ww(LTS_LETTER, t);
			goto next_letter;
		}
		c = node_char(cur);
		if (c < 0x40 || c > 0x5B) {
			ww(LTS_LETTER, node_delete(cur, 0));
			goto next_letter;
		}
		/* find the rule */
		rule = rw(LTS_RULES + 2 * (c - 0x40));
		ww(LTS_AT, rw(LTS_LETTER));
		while (!lts_class_match(rule) || !lts_letters_match(rule) ||
		       !match_context_pattern(ruw(rule + 4), rw(LTS_AT), 0)) {
			ww(LTS_AT, rw(LTS_LETTER));
			rule += 10;
		}
		while (rw(LTS_AT) != rw(LTS_LETTER))
			ww(LTS_AT, node_delete(rw(LTS_AT), 1));
		ww(LTS_LETTER, node_delete(rw(LTS_LETTER), 0));
		if (node_kind(rw(LX_FIRST)) == NODE_FREE)
			boundary = 1;
		ww(LTS_AT, 0);
		out = ruw(rule + 2);
		if (rb(out) != 0 && rsb(out) < 0x20) {
			if (rsb(out) == 0x10) {
				int ws = rw(WORD_START);
				if (!boundary && node_char(rw(rw(LX_FIRST) + 2)) == '[' &&
				    !(node_char(ws) == 'M' && node_char(rw(ws)) == 'C') &&
				    !(node_char(ws) == 'H' && node_char(rw(ws)) == '[')) {
					node_delete(rw(rw(LX_FIRST) + 2), 0);
					ww(LX_FIRST, rw(WORD_START));
				}
				out++;
			}
			if (rsb(out) < 0x10) {
				extra = rsb(out);
				ww(LTS_CONS, 0);
				out++;
				first = 0;
			}
		}
		/* write the output, right to left after the letter's place */
		while ((c = rsb(out)) != 0) {
			if (c != last || !is_consonant_letter(c)) {
				ww(LTS_AT, node_insert(rw(LTS_LETTER), 1, NODE_SYMBOL, c));
				if (node_kind(rw(WORD_END)) == NODE_FREE)
					ww(WORD_END, rw(LTS_AT));
			}
			out++;
			if (feature(c, 0) & 1) {
				/* a vowel */
				if (first && s16(rw(LTS_CONS)) > 0)
					ww(LTS_CONS, rw(LTS_CONS) - 1);
				if (rw(rw(rule + 8) + 2) & 8)
					ww(LTS_OPEN, 0);
				if (rw(LTS_VOWEL) != 0) {
					vowel_reduce(0);
					if (!(feature(node_char(rw(LTS_AT)), 0) & 1)) {
						syllables++;
						goto closed;
					}
				}
				if (rb(STRESS_BYTE) != 0 && !boundary_prev && pattern_left-- > 0) {
					/* the stress pattern of a stress-only lexicon entry, last syllable first */
					pattern_used = 1;
					int two = pattern & 3;
					pattern = (int8_t)(pattern >> 2);
					switch (two) {
					case 3:
						set_stress(rw(LTS_AT), level);
						stressed = 1;
						just = 1;
						ww(LTS_OPEN, 0);
						break;
					case 2:
						set_stress(rw(LTS_AT), 1);
						just = 1;
						ww(LTS_OPEN, 0);
						break;
					case 1:
						just = 0;
						ww(LTS_OPEN, 0);
						break;
					case 0:
						just = 0;
						ww(LTS_OPEN, 1);
						ww(LTS_VOWEL, rw(LTS_AT));
						break;
					}
				} else if (!stressed) {
					/* stress the third syllable from the end (the second after a heavy one) */
					if (syllables == 1 && s16(rw(LTS_CONS)) > 1)
						syllables++;
					syllables += extra;
					extra = 0;
					syllables++;
					if (syllables >= 3) {
						set_stress(rw(LTS_AT), level);
						stressed = just = 1;
					}
				} else if (rw(LTS_SECOND) == 0) {
					/* alternate secondary stress to the left of the main one */
					if (just)
						just = 0;
					else {
						set_stress(rw(LTS_AT), 1);
						just = 1;
					}
				}
				if (pattern_used) {
					pattern_used = 0;
					goto open_done;
				}
				t = rw(LTS_AT);
				if (first == 1 && rw(t) != 0 && (feature(node_char(rw(t)), 0x100) & 0x80))
					goto closed;
				if (c == 'U' && rw(t + 2) != 0 && (feature(node_char(rw(t + 2)), 0) & 8))
					goto closed;
				if (just)
					goto closed;
				if (rw(LTS_CONS) == 0 && c != 'o' && c != 'a')
					goto closed;
				if (feature(c, 0x200) & 0x40)
					goto closed;
				if (rw(rw(rule + 8)) & 0x4000)
					goto closed;
				ww(LTS_OPEN, 1);
				goto open_done;
			closed:
				ww(LTS_OPEN, 0);
			open_done:
				ww(LTS_CONS, 0);
				carry = 0;
				ww(LTS_VOWEL, rw(LTS_AT));
				first = 0;
			} else if (c != '[') {
				if ((s16(rw(LTS_CONS)) > 0 && syllables != 0) || s16(rw(LTS_CONS)) > 1)
					carry = 0;
				if ((affix_bits & 0x400) && rb(out) == 'T' && c == 'S')
					;
				else if (rb(out) == '[')
					;
				else if (rw(LTS_CONS) == 1 && ((last == 'R' && (feature(c, 0x100) & 1)) ||
				                               ((c == 'K' || c == 'G') && last == 'W')))
					;
				else
					ww(LTS_CONS, rw(LTS_CONS) + 1);
			}
			if (c == '[') {
				if (feature(last, 0) & 0x80)
					c = last;
				if (node_stress(rw(WORD_REC)) == 1) {
					just = 1;
					ww(rw(WORD_REC) + N_FLAGS, rw(rw(WORD_REC) + N_FLAGS) & 0xFFE7);
				}
			}
			last = (int8_t)c;
		}
		/* the rule's output is written */
		boundary_prev = boundary;
		if ((boundary_prev == 1 || rw(LTS_SECOND) == 1) && rw(LTS_VOWEL) != 0) {
			if (!stressed) {
				set_stress(rw(LTS_VOWEL), level);
				stressed = just = 1;
				ww(LTS_OPEN, 0);
			}
			vowel_reduce(1);
			if (rw(LTS_STATE) & 0x4000)
				ww(LTS_OPEN, 0);
		}
		{
			int flags = rw(rw(rule + 8)) & 0x7FFF;
			if (just == 1 && (feature(last, 0x100) & 2))
				flags |= 0x4000;
			ww(LTS_CLASS, rw(rw(rule + 8) + 2) & 0x7FFF);
			if (right_keep != 0)
				ww(LTS_CLASS, rw(LTS_CLASS) | right_keep);
			else
				right_keep = rw(LTS_CLASS) & 4;
			if (affix_bits != 0)
				ww(LTS_CLASS, rw(LTS_CLASS) | affix_bits);
			else
				affix_bits = rw(LTS_CLASS) & 0x1F80;
			if (flags != 0) {
				if (flags == 1)
					ww(LTS_STATE, rw(LTS_STATE) | 1);
				else
					ww(LTS_STATE, carry | flags);
				carry = flags & 0x2048;
				if (carry != 0 && syllables != 0)
					ww(LTS_CONS, 0);
			}
		}
		if (node_char(rw(LTS_LETTER)) == '[') {
			if (rb(STRESS_BYTE) != 0)
				ww(LTS_CLASS, rw(LTS_CLASS) | 4);
			else if (just == 1 || rw(LTS_OPEN) == 0)
				ww(LTS_STATE, rw(LTS_STATE) | 0x4000);
		}
	next_letter:
		if (rw(LTS_LETTER) == stop && has_suffix == 1) {
			/* now the suffix */
			ww(LTS_LETTER, rw(WORD_END));
			ww(LTS_STATE, 1);
			if (affix_bits != 0)
				ww(LTS_CLASS, rw(LTS_CLASS) | affix_bits);
			else
				ww(LTS_CLASS, 0);
			right_keep = 0;
			ww(LTS_SECOND, 1);
			carry = 0;
			syllables = 0;
			ww(LTS_CONS, 1);
			last = 0;
			just = 0;
		}
	} while (rw(LTS_LETTER) != stop);
}

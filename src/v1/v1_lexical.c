/* v1.1 lexical stage (F5A7B): turns each word the text rules deliver into phoneme symbols.
 *
 * A word arrives as a word record ('&', kind 2) followed by its letters (kind 2 text nodes), optionally preceded by
 * '~' (emphasis). The stage looks the word up in the pronunciation lexicon (lex_lookup), stripping suffixes and
 * prefixes and repairing the stem if the whole word is not there (affixes). What the lexicon does not cover goes
 * through the letter-to-sound rules (lts_rules), which also place the stress and reduce unstressed vowels.
 *
 * v1.1's lexicon lives in the data segment (v3.4.1's has its own segment): an index by first letter and word length
 * at [DS:4890], with keys packed by the same letter-state machine as v3.4.1's (REFERENCE ยง9.6). */
#include "v1.h"

#define R V1_REC_LEXICAL
#define LX_START (R + V1_REC_DONE)    /* first letter of the word */
#define LX_NEXT (R + V1_REC_CURSOR)   /* the node after the word */
#define LX_END (R + V1_REC_AHEAD)     /* last letter of the word */
#define LX_FIRST (R + 0x0A)           /* first letter of the part being looked up */
#define LX_LAST (R + 0x0C)            /* its last letter */

#define AFFIX 0xA542      /* the suffix record last matched */
#define ADDED_E 0xA544    /* stem_repair added an E that did not help */
#define LEX_STRESS 0xA546 /* byte: stress pattern of a stress-only lexicon entry (2 bits per vowel), 0 if none */
#define EMPHASIS 0xA548   /* '~' before the word */
#define LEX_CLASS 0xA54A  /* byte: word class from the lexicon or a suffix, for the word record's +6 */
#define LAST_VOWEL 0xA54C /* the last vowel written, not yet finished by vowel_finish */
#define LTS_AT 0xA54E     /* match position, then the last phoneme written */
#define LTS_LETTER 0xA550 /* the letter being converted (the walk goes right to left) */
#define LTS_FLAGS 0xA552  /* state bits passed on by the rule to the right */
#define LTS_REDUCE 0xA554 /* the last vowel may be reduced */
#define LTS_CONS 0xA556   /* consonants since the last vowel */

static int ch_of(unsigned n) { return rsb(n + V1_NODE_CH); }
static int kind_of(unsigned n) { return rw(n + V1_NODE_FLAGS) & 7; }
static unsigned nx(unsigned n) { return ruw(n + V1_NODE_NEXT); }
static unsigned pv(unsigned n) { return ruw(n + V1_NODE_PREV); }

/* the feature table at DS:0090 by character, in one of its planes (0, 0x80, 0x100, 0x180, 0x200) */
static int feat(int c, int plane) { return rb(0x90u + (((unsigned)(int8_t)c | (unsigned)plane) & 0xFFFFu)); }

static void set_stress(unsigned n, int level)
{
	ww(n + V1_NODE_FLAGS, (rw(n + V1_NODE_FLAGS) & 0xFFE7) | (level & 3) << 3);
}

/* F673A: does the context pattern match next to node n (dir 0: to its left, 1: to its right)? A pattern is a list
 * of 0-terminated alternatives; the first byte of each says what it tests. */
int v1_context_match(unsigned pat, unsigned node, int dir)
{
	int r = 0;
	if (rb(pat) == 0)
		return 1;
	do {
		unsigned n = node;
		int cls = rb(pat) & 0xE0;
		if (cls & 0x40) { /* a vowel to the left, before a syllable boundary */
			for (;;) {
				n = v1_node_prev(n);
				if (n == 0)
					break;
				int c = ch_of(n);
				if (feat(c, 0x100) & 2)
					break;
				if (feat(c, 0) & 8)
					r = 1;
				if (c == 'Y' && (feat(ch_of(pv(n)), 0x80) & 0x20))
					break;
			}
		} else if (cls & 0x20) { /* two consonants or more since the vowel */
			if (rw(LTS_CONS) <= 1)
				return 0;
			r = 1;
		} else if (cls & 0x80) { /* a doubled consonant */
			n = dir == 1 ? v1_node_next(n) : v1_node_prev(n);
			if (n != 0 && (feat(ch_of(n), 0x80) & 0x20) && ch_of(dir == 1 ? nx(n) : pv(n)) == ch_of(n))
				r = 1;
		} else { /* up to 3 nodes, each (or, with bit 4, the next) tested against characters and classes */
			unsigned start = pat;
			int one = rb(pat) & 0x10, mode = rb(pat) & 0xC, count = rb(pat) & 3, neg = 0;
			for (int i = 1; i <= count; i++) {
				n = dir == 0 ? v1_node_prev(n) : v1_node_next(n);
				if (n == 0) {
					r = 0;
					break;
				}
				int c = ch_of(n);
				if (i == 1 || one == 0) {
					pat = start + 1;
					if (mode & 4) {
						r = 0;
						neg = 1;
					} else {
						r = 1;
						neg = 0;
					}
				}
				for (;;) {
					int b = rsb(pat++), hit;
					if (b == 0)
						break;
					if (b & 0x80) {
						unsigned m = ruw(0x30 + 2 * (unsigned)(b & 0x7F));
						hit = (rb(0x90u + ((unsigned)((s16((int)(m & 0xFF00)) >> 1) | (int8_t)c) & 0xFFFFu)) & m &
						       0xFF) != 0;
					} else {
						hit = (int8_t)b == (int8_t)c;
					}
					/* modes 0 and 12 (F6919): an entry that does not hold decides; modes 4 and 8 (F6989): one that does */
					if (mode == 0 || mode == 12 ? !hit : hit) {
						r = neg;
						break;
					}
					if (one)
						break;
				}
				if (r == 0 && one == 0)
					break;
				if ((rb(start) & 0x1C) == 0x1C && r == 1)
					break;
			}
		}
		if (r == 1)
			return 1;
		while (rb(pat++) != 0)
			;
	} while (rb(pat) != 0);
	return r;
}

/* F6B9F: look up LX_FIRST..LX_LAST in the lexicon. A found word's letters become its phonemes (with their stress);
 * a stress-only entry just sets LEX_STRESS for the letter-to-sound rules. Returns 1 if found.
 *
 * Firmware bug, fixed here: the lookup does not check the length against the size of the first letter's index array
 * (the distance to the next letter's array). For long X, Y and Z words it reads past the end of the index and takes
 * lexicon bytes for bucket pointers; some of those buckets run into RAM, stack included (e.g. Z + 4 letters: ZEBRA
 * scans DS:3661-9382), so the firmware's result depends on its call frames and interrupts. The C treats such a word
 * as not in the lexicon (no word that long with that letter is in it) and counts the case in v1_lex_bound_rejects
 * (REFERENCE ง16). */
long v1_lex_bound_rejects;

int v1_lex_lookup(void)
{
	uint8_t key[16] = {0};
	int stress = 0, mark = 0, cut_right = rw(LX_LAST) != rw(LX_END), cut_left = rw(LX_FIRST) != rw(LX_START);
	int st = 2, pos = 0, len = 0;
	unsigned n = nx(ruw(LX_FIRST));
	count32(0xA4F2);
	for (; nx(ruw(LX_LAST)) != n; n = nx(n), len++) { /* pack the letters after the first */
		if (len >= 16)
			return 0;
		st = rsb(0x47EC + 12 * st);
		int shift = rb(0x47EE + 12 * st);
		key[pos] |= (uint8_t)((ruw(0x47E8 + 12 * st) & (unsigned)ch_of(n)) << shift);
		if (st != 0) {
			pos++;
			key[pos] = (uint8_t)(rb(0x47EA + 12 * st) & (unsigned)ch_of(n));
		}
	}
	if (st == 2)
		pos--;
	int letter = (int8_t)(ch_of(ruw(LX_FIRST)) - 'A');
	unsigned index = ruw(ruw(0x4890) + 2 * (unsigned)letter);
	unsigned e = ruw(index + 2 * (unsigned)len), end = ruw(index + 2 * (unsigned)len + 2);
	if (e == end || !(ruw(ruw(ruw(0x4890) + 2 * (unsigned)letter + 2)) > e) || !(ruw(ruw(ruw(0x4890) + 0x34)) > e))
		return 0;
	if (letter >= 0 && letter < 26 && (unsigned)len + 1 > (ruw(ruw(0x4890) + 2 * (unsigned)letter + 2) - index) / 2) {
		v1_lex_bound_rejects++; /* the firmware would scan a bucket read from past the index */
		return 0;
	}
	do {
		int code = rb(e) & 0x1F, k = 0, cmp_mask;
		unsigned q;
		if (len != 0) {
			q = e + 1;
			for (; k < pos && key[k] == rb(q); q++, k++)
				;
			cmp_mask = rb(0x47F2 + 12 * st);
		} else {
			cmp_mask = 0;
			pos = 0;
			q = e;
			st = 2;
		}
		if (k == pos && (cmp_mask & rb(q)) == (cmp_mask & key[k])) {
			if (code == 0) { /* stress pattern only */
				wb(LEX_STRESS, rb(q + 1));
				return 1;
			}
			/* the letters give way to the phonemes, after the node before them */
			unsigned d = ruw(LX_FIRST);
			while (d != ruw(LX_LAST))
				d = v1_node_free(d, 1);
			d = v1_node_free(d, 0);
			ww(LX_FIRST, d);
			if (st == 2)
				q++;
			unsigned primary = 0, secondary = 0;
			int ps = rsb(0x47F0 + 12 * st);
			for (int i = 0; i < code; i++) {
				ps = rsb(0x4810 + 8 * ps);
				int shift = rb(0x4812 + 8 * ps);
				int v = (int)((rsb(q) & rw(0x480C + 8 * ps)) >> shift);
				switch (ps) {
				case 1:
				case 2:
					q++;
					v |= rb(q) & rb(0x480E + 8 * ps);
					break;
				case 3: q++; break;
				}
				v = (int8_t)(v | 0x40);
				if ((v & 0x3C) == 0x1C)
					v = (int8_t)(v - 0x2B);
				if (v == '1') { /* primary stress on the next vowel */
					mark = 1;
					stress = rw(EMPHASIS) == 1 ? 3 : 2;
				} else if (v == '2') {
					stress = 1;
				} else {
					d = v1_node_insert(d, 1, 3, v);
					if (stress != 0) {
						set_stress(d, stress);
					} else if (mark != 1 && (feat(v, 0) & 1) && primary == 0) {
						if (!(feat(v, 0x200) & 0x40))
							primary = d;
						else if (secondary == 0)
							secondary = d;
					}
					stress = 0;
				}
			}
			ww(LX_LAST, d);
			ww(LX_FIRST, rw(ruw(LX_FIRST) + V1_NODE_NEXT));
			if (!cut_left)
				ww(LX_START, rw(LX_FIRST));
			if (!cut_right)
				ww(LX_END, d);
			int cls = rb(e) & 0xE0;
			if (cls != 0 && rw(EMPHASIS) == 0) {
				if (cls != 0xC0)
					wb(pv(ruw(LX_START)) + V1_NODE_CH, '%');
				if (rb(LEX_CLASS) == 0)
					wb(LEX_CLASS, cls >> 5);
			}
			if (mark != 1) { /* no stress in the entry: the first full vowel (or else a reduced one) gets it */
				if (primary == 0)
					primary = secondary;
				if (primary != 0) {
					if (rw(EMPHASIS) == 1)
						ww(primary + V1_NODE_FLAGS, rw(primary + V1_NODE_FLAGS) | 0x18);
					else
						set_stress(primary, 2);
				}
			}
			return 1;
		}
		int size = pos; /* the key bytes */
		e = (e + (code != 0 ? (unsigned)(rsb(0x482C + 33 * st + code) + size) : (unsigned)(size + 3))) & 0xFFFF;
	} while (e < end);
	return 0;
}

/* F5E49: match the affix list against the word from `from` (dir 0: suffixes, walking left from the last letter; 1:
 * prefixes, walking right) with its context pattern. A match gets a '[' boundary and moves LX_LAST / LX_FIRST past
 * it. Returns the affix record, or 0. */
unsigned v1_affix_match(unsigned p1, unsigned p2, unsigned list, int dir)
{
	unsigned from = dir == 0 ? p2 : p1, to = dir == 0 ? p1 : p2, n;
	for (;; list += 2) {
		if (rw(list) == 0)
			return 0;
		unsigned s = ruw(ruw(list));
		n = from;
		while (ch_of(n) == rsb(s) && n != to) {
			s++;
			if (rb(s) == 0)
				break;
			n = dir == 0 ? pv(n) : nx(n);
		}
		if (rb(s) == 0 && v1_context_match(ruw(ruw(list) + 2), n, dir))
			break;
	}
	if (dir == 0) {
		ww(LX_LAST, pv(n));
		v1_node_insert(n, 0, 2, '[');
	} else {
		ww(LX_FIRST, nx(n));
		v1_node_insert(n, 1, 2, '[');
	}
	return ruw(list);
}

/* F5F40: the stem left by a suffix is not in the lexicon: undouble a final consonant, add an E (always after an E
 * was added before, and for two suffixes when the context patterns at DS:29D8 / 2A30 say so), or change I to Y.
 * Returns 1 if the repaired stem was found. */
int v1_stem_repair(void)
{
	unsigned n = ruw(LX_LAST);
	if ((feat(ch_of(n), 0x80) & 0x20) && ch_of(pv(n)) == ch_of(n)) {
		ww(LX_LAST, pv(n));
		if (v1_lex_lookup() == 1) {
			v1_node_free(n, 0);
			return 1;
		}
		ww(LX_LAST, n);
	} else if (rw(ADDED_E) == 1 ||
	           ((rw(AFFIX) == 0x2B70 || rw(AFFIX) == 0x2BCE) && (feat(ch_of(n), 0x80) & 0x20) &&
	            (v1_context_match(0x29D8, nx(n), 0) || !v1_context_match(0x2A30, nx(n), 0)))) {
		n = v1_node_insert(n, 1, 2, 'E');
		ww(LX_LAST, n);
		if (v1_lex_lookup() == 1)
			return 1;
		n = v1_node_free(n, 0);
		ww(LX_LAST, n);
		ww(ADDED_E, 1);
	}
	if (ch_of(n) == 'E' && ch_of(pv(n)) == 'I') {
		ww(LX_LAST, v1_node_free(n, 0));
		wb(ruw(LX_LAST) + V1_NODE_CH, 'Y');
		if (v1_lex_lookup() == 1)
			return 1;
	}
	return 0;
}

/* how far an affix may reach: stop after `limit` letters once a consonant has been passed (limit 3 from the start
 * of the word for suffixes, 2 from the end for prefixes) */
static unsigned stem_limit(unsigned n, unsigned stop, int limit, int forward)
{
	int consonant = 0;
	for (int k = 0; (k < limit || !consonant) && n != stop; k++) {
		consonant |= !(feat(ch_of(n), 0x80) & 0x20);
		if (consonant && k > 1)
			break;
		n = forward ? nx(n) : pv(n);
	}
	return n;
}

/* F5C12: look the word up, else strip suffixes (list by last letter at DS:2BE4) and prefixes (by first letter at
 * DS:2F1C), looking up and repairing the stem after each affix that asks for it. Returns 1 if the whole word was
 * found, else 0 (the letter-to-sound rules do the rest). */
int v1_affixes(void)
{
	ww(LX_LAST, rw(LX_END));
	ww(LX_FIRST, rw(LX_START));
	if (v1_lex_lookup() == 1)
		return 1;
	ww(ADDED_E, 0);
	unsigned n = stem_limit(ruw(LX_START), ruw(LX_END), 3, 1), a, list;
	if (n != ruw(LX_END)) {
		for (list = ruw(0x2BE4 + 2 * (unsigned)ch_of(ruw(LX_LAST))); list != 0; list = ruw(a + 6)) {
			a = v1_affix_match(n, ruw(LX_LAST), list, 0);
			ww(AFFIX, a);
			if (a == 0)
				break;
			if (rsb(a + 5) == 6)
				wb(LEX_CLASS, 6);
			if (rsb(a + 4) == 1 && (v1_lex_lookup() == 1 || v1_stem_repair() == 1))
				return 0;
			count32(0xA4EE);
		}
		n = stem_limit(ruw(LX_LAST), ruw(LX_START), 2, 0);
		if (n != ruw(LX_START)) {
			for (list = ruw(0x2F1C + 2 * (unsigned)ch_of(ruw(LX_FIRST))); list != 0; list = ruw(a + 6)) {
				a = v1_affix_match(ruw(LX_FIRST), n, list, 1);
				if (a == 0)
					break;
				if (rsb(a + 4) == 1 && (v1_lex_lookup() == 1 || v1_stem_repair() == 1))
					return 0;
				count32(0xA4EA);
			}
		}
	}
	if (rw(ADDED_E) == 1)
		ww(LX_LAST, v1_node_insert(ruw(LX_LAST), 1, 2, 'E'));
	return 0;
}

/* F6A5C: finish the last vowel written. U before a consonant cluster, R or L, or before X, becomes 'b'; without
 * stress it also palatalizes the consonant before it (D J, T C, S s, Z z, N + Y). Otherwise an unstressed vowel the
 * rules marked reducible gets a Y glide (U) and becomes '|' (E, e, i) or '@'. */
void v1_vowel_finish(int final)
{
	unsigned v = ruw(LAST_VOWEL), p = pv(v);
	int c = ch_of(p);
	if (ch_of(v) == 'U') {
		if ((feat(c, 0x100) & 4) || c == 'R' || c == 'L') {
			wb(v + V1_NODE_CH, 'b');
			if (((rw(v + V1_NODE_FLAGS) >> 3) & 3) == 0 && final == 0) {
				switch (c) {
				case 'D': wb(p + V1_NODE_CH, 'J'); break;
				case 'N': v1_node_insert(v, 0, 3, 'Y'); break;
				case 'S': wb(p + V1_NODE_CH, 's'); break;
				case 'T': wb(p + V1_NODE_CH, 'C'); break;
				case 'Z': wb(p + V1_NODE_CH, 'z'); break;
				}
			}
		} else if ((feat(c, 0x180) & 4) || c == 'X') {
			wb(v + V1_NODE_CH, 'b');
		} else if (rw(LTS_REDUCE) == 1) {
			v1_node_insert(v, 0, 3, 'Y');
		}
	}
	if (rw(LTS_REDUCE) == 1) {
		switch (ch_of(ruw(LAST_VOWEL))) {
		case 'E':
		case 'e':
		case 'i': wb(ruw(LAST_VOWEL) + V1_NODE_CH, '|'); break;
		default: wb(ruw(LAST_VOWEL) + V1_NODE_CH, '@'); break;
		}
	}
	ww(LAST_VOWEL, 0);
}

/* F669D: the rule's required state bits (rule +6; with bit 15 set, bits that must be clear) */
int v1_rule_flags_ok(unsigned rule)
{
	count32(0xA4FA);
	unsigned m = ruw(rule + 6);
	if (!(m & 0x8000))
		return (ruw(LTS_FLAGS) & m) == m;
	return (ruw(LTS_FLAGS) & m) == 0;
}

/* F66E6: the rule's extra letters (rule +0) to the left of LTS_AT, which moves onto them */
int v1_rule_letters_ok(unsigned rule)
{
	for (unsigned s = ruw(rule); rb(s) != 0; s++) {
		ww(LTS_AT, v1_node_prev(ruw(LTS_AT)));
		if (rw(LTS_AT) == 0 || ch_of(ruw(LTS_AT)) != rsb(s))
			return 0;
	}
	return 1;
}

/* F6069: the letter-to-sound rules. They walk the word right to left: the rules for each letter (10-byte records
 * by letter at DS:4730: letters to its left, phonemes, left context pattern, required and new state bits) replace
 * the letter (and the letters the rule names) with phonemes. Stress goes on the third vowel from the end unless the
 * lexicon gave a stress pattern; the part after a stripped suffix is done first, then the stem. */
void v1_lts_rules(void)
{
	int prevch = 0, syl = 0, pend = 0, f2000 = 0, first = 1, stressed = 0, skipnext = 0, fromlex = 0, second = 0;
	int hassuffix = 0, hitprefix = 0, prefixdone = 0, level = 2, pattern, ch;
	int8_t budget = 4;
	unsigned stop = pv(ruw(LX_START)), rule = 0;
	ww(LAST_VOWEL, 0);
	ww(LTS_AT, 0);
	ww(LTS_LETTER, 0);
	count32(0xA4F6);
	if (rw(EMPHASIS) == 1)
		level = 3;
	ww(LTS_CONS, 1);
	ww(LTS_FLAGS, 1);
	ww(LTS_REDUCE, 0);
	ww(LTS_LETTER, rw(LX_LAST));
	if (rw(LX_LAST) != rw(LX_END))
		hassuffix = 1;
	pattern = (int8_t)rb(LEX_STRESS);
	do {
		unsigned L = ruw(LTS_LETTER);
		if (kind_of(L) != 2) { /* phonemes already there (a suffix from the lexicon) */
			if (second == 1) {
				if (rw(LAST_VOWEL) != 0)
					v1_vowel_finish(0);
				return;
			}
			int s = (rw(L + V1_NODE_FLAGS) >> 3) & 3;
			if (s != 0) {
				skipnext = 1;
				if (s != 1)
					stressed = 1;
			} else if (feat(ch_of(L), 0x100) & 2) {
				skipnext = 0;
			}
			ww(LTS_LETTER, v1_node_prev(L));
		} else if (ch_of(L) < '@' || ch_of(L) > '[') {
			ww(LTS_LETTER, v1_node_free(L, 0));
		} else {
			rule = ruw(0x4730 + 2 * (unsigned)ch_of(L));
			ww(LTS_AT, L);
			while (!(v1_rule_flags_ok(rule) && v1_rule_letters_ok(rule) && v1_context_match(ruw(rule + 4), ruw(LTS_AT), 0))) {
				ww(LTS_AT, rw(LTS_LETTER));
				rule += 10;
			}
			while (rw(LTS_AT) != rw(LTS_LETTER)) {
				if (rw(LTS_AT) == rw(LX_FIRST))
					hitprefix = 1;
				ww(LTS_AT, v1_node_free(ruw(LTS_AT), 1));
			}
			if (rw(LTS_LETTER) == rw(LX_FIRST))
				hitprefix = 1;
			ww(LTS_LETTER, v1_node_free(ruw(LTS_LETTER), 0));
			ww(LTS_AT, 0);
			unsigned p = ruw(rule + 2);
			if (rsb(p) != 0 && rsb(p) < 0x20) { /* a leading code: 0x10 drops the prefix boundary, else a count */
				if (rsb(p) == 0x10 && hitprefix == 0) {
					unsigned b = pv(ruw(LX_FIRST));
					if (ch_of(b) == '[') {
						v1_node_free(b, 0);
						ww(LX_FIRST, rw(LX_START));
					}
				} else {
					pend = rsb(p);
					ww(LTS_CONS, 0);
					first = 0;
				}
				p++;
			}
			for (; (ch = rsb(p)) != 0; prevch = ch) {
				if (ch != prevch || !(feat(ch, 0x80) & 0x20))
					ww(LTS_AT, v1_node_insert(ruw(LTS_LETTER), 1, 3, ch));
				p++;
				if (feat(ch, 0) & 1) { /* a vowel */
					if (first && rw(LTS_CONS) > 0)
						ww(LTS_CONS, rw(LTS_CONS) - 1);
					if (rw(LAST_VOWEL) != 0)
						v1_vowel_finish(0);
					if ((int8_t)rb(LEX_STRESS) != 0 && prefixdone == 0 && budget-- > 0) {
						fromlex = 1; /* the lexicon's stress pattern, 2 bits per vowel from the right */
						int t = pattern & 3;
						pattern = (int8_t)pattern >> 2;
						switch (t) {
						case 0:
							skipnext = 0;
							ww(LTS_REDUCE, 1);
							ww(LAST_VOWEL, rw(LTS_AT));
							break;
						case 1:
							skipnext = 0;
							ww(LTS_REDUCE, 0);
							break;
						case 2:
							set_stress(ruw(LTS_AT), 1);
							skipnext = 1;
							ww(LTS_REDUCE, 0);
							break;
						case 3:
							set_stress(ruw(LTS_AT), level);
							stressed = 1;
							skipnext = 1;
							ww(LTS_REDUCE, 0);
							break;
						}
					} else if (stressed == 0) {
						if (syl == 1 && rw(LTS_CONS) > 1)
							syl++;
						syl += pend;
						pend = 0;
						syl++;
						if (syl >= 3) {
							set_stress(ruw(LTS_AT), level);
							stressed = 1;
							skipnext = 1;
						}
					} else if (second == 0) {
						if (skipnext) {
							skipnext = 0;
						} else {
							set_stress(ruw(LTS_AT), 1);
							skipnext = 1;
						}
					}
					if (fromlex) {
						fromlex = 0;
					} else {
						unsigned at = ruw(LTS_AT);
						if ((first == 1 && nx(at) != 0 && (feat(ch_of(nx(at)), 0x100) & 0x80)) ||
						    (ch == 'U' && pv(at) != 0 && (feat(ch_of(pv(at)), 0) & 8)))
							ww(LTS_REDUCE, 0);
						else if (skipnext == 0 && (rw(LTS_CONS) != 0 || ch == 'o' || ch == 'a') &&
						         !(feat(ch, 0x200) & 0x40) && !(rw(rule + 8) & 0x4000))
							ww(LTS_REDUCE, 1);
						else
							ww(LTS_REDUCE, 0);
					}
					ww(LTS_CONS, 0);
					f2000 = 0;
					ww(LAST_VOWEL, rw(LTS_AT));
					first = 0;
				} else { /* a consonant */
					if ((rw(LTS_CONS) > 0 && syl != 0) || rw(LTS_CONS) > 1)
						f2000 = 0;
					if (rw(LTS_CONS) != 1 ||
					    !((prevch == 'R' && (feat(ch, 0x100) & 1)) || (ch == 'K' && prevch == 'W')))
						ww(LTS_CONS, rw(LTS_CONS) + 1);
				}
			}
			prefixdone = hitprefix;
			if ((prefixdone == 1 || second == 1) && rw(LAST_VOWEL) != 0) {
				if (stressed == 0) {
					set_stress(ruw(LAST_VOWEL), level);
					stressed = 1;
					skipnext = 1;
					ww(LTS_REDUCE, 0);
				}
				v1_vowel_finish(1);
			}
			int m = rw(rule + 8) & 0x7FFF;
			if (m != 0) {
				if (m == 1)
					ww(LTS_FLAGS, rw(LTS_FLAGS) | 1);
				else
					ww(LTS_FLAGS, m | f2000);
				f2000 = m & 0x2000;
				if (f2000 && syl != 0)
					ww(LTS_CONS, 0);
			}
		}
		if (ruw(LTS_LETTER) == stop && hassuffix == 1) { /* the suffix part is done: now the stem */
			ww(LTS_LETTER, rw(LX_END));
			ww(LTS_FLAGS, 1);
			second = 1;
			f2000 = 0;
			syl = 0;
			ww(LTS_CONS, 1);
			prevch = 0;
			skipnext = 0;
		}
	} while (ruw(LTS_LETTER) != stop);
}

/* F5A7B: one word per call */
int v1_stage_lexical_run(void)
{
	count32(0xA4C2);
	if (!v1_stage_begin(R))
		return v1_stage_commit();
	ww(LX_NEXT, rw(LX_START));
	ww(LX_NEXT, v1_node_next(ruw(LX_NEXT)));
	if (ch_of(ruw(LX_NEXT)) == '~') {
		ww(EMPHASIS, 1);
		ww(LX_NEXT, v1_node_free(ruw(LX_NEXT), 1));
	} else {
		ww(EMPHASIS, 0);
	}
	unsigned w = ruw(LX_NEXT);
	if (ch_of(ruw(LX_START)) != '&' || w == 0 || kind_of(w) != 2 || ch_of(w) < '@' || ch_of(w) > 'Z')
		v1_fatal_error(0x1B);
	ww(ruw(LX_START) + V1_NODE_FLAGS, (rw(ruw(LX_START) + V1_NODE_FLAGS) & 0xFFF8) | 3);
	ww(LX_START, rw(LX_NEXT));
	do {
		ww(LX_END, rw(LX_NEXT));
		ww(LX_NEXT, v1_node_next(ruw(LX_NEXT)));
	} while (rw(LX_NEXT) != 0 && kind_of(ruw(LX_NEXT)) == 2 && ch_of(ruw(LX_NEXT)) != '&');
	for (unsigned n = v1_node_next(ruw(LX_START)); n != ruw(LX_NEXT);) {
		if (ch_of(n) < '@' || ch_of(n) > 'Z')
			v1_fatal_error(0x1C);
		else
			n = v1_node_next(n);
	}
	count32(0xA4E2);
	wb(LEX_STRESS, 0);
	wb(LEX_CLASS, 0);
	if (v1_affixes() == 0 || rb(LEX_STRESS) != 0)
		v1_lts_rules();
	else
		count32(0xA4E6);
	wb(pv(ruw(LX_START)) + V1_NODE_ARG1, rb(LEX_CLASS));
	ww(LX_END, rw(LX_NEXT));
	ww(LX_START, rw(LX_NEXT));
	v1_stage_commit();
	return 1;
}

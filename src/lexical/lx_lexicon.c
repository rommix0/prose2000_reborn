/* The pronunciation lexicon (REFERENCE §9.6) and the affix stripping around it. */
#include "lexical.h"

static const uint8_t *lex_seg; /* segment E900 */

void lexical_load_rom(const prose_rom *rom)
{
	lex_seg = rom->image + (0xE9000u - 0xC0000u);
}

/* D3225: word b of index row a */
int lex_index_word(int a, int b)
{
	unsigned row = (unsigned)(a * 2) & 0xFFFF;
	row = lex_seg[row] | lex_seg[(row + 1) & 0xFFFF] << 8;
	unsigned at = (row + (unsigned)b * 2) & 0xFFFF;
	return (int16_t)(lex_seg[at] | lex_seg[(at + 1) & 0xFFFF] << 8);
}

/* D3245 */
int lex_read_byte(unsigned off)
{
	return (int8_t)lex_seg[off & 0xFFFF];
}

static void set_stress(int n, int level)
{
	ww(n + N_FLAGS, (rw(n + N_FLAGS) & 0xFFE7) | (level & 3) << 3);
}

/* the stress level of a primary-stressed syllable: 3 with '~', 1 with '`', else 2 */
static int primary_level(void)
{
	return rw(EMPHASIS) == 1 ? 3 : rw(SECONDARY) == 1 ? 1 : 2;
}

/* D738A: look up the letters LX_FIRST..LX_END. If found, they are replaced by the entry's phoneme symbols (with
 * the stress marked, by default on the first full vowel) and LX_FIRST / LX_END cover the phonemes; a stress-only
 * entry leaves the letters and sets STRESS_BYTE. The entry's word class goes to LEX_CLASS. Returns 1 if found. */
int lex_lookup(void)
{
	uint8_t key[20] = {0};
	int mid_start = rw(WORD_START) != rw(LX_FIRST), mid_end = rw(WORD_END) != rw(LX_END);
	int st = 2, p = 1, kp, count = 0, saved = 0, n;
	unsigned q, di, lo, hi, next_row, end_row;
	uint8_t mask;

	/* pack letters 2.. of the part into the key, 3 letters per 2 bytes */
	for (n = rw(rw(LX_FIRST)); n != rw(rw(LX_END)); n = rw(n)) {
		if (count >= 16)
			return 0;
		st = rsb(LETTER_STATES + 4 + st * 12);
		int sh = rsb(LETTER_STATES + 6 + st * 12);
		key[p] |= (uint8_t)((rb(LETTER_STATES + st * 12) & rb(n + N_CHAR)) << (sh & 0x1F));
		if (st != 0) {
			p++;
			key[p] = (uint8_t)(rb(LETTER_STATES + 2 + st * 12) & rb(n + N_CHAR));
		}
		count++;
	}
	if (st == 2)
		p--;
	int first = (int8_t)(rb(rw(LX_FIRST) + N_CHAR) - 'A');
	lo = (uint16_t)lex_index_word(first, count);
	hi = (uint16_t)lex_index_word(first, count + 1);
	next_row = (uint16_t)lex_index_word(first + 1, 0);
	end_row = (uint16_t)lex_index_word(26, 0);
	if (lo == hi || lo >= next_row || lo >= end_row)
		goto not_found;
	di = lo;
	for (;;) {
		int k = lex_read_byte(di) & 0xF;
		/* key[1] is the start of the key: index 0 absorbs the p-- above when the key is empty */
		if (count != 0) {
			q = di + 1;
			for (kp = 1; kp < p && key[kp] == (uint8_t)lex_read_byte(q); kp++)
				q++;
			mask = rb(LETTER_STATES + 0xA + st * 12);
		} else {
			mask = 0;
			p = kp = 1;
			q = di;
			st = 2;
		}
		if (kp == p && ((uint8_t)lex_read_byte(q) & mask) == (key[kp] & mask)) {
			int cls = ((lex_read_byte(di) & 0xF0) >> 4);
			int rec = rw(WORD_REC);
			if (((ruw(rec + N_FLAGS) >> 5) & 1) && (rw(rec + 6) & 0xFF) == 4) {
				/* the text rules asked for the second homograph: skip this one */
				ww(rec + N_FLAGS, rw(rec + N_FLAGS) & 0xFFDF);
				saved = di;
				goto next_entry;
			}
			if (cls != 0 && rw(EMPHASIS) == 0) {
				if (!mid_start && !mid_end && cls != 6 && cls != 15)
					wb(rec + N_CHAR, '%'); /* a function word */
				if (rb(LEX_CLASS) == 0)
					wb(LEX_CLASS, cls);
			}
			if (k == 0) {
				wb(STRESS_BYTE, lex_read_byte(q + 1));
				return 1;
			}
			n = rw(LX_FIRST);
			while (n != rw(LX_END))
				n = node_delete(n, 1);
			n = node_delete(n, 0);
			ww(LX_FIRST, n);
			if (st == 2)
				q++;
			int full = 0, reduced = 0, level = 0, primary = 0, no_default = 0;
			int pst = rsb(LETTER_STATES + 8 + st * 12);
			for (int i = 0; i < k; i++) {
				pst = rsb(PHONEME_STATES + 4 + pst * 8);
				int sh = rsb(PHONEME_STATES + 6 + pst * 8);
				uint8_t v = (uint8_t)((uint16_t)(lex_read_byte(q) & rw(PHONEME_STATES + pst * 8)) >>
				                      (sh & 0x1F));
				if (pst >= 1 && pst <= 2) {
					q++;
					v |= (uint8_t)(lex_read_byte(q) & rb(PHONEME_STATES + 2 + pst * 8));
				} else if (pst == 3)
					q++;
				v |= 0x40;
				if (v == 0x60 || ((int8_t)v & 0x3C) == 0x1C)
					v -= 0x2C;
				int ch = (int8_t)v;
				if (ch == '0') {
					no_default = 1;
					continue;
				}
				if (ch == '1') {
					primary = 1;
					level = primary_level();
					continue;
				}
				if (ch == '2') {
					level = 1;
					continue;
				}
				n = node_insert(n, 1, NODE_SYMBOL, ch);
				if (level != 0)
					set_stress(n, level);
				else if (primary != 1 && (feature(node_char(n), 0) & 1) && full == 0 && !no_default) {
					if (!(feature(node_char(n), 0x200) & 0x40))
						full = n;
					else if (reduced == 0)
						reduced = n;
				}
				no_default = 0;
				level = 0;
			}
			ww(LX_END, n);
			ww(LX_FIRST, rw(rw(LX_FIRST)));
			if (!mid_start)
				ww(WORD_START, rw(LX_FIRST));
			if (!mid_end)
				ww(WORD_END, n);
			if (primary != 1) {
				if (full == 0)
					full = reduced;
				if (full != 0)
					set_stress(full, primary_level());
			}
			return 1;
		}
	next_entry:
		if (k != 0)
			di += rsb(ENTRY_ADJUST + st * 17 + k) + (p - 1);
		else
			di += (p - 1) + 3;
		di &= 0xFFFF;
		if (di < hi)
			continue;
	not_found:
		if (saved == 0)
			return 0;
		di = saved;
	}
}

/* E4F57: look up the whole word; failing that, strip suffixes (lists by last letter) and then prefixes (by first
 * letter), looking up the remaining stem after each strip that asks for it. The stripped affixes stay letters for
 * the letter-to-sound rules, behind '[' boundaries. Returns 1 only when the whole word was found. */
int lex_lookup_with_affixes(void)
{
	int stripped = 0, vowel, i, n, list, rec;
	ww(LX_END, rw(WORD_END));
	ww(LX_FIRST, rw(WORD_START));
	if (lex_lookup() == 1)
		return 1;
	ww(ADDED_E, 0);
	/* keep a stem of at least a vowel and two letters */
	vowel = 0;
	n = rw(WORD_START);
	for (i = 0; i <= 2 || vowel == 0; i++) {
		if (n == rw(WORD_END))
			break;
		vowel |= !(feature(node_char(n), 0x80) & 0x20);
		if (vowel && i > 1)
			break;
		n = rw(n);
	}
	if (n == rw(WORD_END))
		goto done;
	for (list = rw(SUFFIXES + 2 * (node_char(rw(LX_END)) - 'A')); list != 0; list = rw(rec + 8)) {
		rec = affix_match(n, rw(LX_END), list, 0);
		ww(AFFIX, rec);
		if (rec == 0)
			break;
		if (!stripped) {
			if (rsb(rec + 5) == 6)
				wb(LEX_CLASS, 6);
			if (rsb(rec + 5) == 15)
				wb(LEX_CLASS, 15);
			if (rsb(rec + 5) == 8) {
				wb(LEX_CLASS, 8);
				wb(rw(WORD_REC) + N_CHAR, '%');
			}
		}
		if (rsb(rec + 4) == 1 && (lex_lookup() == 1 || stem_respell_and_lookup(1) == 1))
			return 0;
		stripped = 1;
	}
	vowel = 0;
	n = rw(LX_END);
	for (i = 0; i < 2 || vowel == 0; i++) {
		if (n == rw(WORD_START))
			break;
		vowel |= !(feature(node_char(n), 0x80) & 0x20);
		if (vowel && i > 1)
			break;
		n = rw(n + 2);
	}
	if (n == rw(WORD_START) || rsb(AFFIX_CODE) == 2)
		goto done;
	for (list = rw(PREFIXES + 2 * (node_char(rw(LX_FIRST)) - 'A')); list != 0; list = rw(rec + 8)) {
		rec = affix_match(rw(LX_FIRST), n, list, 1);
		if (rec == 0)
			break;
		if (rsb(rec + 4) == 1 && (lex_lookup() == 1 || stem_respell_and_lookup(0) == 1))
			return 0;
	}
done:
	if (rw(ADDED_E) == 1)
		ww(LX_END, node_insert(rw(LX_END), 1, NODE_TEXT, 'E'));
	return 0;
}

/* E5152: try the affix records of `list` (a 0-terminated array of pointers) on the letters from..to: a suffix
 * (prefix = 0) is matched right to left from `to`, its string stored reversed; a prefix left to right from
 * `from`. The record's context pattern must match next to it. The affix is split off with a '[' node, and its code
 * either marks the word's stress (0, 1) or goes to AFFIX_CODE. Returns the record, or 0.
 * Record: +0 string, +2 context pattern, +4 1 = look the stem up, +5 word class, +6 code, +8 list to try next. */
int affix_match(int from, int to, int list, int prefix)
{
	int a = prefix ? from : to, b = prefix ? to : from;
	for (;; list += 2) {
		int rec = rw(list), s, n;
		if (rec == 0 || (s = rw(rec)) == 0)
			return 0;
		n = a;
		while (rb(s) == rb(n + N_CHAR) && n != b) {
			s++;
			if (rb(s) == 0)
				break;
			n = prefix ? rw(n) : rw(n + 2);
		}
		if (rb(s) != 0 || !match_context_pattern(ruw(rec + 2), n, prefix))
			continue;
		if (!prefix) {
			ww(LX_END, rw(n + 2));
			node_insert(n, 0, NODE_TEXT, '[');
		} else {
			ww(LX_FIRST, rw(n));
			node_insert(n, 1, NODE_TEXT, '[');
		}
		if (rsb(rec + 6) > 1)
			wb(AFFIX_CODE, rb(rec + 6));
		else
			set_stress(rw(WORD_REC), rsb(rec + 6));
		return rec;
	}
}

/* E5264: repair the stem left by a suffix and look it up again: undouble a final consonant (STOPP-ING), add a
 * silent E (HOP-ING -> HOPE, for some suffixes and contexts), I or IE -> Y (CARRI-ED -> CARRY), and add a T before
 * a "-C..." suffix. `suffix` = 0 when called for a prefix. Returns 1 if found. */
int stem_respell_and_lookup(int suffix)
{
	int si = rw(LX_END), n, m;
	if ((feature(node_char(si), 0x80) & 0x20) && rb(si + N_CHAR) == rb(rw(si + 2) + N_CHAR)) {
		ww(LX_END, rw(si + 2));
		if (lex_lookup() == 1) {
			node_delete(si, 0);
			return 1;
		}
		ww(LX_END, si);
		goto respell;
	}
	if (rw(ADDED_E) != 1) {
		unsigned af = ruw(AFFIX);
		if (af == 0xA310 || af == 0xA4F0 || af == 0xA55E)
			;
		else if (af == 0xA25C || af == 0xA54A || af == 0xA450) {
			if (match_context_pattern(0x9C97, rw(si), 0))
				goto respell;
		} else
			goto respell;
		if (!(feature(node_char(si), 0x80) & 0x20) && node_char(si) != 'U')
			goto respell;
		if (!match_context_pattern(0x9C65, rw(si), 0) && match_context_pattern(0x9D0B, rw(si), 0))
			goto respell;
	}
	si = node_insert(si, 1, NODE_TEXT, 'E');
	ww(LX_END, si);
	if (lex_lookup() == 1)
		return 1;
	si = node_delete(si, 0);
	ww(LX_END, si);
	ww(ADDED_E, 1);
respell:
	if (ruw(AFFIX) == 0xA4B4)
		suffix = 0;
	if (node_char(si) == 'E' && node_char(rw(si + 2)) == 'I') {
		ww(LX_END, node_delete(si, 0));
		wb(rw(LX_END) + N_CHAR, 'Y');
		if (lex_lookup() == 1)
			return 1;
	}
	if (node_char(si) == 'I' && suffix != 0 && rsb(AFFIX_CODE) != 2) {
		wb(rw(LX_END) + N_CHAR, 'Y');
		if (lex_lookup() == 1)
			return 1;
	}
	if (si == rw(WORD_END) || !match_context_pattern(0x9CA0, rw(si), 0))
		return 0;
	n = node_next(rw(LX_END));
	if (node_char(n) != '[')
		return 0;
	m = node_next(n);
	if (node_char(m) != 'C' || node_char(node_next(m)) == 'R')
		return 0;
	si = node_insert(si, 1, NODE_TEXT, 'T');
	ww(LX_END, si);
	return lex_lookup() == 1;
}

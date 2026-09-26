/* The lexical stage driver: taking words from the window, the word-level marks, and handing words on. */
#include "lexical.h"

static void set_stress(int n, int level)
{
	ww(n + N_FLAGS, (rw(n + N_FLAGS) & 0xFFE7) | (level & 3) << 3);
}

static void set_flag(int n, int bit)
{
	ww(n + N_FLAGS, rw(n + N_FLAGS) | 1 << bit);
}

static int word_type(int rec)
{
	return rw(rec + 6) & 0xFF;
}

/* E4A81 */
int stage_lexical_run(void)
{
	stage_window_update(LX_STAGE);
	return lexical_step();
}

/* E4A81 after its stage update. The stage works one word behind: take_word converts the next word while the
 * previous one (the span) waits for its right context, and finish_span then finishes the span and hands it on. */
int lexical_step(void)
{
	if (rw(LX_SCAN) == rw(LX_DONE)) {
		if (!take_word())
			return stage_commit();
		take_span();
	}
	if (!scan_ahead()) {
		/* the end of the window: hand on what follows the span up to a word record */
		while (rw(LX_DONE) != 0 && (feature(node_char(rw(LX_DONE)), 0x80) & 0x40)) {
			ww(PREV_CHAR, 0);
			ww(LX_DONE, node_next(rw(LX_DONE)));
		}
	} else {
		take_word();
		int n = finish_span();
		ww(LX_DONE2, n);
		ww(LX_DONE, n);
		ww(LX_SCAN, node_next(rw(WORD_END)));
		take_span();
	}
	return stage_commit();
}

/* E4AFB: finish the span word (its right neighbour is now converted): ESC[f skipping, the function-word rules,
 * the allophone rules and the syllable marks; remove the '[' affix boundaries. Returns the node after it. */
int finish_span(void)
{
	int rec = rw(SPAN_REC), end, n, next;
	int ch = '&';
	if (node_kind(rec) != NODE_SYMBOL || (feature(node_char(rec), 0x80) & 0x40))
		return node_next(rec);
	if (rw(LX_FAST) != 0) {
		int cls = rsb(rec + 8);
		if (cls == 0 || cls == 6 || cls == 15) {
			/* ESC[f: speak only every f-th content word */
			ww(FAST_COUNT, rw(FAST_COUNT) + 1);
			int q = s16(rw(FAST_COUNT)) % s16(rw(LX_FAST));
			ww(FAST_COUNT, q);
			if (q == 0)
				goto keep;
		}
		while (rw(SPAN_REC) != rw(SPAN_END))
			ww(SPAN_REC, node_delete(rw(SPAN_REC), 1));
		return node_delete(rw(SPAN_REC), 1);
	}
keep:
	rec = rw(SPAN_REC);
	ch = node_char(rec);
	if (ch == '%' && word_type(rec) != 13)
		function_word_rules();
	end = node_next(rw(SPAN_END));
	for (n = rw(SPAN_REC); n != end && n != 0; n = node_next(n)) {
		if (node_kind(n) == NODE_SYMBOL && (feature(node_char(n), 0) & 0x80) &&
		    ((rw(LX_MODE) & 0x100) || rw(LX_INPUT) == 0) && word_type(rw(SPAN_REC)) != 13)
			n = allophone_rules(n);
		if (ch == '%' && node_stress(n) != 0)
			mark_stressed_syllable(n);
	}
	for (n = rw(SPAN_REC); n != end && n != 0; n = next) {
		next = node_next(n);
		if (node_char(n) == '[')
			node_delete(n, 0);
	}
	ww(PREV_CHAR, node_char(rw(end + 2)));
	return end;
}

/* E4C8D: the word just taken becomes the span. */
void take_span(void)
{
	ww(SPAN_REC, rw(WORD_REC));
	ww(SCAN_POS, rw(WORD_END));
	ww(SPAN_END, rw(WORD_END));
	ww(SCAN_STEPS, 0);
	ww(WORD_END, 0);
	ww(WORD_REC, 0);
}

/* E4CAB: move the scan to the next node this stage accepts. Gives up after 12 nodes, or at a C command, so that a
 * long run of other nodes still gets handed on. Returns 0 at the end of the window. */
int scan_ahead(void)
{
	for (;;) {
		if (rw(SCAN_POS) == rw(LX_LAST)) {
			ww(LX_SCAN, rw(SCAN_POS));
			return 0;
		}
		ww(SCAN_POS, node_next(rw(SCAN_POS)));
		int n = rw(SCAN_POS);
		if (n != 0 && (rw(0x20 + 2 * node_kind(n)) & rw(LX_KINDS)))
			break;
		ww(SCAN_STEPS, rw(SCAN_STEPS) + 1);
		if (rw(SCAN_STEPS) >= 12)
			break;
		if (node_kind(n) == NODE_COMMAND && node_char(n) == 'C')
			break;
	}
	ww(LX_SCAN, rw(SCAN_POS));
	return 1;
}

/* E4D16: take the word at LX_SCAN. A word record (text node) followed by letters is looked up and converted; a
 * run of phoneme symbols (phoneme input) is taken as it is. Returns 0 if there is nothing to take. */
int take_word(void)
{
	int n;
	if (rw(LX_SCAN) == 0)
		return 0;
	n = rw(LX_SCAN);
	ww(WORD_REC, n);
	ww(WORD_END, n);
	if (node_kind(rw(WORD_REC)) == NODE_SYMBOL) {
		for (;;) {
			n = node_next(n);
			if (n == 0 || node_kind(n) != NODE_SYMBOL)
				break;
			if (!(feature(node_char(n), 0) & 0x80) && !(feature(node_char(n), 0x80) & 2))
				break;
			ww(WORD_END, n);
		}
		word_stress_marks();
		return 1;
	}
	if (node_kind(rw(WORD_REC)) != NODE_TEXT)
		return 1;
	n = node_next(rw(WORD_REC));
	if (node_char(n) == '~') {
		ww(EMPHASIS, 1);
		n = node_delete(n, 1);
	} else
		ww(EMPHASIS, 0);
	if (node_char(n) == '`') {
		ww(SECONDARY, 1);
		n = node_delete(n, 1);
	} else
		ww(SECONDARY, 0);
	switch (word_type(rw(WORD_REC))) {
	case 1:
	case 2:
	case 5:
		ww(SECONDARY, 1);
		break;
	case 9:
		ww(EMPHASIS, 1);
		break;
	}
	ww(rw(WORD_REC) + N_FLAGS, (rw(rw(WORD_REC) + N_FLAGS) & 0xFFF8) | NODE_SYMBOL);
	ww(WORD_START, n);
	do {
		ww(WORD_END, n);
		n = node_next(n);
	} while (n != 0 && node_kind(n) == NODE_TEXT && node_char(n) != '&');
	wb(STRESS_BYTE, 0);
	wb(LEX_CLASS, 0);
	wb(AFFIX_CODE, 0);
	if (!lex_lookup_with_affixes() || rb(STRESS_BYTE) != 0)
		lts_rules();
	wb(rw(WORD_REC) + 8, rb(LEX_CLASS));
	word_stress_marks();
	return 1;
}

/* E4E90: apply the stress digits of phoneme input ('1' primary, '2' secondary, '"' emphatic, after the vowel) and
 * mark the stressed syllables of content words, then turn the word record into a boundary. */
void word_stress_marks(void)
{
	int di, next = rw(WORD_REC);
	do {
		di = next;
		next = node_next(di);
		if (di != rw(WORD_END)) {
			int c = node_char(next);
			if (c == '1' || c == '2' || c == '"') {
				if (feature(node_char(di), 0) & 1)
					set_stress(di, c == '1' ? 2 : c == '2' ? 1 : 3);
				if (rw(WORD_END) == next)
					ww(WORD_END, di);
				next = node_delete(next, 1);
			}
		}
		if (node_char(rw(WORD_REC)) != '%' && node_stress(di) != 0)
			mark_stressed_syllable(di);
	} while (di != rw(WORD_END));
	word_boundary();
}

/* E546F: flag (bit 5) a stressed vowel and the consonants of its syllable onset before it: one consonant (or Y),
 * a second one if they form a cluster, and an S before that. */
void mark_stressed_syllable(int n)
{
	int c;
	set_flag(n, 5);
	n = prev_symbol(n);
	if (n == 0)
		return;
	c = node_char(n);
	if (!(feature(c, 0x80) & 0x20) && c != 'Y')
		return;
	if (c == '~')
		return;
	set_flag(n, 5);
	n = prev_symbol(n);
	if (n == 0)
		return;
	c = node_char(n);
	if (!(feature(c, 0x80) & 0x20) || (feature(c, 0) & 2))
		return;
	if ((feature(c, 0x100) & 1) && (feature(node_char(rw(n)), 0x100) & 1))
		return;
	set_flag(n, 5);
	n = prev_symbol(n);
	if (n == 0)
		return;
	if (node_char(n) == 'S')
		set_flag(n, 5);
}

/* E5542: a function word (the span) loses a stress level (all of it from speed 9), and at higher speeds its
 * vowels reduce: "a" (class 3) to schwa, the second sound of "the", "to", "and" (classes 2, 14, 10) too. */
void function_word_rules(void)
{
	int skip = 0, next_ch = 0, cls, n, first, di;
	if (rw(WORD_REC) != 0) {
		next_ch = node_char(rw(WORD_REC));
		if (feature(next_ch, 0x80) & 0x40)
			skip = 1;
	}
	if (!(feature(next_ch, 0) & 0x80)) {
		n = node_next(rw(WORD_REC));
		if (n != 0)
			next_ch = node_char(n);
	}
	cls = rsb(rw(SPAN_REC) + 8);
	if (skip)
		return;
	if (cls == 1 || cls == 2 || cls == 3 || cls == 5 || cls == 9 || cls == 10 || cls == 12 || cls == 13 ||
	    cls == 14) {
		for (n = rw(SPAN_REC); n != node_next(rw(SPAN_END)); n = node_next(n)) {
			int s = node_stress(n);
			if (s == 2)
				s = 1;
			else if (s == 1)
				s = 0;
			if (rw(LX_SPEED) >= 9)
				s = 0;
			set_stress(n, s);
		}
	}
	first = n = node_next(rw(SPAN_REC));
	if (cls == 3 && rw(LX_SPEED) > 6) {
		for (; n != rw(SPAN_END); n = node_next(n))
			if (feature(node_char(n), 0x100) & 2)
				wb(n + N_CHAR, '@');
		return;
	}
	if (cls != 2 && cls != 14 && cls != 10)
		return;
	di = node_char(n);
	n = node_next(n);
	if (di == 'x') {
		if ((feature(next_ch, 0x100) & 2) || rw(LX_SPEED) <= 6)
			return;
		wb(n + N_CHAR, '@');
	} else if (di == 'T' && rw(LX_SPEED) >= 22) {
		if (feature(next_ch, 0x100) & 2)
			return;
		wb(n + N_CHAR, '@');
	} else if (di == 'a' && node_char(rw(SPAN_END)) == 'D' && rw(LX_SPEED) >= 22) {
		wb(first + N_CHAR, '@');
		ww(SPAN_END, node_delete(rw(SPAN_END), 0));
	} else if (di == 'F' && rw(LX_SPEED) >= 22)
		wb(n + N_CHAR, '@');
}

/* E5736: the word record becomes a boundary symbol; bit 5 set (by the text rules) asks for one by word type. */
void word_boundary(void)
{
	int rec = rw(WORD_REC), n;
	if (!((ruw(rec + N_FLAGS) >> 5) & 1))
		return;
	switch (word_type(rec)) {
	case 1:
		if (rw(LX_SCAN) != rw(LX_DONE))
			wb(rec + N_CHAR, '$');
		break;
	case 2:
		wb(rec + N_CHAR, '%');
		wb(rec + 8, 1);
		break;
	case 3:
		wb(rec + N_CHAR, '&');
		wb(rec + 8, 0);
		break;
	case 5:
		wb(rec + N_CHAR, ' ');
		ww(rec + N_FLAGS, (rw(rec + N_FLAGS) & 0xFFF8) | NODE_SYMBOL);
		ww(rec + 6, (rw(rec + 6) & 0xFF00) | 4);
		wb(rec + 8, 0x3B);
		break;
	case 9:
		ww(EMPHASIS, 1);
		break;
	case 11:
		n = node_insert(rec, 1, NODE_SYMBOL, ' ');
		ww(rec, n);
		ww(rw(rec) + 6, (rw(rw(rec) + 6) & 0xFF00) | 8);
		wb(rec + 8, 0x3B);
		break;
	}
}

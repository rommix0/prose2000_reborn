/* The prosody stage driver: phrase scan, the walk over the phrase, and the per-segment context. */
#include "prosody.h"

static int is_word_boundary(int ch)
{
	return ch == '%' || ch == '&';
}

/* D9900: forget the phrase state (called at reset and after a flush). */
void prosody_reset(void)
{
	ww(PR_1E, 0x1C);
	ww(SCAN_STATE, 3);
	ww(POS_8, -1);
	ww(POS_7, -1);
	ww(POS_6, -1);
	ww(POS_17, 0);
	ww(WORD_MODE, 0);
	ww(RATE_BREAKS, 0);
	ww(PHRASE_MAX, 180);
	ww(FAST_START, 0);
	ww(CARRY, 0);
	ww(GIVEN_PREV, 0);
	ww(GIVEN, 0);
}

/* D9274 */
int prosody_run(void)
{
	stage_window_update(PR_STAGE);
	return prosody_step();
}

/* D9274 after its stage update. Returns 1 if nodes were handed on or there is more to do. */
int prosody_step(void)
{
	int more = 0, r;
	ww(STEP_BUDGET, 2);
	if (rw(FAST_START) == 0 && rw(FIRST_PHRASE) != 0 && rw(PR_SPEED) < 19)
		ww(PHRASE_MAX, 70);
	r = prosody_scan_phrase();
	switch (r) {
	case 1:
	case 4: /* the phrase continues in the next scan: carry a quarter of it over for the F0 line */
		ww(CARRY, (rw(PHRASE_LEN) >> 2) + 4);
		ww(PHRASE_LEN, rw(PHRASE_LEN) + rw(CARRY));
		/* fall through */
	case 3:
	case 5:
		ww(PR_CUR, rw(PR_FIRST));
		/* fall through */
	case 2:
		if (rw(FAST_START) == 0)
			ww(FIRST_PHRASE, 0);
		break;
	}
	while (r != 0) {
		switch (prosody_walk()) {
		case 1:
			r = 0;
			break;
		case 2:
			stage_run_command();
			break;
		case 3:
			prosody_time_segment();
			break;
		case 4:
			more = 1;
			r = 0;
			break;
		}
	}
	return stage_commit() != 0 || more;
}

/* D9492: scan ahead from PR_SCAN to the end of the phrase (a sentence end, a C command, or the length limit),
 * counting its phonemes and collecting its word boundaries, then choose the F0 contour. Returns SCAN_STATE. */
int prosody_scan_phrase(void)
{
	int n, ch;
	if (rw(SCAN_STATE) == 2)
		return 2;
	if (rw(SCAN_STATE) != 0) {
		ww(PHRASE_LEN, rw(PHRASE_LEN) - rw(CARRY));
		ww(CARRY, 0);
		ww(RUN, 0);
		ww(SCAN_PHONEMES, 0);
		ww(SCANNED, 0);
		ww(IN_WORD, 0);
		ww(WORD_COUNT, 0);
		ww(POS_8, -1);
		ww(POS_7, -1);
		ww(POS_6, -1);
		ww(POS_17, 0);
		if (rw(SCAN_STATE) == 3 || rw(SCAN_STATE) == 5) {
			accent_reset();
			ww(PHRASE_LEN, 0);
			ww(BREAK_SEEN, 0);
			ww(NEW_SENTENCE, 1);
		}
	}
	/* commands before the first symbol */
	while (rw(PR_FIRST) != 0 && rw(PR_FIRST) == rw(PR_SCAN) && node_kind(rw(PR_FIRST)) != NODE_SYMBOL) {
		stage_run_command();
		ww(FAST_START, rw(PR_MODE) & 0x2000);
		ww(PHRASE_MAX, rw(FAST_START) ? 5 : 180);
		n = node_next(rw(PR_FIRST));
		ww(PR_FIRST, n);
		ww(PR_CUR, n);
		ww(PR_SCAN, n);
	}
	ww(SCAN_LAST, rw(PR_SCAN));
	ww(SCAN_STATE, 0);
	while ((n = rw(PR_SCAN)) != 0 && rw(SCAN_STATE) == 0) {
		ch = node_char(n);
		ww(SCANNED, rw(SCANNED) + 1);
		if (node_kind(n) == NODE_SYMBOL && (feature(ch, 0) & 0x80)) {
			ww(RUN, rw(RUN) + 1);
			ww(IN_WORD, 1);
		} else {
			ww(SCAN_PHONEMES, rw(SCAN_PHONEMES) + rw(RUN));
			ww(PHRASE_LEN, rw(PHRASE_LEN) + rw(RUN));
			ww(RUN, 0);
			ww(SCAN_LAST, n);
			if (node_kind(n) == NODE_SYMBOL) {
				if (is_word_boundary(ch) && rw(WORD_COUNT) < 50) {
					switch (node_dur(n)) { /* the word's type (its class is in +8) */
					case 6:
						ww(POS_6, rw(PHRASE_LEN));
						break;
					case 7:
						ww(POS_7, rw(PHRASE_LEN));
						break;
					case 8:
						ww(POS_8, rw(PHRASE_LEN));
						break;
					case 17:
						ww(POS_17, rw(PHRASE_LEN));
						break;
					}
					ww(WORDS + 2 * rw(WORD_COUNT), n);
					ww(WORD_COUNT, rw(WORD_COUNT) + 1);
				} else if (feature(ch, 0x80) & 0x40) { /* punctuation */
					if (ch == '.' || ch == '?') {
						ww(SCAN_STATE, 3);
						for (;;) { /* phonemes after the sentence end do not count */
							n = node_prev(n);
							if (!node_has(n, 1, -1))
								break;
							if (node_kind(n) == NODE_SYMBOL &&
							    (feature(node_char(n), 0) & 0x80))
								ww(PHRASE_LEN, rw(PHRASE_LEN) - 1);
						}
					} else if (ch == ',' && rw(WORD_MODE) != 0) {
						ww(SCAN_STATE, 5);
					}
					if (rw(BREAK_SEEN) == 0 && rw(WORD_MODE) == 0 && rw(PR_SPEED) < 19)
						phrase_breaks(ch);
					ww(IN_WORD, 0);
					ww(BREAK_SEEN, 0);
					ww(WORD_COUNT_PREV, rw(WORD_COUNT));
					ww(WORD_COUNT, 0);
				}
			} else if (node_kind(n) == NODE_COMMAND) {
				switch (ch) {
				case 'P':
					ww(WORD_MODE, node_dur(n) == 0);
					if (rw(IN_WORD) != 0) {
						if (rw(FREE_COUNT) >
						    3) { /* a mode change inside a word ends it there */
							n = node_insert(n, 0, NODE_SYMBOL, '\\');
							ww(SCANNED, rw(SCANNED) + 1);
							ww(PR_SCAN, node_prev(n));
						}
					} else {
						ww(BREAK_SEEN, 1);
					}
					break;
				case 'C':
					if (rw(BREAK_SEEN) == 0 && rw(WORD_MODE) == 0)
						phrase_breaks(0);
					ww(SCAN_STATE, 1);
					break;
				case 'r':
				case 'v':
					ww(RATE_BREAKS, (unsigned)node_dur(n) < 9);
					break;
				case 'N':
					ww(FAST_START, rsb(n + N_F0) & 0x20);
					ww(PHRASE_MAX, rw(FAST_START) ? 5 : 180);
					break;
				}
			}
		}
		if (rw(SCAN_STATE) != 0)
			continue;
		if (rw(SCANNED) >= 240 || rw(SCAN_PHONEMES) >= rw(PHRASE_MAX)) {
			ww(PR_SCAN, node_prev(rw(SCAN_LAST)));
			if (is_word_boundary(node_char(rw(SCAN_LAST))))
				ww(WORD_COUNT, rw(WORD_COUNT) - 1);
			if (rw(BREAK_SEEN) == 0 && rw(WORD_MODE) == 0)
				phrase_breaks(0);
			ww(SCAN_STATE, 4);
		} else {
			ww(PR_SCAN, node_next(rw(PR_SCAN)));
		}
	}

	/* the contour, from where the words of types 6, 7, 8 and 17 fall in the phrase */
	int p6 = rw(POS_6), p7 = rw(POS_7), c;
	if (p6 < 0 && p7 < 0)
		c = rw(POS_8) >= 0 ? 8 : 0;
	else if (p7 < 0)
		c = p6 == 0 ? 0 : 6;
	else if (p6 < 0)
		c = p7 == rw(PHRASE_LEN) ? 0 : 7;
	else if (p6 < p7)
		c = p6 != 0 ? 16 : p7 == rw(PHRASE_LEN) ? 0 : 7;
	else
		c = p7 != 0 ? 15 : p6 == 0 ? 0 : 6;
	ww(CONTOUR, c);
	return rw(SCAN_STATE);
}

/* the pauses of a phrase given back lose their stress marks */
static void clear_pause_marks(int from, int end)
{
	for (int n = from; n != end; n = node_next(n))
		if (node_kind(n) == NODE_SEGMENT && node_char(n) == ' ')
			ww(n + N_FLAGS, rw(n + N_FLAGS) & 0xFFE7);
}

/* D9336: move PR_CUR on through the scanned phrase. Returns 1 at the end of the phrase, 2 for a command node,
 * 3 for a symbol to time, 4 to stop for now (a comma break, or the ring is full enough). */
int prosody_walk(void)
{
	int end;
	if (rw(SCAN_STATE) != 2) {
		ww(SAVED_STATE, rw(SCAN_STATE));
		ww(WALK, 1);
	}
	ww(SCAN_STATE, 2);
	if (rw(WALK) == 3) {
		end = node_next(rw(PR_SCAN));
		clear_pause_marks(rw(PR_FIRST), end);
		ww(PR_SCAN, end);
		ww(PR_CUR, end);
		ww(PR_FIRST, end);
		ww(SCAN_STATE, rw(SAVED_STATE));
		ww(WALK, 1);
		return 1;
	}
	int cur = rw(PR_CUR);
	if (node_kind(cur) == NODE_SYMBOL && node_char(cur) == ',' && rw(FAST_START) == 0 && rw(WORD_MODE) == 0 &&
	    rw(WALK) != 1) {
		/* hand the phrase up to the comma on */
		end = node_next(cur);
		clear_pause_marks(rw(PR_FIRST), end);
		ww(PR_FIRST, rw(PR_CUR));
		ww(PR_CUR, end);
		ww(WALK, 1);
		return 4;
	}
	ww(STEP_BUDGET, rw(STEP_BUDGET) - 1);
	if (rw(STEP_BUDGET) < 0 && s16(rw(RING_LOW) - rw(RING_LAG) - rw(RING_READ)) <= 0x20)
		return 4;
	if (rw(WALK) == 2)
		ww(PR_CUR, node_next(rw(PR_CUR)));
	else
		ww(WALK, 2);
	if (rw(PR_CUR) == rw(PR_SCAN))
		ww(WALK, 3);
	return node_kind(rw(PR_CUR)) == NODE_SYMBOL ? 3 : 2;
}

/* D9A20: time the symbol at PR_CUR. */
void prosody_time_segment(void)
{
	given_values(0);
	context_load();
	segment_prosody();
	if ((feature(rsb(CUR_CH), 0) & 8) && node_dur(rw(PR_CUR)) == 12) {
		insert_pause(rw(0xDC78));
		ww(0xDC78, 0);
	}
	if (feature(rsb(CUR_CH), 0x80) & 0x40)
		insert_pause(0);
	if (rw(CUR_PHONEME) != 0) {
		int n = rw(PR_CUR);
		ww(n + N_FLAGS, (rw(n + N_FLAGS) & 0xFFF8) | NODE_SEGMENT);
		ww(0xDC78, rw(0xDC78) + 1);
		given_values(1);
	}
}

/* D9945: the symbol before n, stopping at a segment (returned) or the start of the window (0). */
int prev_symbol(int n)
{
	int p = 0;
	if (n == 0)
		return 0;
	do {
		p = node_prev(n);
		if (p == 0)
			return 0;
		if (node_kind(p) == NODE_SYMBOL)
			return p;
		n = p;
	} while (node_kind(p) != NODE_SEGMENT);
	return p;
}

/* D997E: test a feature of n's char. test = (plane << 1 & 0xFF00) | mask, as in rule_condition; a negative test
 * or negative `negate` inverts the result. 0 for no node. */
int node_has(int n, int test, int negate)
{
	int r, inv = 0;
	if (n == 0)
		return 0;
	if (s16(test) < 0) {
		inv = 1;
		test = -s16(test);
	}
	r = rsb(0xA8 + ((((test & 0xFF00) >> 1) | node_char(n)) & 0xFFFF)) & test & 0xFF;
	if (inv)
		r = !r;
	if (s16(negate) < 0)
		r = !r;
	return r;
}

/* D99F2: the next symbol after n, or 0. */
int next_symbol(int n)
{
	if (n == 0)
		return 0;
	do {
		n = node_next(n);
		if (n == 0)
			return 0;
	} while (node_kind(n) != NODE_SYMBOL);
	return n;
}

/* stress class of a node for context_search: 1 emphatic (level 3), 2 primary (level 2, only when |want| is 2),
 * 0 otherwise */
static int stress_match(int n, int want)
{
	if (node_stress(n) == 3)
		return 1;
	if ((want < 0 ? -want : want) == 2 && node_stress(n) == 2)
		return 2;
	return 0;
}

/* D9A9D: look up to `count` symbols forward (forward = 1) or back from PR_CUR for one matching `want` before one
 * matching `stop`. With by_stress = 1 `want`, and with 2 `stop`, is a stress test (1 or 2, negative = absent)
 * instead of a feature test. */
int context_search(int forward, int count, int want, int stop, int by_stress)
{
	int n = rw(PR_CUR), s;
	for (; count > 0; count--) {
		n = forward == 1 ? next_symbol(n) : prev_symbol(n);
		if (n == 0)
			break;
		if (by_stress == 1) {
			s = stress_match(n, want);
			if (s != 0 && want > 0)
				return 1;
			if (s == 0 && want < 0)
				return 1;
		} else if (node_has(n, want, 0)) {
			return 1;
		}
		if (by_stress == 2) {
			s = stress_match(n, stop);
			if ((s != 0 && stop < 0) || (s == 0 && stop > 0))
				return 0;
		} else if (node_has(n, stop, -1)) {
			return 0;
		}
	}
	return 0;
}

/* D9BE6: phoneme input can give a duration (node +6) and F0 (+8) with the phoneme. given_values(0) notes them
 * before the rules run; given_values(1) puts them back afterwards (bit 2: relative, added to the rule values). */
void given_values(int restore)
{
	int n = rw(PR_CUR);
	if (!restore) {
		ww(GIVEN_NODE, n);
		ww(GIVEN_F0, rsb(n + N_F0));
		ww(DUR_SET, node_dur(n));
		wb(GIVEN_CHAR, rb(n + N_CHAR));
		if (rw(GIVEN_F0) & 0x80)
			ww(GIVEN_F0, rw(GIVEN_F0) | 0xFF00);
		if (rw(DUR_SET) & 0x80)
			ww(DUR_SET, rw(DUR_SET) | 0xFF00);
		ww(GIVEN_PREV, rw(GIVEN));
		ww(GIVEN, 0);
		if (node_bit(n, 7))
			ww(GIVEN, rw(GIVEN) | 4);
		if (rw(DUR_SET) != 0)
			ww(GIVEN, rw(GIVEN) | 1);
		if (rw(GIVEN_F0) != 0)
			ww(GIVEN, rw(GIVEN) | 2);
		if (rsb(GIVEN_CHAR) == ' ' && rw(DUR_SET) > 50)
			ww(DUR_SET, 50);
		return;
	}
	if (rw(GIVEN_NODE) != n || rsb(GIVEN_CHAR) != node_char(n))
		return;
	if (rw(GIVEN) & 1) {
		if (rw(GIVEN) & 4) {
			ww(DUR_SET, rw(DUR_SET) + node_dur(n));
			if (rw(DUR_SET) < 2)
				ww(DUR_SET, 2);
			else if (rw(DUR_SET) > 60)
				ww(DUR_SET, 60);
		}
		ww(n + N_DUR, (rw(n + N_DUR) & 0xFF00) | (rw(DUR_SET) & 0xFF));
	}
	if (rw(GIVEN) & 2) {
		if (!(rw(GIVEN) & 4)) {
			if (rw(GIVEN_F0) == 1)
				ww(GIVEN_F0, 0);
		} else {
			ww(GIVEN_F0, rw(GIVEN_F0) + rsb(n + N_F0));
			if (rw(GIVEN_F0) < 25)
				ww(GIVEN_F0, 25);
			else if (rw(GIVEN_F0) > 100)
				ww(GIVEN_F0, 100);
		}
		wb(n + N_F0, rw(GIVEN_F0));
	}
}

/* D9F9A: load the context of PR_CUR: its neighbours and the features the rules test. */
void context_load(void)
{
	int n = rw(PR_CUR), prev = prev_symbol(n), next = next_symbol(n), ch;
	ww(PREV_NODE, prev);
	ww(NEXT_NODE, next);
	wb(CUR_CH, rb(n + N_CHAR));
	wb(PREV_CH, prev ? rb(prev + N_CHAR) : 0);
	wb(NEXT_CH, next ? rb(next + N_CHAR) : 0);
	ch = rsb(CUR_CH);
	ww(CUR_AFFR, rsb(0xA8 + (ch | 0x80)) & 8);
	ww(CUR_FRIC, rsb(0xA8 + ch) & 0x40);
	ww(CUR_NASAL, rsb(0xA8 + ch) & 0x10);
	ww(CUR_PHONEME, rsb(0xA8 + ch) & 0x80);
	ww(CUR_SON, rsb(0xA8 + ch) & 2);
	ww(CUR_CLOSURE, rsb(0xA8 + (ch | 0x100)) & 1);
	ww(CUR_SYL, rsb(0xA8 + ch) & 1);
	ww(CUR_VOWEL, rsb(0xA8 + (ch | 0x100)) & 2);
	ww(CUR_B5, node_bit(n, 5) != 0);
	ww(PREV_SYL, rsb(0xA8 + rsb(PREV_CH)) & 1);
	ww(NEXT_SYL, rsb(0xA8 + rsb(NEXT_CH)) & 1);
	ww(NEXT_B5, next != 0 && node_bit(rw(NEXT_NODE), 5));
}

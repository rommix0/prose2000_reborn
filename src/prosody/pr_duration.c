/* Segment durations: Klatt's percentage rules, pauses, geminates and a few consonant allophones. */
#include "prosody.h"

static int cur(void)
{
	return rw(PR_CUR);
}
static int cur_ch(void)
{
	return rsb(CUR_CH);
}
static int prev_ch(void)
{
	return rsb(PREV_CH);
}
static int next_ch(void)
{
	return rsb(NEXT_CH);
}
static int f(int ch, int plane)
{
	return feature(ch, plane);
}
static void set_dur(int n, int d)
{
	ww(n + N_DUR, (rw(n + N_DUR) & 0xFF00) | (d & 0xFF));
}
static int ch_in(int ch, const char *set)
{
	for (; *set; set++)
		if (ch == *set)
			return 1;
	return 0;
}

/* a pause node before PR_CUR */
static int new_pause(int dur)
{
	int p = node_insert(cur(), 0, NODE_SEGMENT, ' ');
	set_dur(p, dur);
	wb(p + N_F0, 50);
	return p;
}

/* DBC51 */
void accent_reset(void)
{
	ww(PEAK, 0);
	ww(F0_POS, 0);
	ww(F0_HOLD, 0);
	ww(ACCENT_NODE, 0);
	ww(ACCENT_DONE, 0);
	ww(ACCENT_HERE, 0);
	ww(PHRASE_FINAL, 0);
	ww(CLAUSE_FINAL, 0);
	ww(NEXT_PLACE, 4);
	ww(LOWER_F0, 0);
}

/* DC1EA: the per-segment rules: accent context, F0, then the duration. */
void segment_prosody(void)
{
	int n;
	ww(NEXT_SEG, 0);
	ww(PREV_SEG, 0);
	for (n = rw(PREV_NODE); n != 0; n = prev_symbol(n))
		if (node_kind(n) == NODE_SEGMENT) {
			ww(PREV_SEG, n);
			break;
		}
	for (n = rw(NEXT_NODE); n != 0; n = next_symbol(n))
		if (node_kind(n) == NODE_SYMBOL && (feature(node_char(n), 0) & 0x80)) {
			ww(NEXT_SEG, n);
			break;
		}
	accent_context();
	f0_target();
	if (duration_rules()) {
		/* the consonant rules look at the neighbouring phonemes, not at boundaries */
		ww(PREV_NODE, rw(PREV_SEG));
		ww(NEXT_NODE, rw(NEXT_SEG));
		wb(PREV_CH, rw(PREV_NODE) ? rb(rw(PREV_NODE) + N_CHAR) : 0);
		wb(NEXT_CH, rw(NEXT_NODE) ? rb(rw(NEXT_NODE) + N_CHAR) : 0);
		ww(PREV_SYL, rsb(0xA8 + prev_ch()) & 1);
		ww(NEXT_SYL, rsb(0xA8 + next_ch()) & 1);
		if (!(rw(PR_AFLAGS) & 0x10))
			klatt_duration();
	}
	/* A-flag 5: every phoneme gets 85 % of its minimum duration */
	if ((rw(PR_AFLAGS) & 0x10) && (feature(node_char(cur()), 0) & 0x80) && node_char(cur()) != ' ')
		set_dur(cur(), mul_div_s(rsb(rw(MIN_TABLE) + cur_ch()), 17, 20));
	if (feature(cur_ch(), 0) & 0x80)
		merge_geminate();
}

/* DBA35: find the accented syllable of the phrase and classify what follows the current vowel. */
void accent_context(void)
{
	int n, target;
	if (f(cur_ch(), 0) & 8) {
		ww(CLAUSE_FINAL, 0);
		return;
	}
	if (f(cur_ch(), 0x80) & 0x40) { /* punctuation ends the accent domain */
		ww(F0_HOLD, 0);
		ww(ACCENT_DONE, 0);
		ww(ACCENT_HERE, 0);
		ww(PHRASE_FINAL, 0);
		ww(CLAUSE_FINAL, 0);
		ww(ACCENT_NODE, 0);
		return;
	}
	if (rw(CUR_SYL) == 0)
		return;
	if (rw(ACCENT_NODE) == 0) {
		/* the last primary-stressed vowel before the next punctuation, else the last secondary one */
		for (n = cur();; n = node_next(n)) {
			if (node_kind(n) == NODE_SYMBOL) {
				if (!(feature(node_char(n), 0) & 0x80)) {
					if (feature(node_char(n), 0x80) & 0x40)
						goto found;
				} else if (node_stress(n) > 1 &&
				           (rw(ACCENT_NODE) == 0 || node_stress(rw(ACCENT_NODE)) != 3 ||
				            node_stress(n) == 3)) {
					ww(ACCENT_NODE, n);
				}
			}
			if (n == rw(PR_SCAN))
				break;
		}
		ww(ACCENT_NODE, 0);
	}
found:
	if (context_search(1, 9, 0x140, -1, 0)) {
		ww(PHRASE_FINAL, 1);
		ww(CLAUSE_FINAL, 1);
	} else if (context_search(1, 10, 8, -1, 0)) {
		ww(CLAUSE_FINAL, 1);
	}
	if (cur() == rw(ACCENT_NODE))
		ww(ACCENT_HERE, 1);
	/* the place of the consonant after the vowel, for CLUSTER_PCT */
	ww(NEXT_PLACE, 4);
	target = rw(NEXT_NODE);
	if (node_has(target, 2, 0)) {
		n = next_symbol(target);
		if (node_has(n, 2, -1) && node_has(n, 0x80, 0))
			target = rw(NEXT_NODE);
		else
			target = n;
	}
	if (node_has(target, 4, 0)) {
		if (node_has(target, 0x40, 0) || node_has(target, 0x108, 0))
			ww(NEXT_PLACE, 3);
	} else if (node_has(target, 0x40, 0)) {
		ww(NEXT_PLACE, 1);
	} else if (node_has(target, 0x20, 0)) {
		ww(NEXT_PLACE, 2);
	}
}

/* push a percentage onto PRCNT */
static int push(int i, int pct)
{
	ww(PRCNT_AT(i + 1), pct);
	return i + 1;
}

/* the product of PRCNT[0..i], each step (a * p) / 100 on 16 bits */
static int product(int i)
{
	int r = 100;
	for (; i >= 0; i--)
		r = mul_div_u(rw(PRCNT_AT(i)), r, 100);
	return r;
}

/* DA9DC: durations of pauses and word boundaries, and the vowel percentage rules. Returns 1 when klatt_duration
 * must still run (the phoneme has distinct inherent and minimum durations). */
int duration_rules(void)
{
	int n = cur(), i = 0, d, ch = cur_ch(), after;
	if (rw(CUR_PHONEME) == 0) {
		/* a boundary: word boundaries remember whether the word is a function word */
		if (!(feature(node_char(n), 0) & 8))
			ww(n + N_DUR, rw(n + N_DUR) & 0xFF00);
		else if (node_dur(n) == 10)
			ww(WORD_CLASS, rsb(n + N_F0) == 0 || rsb(n + N_F0) == 6 ? 1 : 2);
		else
			ww(WORD_CLASS, 0);
		return 0;
	}
	if (ch == ' ') { /* a pause from the text, scaled by the rate, split into 50-frame nodes */
		d = mul_div_u(rw(PAUSE_SCALE + 2 * rw(PR_SPEED)) / 4, node_dur(n), 100);
		d = s16(d << 2);
		if (rw(PR_AFLAGS) & 0x10)
			d >>= 2;
		if (d < 4)
			d = 4;
		if (rw(DUR_SET) != 0)
			d = rw(DUR_SET);
		while (d > 50 && rw(FREE_COUNT) > 3) {
			new_pause(50);
			d -= 50;
		}
		set_dur(n, d > 50 ? 50 : d);
		wb(n + N_F0, 50);
		return 0;
	}
	if (ch == 'Y') { /* a syllabic consonant's schwa */
		if (f(node_char(rw(PREV_NODE)), 0) & 8)
			set_dur(n, 7);
		else if (rw(CUR_B5) == 0 && (f(prev_ch(), 0) & 0x10))
			set_dur(n, 6);
		else
			set_dur(n, 8);
		return 0;
	}
	ww(INH_DUR, (uint16_t)(rsb(rw(INH_TABLE) + ch) * 10));
	ww(MIN_DUR, (uint16_t)(rsb(rw(MIN_TABLE) + ch) * 10));
	ww(PRCNT, 100);
	if (rw(INH_DUR) == rw(MIN_DUR))
		return 1;

	/* rule 2: phrase-final lengthening of the vowel in the last syllable before a boundary */
	if (rw(FAST_START) == 0 && context_search(1, 6, 0x140, -1, 0)) {
		int p;
		if (f(ch, 0) & 0x20) {
			p = 150;
		} else if (rw(CUR_SYL) == 0) {
			p = 160;
		} else {
			int nx = node_char(rw(NEXT_NODE));
			int voiced_next = context_search(0, 5, 8, -1, 0) && !(f(nx, 0x80) & 0x40) &&
			                  (!(f(nx, 0x80) & 8) || (f(nx, 0) & 4));
			if (voiced_next) {
				if ((f(ch, 0x200) & 2) && ch != 'r' && ch != 'g')
					p = 60;
				else if (ch_in(ch, "IyOfw3"))
					p = 78;
				else
					p = 100;
			} else if ((f(ch, 0x200) & 2) && ch != 'r') {
				p = ch == 'g' ? 120 : 82;
			} else if (ch_in(ch, "IyOfw3")) {
				p = 90;
			} else {
				p = 140;
			}
		}
		i = push(i, p);
	}
	/* vowels: position in the phrase and the word */
	if (rw(CUR_SYL) != 0) {
		if (rw(PHRASE_FINAL) == 0)
			i = push(i, 60);
		if (ch == 'a' && rw(CUR_B5) != 0 && (f(node_char(rw(PREV_NODE)), 0) & 8) &&
		    !(f(node_char(rw(NEXT_NODE)), 0) & 4) && (f(node_char(rw(NEXT_NODE)), 0x100) & 1))
			i = push(i, 65);
		else if (ch == 'w' && rw(CUR_B5) != 0 && (f(node_char(rw(PREV_NODE)), 0) & 8) && next_ch() == 'j')
			i = push(i, 50);
		else if (rw(CLAUSE_FINAL) == 0)
			i = push(i, 85);
		else if (rw(CUR_B5) == 0 && context_search(0, 5, 8, -1, 0))
			i = push(i, 80);
	}
	if (rw(CUR_SYL) != 0 && rw(CLAUSE_FINAL) == 0) {
		i = push(i, 80);
	} else if (context_search(0, 7, 1, -8, 0)) {
		if (rw(CUR_SYL) == 0)
			d = 85;
		else if (rw(PHRASE_FINAL) != 0 && node_char(rw(NEXT_NODE)) == 'Z' &&
		         (ch == 'b' || ch == 'O' || ch == '3'))
			d = 55;
		else
			d = 80;
		i = push(i, d);
	}
	/* unstressed segments */
	if (rw(CUR_B5) == 0) {
		ww(MIN_DUR, rw(MIN_DUR) >> 1);
		if (rw(CUR_FRIC) == 0) {
			d = 75;
			if (rw(CUR_SYL) != 0)
				d = rw(CLAUSE_FINAL) == 0 && context_search(0, 5, 1, -8, 0) ? 50 : 70;
			if (rw(CUR_NASAL) != 0 && rw(PREV_SYL) != 0 && rw(NEXT_SYL) != 0 && rw(NEXT_B5) == 0)
				d = 50;
			if (ch == 'R' && ((f(prev_ch(), 0x200) & 2) || prev_ch() == '3') && (f(next_ch(), 0x100) & 2) &&
			    !node_bit(rw(NEXT_NODE), 5))
				d = 40;
			if (ch == 'B')
				d = 100;
			i = push(i, d);
		}
	}
	if (node_stress(n) == 3)
		i = push(i, 130);
	if ((ch == 'E' || ch == 'v') && node_has(rw(NEXT_NODE), 8, 0))
		i = push(i, 150);
	if (rw(CUR_NASAL) != 0 && (node_has(rw(NEXT_NODE), 8, 0) || node_has(rw(NEXT_NODE), 0x140, 0)))
		i = push(i, 160);
	/* sonorants before a stressed vowel in the same cluster */
	if (rw(CLAUSE_FINAL) != 0 && rw(CUR_SON) != 0 &&
	    (rw(CUR_VOWEL) != 0 || (node_has(rw(PREV_NODE), 0x202, 0) && !node_bit(n, 5)))) {
		d = rw(CLUSTER_PCT + 2 * (rw(NEXT_PLACE) - 1));
		if (prev_ch() == 'R')
			d -= 20;
		if ((node_has(rw(NEXT_NODE), 0x10, 0) || node_char(rw(NEXT_NODE)) == 'j') && rw(NEXT_B5) == 0)
			d = percent(d, (f(ch, 0x80) & 0x10) ? 40 : 70);
		else if (rw(PHRASE_FINAL) == 0 && d > 100)
			d = mul_div_u(d - 100, 3, 10) + 100;
		i = push(i, d);
	}
	/* consonant-specific rules */
	after = ' ';
	if (rw(NEXT_NODE) != 0 && node_next(rw(NEXT_NODE)) != 0)
		after = node_char(node_next(rw(NEXT_NODE)));
	int next_yb = next_ch() == 'Y' && after == 'b';
	if (ch == 'K' && ((rw(CUR_B5) == 0 && (f(prev_ch(), 0x100) & 2) && ((f(next_ch(), 0x100) & 2) || next_yb)) ||
	                  (f(node_char(rw(NEXT_NODE)), 0) & 8)))
		i = push(i, 180);
	if (ch == 'G') {
		if (rw(CUR_B5) == 0 && (f(prev_ch(), 0x100) & 2) &&
		    (((f(next_ch(), 0x100) & 2) && next_ch() != 'p') || next_yb))
			i = push(i, 60);
		else if (f(node_char(rw(NEXT_NODE)), 0) & 8)
			i = push(i, 80);
	}
	if (ch == 'B') {
		if ((f(prev_ch(), 0) & 1) && ((f(next_ch(), 0) & 1) || next_yb))
			i = push(i, 50);
		if (f(node_char(rw(NEXT_NODE)), 0) & 8)
			i = push(i, 80);
		if (next_ch() == 'p')
			i = push(i, 70);
		if (prev_ch() == 'S')
			i = push(i, 40);
	}
	if (ch == 'T' && ((f(next_ch(), 0) & 8) || next_ch() == 'p'))
		i = push(i, 260);
	if (ch == 'P' && ((f(next_ch(), 0) & 8) || next_ch() == 'p' || next_ch() == 'S'))
		i = push(i, 150);
	if (ch == 'D' && ((f(next_ch(), 0) & 8) || next_ch() == 'p'))
		i = push(i, 60);
	if (ch == 'V') {
		if ((f(prev_ch(), 0) & 1) && ((f(next_ch(), 0) & 1) || next_yb))
			i = push(i, 80);
		else if (f(node_char(rw(PREV_NODE)), 0) & 8)
			i = push(i, 50);
	}
	if (rw(CUR_B5) != 0 && ch == 's' && (f(node_char(rw(PREV_NODE)), 0) & 8))
		i = push(i, 190);
	if (rw(CUR_B5) != 0 && (f(node_char(rw(PREV_NODE)), 0) & 8)) {
		if (ch == 'R' && next_ch() != 'E')
			i = push(i, next_ch() == 'b' ? 80 : 60);
		if (ch == 'C')
			i = push(i, 40);
	}
	/* (DB3D1 compares the char with 0x5820, which never matches) */
	if ((ch == 'T' || ch == 'F') && next_ch() == 'S' && prev_ch() == 'j')
		i = push(i, 150);
	if (ch == 'P' && next_ch() == 'T' && prev_ch() == 'j')
		i = push(i, 150);
	if (ch == 'R' && (f(prev_ch(), 0x100) & 0x80) && (f(next_ch(), 0x180) & 0x20) && !(f(next_ch(), 0x100) & 0x20))
		i = push(i, 160);
	ww(PRCNT, product(i));
	return 1;
}

/* DB4A6: the consonant percentages, then Klatt's formula DUR = MIN + (INH - MIN) * PRCNT / 100, scaled by the
 * speed, and a few additive corrections. Node +6 gets the duration in frames, at most 55. */
void klatt_duration(void)
{
	int n = cur(), i = 0, p, d, ch;
	if (cur_ch() == 'x' && (prev_ch() == 'T' || prev_ch() == 'D') && rw(PR_SPEED) > 19) {
		/* at high speed a schwa after T or D becomes a flap */
		wb(n + N_CHAR, 'D');
		context_load();
		ww(PREV_NODE, rw(PREV_SEG));
		wb(PREV_CH, rw(PREV_NODE) ? rb(rw(PREV_NODE) + N_CHAR) : 0);
		ww(PREV_SYL, rsb(0xA8 + prev_ch()) & 1);
		ww(NEXT_NODE, rw(NEXT_SEG));
		wb(NEXT_CH, rw(NEXT_NODE) ? rb(rw(NEXT_NODE) + N_CHAR) : 0);
		ww(NEXT_SYL, rsb(0xA8 + next_ch()) & 1);
	}
	ch = cur_ch();
	if (rw(INH_DUR) <= rw(MIN_DUR)) {
		d = rw(INH_DUR);
		if (ch == 'p') {
			if (prev_ch() == 'N' || prev_ch() == 'M')
				d = 40;
			else if (prev_ch() == '~')
				d = 60;
		}
		goto done;
	}
	p = 100;
	if (rw(CUR_SYL) != 0) {
		if (rw(CUR_B5) == 0) {
			if (rw(PREV_SYL) != 0)
				p = 70;
			else if (rw(NEXT_SYL) != 0)
				p = 120;
		}
	} else {
		if (!(f(ch, 0x180) & 2) && rw(PREV_SYL) == 0 && node_has(rw(PREV_NODE), 0x108, -1) &&
		    node_has(rw(PREV_NODE), 0x302, -1)) {
			p = 90;
			if (prev_ch() == 'S' && rw(CUR_CLOSURE) != 0 && (ch != 'K' || next_ch() != 'T'))
				p = 50;
			if (f(ch, 0x180) & 1)
				p = 60;
			if (rw(CUR_B5) == 0 && p > 0) {
				p -= 20;
				if (rw(CUR_CLOSURE) != 0 && node_has(rw(PREV_NODE), 0x201, 0) &&
				    rsb(rw(PLACE_TABLE) + node_char(n)) ==
				        rsb(rw(PLACE_TABLE) + node_char(rw(PREV_NODE))))
					p = 40;
				goto have_p;
			}
		}
		if (rw(CUR_SON) == 0 && rw(CUR_AFFR) == 0 && rw(NEXT_NODE) != 0 && rw(NEXT_SYL) == 0 &&
		    prev_ch() != 'S' && node_has(rw(NEXT_NODE), 0x302, -1)) {
			p = 70;
			if (rw(CUR_FRIC) != 0 &&
			    (node_has(rw(NEXT_NODE), 0x201, 0) ||
			     (node_has(rw(NEXT_NODE), 0x40, 0) && node_has(rw(PREV_NODE), 0x108, -1))))
				p = 30;
		}
	}
have_p:
	i = push(i, p);
	if (rw(WORD_CLASS) != 0) {
		if (rw(CUR_SYL) != 0)
			i = push(i, rw(CUR_B5) ? 200 : 125);
		else if (rw(CUR_SON) != 0)
			i = push(i, rw(CUR_B5) ? 150 : 125);
		else if (rw(CUR_FRIC) != 0)
			i = push(i, 125);
		if (rw(WORD_CLASS) == 2)
			i = push(i, 150);
	}
	if (rw(CUR_VOWEL) != 0 && node_bit(n, 6))
		i = push(i, 120);
	if (prev_ch() == 'H')
		i = push(i, 80);
	p = product(i);
	p = s16(mul_div_u(rw(SPEED_PCT + 2 * rw(PR_SPEED)), p / 2, 100) << 1);
	ww(PRCNT, p);
	d = mul_div_u(rw(INH_DUR) - rw(MIN_DUR), rw(PRCNT), 100) + rw(MIN_DUR);
	if (rw(CUR_FRIC) != 0 && node_has(rw(PREV_NODE), 0x108, 0)) {
		/* a fricative after an affricate-like segment takes that segment's length */
		d = (uint16_t)(node_dur(rw(PREV_NODE)) * 10);
		if (rw(CUR_B5) != 0) {
			if (feature(node_char(next_symbol(n)), 0x80) & 0x40)
				d += 40;
			else if (prev_ch() == 'J' && ch == 'z')
				d -= 17;
			else
				d += 30;
		}
		d = percent(d, 80);
	}
	if (rw(CUR_SON) != 0 && rw(CUR_VOWEL) == 0 && prev_ch() == 'S')
		d += 15;
	if (rw(CUR_B5) != 0 && rw(CUR_VOWEL) != 0 && node_has(rw(PREV_NODE), 0x20, 0) && node_has(rw(PREV_NODE), 4, -1))
		d += 25;
	if (rw(CUR_B5) != 0 && prev_ch() == 'S' && (f(ch, 0) & 0x20) && (next_ch() == 'L' || next_ch() == 'R') &&
	    ch != 'B' && ch != 'G')
		d = percent(d, 50);
	if (!(f(prev_ch(), 0) & 0x10) && ch == 'D' && (f(next_ch(), 0) & 1) && !(f(next_ch(), 0x100) & 2))
		d = percent(d, 50);
	if (ch == 'T' && (f(next_ch(), 0x100) & 2)) {
		if (rw(CUR_B5) != 0)
			d += 20;
		else if (prev_ch() == 'S' || (f(prev_ch(), 0) & 2))
			d += 30;
	}
done:
	d = s16(d) / 10;
	if (d >= 55)
		d = 55;
	set_dur(n, d);
}

/* DC332: insert the pause for punctuation (length 0) or an explicit pause of `length`. */
void insert_pause(int length)
{
	int d, split = 0, p;
	if (cur_ch() == ')' && rw(PR_SPEED) >= 6)
		return;
	if (rw(PR_SPEED) < 19 && rw(FAST_START) == 0) {
		if (length > 0) {
			split = 1;
			d = length < 30 ? 30 : length > 80 ? 80 : length;
		} else {
			switch (cur_ch()) {
			case '.':
			case '?':
				d = 39;
				split = 1;
				break;
			case ',':
				d = 10;
				break;
			case ']':
				d = 12;
				break;
			case '\\':
				d = 4;
				break;
			default:
				d = 8;
				break;
			}
		}
		d = s16(mul_div_u(rw(PAUSE_SCALE + 2 * rw(PR_SPEED)) / 4, d, 100) << 2);
		if (length == 0 && cur_ch() == ']')
			d = 12;
		for (;;) {
			if (d < 4)
				break;
			if (split && d < 50) { /* a short sentence pause becomes two nodes, 1/3 and 2/3 */
				new_pause(d / 3);
				new_pause(s16(d << 1) / 3);
				split = 0;
			} else {
				new_pause(d > 50 ? 50 : d);
			}
			d -= 50;
			if (d < 4 && d > 0)
				d = 4;
			if (rw(FREE_COUNT) < 3)
				break;
		}
	} else {
		p = node_insert(cur(), 0, NODE_SEGMENT, ' ');
		if (length == 0 && cur_ch() == ']') {
			set_dur(p, 12);
		} else if (rw(FAST_START) == 0) {
			set_dur(p, 7);
			wb(p + N_F0, 50);
			p = node_insert(cur(), 0, NODE_SEGMENT, ' ');
			set_dur(p, 3);
		} else {
			set_dur(p, 2);
			wb(p + N_F0, 50);
			p = node_insert(cur(), 0, NODE_SEGMENT, ' ');
			set_dur(p, 2);
		}
		wb(p + N_F0, 50);
	}
	context_load();
}

/* DA8AB: a consonant after the same (or a homorganic) consonant is merged with it, then the allophone-specific
 * durations are applied. */
void merge_geminate(void)
{
	int prev = rw(PREV_NODE), c = cur_ch(), pc = prev_ch(), d;
	if ((f(c, 0x80) & 0x20) && rw(PR_SPEED) > 6 &&
	    ((c == pc && !node_bit(cur(), 6)) || ((pc == 'S' || pc == 'Z') && c == 's') || (pc == 'Z' && c == 'S') ||
	     (pc == 'V' && c == 'F') || (pc == 'D' && c == 'T') || (pc == 'B' && c == 'P'))) {
		d = node_dur(cur()) + node_dur(prev);
		if (d >= 55)
			d = 55;
		set_dur(cur(), d);
		if (rw(CUR_B5) != 0)
			ww(cur() + N_FLAGS, rw(cur() + N_FLAGS) | 0x20);
		wb(cur() + N_F0, rb(prev + N_F0));
		node_delete(prev, 0);
	}
	if (ch_in(cur_ch(), "CDHPSTZlqs"))
		cluster_duration();
	context_load();
}

/* DC57B: fixed durations for some consonant allophones, by what follows them. */
void cluster_duration(void)
{
	int d = node_dur(cur()), n1 = next_symbol(cur()), n2 = next_symbol(n1);
	int c1 = node_char(n1), c2 = n2 ? node_char(n2) : ' ', pc = prev_ch();
	switch (cur_ch()) {
	case 'l':
		if (c1 == '%' || c1 == '&') {
			if (f(c2, 0) & 2)
				d = 12;
			else if (c2 == 'Z')
				d = 13;
			else
				d = 10;
		} else if (f(c1, 0) & 2) {
			d = 12;
		} else if (c1 == 'Z') {
			d = 13;
		} else if ((f(c1, 0x80) & 1) || c1 == ' ') {
			d = 16;
		} else {
			d = 10;
		}
		break;
	case 'S':
		if (pc == 'S') {
			d = 14;
			break;
		}
		switch (c1) {
		case '%':
		case '&':
			switch (c2) {
			case 'B':
				d = 10;
				break;
			case 'D':
			case 'F':
				d = 9;
				break;
			case 'G':
			case 'K':
			case 'T':
				d = 8;
				break;
			case 'P':
				d = 7;
				break;
			default:
				if (f(c2, 0x100) & 2)
					d = node_bit(n2, 5) ? 11 : 10;
				else
					d = (f(c2, 0) & 2) ? 10 : 8;
				break;
			}
			break;
		case ' ':
		case '.':
		case '\\':
		case ']':
			d = 18;
			break;
		case 'B':
			d = 10;
			break;
		case 'D':
		case 'F':
			d = 9;
			break;
		case 'G':
		case 'K':
		case 'T':
			d = 8;
			break;
		case 'P':
			d = 7;
			break;
		default:
			if (f(c1, 0x100) & 2)
				d = node_bit(n1, 5) ? 11 : 10;
			else
				d = (f(c1, 0) & 2) ? 11 : 8;
			break;
		}
		break;
	case 'Z':
		d = 8;
		if (c1 == '.') {
			d = 17;
		} else if (c1 == 'F') {
			d = 12;
		} else if ((f(c1, 0) & 0x20) && (f(c1, 0) & 4)) {
			d = 6;
		} else if (f(c1, 0x80) & 0x40) {
			d = 13;
		} else if (f(c1, 0) & 8) {
			if ((f(c2, 0) & 0x20) && (f(c2, 0) & 4))
				d = 6;
			else if (c1 == 'F')
				d = 12;
		}
		break;
	case 'H':
		if (pc == 'S')
			d = 3;
		break;
	case 's':
		switch (pc) {
		case 'C':
			d = c1 == '.' ? 14 : (f(c1, 0x80) & 0x40) ? 12 : 7;
			break;
		case 'S':
		case 'Z':
		case 's':
			d = 14;
			break;
		}
		break;
	case 'D':
		if (c1 == 'n')
			d = 4;
		break;
	case 'C':
		d = 4;
		break;
	case 'P':
		if ((f(pc, 0x100) & 2) && c1 == 'R' && rw(CUR_B5) == 0)
			d = 8;
		break;
	case 'q':
		if (c1 == 'n')
			d = 7;
		break;
	}
	if (rw(WORD_CLASS) != 0) {
		d = percent(d, 125);
		if (rw(WORD_CLASS) == 2)
			d = percent(d, 150);
	}
	if (rw(PR_AFLAGS) & 0x10) {
		if ((feature(node_char(cur()), 0) & 0x80) && node_char(cur()) != ' ')
			d = mul_div_s(rsb(rw(MIN_TABLE) + cur_ch()), 17, 20);
		if (rw(CUR_B5) == 0)
			d = s16(d) >> 1;
	}
	if (rw(PR_SPEED) != 13) {
		int min = rw(MIN_DUR) / 10;
		d = percent(rw(SPEED_PCT + 2 * rw(PR_SPEED)), percent(d, 91));
		if (d < min) {
			set_dur(cur(), min);
			return;
		}
		if (d >= 55) {
			set_dur(cur(), 55);
			return;
		}
	}
	set_dur(cur(), d);
}

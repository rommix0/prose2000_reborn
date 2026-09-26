/* v1.1 prosody stage (EEB9C): turns the lexical stage's phoneme symbols into timed segments.
 *
 * It works like v3.4.1's (REFERENCE §15.2) but is a different program. A phrase scan (phrase_scan) runs ahead of the
 * cursor to the next '.', '?', ',' or C command, marks word boundaries and reduces function words; the walk
 * (phrase_walk) folds the stress digits into the vowels. Then each phoneme symbol at the cursor gets its allophone
 * rules (allophones: flaps, glottal stops, syllabic L, R-coloured vowels, pauses for punctuation), a duration
 * (duration_rules, duration_set: Klatt's formula from inherent and minimum durations and a list of percentages) and
 * an F0 (f0_set), and becomes kind 4 with the duration in frames in its high flags byte and F0/2 at +6.
 *
 * All arithmetic is 16-bit as on the 8086; the percentages are taken from the low word of the product, unsigned. */
#include "v1.h"

#define R V1_REC_PROSODY
#define PR_DONE (R + V1_REC_DONE)     /* first segment not yet handed on */
#define PR_CUR (R + V1_REC_CURSOR)    /* the symbol being timed */
#define PR_AHEAD (R + V1_REC_AHEAD)   /* the phrase scan's position */
#define PR_LAST (R + V1_REC_LAST)
#define PR_FIX (R + 0x0A)             /* a word whose last vowel word_reduce changes once the next word is known */
#define PR_INPUT (R + V1_REC_INPUT)
#define PR_WORD (R + V1_REC_WORD)     /* ESC[P: 1 = function words are reduced and phrases broken */
#define PR_RATE (R + V1_REC_RATE)
#define PR_PITCH (R + V1_REC_PITCH)
#define PR_MODE (R + V1_REC_MODE)

#define WORD_REC 0x9652     /* the last word record ('&', '%') or break symbol the scan passed */
#define PREV_REC 0x9654     /* the one before it */
#define BEHIND 0x9656       /* segments kept behind the cursor (not yet handed on) */
#define BEHIND_MAX 0x9658   /* 7 */
#define RUN 0x965A          /* non-symbol nodes in a row */
#define WORDS 0x965C        /* words since the last break */
#define INH 0x965E          /* inherent duration, in 1 ms */
#define MIN 0x9660          /* minimum duration, in 1 ms */
#define ACCENT 0x9664       /* F0 accent in Hz: 20 at a sentence start, 10 after a boundary, 6 after an accent */
#define RISE 0x9666         /* the hat pattern's raise (30 Hz) */
#define EMPHASIS 0x9668     /* a primary-stressed ('"') vowel lies ahead */
#define PHRASE_END 0x966A   /* the walk reached the end of the phrase */
#define SCAN_BREAK 0x966C   /* the scan stopped at a break */
#define FAST 0x966E         /* mode flag 14: fixed short pauses, segments handed on at once */
#define CONS_CLASS 0x9670   /* class of the consonants after a vowel, for the vowel's duration (1-4) */
#define WALK 0x9672         /* the walk's position */
#define WALK_ON 0x9674      /* WALK has been visited */
#define AHEAD_COUNT 0x9676  /* symbols walked ahead of the cursor */
#define AHEAD_MAX 0x9678    /* 20 */
#define DECLINE 0x967A      /* F0 above the pitch setting; falls by 5 % after each vowel */
#define QUESTION 0x967C     /* a 'Q' or 'q' symbol: mark the next symbol */
#define SCAN_END 0x967E     /* where the scan stopped */
#define BREAK_WORDS 0x9680  /* 5: words before a function word may get a comma */
#define NEW_SENTENCE 0x9684
#define WORD_CH 0x9686      /* byte: WORD_REC's character */
#define PREV_WORD_CH 0x9688 /* byte */
#define CMD_RATE 0x968A     /* the rate from the last r command the scan passed (160 at first) */
#define ACCENTS 0x968C      /* stressed syllables since the phrase start */
#define PHRASE_FINAL 0x968E /* the vowel is in the phrase's last syllable (a pause before the next vowel) */
#define WORD_FINAL 0x9690   /* ... in its word's last syllable (a boundary before the next vowel) */
#define ACCENT_ON 0x9692   /* the hat is up */
#define FIX_KIND 0x9694     /* 1: 'T', 2: 'x' word start (PR_FIX) */
#define GIVEN_DUR 0x9696    /* duration given with phoneme input (frames) */
#define BREAK_TICK 0x9698   /* counts the ')' decisions, for the table at DS:48A6 */
#define GIVEN_CH 0x969A     /* byte */
#define GIVEN_F0 0x969C     /* F0/2 given with phoneme input */
#define GIVEN_NODE 0x969E
#define PCT 0x96A0          /* the duration percentages, multiplied together */
#define PREV_SEG 0x96B8     /* the segment timed before the cursor */
#define NEXT_PH 0x96BA      /* the next phoneme symbol */
#define LEFT 0x96BC         /* the symbol or segment before the cursor */
#define RIGHT 0x96BE        /* the symbol after it */
/* the cursor's character and features (context_load) */
#define CF80_08 0x96C0      /* feature plane 0x80, bit 8 */
#define CF_40 0x96C2
#define CF_10 0x96C4
#define CF_PHONEME 0x96C6   /* bit 0x80 */
#define CF_02 0x96C8
#define CF100_01 0x96CA
#define STRESSED 0x96CC     /* flags bit 5: in a stressed syllable */
#define CF_VOWEL 0x96CE     /* bit 1 */
#define CF100_02 0x96D0
#define C 0x96D2            /* byte */
#define L_VOWEL 0x96D4
#define LC 0x96D6           /* byte: LEFT's character */
#define R_VOWEL 0x96D8
#define RC 0x96DA           /* byte: RIGHT's character */

#define TBL_BREAK 0x21D7    /* by rate: bits of ')' breaks, against DS:48A6 */
#define TBL_PAUSE 0x21F5    /* by rate: pause percentage */
#define TBL_DUR 0x221D      /* by rate: duration percentage */
#define TBL_CLASS 0x224F    /* by CONS_CLASS: vowel percentage */
#define P_INH 0x22B9        /* -> inherent durations by character (10 ms) */
#define P_MIN 0x231B        /* -> minimum durations */
#define P_PLACE 0x237D      /* -> place of articulation by character */

static int ch_of(unsigned n) { return rsb(n + V1_NODE_CH); }
static int kind_of(unsigned n) { return rw(n + V1_NODE_FLAGS) & 7; }
static int stress_of(unsigned n) { return (rw(n + V1_NODE_FLAGS) >> 3) & 3; }
static int bit5(unsigned n) { return (rw(n + V1_NODE_FLAGS) >> 5) & 1; }
static int arg0_of(unsigned n) { return (rw(n + V1_NODE_FLAGS) >> 8) & 0xFF; }
static void set_arg0(unsigned n, int v) { ww(n + V1_NODE_FLAGS, (rw(n + V1_NODE_FLAGS) & 0xFF) | v << 8); }
static void set_flags(unsigned n, int bits) { ww(n + V1_NODE_FLAGS, rw(n + V1_NODE_FLAGS) | bits); }
static void set_stress(unsigned n, int level)
{
	ww(n + V1_NODE_FLAGS, (rw(n + V1_NODE_FLAGS) & 0xFFE7) | (level & 3) << 3);
}
static void inc(unsigned a, int d) { ww(a, rw(a) + d); }

/* the feature table at DS:0090 by character, in one of its planes (0, 0x80, 0x100, 0x180, 0x200) */
static int feat(int c, int plane) { return rb(0x90u + (((unsigned)c | (unsigned)plane) & 0xFFFFu)); }
static int pct(int a, int b) { return (uint16_t)(a * b) / 100; }
static int by_rate(unsigned table) { return rw(table + 2 * (s16(rw(PR_RATE) - 0x2E) >> 3)); }

/* EF393: the symbol or segment before n */
unsigned v1_pr_prev_ph(unsigned n)
{
	unsigned p = 0;
	if (n != 0)
		do {
			p = v1_node_prev(n);
			if (p == 0)
				return 0;
			if (kind_of(p) == 3)
				return p;
			n = p;
		} while (kind_of(p) != 4);
	return p;
}

/* EF453: the symbol after n */
unsigned v1_pr_next_sym(unsigned n)
{
	unsigned p = 0;
	if (n != 0)
		do {
			p = v1_node_next(n);
			if (p == 0)
				return 0;
			n = p;
		} while (kind_of(p) != 3);
	return p;
}

/* EF3D4: does n's character have the feature bits in mask's low byte, in plane (mask's high byte) · 0x80? A negative
 * mask and a negative neg each invert the answer. */
int v1_pr_test(unsigned n, int mask, int neg)
{
	int r = 0;
	if (n != 0) {
		int inv = mask < 0;
		if (inv)
			mask = -mask;
		r = rb(0x90u + ((((mask & 0xFF00) >> 1) | ch_of(n)) & 0xFFFFu)) & mask & 0xFF;
		if (inv)
			r = r == 0;
		if (neg < 0)
			r = r == 0;
	}
	return r;
}

/* EF4F4 helper: a stress level test (a = 1 for primary, 2 for any stress; negative inverts) */
static int stress_hit(unsigned n, int a)
{
	int lv = 0;
	if (stress_of(n) == 3)
		lv = 1;
	else if ((a < 0 ? -a : a) == 2 && stress_of(n) == 2)
		lv = 2;
	return lv;
}

/* EF4F4: look up to count symbols to the right (dir 1) or left of the cursor for one matching yes (1) before one
 * matching no (0). mode 1 makes yes a stress test, mode 2 no. */
int v1_pr_scan(int dir, int count, int yes, int no, int mode)
{
	unsigned n = ruw(PR_CUR);
	for (; count >= 1; count--) {
		n = (int8_t)dir == 1 ? v1_pr_next_sym(n) : v1_pr_prev_ph(n);
		if (n == 0)
			break;
		if ((int8_t)mode == 1) {
			int lv = stress_hit(n, yes);
			if ((lv != 0 && yes > 0) || (lv == 0 && yes < 0))
				return 1;
		} else if (v1_pr_test(n, yes, 0))
			return 1;
		if ((int8_t)mode == 2) {
			int lv = stress_hit(n, no);
			if ((lv != 0 && no < 0) || (lv == 0 && no > 0))
				return 0;
		} else if (v1_pr_test(n, no, -1))
			return 0;
	}
	return 0;
}

/* EF488: a new sentence */
void v1_pr_sentence_reset(void)
{
	ww(PHRASE_END, 0);
	wb(PREV_WORD_CH, 0);
	wb(WORD_CH, 0);
	ww(SCAN_END, 0);
	ww(RUN, 0);
	ww(0x9682, 0);
	ww(EMPHASIS, 0);
	ww(ACCENT_ON, 0);
	ww(ACCENT, 0x14);
	ww(RISE, 0x1E);
	ww(ACCENTS, 0);
	ww(DECLINE, 0x23);
	ww(CONS_CLASS, 4);
	ww(PHRASE_FINAL, 0);
	ww(WORD_FINAL, 0);
	ww(FIX_KIND, 0);
	ww(WORDS, 0);
}

/* EF29A: run the command nodes at the cursor, up to the next symbol or C command */
void v1_pr_skip_commands(void)
{
	unsigned n = ruw(PR_CUR);
	while (n != 0 && kind_of(n) != 3 && !(kind_of(n) == 0 && ch_of(n) == 'C')) {
		if (ruw(PR_AHEAD) == ruw(PR_DONE) && kind_of(n) == 0 && ch_of(n) == 'r')
			ww(CMD_RATE, arg0_of(n));
		v1_command_apply();
		ww(PR_CUR, v1_node_next(n));
		if (ruw(WALK) == n) {
			ww(WALK, rw(PR_CUR));
			ww(WALK_ON, 0);
		}
		if (ruw(PR_AHEAD) == n)
			ww(PR_AHEAD, rw(PR_CUR));
		if (ruw(PR_DONE) == n)
			ww(PR_DONE, rw(PR_CUR));
		n = ruw(PR_CUR);
	}
}

/* EF663: keep the duration and F0 given with a phoneme (phoneme input) across the rules (restore 0: save) */
void v1_pr_given_values(int restore)
{
	unsigned n = ruw(PR_CUR);
	if (restore == 0) {
		ww(GIVEN_NODE, n);
		ww(GIVEN_F0, rsb(n + V1_NODE_ARG1));
		ww(GIVEN_DUR, arg0_of(n));
		wb(GIVEN_CH, rb(n + V1_NODE_CH));
		if (rsb(GIVEN_CH) == ' ' && rw(GIVEN_DUR) > 0x32)
			ww(GIVEN_DUR, 0x32);
	} else if (ruw(GIVEN_NODE) == n && rsb(GIVEN_CH) == ch_of(n)) {
		if (rw(GIVEN_DUR) != 0)
			set_arg0(n, rw(GIVEN_DUR));
		if (rw(GIVEN_F0) != 0) {
			if (rw(GIVEN_F0) == 1)
				ww(GIVEN_F0, 0);
			wb(n + V1_NODE_ARG1, rw(GIVEN_F0));
		}
	}
}

/* F11BC: load the cursor's context: its neighbours, characters and features */
void v1_pr_context_load(void)
{
	unsigned n = ruw(PR_CUR), l = v1_pr_prev_ph(n), r;
	ww(LEFT, l);
	r = v1_pr_next_sym(n);
	ww(RIGHT, r);
	wb(C, rb(n + V1_NODE_CH));
	wb(LC, l == 0 ? 0 : rb(l + V1_NODE_CH));
	wb(RC, r == 0 ? 0 : rb(r + V1_NODE_CH));
	int c = rsb(C);
	ww(CF80_08, feat(c, 0x80) & 8);
	ww(CF_40, feat(c, 0) & 0x40);
	ww(CF_10, feat(c, 0) & 0x10);
	ww(CF_PHONEME, feat(c, 0) & 0x80);
	ww(CF_02, feat(c, 0) & 2);
	ww(CF100_01, feat(c, 0x100) & 1);
	ww(CF_VOWEL, feat(c, 0) & 1);
	ww(CF100_02, feat(c, 0x100) & 2);
	ww(STRESSED, bit5(n) != 0);
	ww(L_VOWEL, feat(rsb(LC), 0) & 1);
	ww(R_VOWEL, feat(rsb(RC), 0) & 1);
}

/* after LEFT and RIGHT move: their characters and vowel flags */
static void neighbours_load(unsigned l, unsigned r)
{
	ww(LEFT, l);
	ww(RIGHT, r);
	wb(LC, l == 0 ? 0 : rb(l + V1_NODE_CH));
	wb(RC, r == 0 ? 0 : rb(r + V1_NODE_CH));
	ww(L_VOWEL, feat(rsb(LC), 0) & 1);
	ww(R_VOWEL, feat(rsb(RC), 0) & 1);
}

/* F0F5F: function-word reduction. At a word record (end 0) the word before it (PREV_REC to WORD_REC) loses stress
 * if it is a function word (a '%' or '&' record) of class 1 (weaker stress) or, above rate 100, any class; above
 * rate 160 a word-initial H goes after a consonant. At higher rates class 3 words reduce their vowels to '@', and
 * class 2 words lose an A's second part or an H; a class-3 T... or class-2 x... word waits for the next word
 * (PR_FIX), whose first sound then decides its vowel (end 1). */
void v1_pr_word_reduce(int end, int c)
{
	int content = !(rsb(WORD_CH) == '&' || rsb(WORD_CH) == '%');
	unsigned n = ruw(PREV_REC), w = ruw(WORD_REC);
	if (end != 0) {
		n = v1_node_next(v1_node_next(ruw(PR_FIX)));
		if (rw(FIX_KIND) == 1 && rw(CMD_RATE) > 0x64)
			c = feat(c, 0x100) & 2 ? 'b' : '@';
		else if (rw(FIX_KIND) == 2)
			c = feat(c, 0x100) & 2 ? 'E' : rw(CMD_RATE) > 0x64 ? '@' : 'v';
		wb(n + V1_NODE_CH, c);
		ww(PR_FIX, 0);
		return;
	}
	int cls = rsb(n + V1_NODE_ARG1);
	if (!content)
		for (; n != w; n = v1_node_next(n)) {
			int st = stress_of(n);
			if (st == 2 || st == 1) {
				if (cls == 1)
					st = st == 2;
				else if (rw(CMD_RATE) > 0x64)
					st = 0;
				set_stress(n, st);
			}
		}
	n = v1_node_next(ruw(PREV_REC));
	unsigned p = v1_pr_prev_ph(ruw(PREV_REC));
	if (rw(CMD_RATE) > 0xA0 && ch_of(n) == 'H' && cls != 2 && p != 0 && ch_of(p) != ' ' &&
	    !(feat(ch_of(p), 0x80) & 0x40)) {
		v1_node_free(n, 0);
		n = ruw(PREV_REC);
	}
	if (content)
		return;
	if (cls == 3 && rw(CMD_RATE) > 0x64) {
		if (ch_of(n) == 'T') {
			ww(PR_FIX, rw(PREV_REC));
			ww(FIX_KIND, 1);
		} else
			for (; n != w; n = v1_node_next(n))
				if (kind_of(n) == 3 && (feat(ch_of(n), 0x100) & 2))
					wb(n + V1_NODE_CH, '@');
	} else if (cls == 2) {
		int f = ch_of(n);
		if (f == 'x') {
			ww(PR_FIX, rw(PREV_REC));
			ww(FIX_KIND, 2);
		} else if (f == 'a' && rw(CMD_RATE) > 0x64)
			v1_node_free(v1_node_next(v1_node_next(n)), 0);
		else if (f == 'H' && rw(CMD_RATE) > 0x64)
			v1_node_free(n, 0);
	}
}

/* EED9F: scan ahead to the end of the phrase: a '.', '?' or ',' symbol, a C command, or (with no break for a while) an
 * added C command. Blanks get 10 frames; with ESC[1P words are reduced and long runs of words broken with ',' before
 * a function word or ')' (by rate). Returns the last node the walk may reach. */
unsigned v1_pr_phrase_scan(void)
{
	unsigned ret = 0;
	ww(SCAN_BREAK, 0);
	do {
		unsigned n = ruw(PR_AHEAD);
		if (n == 0) {
			if (rw(RUN) < 2)
				return ret;
			ww(RUN, 0);
			for (unsigned m = ruw(R + V1_REC_FIRST); m != 0; m = v1_node_next(m))
				if (kind_of(m) != 3)
					inc(RUN, 1);
			if (rw(RUN) < 0xF)
				return ret;
			n = v1_node_insert(ruw(PR_LAST), 1, 0, 'C');
			ww(PR_AHEAD, n);
			ww(RUN, 0);
		}
		if (rw(WALK) == 0)
			ww(WALK, n);
		int c = ch_of(n), brk;
		if (c == ' ' && kind_of(n) == 3 && arg0_of(n) == 0)
			set_arg0(n, 10);
		if (kind_of(n) == 0 && c == 'x')
			v1_node_insert(n, 1, 0, 'C');
		if (kind_of(n) == 3 && (c == '.' || c == '?' || c == ',')) {
			brk = 1;
			ww(WORDS, 0);
		} else
			brk = kind_of(n) == 0 && c == 'C';
		if (kind_of(n) == 3 && (c == '1' || c == '2' || c == '"')) {
			ww(PR_AHEAD, v1_node_next(n));
			continue;
		}
		if (kind_of(n) == 3) {
			if (c == 'Q' || c == 'q') {
				ww(QUESTION, 1);
				if (c == 'Q') {
					if (v1_node_prev(n) != 0) {
						ww(PR_AHEAD, v1_node_free(n, 1));
						continue;
					}
					wb(n + V1_NODE_CH, ' ');
				}
			} else if (rw(QUESTION)) {
				set_flags(n, 0x40);
				ww(QUESTION, 0);
			}
		}
		if (rw(PR_WORD) == 1) {
			if (brk || (kind_of(n) == 3 && (c == '&' || c == '%'))) {
				if (c == '&')
					inc(WORDS, 1);
				if (brk)
					ww(SCAN_BREAK, 1);
				wb(PREV_WORD_CH, rb(WORD_CH));
				ww(PREV_REC, rw(WORD_REC));
				wb(WORD_CH, c);
				ww(WORD_REC, n);
				int cls = rsb(ruw(WORD_REC) + V1_NODE_ARG1);
				if (c == '%' && rsb(PREV_WORD_CH) == '&' && rw(WORDS) > rw(BREAK_WORDS) &&
				    ((rsb(ruw(PREV_REC) + V1_NODE_ARG1) != 6 && (cls == 3 || cls == 4)) || cls == 5)) {
					v1_node_insert(n, 0, 3, ',');
					ww(SCAN_BREAK, 1);
					ww(WORDS, 0);
					wb(PREV_WORD_CH, 0);
				}
				if (rw(CMD_RATE) < 0x96 && c == '%' && rsb(PREV_WORD_CH) == '&') {
					unsigned bits = ruw(TBL_BREAK + 2 * (s16(rw(PR_RATE) - 0x2E) >> 3));
					unsigned k = ruw(BREAK_TICK);
					inc(BREAK_TICK, 1);
					if (bits & ruw(0x48A6 + 2 * (k & 0xF)))
						v1_node_insert(n, 0, 3, ')');
				}
				if (rsb(PREV_WORD_CH) == '%')
					v1_pr_word_reduce(0, c);
				if (rw(PR_FIX) == 0)
					ret = n;
			} else if (rw(PR_FIX) != 0) {
				v1_pr_word_reduce(1, c);
				ret = ruw(WORD_REC);
			} else if (rsb(WORD_CH) != '%')
				ret = n;
		} else {
			ret = ruw(PR_AHEAD);
			if (brk)
				ww(SCAN_BREAK, 1);
		}
		if (kind_of(n) == 3)
			ww(RUN, 0);
		else
			inc(RUN, 1);
		if (kind_of(n) == 0 && ch_of(n) == 'r')
			ww(CMD_RATE, arg0_of(n));
		ww(PR_AHEAD, v1_node_next(ruw(PR_AHEAD)));
	} while (rw(SCAN_BREAK) == 0);
	return ret;
}

/* F0D15: mark the stressed syllable around vowel n (flags bit 5): the vowel, a consonant before it, a second one if
 * that is a stop-like cluster, and an S before that */
void v1_pr_mark_syllable(unsigned n)
{
	set_flags(n, 0x20);
	unsigned p = v1_pr_prev_ph(n);
	if (v1_pr_test(p, 0x120, 0) && ch_of(p) != '~') {
		set_flags(p, 0x20);
		p = v1_pr_prev_ph(p);
		if (v1_pr_test(p, 0x120, 0) && v1_pr_test(p, 2, -1)) {
			set_flags(p, 0x20);
			p = v1_pr_prev_ph(p);
			if (p != 0 && ch_of(p) == 'S')
				set_flags(p, 0x20);
		}
	}
}

/* F0DA0: a stress digit ('"' primary with emphasis, '1' primary, '2' secondary) moves onto the vowel before it */
unsigned v1_pr_stress_digit(unsigned n)
{
	int c = ch_of(n);
	if (c == '1' || c == '2' || c == '"') {
		if (v1_node_prev(n) == 0)
			wb(n + V1_NODE_CH, ' ');
		else {
			n = v1_node_free(n, 0);
			if (kind_of(n) == 3 && v1_pr_test(n, 1, 0))
				set_stress(n, c == '"' ? 3 : c == '1' ? 2 : 1);
		}
	}
	return n;
}

/* EF161: walk the phrase from WALK towards SCAN_END (scanning further when needed), placing the stress digits.
 * Returns 1 when the cursor may go on: the phrase is complete or enough symbols lie ahead. */
int v1_pr_phrase_walk(void)
{
	int ok = 0;
	unsigned n = ruw(WALK), m;
	if (rw(SCAN_END) == rw(WALK) || rw(SCAN_END) == 0 || rw(WALK) == 0) {
		if (rw(WALK) == 0)
			ww(WALK_ON, 0);
		ww(SCAN_END, v1_pr_phrase_scan());
		if (rw(SCAN_END) == 0)
			return ok;
	}
	for (;;) {
		if (rw(WALK_ON))
			n = v1_node_next(n);
		else {
			n = ruw(WALK);
			ww(WALK_ON, 1);
		}
		int c = ch_of(n);
		if (kind_of(n) == 0 && c == 'C') {
			ok = 1;
			ww(PHRASE_END, 1);
			break;
		}
		if (kind_of(n) == 3) {
			m = v1_pr_stress_digit(n);
			if (stress_of(m) != 0)
				v1_pr_mark_syllable(m);
			if (stress_of(m) == 3)
				ww(EMPHASIS, 1);
			if (m != n)
				n = m;
			else {
				if (n != ruw(PR_CUR))
					inc(AHEAD_COUNT, 1);
				if (c == '.' || c == ',' || c == '?') {
					ok = 1;
					ww(PHRASE_END, 1);
					break;
				}
				if (rw(AHEAD_COUNT) >= rw(AHEAD_MAX))
					ok = 1;
			}
		} else if (rw(AHEAD_COUNT) >= rw(AHEAD_MAX))
			ok = 1;
		if (n == ruw(SCAN_END))
			break;
	}
	ww(WALK, n);
	return ok;
}

/* EEC6E: move the cursor on. At the end of the phrase the finished part is handed on (a new sentence after '.', '?' or
 * without ESC[1P); otherwise segments are handed on once BEHIND_MAX of them are kept (at once with FAST). Returns 1
 * when the stage should commit. */
int v1_pr_cursor_advance(void)
{
	int ret = 0;
	unsigned next = v1_node_next(ruw(PR_CUR));
	if (ruw(WALK) == ruw(PR_CUR) && rw(PHRASE_END) == 1) {
		ww(PHRASE_END, 0);
		ww(SCAN_END, 0);
		ww(BEHIND, 0);
		ww(PR_DONE, next);
		ww(PR_CUR, next);
		ww(WALK, next);
		ww(WALK_ON, 0);
		ww(AHEAD_COUNT, 0);
		ret = 1;
		if (rw(PR_WORD) == 0 || rsb(C) == '.' || rsb(C) == '?')
			ww(NEW_SENTENCE, 1);
	} else {
		ww(PR_CUR, next);
		inc(AHEAD_COUNT, -1);
		if (rw(BEHIND) < rw(BEHIND_MAX))
			inc(BEHIND, 1);
		else {
			int other = 0, k = rw(BEHIND);
			unsigned n = ruw(PR_DONE);
			while ((n = v1_node_next(n)) != 0 && (k >= rw(BEHIND_MAX) || (rw(FAST) != 0 && k >= 1))) {
				if (kind_of(n) != 4)
					other = 1;
				if (kind_of(n) == 3 || kind_of(n) == 4) {
					k--;
					if (other == 1 || rw(FAST) != 0)
						break;
				}
			}
			if (other == 1 || rw(FAST) != 0) {
				ww(BEHIND, k);
				ww(PR_DONE, n);
				ret = 1;
			} else
				inc(BEHIND, 1);
		}
	}
	v1_pr_skip_commands();
	return ret;
}

/* one pause of d units after node n, as 50-frame segments (at most) while nodes last */
static void pause_segments(unsigned n, int d)
{
	do {
		if (d < 1)
			break;
		unsigned m = v1_node_insert(n, 0, 4, ' ');
		set_arg0(m, d > 0x32 ? 0x32 : d);
		d -= 0x32;
		wb(m + V1_NODE_ARG1, 0x32);
		inc(BEHIND, 1);
	} while (rw(V1_FREE_COUNT) > 2);
}

/* EF6F6: the allophone rules for the symbol at the cursor, and the pauses for punctuation */
void v1_pr_allophones(void)
{
	unsigned n = ruw(PR_CUR), l = ruw(LEFT), r = ruw(RIGHT), rr = v1_pr_next_sym(ruw(RIGHT)), m;
	int c8 = 0;
	if (!(feat(rsb(C), 0) & 0x80) || (!(rw(PR_MODE) & 0x100) && rw(PR_INPUT) != 0))
		goto pauses;
	if (!(feat(rsb(C), 0x80) & 0x20)) { /* vowels and the other non-consonants */
		if ((feat(rsb(C), 0x100) & 2) && rsb(C) != 'U' && l != 0 && (feat(ch_of(l), 0) & 8)) {
			ww(PREV_SEG, 0);
			while ((l = v1_pr_prev_ph(l)) != 0)
				if (kind_of(l) == 4) {
					ww(PREV_SEG, l);
					break;
				}
			l = ruw(LEFT);
			rr = v1_pr_prev_ph(ruw(LEFT));
			if (rr != 0 && (feat(ch_of(rr), 0x80) & 0x40))
				set_flags(n, 0x40);
			else if (rw(PREV_SEG) == 0 || rsb(ruw(PREV_SEG) + V1_NODE_CH) == rsb(C))
				set_flags(n, 0x40);
			else {
				int st = stress_of(n);
				if (st != 0) {
					int pc = ch_of(ruw(PREV_SEG));
					if ((feat(pc, 0x100) & 2) && rw(PR_RATE) < 200)
						set_flags(n, 0x40);
					else if (st == 2 && rw(PR_RATE) < 0xA0 && (feat(pc, 0) & 1))
						set_flags(n, 0x40);
					else if (rw(PR_RATE) < 0x65 && ((feat(pc, 0) & 2) || (feat(pc, 0) & 0x40)))
						set_flags(n, 0x40);
				}
			}
		}
		if (rsb(C) == '@' && rsb(RC) == 'L' && v1_pr_test(l, 4, 0) && v1_pr_test(l, 0x280, -1) && v1_pr_test(l, 0x40, -1) &&
		    (rr == 0 || stress_of(rr) == 0)) { /* syllabic L */
			wb(n + V1_NODE_CH, 'l');
			v1_node_free(ruw(RIGHT), 0);
			inc(AHEAD_COUNT, -1);
		} else if (rsb(C) == 'l' && v1_pr_test(ruw(LEFT), 0x201, 0) && v1_pr_test(ruw(LEFT), 4, -1)) {
			wb(n + V1_NODE_CH, '@');
			v1_node_insert(n, 1, 3, 'j');
			inc(AHEAD_COUNT, 1);
		} else if (rw(CF_VOWEL) != 0 && rsb(RC) == 'R') { /* R-coloured vowels */
			if (rr == 0 || stress_of(rr) == 0) {
				int v = rsb(C);
				if (v == 'v' || v == '|' || v == '@' || v == 'i')
					c8 = '3';
				else if (v == 'E')
					c8 = '4';
				else if (v == 'e' || v == 'A')
					c8 = 'k';
				else if (v == 'o')
					c8 = 'r';
				else if (v == 'U')
					c8 = 'Y';
				else if (v == 'O' || v == 'w')
					c8 = 'g';
				else if (v == 'b' || v == 'u')
					c8 = 'c';
				else if (v == 'I' || v == 'f' || v == 'y')
					wb(ruw(RIGHT) + V1_NODE_CH, '3');
				if (c8 != 0) {
					wb(n + V1_NODE_CH, c8);
					if (c8 == 'Y')
						wb(ruw(RIGHT) + V1_NODE_CH, 'c');
					else {
						v1_node_free(ruw(RIGHT), 0);
						inc(AHEAD_COUNT, -1);
						if (c8 == '3' && rsb(LC) == '3') {
							ww(PR_CUR, v1_node_insert(n, 0, 4, 'R'));
							inc(AHEAD_COUNT, 1);
						}
					}
					goto done;
				}
			}
			goto pauses;
		} else if (rsb(C) == '3' && rsb(LC) == '3') {
			ww(PR_CUR, v1_node_insert(n, 0, 4, 'R'));
			inc(AHEAD_COUNT, 1);
		} else
			goto pauses;
		goto done;
	}
	/* consonants */
	if (rsb(C) == 'L' && rw(L_VOWEL) != 0 && ruw(RIGHT) != 0 && stress_of(ruw(RIGHT)) == 0) {
		wb(n + V1_NODE_CH, 'j');
		goto done;
	}
	if (rsb(C) == 'H' && (feat(rsb(LC), 0) & 2)) {
		wb(n + V1_NODE_CH, 'd');
		goto done;
	}
	if (!(feat(rsb(C), 0) & 0x20))
		return;
	if (rsb(LC) == 'S' && rw(CF100_01) != 0 && ruw(RIGHT) != 0 && bit5(r)) { /* unaspirated after S */
		c8 = rsb(C) == 'T' ? 'D' : rsb(C) == 'P' ? 'B' : rsb(C) == 'K' ? 'G' : 0;
		if (c8 != 0) {
			wb(n + V1_NODE_CH, c8);
			goto done;
		}
	}
	if (rsb(C) == 'T' && rsb(RC) == 'C') {
		ww(PR_CUR, v1_node_free(ruw(PR_CUR), 1));
		inc(AHEAD_COUNT, -1);
		v1_pr_context_load();
	}
	c8 = 0;
	if (rsb(C) == 'C' && rsb(RC) != 's')
		c8 = 's';
	else if (rsb(C) == 'J' && rsb(RC) != 'z')
		c8 = 'z';
	if (c8 != 0) {
		v1_node_insert(ruw(PR_CUR), 1, 3, c8);
		inc(AHEAD_COUNT, 1);
		goto done;
	}
	if (r != 0) {
		rr = v1_pr_next_sym(r);
		if ((rsb(C) == 'D' || rsb(C) == 'T') && v1_pr_test(l, 2, 0) && ch_of(l) != '~') {
			int rc = ch_of(r);
			if (rw(PR_RATE) > 0x64 && rw(PR_WORD) == 1 && (feat(rsb(LC), 0x100) & 2) && (feat(rc, 0x100) & 2) &&
			    !bit5(r) && (rr == 0 || ch_of(rr) != 'N') && ((feat(rc, 0x200) & 0x40) || rc == 'E')) {
				wb(n + V1_NODE_CH, 't'); /* flap */
				goto done;
			}
			if (rsb(C) == 'T' && v1_pr_test(r, 0x110, 0) && rr != 0 && ch_of(rr) == 'N') {
				wb(n + V1_NODE_CH, 'q'); /* glottal stop and syllabic N */
				v1_node_free(rr, 0);
				inc(AHEAD_COUNT, -1);
				wb(r + V1_NODE_CH, 'n');
				goto done;
			}
			if (rsb(C) == 'T' && (v1_pr_test(r, 0x140, 0) || v1_pr_test(r, 8, 0))) {
				while (v1_pr_test(rr, 0x80, -1))
					rr = v1_pr_next_sym(rr);
				if (v1_pr_test(rr, 2, 0) && bit5(rr)) {
					wb(n + V1_NODE_CH, 'q');
					goto done;
				}
			}
		}
	}
	if (!v1_pr_test(r, 0x101, 0) || rw(PR_RATE) > 199 || (feat(rsb(C), 0x80) & 8))
		goto pauses;
	v1_node_insert(n, 1, 3, 'p');
	inc(AHEAD_COUNT, 1);
	goto done;

pauses:
	if (!(feat(rsb(C), 0x80) & 0x40))
		return;
	if (rsb(C) == ')' && rw(PR_RATE) > 99)
		return;
	if (rw(PR_RATE) > 199 || rw(FAST) != 0) {
		m = v1_node_insert(n, 0, 4, ' ');
		set_arg0(m, rw(FAST) == 0 ? 10 : 4);
		wb(m + V1_NODE_ARG1, 0x32);
		inc(BEHIND, 1);
		v1_pr_context_load();
		return;
	}
	{
		int d = rsb(C) == '.' || rsb(C) == '?' ? 0x1E : rsb(C) == ',' ? 0x12 : 10;
		pause_segments(n, pct(by_rate(TBL_PAUSE), d));
	}
	v1_pr_context_load();
	return;
done:
	v1_pr_context_load();
}

/* EFFC1: a geminate: the same consonant as the segment before merges into this one */
void v1_pr_merge_geminate(void)
{
	unsigned l = ruw(LEFT), n = ruw(PR_CUR);
	if (rsb(C) == rsb(LC) && rsb(C) != ' ' && !((rw(n + V1_NODE_FLAGS) >> 6) & 1) && (feat(rsb(C), 0x80) & 0x20)) {
		unsigned d = arg0_of(n) + arg0_of(l);
		if (d > 0x36)
			d = 0x37;
		set_arg0(n, d);
		if (rw(STRESSED))
			set_flags(ruw(PR_CUR), 0x20);
		wb(ruw(PR_CUR) + V1_NODE_ARG1, rb(l + V1_NODE_ARG1));
		v1_node_free(l, 0);
		inc(BEHIND, -1);
	}
	v1_pr_context_load();
}

/* F0082: the duration percentages for the cursor (Klatt's rules: clause-final lengthening, non-phrase-final and
 * non-word-final shortening, unstressed shortening, emphasis, the consonants after a vowel) into PCT. A blank becomes a pause of its units scaled by rate (at most 50 frames per
 * segment). Returns 1 when duration_set should finish the duration. */
int v1_pr_duration_rules(void)
{
	unsigned n = ruw(PR_CUR);
	int k = 0, after_stressed = 0, v;
	if (rw(CF_PHONEME) == 0) {
		ww(n + V1_NODE_FLAGS, rw(n + V1_NODE_FLAGS) & 0xFF);
		return 0;
	}
	if (rsb(C) == ' ') {
		int d = pct(by_rate(TBL_PAUSE), arg0_of(n));
		if (d < 4)
			d = 4;
		if (rw(GIVEN_DUR) != 0)
			d = rw(GIVEN_DUR);
		while (d > 0x32 && rw(V1_FREE_COUNT) > 3) {
			unsigned m = v1_node_insert(n, 0, 4, ' ');
			ww(m + V1_NODE_FLAGS, (rw(m + V1_NODE_FLAGS) & 0xFF) | 0x3200);
			wb(m + V1_NODE_ARG1, 0x32);
			d -= 0x32;
			inc(BEHIND, 1);
		}
		if (d > 0x32)
			d = 0x32;
		set_arg0(n, d);
		wb(n + V1_NODE_ARG1, 0x32);
		return 0;
	}
	if (ruw(RIGHT) != 0 && bit5(ruw(RIGHT)))
		after_stressed = 1;
	if (rsb(C) == 'Y' && ruw(NEXT_PH) != 0 && ch_of(ruw(NEXT_PH)) == 'b' && !bit5(ruw(NEXT_PH))) {
		set_arg0(n, 3);
		return 0;
	}
	ww(INH, rsb(ruw(P_INH) + rsb(C)) * 10);
	ww(MIN, rsb(ruw(P_MIN) + rsb(C)) * 10);
	ww(PCT, 100);
	if (rw(INH) == rw(MIN))
		return 1;
	if (rw(FAST) == 0 && v1_pr_scan(1, 6, 0x140, -1, 0)) /* phrase-final */
		ww(PCT + 2 * ++k, feat(rsb(C), 0) & 0x20 ? 0x96 : rw(CF_VOWEL) ? 0x8C : 0x118);
	if (rw(CF_VOWEL) != 0) {
		if (rw(PHRASE_FINAL) == 0)
			ww(PCT + 2 * ++k, 0x3C);
		if (rw(WORD_FINAL) == 0)
			ww(PCT + 2 * ++k, 0x55);
		else if (rw(STRESSED) == 0 && v1_pr_scan(0, 5, 8, -1, 0))
			ww(PCT + 2 * ++k, 0x50);
	}
	if (rw(CF_VOWEL) != 0 && rw(WORD_FINAL) == 0)
		ww(PCT + 2 * ++k, 0x50);
	else if (v1_pr_scan(0, 7, 1, -8, 0))
		ww(PCT + 2 * ++k, rw(CF_VOWEL) ? 0x50 : 0x55);
	if (rw(STRESSED) == 0) { /* unstressed: the minimum halves */
		ww(MIN, rw(MIN) >> 1);
		if (rw(CF_40) == 0) {
			v = 0x4B;
			if (rw(CF_VOWEL) != 0) {
				v = 0x46;
				if (rw(WORD_FINAL) == 0 && v1_pr_scan(0, 5, 1, -8, 0))
					v = 0x32;
			}
			if (rw(CF_10) != 0 && rw(L_VOWEL) != 0 && rw(R_VOWEL) != 0 && !after_stressed)
				v = 0x32;
			ww(PCT + 2 * ++k, v);
		}
	}
	if (stress_of(n) == 3)
		ww(PCT + 2 * ++k, 0x82);
	if ((rsb(C) == 'E' || rsb(C) == 'v') && v1_pr_test(ruw(RIGHT), 8, 0))
		ww(PCT + 2 * ++k, 0x96);
	if (rw(WORD_FINAL) != 0 && rw(CF_02) != 0 &&
	    (rw(CF100_02) != 0 || (v1_pr_test(ruw(LEFT), 0x202, 0) && !bit5(n)))) {
		v = rw(TBL_CLASS + 2 * rw(CONS_CLASS));
		if (rsb(LC) == 'R')
			v -= 0x14;
		if (v1_pr_test(ruw(RIGHT), 0x10, 0) && !after_stressed) {
			v = pct(v, 0x46);
			if (feat(rsb(C), 0x80) & 0x10)
				v = pct(v, 0x28);
		} else if (rw(PHRASE_FINAL) == 0)
			v = (uint16_t)((v - 100) * 3) / 10 + 100;
		ww(PCT + 2 * ++k, v);
	}
	for (v = 100; k >= 0; k--)
		v = pct(rw(PCT + 2 * k), v);
	ww(PCT, v);
	return 1;
}

/* F050A: the cursor's duration from INH, MIN and PCT (with the consonant-context percentage), in frames (≤ 55). A
 * schwa after T or D above rate 100 becomes a flap D first. */
void v1_pr_duration_set(void)
{
	unsigned n = ruw(PR_CUR);
	int k = 0, v, d;
	if (rsb(C) == 'x' && (rsb(LC) == 'T' || rsb(LC) == 'D') && rw(PR_RATE) > 0x64) {
		wb(n + V1_NODE_CH, 'D');
		v1_pr_context_load();
		neighbours_load(ruw(PREV_SEG), ruw(NEXT_PH));
	}
	if (rw(INH) <= rw(MIN)) {
		d = rw(INH);
		goto frames;
	}
	v = 100;
	if (rw(CF_VOWEL) != 0) {
		if (rw(STRESSED) == 0) {
			if (rw(L_VOWEL) != 0)
				v = 0x46;
			else if (rw(R_VOWEL) != 0)
				v = 0x78;
		}
	} else {
		if (!(feat(rsb(C), 0x180) & 2) && rw(L_VOWEL) == 0 && v1_pr_test(ruw(LEFT), 0x108, -1) &&
		    v1_pr_test(ruw(LEFT), 0x302, -1)) {
			v = 0x5A;
			if (rsb(LC) == 'S' && rw(CF100_01) != 0)
				v = 0x3C;
			if (feat(rsb(C), 0x180) & 1)
				v = 0x3C;
			if (rw(STRESSED) == 0) {
				v -= 0x14;
				if (rw(CF100_01) != 0 && v1_pr_test(ruw(LEFT), 0x201, 0) &&
				    rb(ruw(P_PLACE) + ch_of(ruw(LEFT))) == rb(ruw(P_PLACE) + ch_of(n)))
					v = 0x28;
				goto listed;
			}
		}
		if (rw(CF_02) == 0 && rw(CF80_08) == 0 && ruw(RIGHT) != 0 && rw(R_VOWEL) == 0 &&
		    v1_pr_test(ruw(RIGHT), 0x302, -1)) {
			v = 0x46;
			if (rw(CF_40) != 0 && (v1_pr_test(ruw(RIGHT), 0x201, 0) || v1_pr_test(ruw(RIGHT), 0x40, 0)))
				v = 0x1E;
		}
	}
listed:
	ww(PCT + 2 * ++k, v);
	if (rsb(C) == 'x' && rw(STRESSED) == 0 && v1_pr_test(ruw(LEFT), 0x204, 0)) {
		ww(PCT, 0x14);
		k = 0;
	}
	if (rw(CF100_02) != 0 && ((rw(n + V1_NODE_FLAGS) >> 6) & 1))
		ww(PCT + 2 * ++k, 0x78);
	if (rsb(LC) == 'H')
		ww(PCT + 2 * ++k, 0x50);
	for (v = 100; k >= 0; k--)
		v = pct(rw(PCT + 2 * k), v);
	ww(PCT, pct(by_rate(TBL_DUR), v));
	d = pct(rw(INH) - rw(MIN), rw(PCT)) + rw(MIN);
	if (rw(CF_40) != 0 && v1_pr_test(ruw(LEFT), 0x108, 0)) {
		d = pct(d, 0x41);
		if (rsb(RC) == ' ')
			d += 0x28;
	}
	if (rw(CF_02) != 0 && rw(CF100_02) == 0 && rsb(LC) == 'S')
		d += 0xF;
	if (rw(STRESSED) != 0 && rw(CF100_02) != 0 && rw(CF_02) != 0 && !(feat(rsb(C), 0) & 0x10) &&
	    v1_pr_test(ruw(LEFT), 0x20, 0) && v1_pr_test(ruw(LEFT), 4, -1))
		d += 0x19;
frames:
	d = s16(d + 9) / 10;
	if (d > 0x36)
		d = 0x37;
	set_arg0(n, d & 0xFF);
}

/* F08F6: for a vowel, whether it is in the last syllable of its phrase or word (PHRASE_FINAL, WORD_FINAL) and the
 * class of the consonants after it (CONS_CLASS: 1-4). A 'q' before the cursor marks it (flags bit 6). */
void v1_pr_vowel_context(void)
{
	if (ruw(PREV_SEG) != 0 && ch_of(ruw(PREV_SEG)) == 'q')
		set_flags(ruw(PR_CUR), 0x40);
	if (rw(CF_VOWEL) == 0)
		return;
	ww(WORD_FINAL, 0);
	ww(PHRASE_FINAL, 0);
	if (v1_pr_scan(1, 9, 0x140, -1, 0)) {
		ww(WORD_FINAL, 1);
		ww(PHRASE_FINAL, 1);
	} else if (v1_pr_scan(1, 10, 8, -1, 0))
		ww(WORD_FINAL, 1);
	ww(CONS_CLASS, 4);
	unsigned r = ruw(RIGHT);
	if (v1_pr_test(r, 2, 0)) {
		r = v1_pr_next_sym(r);
		if (v1_pr_test(r, 2, -1) && v1_pr_test(r, 0x80, 0))
			r = ruw(RIGHT);
	}
	if (v1_pr_test(r, 4, 0)) {
		if (v1_pr_test(r, 0x40, 0) || v1_pr_test(r, 0x108, 0))
			ww(CONS_CLASS, 3);
	} else if (v1_pr_test(r, 0x40, 0))
		ww(CONS_CLASS, 1);
	else if (v1_pr_test(r, 0x20, 0))
		ww(CONS_CLASS, 2);
}

/* F0A45: the cursor's F0: the pitch setting plus a declining offset and a hat pattern (RISE from just before the
 * first stressed syllable to the last one of the phrase, an ACCENT on each stressed vowel), small segmental effects
 * and a fall or rise at the phrase end; 50-200 Hz, stored halved at +6. Monotone with mode flag 12 (0x800); 0 with
 * pitch 0. */
void v1_pr_f0_set(void)
{
	unsigned n = ruw(PR_CUR);
	int f, k;
	if (rw(L_VOWEL) != 0)
		ww(DECLINE, pct(rw(DECLINE), 0x5F));
	f = rw(CF_PHONEME) == 0 ? 0 : rw(PR_PITCH) + rw(DECLINE);
	if (feat(rsb(C), 0x80) & 1) {
		ww(ACCENT, 10);
		ww(ACCENTS, 0);
	}
	if (rw(EMPHASIS) != 0) {
		if (v1_pr_scan(1, 0x5A, 1, -0x101, 1)) {
			ww(EMPHASIS, 0);
			ww(ACCENT, rw(ACCENT) >> 1);
		}
		if (v1_pr_scan(0, 0x5A, 1, -0x101, 1)) {
			ww(EMPHASIS, 0);
			ww(ACCENT, rw(ACCENT) >> 1);
		}
	}
	if (v1_pr_scan(1, 3, 2, -0x80, 1)) {
		ww(ACCENT_ON, 1);
		inc(ACCENTS, 1);
		if (stress_of(n) == 3)
			inc(ACCENTS, 1);
	}
	if (stress_of(n) == 2 || stress_of(n) == 3) {
		if (v1_pr_scan(1, 0x14, 0x140, -2, 2) && (rw(ACCENTS) > 1 || v1_pr_scan(1, 0x14, 0x408, -2, 2)))
			ww(ACCENT_ON, 0);
		if (rw(ACCENT_ON) == 0) {
			unsigned p = ruw(PREV_SEG);
			if (p != 0 && !(rw(PR_MODE) & 0x800) && rw(PR_PITCH) != 0) {
				if (rw(ACCENTS) < 2)
					wb(p + V1_NODE_ARG1, rb(p + V1_NODE_ARG1) - (rw(ACCENT) >> 1));
				else
					wb(p + V1_NODE_ARG1, rb(p + V1_NODE_ARG1) + (rw(ACCENT) >> 1));
			}
		} else
			f += rw(ACCENT);
		ww(ACCENT, 6);
	}
	if (rw(ACCENT_ON) != 0 && rw(CF_PHONEME) != 0)
		f += rw(RISE);
	if (v1_pr_scan(1, 3, 1, -0x80, 1))
		f += 0xF;
	if (feat(rsb(C), 0) & 4)
		f -= rw(CF_02) == 0 ? 10 : 5;
	if (feat(rsb(C), 0x200) & 1)
		f += 3;
	if ((feat(rsb(C), 0) & 4) && v1_pr_scan(1, 8, 0x101, -1, 0)) {
		unsigned m = n;
		while (!v1_pr_test(m, 0x101, 0))
			m = v1_node_next(m);
		f = ch_of(m) == '.' ? f - 0xF : rw(CF_VOWEL) == 0 ? f + 0x14 : f + 0xF;
	}
	f = s16(f);
	if (rw(PR_PITCH) == 0)
		f = 0;
	else if (f > 200)
		f = 200;
	else if (f < 0x32)
		f = 0x32;
	if (rw(PR_MODE) & 0x800)
		f = rw(PR_PITCH);
	k = f >> 1;
	wb(n + V1_NODE_ARG1, k);
}

/* F0E6A: time the phoneme at the cursor */
void v1_pr_time_segment(void)
{
	unsigned r = ruw(RIGHT), l;
	ww(NEXT_PH, 0);
	ww(PREV_SEG, 0);
	for (l = ruw(LEFT); l != 0; l = v1_pr_prev_ph(l))
		if (kind_of(l) == 4) {
			ww(PREV_SEG, l);
			break;
		}
	for (; r != 0; r = v1_pr_next_sym(r))
		if (kind_of(r) == 3 && (feat(ch_of(r), 0) & 0x80)) {
			ww(NEXT_PH, r);
			break;
		}
	v1_pr_vowel_context();
	v1_pr_f0_set();
	if (v1_pr_duration_rules()) {
		neighbours_load(ruw(PREV_SEG), ruw(NEXT_PH));
		v1_pr_duration_set();
	}
	if (feat(rsb(C), 0) & 0x80)
		v1_pr_merge_geminate();
}

/* EEB9C */
int v1_stage_prosody_run(void)
{
	count32(0xA4C6);
	if (rw(NEW_SENTENCE)) {
		ww(NEW_SENTENCE, 0);
		v1_pr_sentence_reset();
	}
	if (v1_stage_begin(R) || rw(AHEAD_COUNT) >= rw(AHEAD_MAX) || rw(SCAN_BREAK) != 0) {
		v1_pr_skip_commands();
		ww(FAST, rw(PR_MODE) & 0x2000);
		do {
			if (rw(PHRASE_END) == 0 && rw(AHEAD_COUNT) < rw(AHEAD_MAX) && !v1_pr_phrase_walk())
				break;
			if (kind_of(ruw(PR_CUR)) == 3) {
				count32(0xA4FE);
				v1_pr_given_values(0);
				v1_pr_context_load();
				v1_pr_allophones();
				v1_pr_time_segment();
				if (rw(CF_PHONEME) != 0) {
					unsigned n = ruw(PR_CUR);
					ww(n + V1_NODE_FLAGS, (rw(n + V1_NODE_FLAGS) & 0xFFF8) | 4);
					ww(n + V1_NODE_FLAGS, rw(n + V1_NODE_FLAGS) & 0xFFE7);
					v1_pr_given_values(1);
				}
			}
		} while (!v1_pr_cursor_advance());
	}
	return v1_stage_commit();
}

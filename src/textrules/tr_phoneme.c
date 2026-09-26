/* D4DB0: phoneme input (ESC[2I): the two-letter phoneme names and marks become the one-letter alphabet (REFERENCE
 * §12.3a) and the stress and timing marks kept in TR_ATTR. */
#include "textrules.h"

/* Per letter: its phoneme alone, and pairs (second letter, phoneme) for the two-letter names. The second letter
 * matches in either case and is deleted. A phoneme 0 clears the node. */
static const struct {
	char letter, alone;
	const char *pairs;
} names[] = {
    {'A', 'o', "AoHoEaOwRrWfX@YI"},
    {'C', 'C', "HC"},
    {'D', 'D', "HxTt"},
    {'E', 'e', "HeRkYA"},
    {'H', 'H', "HdWhXH"},
    {'I', 'i', "HiR4X|YE"},
    {'J', 'J', "HJ"},
    {'L', 'L', "Xj"},
    {'N', 'N', "G~X~"},
    {'O', 'o', "RgWOYy"},
    {'Q', 'Q', "Qq"},
    {'R', 'R', "R3"},
    {'S', 'S', "Hs"},
    {'T', 'T', "HX"},
    {'U', 'u', "HuLlMmNnRcWbXv"},
    {'X', 0, "Xp"},
    {'Z', 'Z', "Hz"},
};

static void set_char(int ch)
{
	wb(rw(TR_CUR) + N_CHAR, ch);
}

static void delete_next(void)
{
	ww(TR_CUR, node_delete(rw(rw(TR_CUR)), 0));
}

/* ESC[0I at n: phoneme input ends there */
static int input_ends(int n)
{
	return node_kind(n) == NODE_COMMAND && node_char(n) == 'I' && node_dur(n) == 0;
}

/* the same, and the node before the current one is not a symbol */
static int input_ends_after_text(void)
{
	int cur = rw(TR_CUR);
	return input_ends(rw(cur)) && node_kind(rw(cur + 2)) != NODE_SYMBOL;
}

/* a mark waits in TR_ATTR for the next phoneme; the mark character goes */
static void mark_pending(void)
{
	int a = ruw(TR_ATTR_PTR);
	ww(a + N_FLAGS, rw(a + N_FLAGS) | 0x20);
	set_char(0);
}

static void mark(int code)
{
	int a = ruw(TR_ATTR_PTR);
	ww(a + N_DUR, (rw(a + N_DUR) & 0xFF00) | code);
	mark_pending();
}

static void letter(int up)
{
	int nx = node_char(rw(rw(TR_CUR)));
	for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++) {
		if (names[i].letter != up)
			continue;
		for (const char *p = names[i].pairs; *p; p += 2)
			if (nx == p[0] || nx == p[0] + 0x20) {
				delete_next();
				set_char(p[1]);
				return;
			}
		set_char(names[i].alone);
		return;
	}
	set_char(up);
}

void phoneme_spelling(void)
{
	int cur = rw(TR_CUR), ch = node_char(cur), nx = rw(cur), a = ruw(TR_ATTR_PTR), v;
	if (ch >= 'A' && ch <= 'Z') {
		letter(ch);
		return;
	}
	if (ch >= 'a' && ch <= 'z') {
		letter(ch - 0x20);
		return;
	}
	switch (ch) {
	case ' ':
	case '(':
	case ')':
	case ',':
	case '.':
	case ';':
	case '?':
		break;
	case '!':
	case '"':
	case '~':
		if (input_ends_after_text())
			mark(9);
		else
			set_char('"');
		break;
	case '#':
		if (input_ends_after_text()) {
			mark(0xB);
		} else {
			set_char(' ');
			ww(cur + N_DUR, (rw(cur + N_DUR) & 0xFF00) | 8);
		}
		break;
	case '$':
		if (input_ends(nx))
			mark(0xD);
		break;
	case '%':
	case '&': /* the waiting marks go to this node */
		if ((ruw(a + N_FLAGS) >> 5) & 1) {
			ww(cur + N_DUR, (rw(cur + N_DUR) & 0xFF00) | (rw(a + N_DUR) & 0xFF));
			wb(cur + N_F0, rb(a + N_F0));
			ww(a + N_FLAGS, rw(a + N_FLAGS) & ~0x20);
		}
		break;
	case '\'':
		if (input_ends_after_text()) {
			ww(a + N_DUR, (rw(a + N_DUR) & 0xFF00) | 3);
			ww(a + N_FLAGS, rw(a + N_FLAGS) | 0x20);
		} else {
			set_char('1');
		}
		break;
	case '*':
		if (input_ends(nx))
			mark(4);
		else
			set_char(0);
		break;
	case '+':
		set_char('[');
		break;
	case '-':
		if (input_ends(nx))
			mark(5);
		else
			set_char(0);
		break;
	case '/':
		if (input_ends(nx)) {
			ww(a + N_DUR, (rw(a + N_DUR) & 0xFF00) | 7);
			ww(a + N_FLAGS, rw(a + N_FLAGS) | 0x20);
			set_char(0);
		} else if (node_char(nx) == '\\' && input_ends(rw(nx))) {
			ww(a + N_DUR, (rw(a + N_DUR) & 0xFF00) | 8);
			ww(a + N_FLAGS, rw(a + N_FLAGS) | 0x20);
			delete_next();
		}
		set_char(0);
		break;
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9': /* a number (up to 63) before ESC[0I goes to TR_ATTR +8 */
		v = node_char(nx);
		if (v >= '0' && v <= '9') {
			if (input_ends(rw(nx))) {
				wb(a + N_F0, (ch - '0') * 10 + v - '0');
				if (rsb(a + N_F0) > 0x3F)
					wb(a + N_F0, 0x3F);
				ww(a + N_FLAGS, rw(a + N_FLAGS) | 0x20);
			}
			delete_next();
			set_char(0);
		} else if (v == 'I' && node_kind(nx) == NODE_COMMAND && node_dur(nx) == 0) {
			wb(a + N_F0, ch - '0');
			mark_pending();
		} else {
			set_char(0);
		}
		break;
	case ':':
		if (input_ends(nx))
			mark(0xA);
		else
			set_char(0);
		break;
	case '<':
		if (input_ends(nx))
			ww(OUTPUT_HOLD, 1);
		set_char(0);
		break;
	case '=':
		if (input_ends(nx))
			mark(1);
		else
			set_char(0);
		break;
	case '>': /* ends phoneme input here: the ESC[0I becomes a C command */
		if (input_ends(nx)) {
			int prev = rw(cur + 2);
			wb(prev + N_CHAR, 0);
			ww(prev + N_FLAGS, rw(prev + N_FLAGS) & 0xFFF8);
			wb(cur + N_CHAR, 0);
			ww(cur + N_FLAGS, rw(cur + N_FLAGS) & 0xFFF8);
			wb(rw(cur) + N_CHAR, 'C');
			ww(TR_INPUT, 0);
			ww(OUTPUT_HOLD, 0);
		} else {
			set_char(0);
		}
		break;
	case '\\':
		if (input_ends(nx))
			mark(6);
		else
			set_char(0);
		break;
	case '_':
		if (input_ends(nx))
			mark(0x11);
		else
			set_char(0);
		break;
	case '`':
		if (input_ends_after_text())
			mark(2);
		else
			set_char('2');
		break;
	case '{':
		if (input_ends(nx))
			mark(0xC);
		else
			set_char(0);
		break;
	case '|':
		set_char('\\');
		break;
	case '}':
		set_char(']');
		break;
	default: /* '@' '[' ']' '^' and anything else */
		set_char(0);
		break;
	}
}

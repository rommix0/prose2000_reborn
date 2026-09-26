/* E3DBF: the allophone rules, run on each phoneme of a finished word once its right neighbour is known. */
#include "lexical.h"

static void set_bit(int n, int bit)
{
	ww(n + N_FLAGS, rw(n + N_FLAGS) | 1 << bit);
}

static int is_boundary(int ch)
{
	return ch == '&' || ch == '%';
}

/* Consonants (phoneme codes of REFERENCE §12.3a): T and D become the flap 't' or the glottalized 'q' between vowels,
 * or are dropped in clusters; T P K after S are written unaspirated (D B G); T D before R become C J; the
 * affricates C J get their release s z; H is dropped, or voiced ('d') after a vowel; wh ('h') becomes W; L after a
 * vowel is dark ('j'); N before a velar across a boundary becomes '~'; Z devoices to S; V of "of" devoices before
 * a voiceless sound; a release vocoid 'p' follows some final consonants. Vowels: a glottal attack (node bit 6) on
 * a word-initial vowel after a pause, syllabic 'l' and 'n' from a reduced vowel + L / N, and r-coloured vowels
 * ('3' '4' 'k' 'r' 'g' 'c') from vowel + R. Node bit 7 on a word boundary marks a flap or glottal stop across it.
 * Returns the node the caller continues from (a node inserted after n, n itself, or the node before n if n was
 * deleted).
 * Settings used: speed (LX_SPEED; most rules need a minimum speed), LX_PITCH as an on/off switch for the glottal
 * stops, word mode (LX_PROSODY 0) against cross-word flapping. */
int allophone_rules(int n)
{
	int c = node_char(n), nx, pv, nn = 0, nnn = 0, w, npn = 0, res = 0, flap = 0, glot = 0, cls;
	int nc = 0, pc = 0, nnc = 0, pp = 0, np = 0;
	int d = 0; /* uninitialized in the firmware on some paths; only compared with '3' */
	int speed = rw(LX_SPEED);

	nx = next_symbol(n);
	pv = prev_symbol(n);
	if (nx != 0)
		nc = node_char(nx);
	if (pv != 0)
		pc = node_char(pv);
	nn = next_symbol(nx);
	if (nn != 0)
		nnc = node_char(nn);
	/* the nearest phonemes before and after, within the word */
	for (w = pv; w != 0 && !(feature(node_char(w), 0x80) & 0x40); w = prev_symbol(w))
		if (feature(node_char(w), 0) & 0x80) {
			pp = node_char(w);
			break;
		}
	if (pp == 0)
		pp = rw(PREV_CHAR);
	else
		ww(PREV_CHAR, 0);
	for (w = nx; w != 0 && !(feature(node_char(w), 0x80) & 0x40); w = next_symbol(w))
		if (node_kind(w) == NODE_SYMBOL && (feature(node_char(w), 0) & 0x80)) {
			np = node_char(w);
			npn = w;
			break;
		}

	if (!(feature(c, 0x80) & 0x20))
		goto vowel;

	/* ---- consonants ---- */
	if (pp == 'q' && rw(LX_PITCH) != 0) {
		set_bit(n, 6);
		glot = 1;
	}
	if (c == 'Q') {
		if (feature(node_char(nx), 0) & 0x80) {
			if ((feature(node_char(nx), 0x100) & 2) && rw(LX_PITCH) != 0)
				set_bit(npn, 6);
			goto delete_n;
		}
		wb(n + N_CHAR, ' ');
		goto done;
	}
	if (c == 'Z' && !(feature(np, 0) & 4) && nc != '%' && nc != '&' && !(feature(nc, 0x80) & 0x40)) {
		wb(n + N_CHAR, 'S');
		goto done;
	}
	if (c == 'V' && (feature(nc, 0) & 8) && (feature(np, 0) & 0x40) && !(feature(np, 0) & 4)) {
		cls = rsb(rw(SPAN_REC) + 8);
		if (cls == 2 || cls == 3) {
			wb(n + N_CHAR, 'F');
			goto flapped;
		}
	}
	if (c == 'L' && nc == '[' && np == 'L')
		node_delete(npn, 0);
	if (c == 'L' && (feature(pc, 0) & 1) && nx != 0 && node_stress(nx) == 0) {
		wb(n + N_CHAR, 'j');
		goto done;
	}
	if ((feature(c, 0x180) & 1) && is_boundary(pc) && node_bit(n, 5) && rw(LX_PITCH) != 0 && pp == 0 && speed < 19)
		set_bit(n, 6);
	if (c == 'N' && nc == '[' && (feature(np, 0x100) & 0x80) && speed > 9) {
		wb(n + N_CHAR, '~');
		goto done;
	}
	if (nx != 0)
		nnn = node_next(nx);
	if (c == 'H') {
		if (speed > 13 && (feature(node_char(pv), 0) & 8) && !node_bit(n, 5) && (feature(pp, 0x80) & 0x20))
			goto delete_n;
		if (pv != 0 && !(feature(nc, 0x100) & 2) && (nc != 'Y' || nnn == 0 || node_char(nnn) != 'b'))
			goto delete_n;
	}
	if (speed > 6 && (feature(pp, 0) & 4)) {
		if (c == 'H') {
			wb(n + N_CHAR, 'd');
			glot = 1;
			goto done;
		}
		if (c == 'h') {
			wb(n + N_CHAR, 'W');
			glot = 1;
			goto done;
		}
	}
	if ((feature(nc, 0x80) & 1) && speed < 19 && (c == '~' || c == 'M' || c == 'N'))
		goto insert_p;
	if (!(feature(c, 0) & 0x20))
		goto done;
	if ((pc == 'S' || (pc == '[' && pp == 'S')) && (c == 'T' || c == 'P' || c == 'K') && nx != 0 &&
	    node_bit(nx, 5)) {
		d = c == 'T' ? 'D' : c == 'P' ? 'B' : 'G';
		wb(n + N_CHAR, d);
		goto done;
	}
	if ((c == 'D' || c == 'T') && nc == 'R' && pc != 'S') {
		c = c == 'D' ? 'J' : 'C';
		wb(n + N_CHAR, c);
	}
	d = 0;
	if (c == 'C' && nc != 's')
		d = 's';
	else if (c == 'J' && nc != 'z')
		d = 'z';
	if (d != 0) {
		res = node_insert(n, 1, NODE_SYMBOL, d);
		ww(res + N_FLAGS, (rw(res + N_FLAGS) & 0xFFDF) | (node_bit(n, 5) << 5));
		goto done;
	}
	if (nx == 0 || (c != 'D' && c != 'T') || pc == '~')
		goto schwa;
	if ((c == 'T' && nc == 'C') || (c == 'D' && nc == 'J'))
		goto delete_flapped;
	if (speed > 13 && (feature(np, 0x80) & 0x20) && !(feature(pc, 0) & 8) && np != 'R' && np != 'W') {
		if (c == 'T' && (pp == 'F' || pp == 'S' || pp == 'P' || pp == 'K'))
			goto delete_flapped;
		if (c == 'D' && pp == 'N')
			goto delete_flapped;
		if (speed >= 19 && (pp == 's' || pp == 'z' || pp == 'V'))
			goto delete_flapped;
	}
	if (speed > 9 && nc == '[' && c == 'T' && (pp == 'N' || pp == 'n') && np == 'S')
		goto delete_flapped;
	if (feature(pc, 0) & 2) {
		if (node_bit(pv, 5) && (feature(nc, 0x80) & 0x10) && nnc == 'N') {
			if (c == 'T') {
				wb(n + N_CHAR, 'q');
				goto done;
			}
			goto to_t;
		}
		if (c == 'T') {
			if ((feature(nc, 0) & 8) && (feature(np, 0) & 2) && !(feature(np, 0) & 1) && np != 'H')
				goto to_q;
			if (np == 'x' || np == 'D' || np == 'L' || np == 'N')
				goto to_q;
			if (np == 'Y') {
				if (!(feature(nnc, 0x100) & 2) || node_bit(nn, 5) != 1)
					goto to_q;
			}
			if (np == 'M')
				goto to_q;
		}
	}
	if (speed > 6 && rw(LX_PROSODY) != 0) {
		d = (int8_t)np;
		if ((feature(pc, 0x100) & 2) && node_char(rw(pv + 2)) != 't' && (feature(d, 0x100) & 2) &&
		    !node_bit(nx, 5) && ((feature(d, 0x200) & 0x40) || d == 'E' || d == 'O')) {
			if (nnc != 'N' || (feature(node_char(rw(nn)), 0) & 0x80))
				goto to_t;
		}
		if (c == 'T' && (feature(pc, 0) & 2) && is_boundary(nc) && ((feature(np, 0x100) & 2) || np == 'H')) {
			cls = rsb(rw(SPAN_REC) + 8);
			if ((cls == 0 || cls == 6 || cls == 11) && (feature(pp, 0) & 0x10)) {
				if (!(np == 'H' && !node_bit(rw(pv + 2), 5)) && cls != 11 && cls != 2)
					goto to_q;
			}
			wb(n + N_CHAR, 't');
			goto flapped;
		}
	}
	if (c == 'D' && !(feature(pc, 0) & 8) && (feature(pp, 0x100) & 2) && (feature(np, 0x100) & 2) &&
	    !node_bit(nx, 5))
		goto to_t;
schwa:
	if (c == 'x' && (pp == 'T' || pp == 'D') && speed > 19)
		wb(n + N_CHAR, 'D');
	if (!(feature(nc, 0x80) & 1) || speed >= 19 || (feature(c, 0x80) & 8))
		goto done;
insert_p:
	res = node_insert(n, 1, NODE_SYMBOL, 'p');
	goto done;
to_q:
	wb(n + N_CHAR, 'q');
	goto flapped;
to_t:
	wb(n + N_CHAR, 't');
	goto done;
delete_flapped:
	res = node_delete(n, 0);
flapped:
	flap = 1;
	goto done;
delete_n:
	res = node_delete(n, 0);
	goto done;

	/* ---- vowels ---- */
vowel:
	if ((feature(c, 0x100) & 2) && c != 'U' && is_boundary(pc) && rw(LX_PITCH) != 0) {
		/* a glottal attack on a word-initial vowel */
		int s, set = 0;
		if (pp == 0 || pp == c)
			set = 1;
		else if ((s = node_stress(n)) != 0) {
			d = s;
			if ((feature(pp, 0x100) & 2) && speed < 13)
				set = 1;
			else if (s == 2 && speed < 13 && (feature(pp, 0) & 1))
				set = 1;
			else if (speed <= 6 && ((feature(pp, 0) & 2) || (feature(pp, 0) & 0x40)))
				set = 1;
		}
		if (set)
			set_bit(n, 6);
	}
	if (pp == '~' && node_bit(n, 5) && rw(LX_PITCH) != 0 && (c == 'A' || c == 'E' || c == 'b')) {
		res = node_insert(n, 0, NODE_SYMBOL, ' ');
		ww(res + 6, (rw(res + 6) & 0xFF00) | 6);
		set_bit(n, 6);
		goto done;
	}
	if ((c == '@' || c == '|') && nc == 'L') {
		if (nn == 0 || (feature(nnc, 0x200) & 0x40) || (feature(nnc, 0) & 8) || (feature(nnc, 0x80) & 1) ||
		    (nnc == 'Z' && ((feature(node_char(rw(nn)), 0) & 8) || (feature(node_char(rw(nn)), 0x80) & 1)))) {
			wb(n + N_CHAR, 'l');
			goto delete_next;
		}
	}
	if ((((feature(pc, 0x80) & 0x20) && !(feature(pc, 0) & 2)) || (feature(pc, 0x80) & 1)) &&
	    (feature(c, 0x80) & 0x10) && nc == 'N') {
		if (((feature(nnc, 0x80) & 0x20) && !(feature(nnc, 0) & 2)) || (feature(nnc, 0x80) & 1) ||
		    ((feature(nnc, 0) & 8) && !(feature(node_char(rw(nn)), 0x100) & 2))) {
			wb(n + N_CHAR, 'n');
			goto delete_next;
		}
	}
	if (c == 'E' && nc == '@' && pc == 'j') {
		c = 'Y';
		wb(n + N_CHAR, c);
	}
	if ((feature(c, 0) & 1) && nc == 'R') {
		d = 0;
		if (nn != 0 && node_stress(nn) != 0)
			goto r_done;
		switch (c) {
		case 'v':
		case '|':
		case '@':
		case 'i':
			d = '3';
			break;
		case 'E':
			d = '4';
			break;
		case 'a':
		case 'e':
		case 'A':
			d = 'k';
			break;
		case 'o':
			d = 'r';
			break;
		case 'O':
		case 'w':
			d = 'g';
			break;
		case 'b':
		case 'u':
			d = 'c';
			break;
		case 'I':
		case 'f':
		case 'y':
			wb(nx + N_CHAR, '3');
			break;
		}
		if (d != 0) {
			wb(n + N_CHAR, d);
			if (!(feature(node_char(nn), 0x100) & 2)) {
				if (node_char(nn) == '[' && (feature(node_char(rw(nn)), 0x100) & 2))
					goto done;
				res = node_delete(nx, 0);
			}
		}
	}
r_done:
	if ((c == '3' && pp == '3' && pc != '&') || (pp == '3' && (int8_t)d == '3'))
		node_insert(n, 0, NODE_SYMBOL, 'R');
	goto done;
delete_next:
	res = node_delete(nx, 0);

done:
	if (res == 0)
		res = n;
	if (flap == 1 && is_boundary(nc))
		set_bit(nx, 7);
	else if (glot == 1 && is_boundary(pc))
		set_bit(pv, 7);
	return res;
}

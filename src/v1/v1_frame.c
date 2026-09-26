/* v1.1 frame builder and frame interrupt: each 10 ms the DSP asks for a frame (IR0, dsp_irq_service EC1E0), and
 * dsp_frame_tick (EDA11) sends the one built at the previous interrupt, then dsp_build_frame (EE564) builds the next
 * from the 18 parameter tracks. The frame is 37 words at DS:95C8; v1.1's DSP program was never dumped, so the words'
 * meaning comes from the arithmetic, which is v3.4.1's (REFERENCE §11.3, §16) without the voice tables, jitter and
 * shimmer.
 *
 * Tables (DS offsets): 1A51 cos(2*pi*F*T) by frequency, 186D / 1945 r = exp(-pi*B*T) / r^2 by bandwidth, 1E4F
 * dB -> linear, 1A17 nasal-zero gain by FN, 1FF9 and 20EB the parallel-amplitude corrections for formant spacing. The parameters as read are kept at DS:9612 + 2i (p1-p16; AV and F0 are used directly) and the working
 * values at 9636-9650, as the firmware leaves them. */
#include "v1.h"

#define FRAME 0x95C8       /* 37 words */
#define FRAME_READY 0x95C4 /* a built frame waits for the DSP */
#define BUILDING 0x95C6    /* a build is running */
#define MISSES 0x959A      /* frame requests in a row with no frame ready */
#define STOP_AT 0x96E0     /* ring position where the frame builder must stop, or -1 */
#define STOPPED 0x96DC     /* the builder has reached STOP_AT: no more frames */
#define PLAYED 0x96F6      /* segment boundaries played (read by the playback stage) */
#define MARKS 0x96E6       /* 128-bit set: a segment boundary before this frame */
#define ALT 0xA20E         /* 128-bit set: F1-F3 in 10 Hz steps (an index in a t hold) */
#define BIT 0x48A6         /* word table: 1 << n */
#define P(i) (0x9612 + 2 * (i))
#define W(n) (FRAME + 2 * (n))

enum { AF = 1, AH, A2, A3, A4, A5, A6, AB, F1, F2, F3, F4, B1, B2, B3, FN };

#define T_R 0x186D  /* r = exp(-pi*B*T) */
#define T_R2 0x1945 /* r^2 */
#define T_NZ_GAIN 0x1A17
#define T_COS 0x1A51
#define T_DB 0x1E4F
#define T_SPACING1 0x1FF9
#define T_SPACING2 0x20EB

void (*v1_frame_hook)(const uint16_t *frame, int words);
unsigned v1_dsp_status = 0x80; /* the µPD7720 status: RQM; USF0 (0x20) = wants a frame */

/* FB26D: the product >> 11 */
static int mul11(int a, int b) { return (int16_t)(((int32_t)(int16_t)a * (int16_t)b) >> 11); }

/* FB283: the product >> 12; the product >> 13 goes to DS:9638 */
static int mul12(int a, int b)
{
	int32_t p = (int32_t)(int16_t)a * (int16_t)b;
	ww(0x9638, (uint16_t)(p >> 13));
	return (int16_t)(p >> 12);
}

static int tab(unsigned table, int i) { return rw(table + 2 * i); }

/* EE49E: the frame buffer's owner. 3: may a build start (no frame waiting, no build running)? 4: frame built.
 * 5: is a frame ready? 6: frame sent. */
int v1_frame_handshake(int op)
{
	switch (op) {
	case 3:
		count32(0xA516);
		if (rw(FRAME_READY))
			return 0;
		v1_irq_disable_nested();
		if (rw(BUILDING)) {
			v1_irq_enable_nested();
			return 0;
		}
		ww(BUILDING, 1);
		v1_irq_enable_nested();
		return 1;
	case 4:
		count32(0xA51A);
		ww(FRAME_READY, 1);
		ww(BUILDING, 0);
		return 1;
	case 5:
		count32(0xA51E);
		return rw(FRAME_READY);
	case 6:
		count32(0xA522);
		ww(FRAME_READY, 0);
		return 1;
	default:
		v1_fatal_error(0x22);
		return 0;
	}
}

/* take this frame's bit out of a 128-bit set; returns it */
static int take_bit(unsigned set, unsigned pos)
{
	unsigned mask = ruw(BIT + 2 * (pos & 7));
	int bit = (int8_t)rb(set + (pos >> 3)) & mask;
	wb(set + (pos >> 3), rb(set + (pos >> 3)) & ~mask);
	return bit;
}

/* one cascade resonator: r cos into fw, 4 r^2 into bw; returns the gain term 0x2000 - hi + r^2 */
static int resonator(int cos, int b, unsigned fw, unsigned bw)
{
	int r = tab(T_R, b >> 1), r2 = tab(T_R2, b >> 1);
	ww(fw, mul12(r, cos));
	ww(bw, r2 << 2);
	return (int16_t)(0x2000 - rw(0x9638) + r2);
}

/* a formant's cosine: F at its usual table index i, or in 10 Hz steps in an alt frame */
static int formant_cos(int f, int alt, int i) { return tab(T_COS, alt ? (f * 10) >> 3 : i); }

/* the dB index of a parallel formant amplitude, floored at 0, as a linear value */
static int par_db(int v) { return tab(T_DB, v < 0 ? 0 : v); }

/* EE564: the next frame from the tracks, if the generator is far enough ahead and the buffer is free */
void v1_frame_build(void)
{
	if (!v1_pg_ring(3, 0) || !v1_frame_handshake(3))
		return;
	unsigned pos = rw(V1_TRACK_READ) & 0x7F;
	if (take_bit(MARKS, pos))
		ww(PLAYED, rw(PLAYED) + 1);
	int alt = take_bit(ALT, pos);
	for (int i = 1; i <= 16; i++)
		ww(P(i), rb(ruw(V1_TRACKS + 2 * i) + pos));
	ww(W(2), 0x1800 | rb(ruw(V1_TRACKS + 34) + pos) << 1);
	ww(W(36), -tab(T_DB, rb(ruw(V1_TRACKS) + pos) + 0x8C));
	ww(W(21), tab(T_DB, rw(P(AH)) + 0x69));

	/* F4: fixed bandwidth */
	ww(W(11), mul12(tab(T_COS, rw(P(F4)) * 2), 0x1E11));
	ww(0x963E, (int16_t)(0x3C40 - rw(0x9638)));
	ww(W(28), rw(0x963E) + (rw(0x963E) >> 3));
	/* F3, F2, F1 */
	ww(0x9636, formant_cos(rw(P(F3)), alt, rw(P(F3)) * 2));
	ww(0x963C, resonator(rw(0x9636), rw(P(B3)), W(13), W(14)));
	ww(W(30), rw(0x963C));
	ww(0x9636, formant_cos(rw(P(F2)), alt, rw(P(F2)) + 0x3F));
	ww(0x963A, resonator(rw(0x9636), rw(P(B2)), W(15), W(16)));
	ww(W(32), rw(0x963A));
	ww(0x9636, formant_cos(rw(P(F1)), alt, rw(P(F1)) >> 1));
	ww(W(24), resonator(rw(0x9636), rw(P(B1)), W(19), W(20)) << 3);
	/* nasal zero at 2*FN Hz */
	ww(W(3), mul12(tab(T_COS, rw(P(FN)) >> 2), (int16_t)0xE105));
	ww(W(34), tab(T_NZ_GAIN, rw(P(FN)) >> 3) << 1);

	if (rw(P(AF)) <= 0) {
		ww(W(23), 0);
		ww(W(25), 0);
		ww(W(27), 0);
		ww(W(29), 0);
		ww(W(31), 0);
		ww(W(33), 0);
	} else {
		/* formant positions in 32 Hz (F1), 16 Hz steps; the spacing corrections */
		ww(0x9640, alt ? (rw(P(F1)) * 10) >> 5 : rw(P(F1)) >> 3);
		ww(0x9642, alt ? (rw(P(F2)) * 10) >> 5 : (rw(P(F2)) >> 2) + 16);
		ww(0x9644, tab(T_SPACING1, rw(0x9640)) << 1);
		ww(0x964E, rw(0x9644) - tab(T_SPACING1, rw(0x9642)) - 0x2C);
		ww(0x9650, (tab(T_SPACING1, rw(0x9642)) << 1) + rw(0x9644) - 0xEB);
		ww(0x9644, alt ? (rw(P(F3)) * 10) >> 5 : rw(P(F3)) >> 1);
		ww(0x9646, rw(P(F4)) >> 1);
		ww(0x9646, rw(0x9646) - rw(0x9644));
		ww(0x9644, rw(0x9644) - rw(0x9642));
		ww(0x9642, rw(0x9642) - rw(0x9640));
		if (rw(0x9642) <= 2)
			ww(0x9642, 0);
		ww(0x9644, rw(0x9644) - 2);
		if (rw(0x9644) <= 2)
			ww(0x9644, 0);
		ww(0x9646, rw(0x9646) - 5);
		if (rw(0x9646) <= 2)
			ww(0x9646, 0);
		ww(0x9648, tab(T_SPACING2, rw(0x9642)));
		ww(0x964A, tab(T_SPACING2, rw(0x9644)));
		ww(0x964C, tab(T_SPACING2, rw(0x9646)));
		int af = rw(P(AF)), c1 = rw(0x9648), c2 = rw(0x964A), c3 = rw(0x964C), lo = rw(0x964E),
		    hi = rw(0x9650);
		/* the parallel branch: A2-A6 and AB, each raised by AF */
		ww(0x9642, rw(P(A2)) + af + 2 * c1 + c2 + lo + 0x1E);
		ww(0x9642, par_db(rw(0x9642)));
		ww(W(31), mul11(rw(0x9642), rw(0x963A)));
		ww(0x9642, rw(P(A3)) + af + 2 * c2 + c3 + hi + 0x16);
		ww(0x9642, par_db(rw(0x9642)));
		ww(W(29), -mul11(rw(0x9642), rw(0x963C)));
		ww(0x9642, rw(P(A4)) + af + 2 * c3 + hi + 0x11);
		ww(0x9642, par_db(rw(0x9642)));
		ww(W(27), mul11(rw(0x9642), rw(0x963E)));
		ww(0x9642, rw(P(A5)) + af + hi + 0x10);
		ww(0x9642, par_db(rw(0x9642)));
		ww(W(25), -mul11(rw(0x9642), 0x63E0));
		ww(0x9642, rw(P(A6)) + af + hi + 0xF);
		ww(0x9642, par_db(rw(0x9642)));
		ww(W(23), mul11(rw(0x9642), 0x6520));
		ww(0x9642, rw(P(AB)) + af + 0x25);
		ww(W(33), tab(T_DB, rw(0x9642)) << 2);
	}
	v1_frame_handshake(4);
	v1_pg_ring(4, 0);
}

/* EC236: the 37 frame words to the DSP (the firmware raises fatal error 0x24 if the DSP still wants a frame after
 * them; the board's business) */
static void write_frame(void)
{
	uint16_t f[37];
	for (int i = 0; i < 37; i++) {
		f[i] = ruw(W(i));
		if (v1_dsp_write_hook)
			v1_dsp_write_hook(f[i]);
	}
	if (v1_frame_hook)
		v1_frame_hook(f, 37);
}

/* EDA11: send the ready frame unless output is held (ESC[H) or the builder has stopped at STOP_AT; a request with no
 * frame ready is a miss, and the length of each run of misses is counted (A526-A53A). Then build the next frame. */
void v1_frame_tick(void)
{
	ww(V1_IRQ_NEST, rw(V1_IRQ_NEST) + 1);
	v1_latch_clear(8);
	if (!rw(V1_OUTPUT_HOLD) && !rw(STOPPED)) {
		if (!v1_frame_handshake(5)) {
			count32(0xA41E);
			ww(MISSES, rw(MISSES) + 1);
		} else {
			write_frame();
			v1_frame_handshake(6);
			if (rw(V1_TRACK_READ) == rw(STOP_AT))
				ww(STOPPED, 1);
			unsigned m = rw(MISSES);
			if (m) {
				count32(m <= 5 ? 0xA526 + 4 * (m - 1) : 0xA53A);
				count32(0xA422);
				ww(MISSES, 0);
			}
		}
	}
	v1_latch_set(8);
	ww(V1_IRQ_NEST, rw(V1_IRQ_NEST) - 1);
	v1_irq_enable();
	v1_frame_build();
}

/* EC1E0, IR0: latch bit 2 up while it runs. A frame request (USF0) runs the tick; 20 interrupts in a row without one
 * are fatal error 0x23. */
void v1_dsp_irq_service(void)
{
	v1_latch_set(4);
	if (!(v1_dsp_status & 0x20)) {
		wb(V1_DSP_SPURIOUS, rb(V1_DSP_SPURIOUS) + 1);
		if (rb(V1_DSP_SPURIOUS) != 0x14) {
			v1_latch_clear(4);
			return;
		}
		v1_fatal_error(0x23);
	}
	wb(V1_DSP_SPURIOUS, 0);
	v1_frame_tick();
	v1_latch_clear(4);
}

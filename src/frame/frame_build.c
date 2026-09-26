/*
 * dsp_build_frame (D89CB): 22 parameters -> 40-word DSP frame. See frame_build.h.
 *
 * Tables (DS offsets, read from the ROM):
 *   5656  frame template (40 words)
 *   5368-53D8  per-voice words, 8 entries each, indexed by p21's high nibble
 *   53E8  jitter depth by p20's low nibble; 5428 jitter/shimmer values by LFSR (64); 5408 shimmer depth by p21's low nibble
 *   54A8, 54E8  voicing gain by p18 and the resulting value; 55E4, 5624  AV scaling by p18 and p19
 *   56A6  nasal-zero gain by FN; 56CA / 57CA  exp(-pi*B*T) / exp(-2*pi*B*T) by bandwidth (4 Hz steps)
 *   58CA  cos(2*pi*F*T) by frequency (8 Hz steps); 5CCA  dB -> linear
 *   5E74, 5F66  parallel-amplitude corrections for formant spacing (Klatt)
 */
#include "frame_build.h"

#include <string.h>

enum {
	T_TEMPLATE = 0x5656,
	T_VOICE_W17 = 0x5368, T_VOICE_W38 = 0x5388, T_VOICE_W39 = 0x5398, T_VOICE_W37 = 0x53A8,
	T_VOICE_F5A = 0x53B8, T_VOICE_F5B = 0x53C8, T_VOICE_F5G = 0x53D8,
	T_JITTER_DEPTH = 0x53E8, T_SHIMMER_DEPTH = 0x5408, T_JITTER = 0x5428,
	T_VGAIN_SCALE = 0x54A8, T_VGAIN = 0x54E8, T_AV_BY_P18 = 0x55E4, T_AV_BY_P19 = 0x5624,
	T_NZ_GAIN = 0x56A6, T_EXP1 = 0x56CA, T_EXP2 = 0x57CA, T_COS = 0x58CA, T_DB = 0x5CCA,
	T_PAR_CORR1 = 0x5E74, T_PAR_CORR2 = 0x5F66,
};

/* Frame word numbers (DS:DBD2 + 2n). */
enum {
	W_SOURCE = 0, W_VGAIN = 1, W_F5A = 4, W_F5B = 5, W_F4A = 6, W_F3A = 8, W_F3B = 9, W_F2A = 10, W_F2B = 11,
	W_F1A = 14, W_F1B = 15, W_AH = 16, W_VOICE = 17, W_PAR_HF = 18, W_PAR_F5 = 20, W_F5G = 21, W_PAR_F4 = 22,
	W_F4G = 23, W_PAR_F3 = 24, W_F3G = 25, W_PAR_F2 = 26, W_F2G = 27, W_PAR_AB = 28, W_NZ_GAIN = 29,
	W_PERIOD1 = 30, W_PERIOD2 = 31, W_NZ_A = 32, W_AV1 = 34, W_AV2 = 35, W_F1G = 36, W_GLOT_G = 37,
	W_GLOT_A = 38, W_GLOT_B = 39,
};

/* fb->work: the firmware's working words DC28 + 2i. The parallel-branch terms (up to WK_D1) are set only when AF > 0;
   WK_TMP ends as the index of the last dB lookup (AB). */
enum {
	WK_K3 = 0, WK_K2 = 1, WK_C34 = 2, WK_C23 = 3, WK_C12 = 4, WK_S34 = 6, WK_S23 = 7, WK_TMP = 8, WK_D1 = 9,
	WK_G4 = 10, WK_G3 = 11, WK_G2 = 12, WK_HI = 13, WK_COS = 14,
};

/* The 8086 fixed-point helpers. All take a 16 x 16 -> 32-bit signed product. */
static int16_t mul_q15(int16_t a, int16_t b) { return (int16_t)(((int32_t)a * b) >> 15); } /* D3521 */
static int16_t mul_shr11(int16_t a, int16_t b) { return (int16_t)(((int32_t)a * b) >> 11); } /* D34DC */
/* D3501: returns product >> 12 and stores product >> 13 through its third argument ([DC42] here). */
static int16_t mul_shr12(int16_t a, int16_t b, int16_t *hi)
{
	int32_t p = (int32_t)a * b;
	*hi = (int16_t)(p >> 13);
	return (int16_t)(p >> 12);
}

static int16_t T(const frame_builder *fb, uint16_t table, int idx) { return prose_ds_table(fb->rom, table, idx); }

static uint16_t lfsr_step(frame_builder *fb)
{
	int16_t v = (int16_t)fb->lfsr;
	fb->lfsr = (uint16_t)((((v & 2) >> 1) ^ (v & 1)) * 0x80 + (v >> 1));
	return fb->lfsr;
}

/* 20000 / x as the 8086 does it: 32-bit quotient truncated to 16 bits, then halved. */
static int16_t half_period(int16_t x)
{
	int16_t q = (int16_t)(20000L / x);
	return (int16_t)((int32_t)q / 2);
}

void frame_builder_reset_frame(frame_builder *fb)
{
	for (int i = 0; i < 40; i++)
		fb->w[i] = (uint16_t)prose_ds_table(fb->rom, T_TEMPLATE, i);
}

void frame_builder_reset_source(frame_builder *fb)
{
	fb->silent = 0;
	fb->lfsr = 0x55;
	fb->silent_run = 0;
}

void frame_builder_init(frame_builder *fb, const prose_rom *rom, uint8_t latch)
{
	memset(fb, 0, sizeof *fb);
	fb->rom = rom;
	fb->latch = latch;
	frame_builder_reset_frame(fb);
	frame_builder_reset_source(fb);
}

/* One resonator's coefficient pair from a bandwidth byte and a cosine: a = r*cos (Q15 of 32768), b = 4*r^2. Returns
   the gain term 0x2000 - (a >> 1) + r^2 = 8192 * (1 - 2 r cos + r^2). */
static int16_t resonator(frame_builder *fb, int bw, int16_t cosv, int wa, int wb)
{
	int16_t r = T(fb, T_EXP1, bw >> 1), r2 = T(fb, T_EXP2, bw >> 1);
	fb->work[WK_COS] = cosv;
	fb->w[wa] = (uint16_t)mul_shr12(r, cosv, &fb->work[WK_HI]);
	fb->w[wb] = (uint16_t)(r2 << 2);
	return (int16_t)(0x2000 - fb->work[WK_HI] + r2);
}

void frame_build(frame_builder *fb, const uint8_t track[22], int mark, int alt)
{
	int16_t *p = fb->p, *wk = fb->work;
	for (int i = 0; i < 22; i++)
		p[i] = track[i];
	uint16_t *w = fb->w;

	if (mark)
		fb->played++;

	int voice = (p[21] & 0xF0) >> 4;
	p[21] &= 0xF;

	/* silence: AV, AF and AH all zero. The latch's bit 1 goes up on the first silent frame and down again, with bit
	   5 set, on the first sounding one. */
	if (p[0] == 0 && p[1] == 0 && p[2] == 0) {
		if (fb->silent == 0) {
			fb->latch |= 0x02;
			fb->silent = 1;
		}
		fb->silent_run++;
	} else {
		if (fb->silent == 1) {
			fb->latch &= (uint8_t)~0x02;
			fb->latch |= 0x20;
			fb->silent = 0;
		}
		fb->silent_run = 0;
	}

	/* voicing source */
	if (p[0] == 0)
		p[17] = 0;
	if (p[17] == 0) {
		w[W_SOURCE] = 0;
		w[W_PERIOD1] = w[W_PERIOD2] = 0;
		w[W_AV1] = w[W_AV2] = 0;
	} else {
		if (p[17] == 0x45 || p[17] == 0x4A || p[17] == 0x4F || p[17] == 0x54 || p[17] == 0x5A)
			p[17]++;
		/* two jittered periods */
		uint16_t rnd = lfsr_step(fb);
		int16_t depth = mul_q15(p[17], T(fb, T_JITTER_DEPTH, p[20] & 0xF));
		int16_t f1 = (int16_t)(mul_q15(depth, T(fb, T_JITTER, rnd & 0x3F)) + p[17]);
		rnd = lfsr_step(fb);
		int16_t f2 = (int16_t)(mul_q15(depth, T(fb, T_JITTER, rnd & 0x3F)) + p[17]);
		int16_t per1 = half_period(f1), per2 = half_period(f2);
		w[W_PERIOD1] = (uint16_t)per1;
		w[W_PERIOD2] = (uint16_t)per2;

		w[W_SOURCE] = (uint16_t)p[18];
		int16_t g = mul_q15(per1, (int16_t)(p[19] << 11));
		g = mul_q15(g, T(fb, T_VGAIN_SCALE, p[18]));
		w[W_VGAIN] = (uint16_t)T(fb, T_VGAIN, g);

		/* two voicing amplitudes: AV in dB -> linear, scaled by p18, p19 and the period; the first gets 8x the
		   shimmer of the second */
		int16_t av = T(fb, T_DB, p[0] + 0x8C);
		av = mul_q15(av, T(fb, T_AV_BY_P18, p[18]));
		av = (int16_t)(mul_q15(av, T(fb, T_AV_BY_P19, p[19])) >> 3);
		av = (int16_t)((per1 >> 2) * av);
		int16_t av2 = av;
		int16_t sh = mul_q15(av, T(fb, T_SHIMMER_DEPTH, p[21]));
		av = (int16_t)(av + mul_q15(sh, T(fb, T_JITTER, rnd & 0x3F)) * 8);
		rnd = lfsr_step(fb);
		sh = mul_q15(av2, T(fb, T_SHIMMER_DEPTH, p[21]));
		av2 = (int16_t)(av2 + mul_q15(sh, T(fb, T_JITTER, rnd & 0x3F)));
		w[W_AV1] = (uint16_t)av;
		w[W_AV2] = (uint16_t)av2;
	}
	if (fb->silent_run > 2) {
		w[W_SOURCE] |= 0x80;
		fb->silent_run = 2;
	}

	w[W_AH] = (uint16_t)T(fb, T_DB, p[2] + 0x69);
	w[W_GLOT_A] = (uint16_t)T(fb, T_VOICE_W38, voice);
	w[W_GLOT_B] = (uint16_t)T(fb, T_VOICE_W39, voice);
	w[W_GLOT_G] = (uint16_t)T(fb, T_VOICE_W37, voice);
	w[W_F5A] = (uint16_t)T(fb, T_VOICE_F5A, voice);
	w[W_F5B] = (uint16_t)T(fb, T_VOICE_F5B, voice);
	w[W_F5G] = (uint16_t)T(fb, T_VOICE_F5G, voice);

	/* F4: fixed bandwidth (r = 0x1E11/8192) */
	w[W_F4A] = (uint16_t)mul_shr12(T(fb, T_COS, p[12] * 2), 0x1E11, &wk[WK_HI]);
	int16_t g4 = wk[WK_G4] = (int16_t)(0x3C40 - wk[WK_HI]);
	w[W_F4G] = (uint16_t)g4;

	/* F3, F2, F1. Normally F3 = p11*16 Hz, F2 = p10*8 + 504 Hz, F1 = p9*4 Hz; flagged frames use p*10 Hz for all
	   three. */
	int16_t g3 = resonator(fb, p[15], T(fb, T_COS, alt ? (p[11] * 10) >> 3 : p[11] * 2), W_F3A, W_F3B);
	w[W_F3G] = (uint16_t)(wk[WK_G3] = g3);
	int16_t g2 = resonator(fb, p[14], T(fb, T_COS, alt ? (p[10] * 10) >> 3 : p[10] + 0x3F), W_F2A, W_F2B);
	w[W_F2G] = (uint16_t)(wk[WK_G2] = g2);
	int16_t g1 = resonator(fb, p[13], T(fb, T_COS, alt ? (p[9] * 10) >> 3 : p[9] >> 1), W_F1A, W_F1B);
	w[W_F1G] = (uint16_t)(g1 * 0x10);

	w[W_VOICE] = (uint16_t)T(fb, T_VOICE_W17, voice);

	/* nasal zero at FN = p16*4 + 192 Hz, bandwidth fixed (r*cos scale 0xE105) */
	wk[WK_COS] = T(fb, T_COS, ((p[16] & 0xFF) * 4 + 0xC0) >> 3);
	w[W_NZ_A] = (uint16_t)mul_shr12((int16_t)0xE105, wk[WK_COS], &wk[WK_HI]);
	w[W_NZ_GAIN] = (uint16_t)(T(fb, T_NZ_GAIN, p[16] >> 2) << 1);

	/* parallel branch, gated by AF (p1) */
	if (p[1] < 1) {
		w[W_PAR_HF] = w[W_PAR_F5] = w[W_PAR_F4] = w[W_PAR_F3] = w[W_PAR_F2] = w[W_PAR_AB] = 0;
	} else {
		int d1 = alt ? (p[9] * 10) >> 5 : p[9] >> 3;
		int d2 = alt ? (p[10] * 10) >> 5 : (p[10] >> 2) + 0x10;
		int c1 = T(fb, T_PAR_CORR1, d1) << 1;
		int k2 = c1 - T(fb, T_PAR_CORR1, d2) - 0x2C;
		int k3 = T(fb, T_PAR_CORR1, d2) * 2 + c1 - 0xEB;
		int d3 = alt ? (p[11] * 10) >> 5 : p[11] >> 1;
		int s34 = (p[12] >> 1) - d3; /* F4 - F3 */
		int s23 = d3 - d2;           /* F3 - F2 */
		int s12 = d2 - d1;           /* F2 - F1 */
		if (s12 < 3)
			s12 = 0;
		s23 -= 2;
		if (s23 < 3)
			s23 = 0;
		s34 -= 5;
		if (s34 < 3)
			s34 = 0;
		int c12 = T(fb, T_PAR_CORR2, s12), c23 = T(fb, T_PAR_CORR2, s23), c34 = T(fb, T_PAR_CORR2, s34);
		wk[WK_D1] = (int16_t)d1;
		wk[WK_K2] = (int16_t)k2;
		wk[WK_K3] = (int16_t)k3;
		wk[WK_S23] = (int16_t)s23;
		wk[WK_S34] = (int16_t)s34;
		wk[WK_C12] = (int16_t)c12;
		wk[WK_C23] = (int16_t)c23;
		wk[WK_C34] = (int16_t)c34;
		wk[WK_TMP] = (int16_t)(p[8] + p[1] + 0x25);
		int a;
		a = p[3] + p[1] + c12 * 2 + c23 + k2 + 0x1E;
		w[W_PAR_F2] = (uint16_t)mul_shr11(T(fb, T_DB, a < 0 ? 0 : a), g2);
		a = p[4] + p[1] + c23 * 2 + c34 + k3 + 0x16;
		w[W_PAR_F3] = (uint16_t)-mul_shr11(T(fb, T_DB, a < 0 ? 0 : a), g3);
		a = p[5] + p[1] + c34 * 2 + k3 + 0x11;
		w[W_PAR_F4] = (uint16_t)mul_shr11(T(fb, T_DB, a < 0 ? 0 : a), g4);
		a = p[6] + p[1] + k3 + 0x10;
		w[W_PAR_F5] = (uint16_t)-mul_shr11(T(fb, T_DB, a < 0 ? 0 : a), T(fb, T_VOICE_F5G, voice));
		a = p[7] + p[1] + k3 + 0xF;
		w[W_PAR_HF] = (uint16_t)mul_shr11(T(fb, T_DB, a < 0 ? 0 : a), 0x6520);
		w[W_PAR_AB] = (uint16_t)(T(fb, T_DB, p[8] + p[1] + 0x25) << 2);
	}
}

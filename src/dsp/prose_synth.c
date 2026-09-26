/*
 * The Prose 2000 formant synthesizer: a C decompilation of the uPD7720 program (DSP v3.12, 8/9/88).
 *
 * Structure (REFERENCE.md section 13 has the addresses):
 *
 *   glottal pulse (table, two phases) -> first difference, x8 -> * w17 --+
 *   noise generator -> * AH (w16) ----------------------------------------+-> glottal filter (w37-w39)
 *        -> cascade: F5 -> F4 -> F3 -> F2 -> nasal pole (fixed) -> nasal zero (FN) -> F1 -> * 2*w36 --+
 *   noise (halved in the second half of each pitch period) -> parallel: fixed HF, F5, F4, F3, F2,    |
 *        bypass -> * init word 0 -> first difference -------------------------------------------------+
 *   -> sum -> one-pole output filter -> + 0x1001 -> clip to 13 bits -> 2-sample FIFO -> serial port
 *
 * F1-F3 and the nasal pair take their new coefficients only at the start of a pitch period. The two pitch periods
 * and the two voicing amplitudes of a frame alternate from period to period (jitter and shimmer).
 *
 * The arithmetic is the 7720's: 16-bit accumulators whose overflow flags decide saturation, and a Q15 multiplier.
 * `acc` reproduces the flags, because several sums saturate only through them. The model matches the instruction
 * translation (tools/dsp_recompile.py) and the emulator sample for sample.
 *
 * RAM addresses are kept (as named constants) because the frame words are scattered into RAM through a table in the
 * data ROM, and some slots are shared between stages.
 */
#include "prose_synth.h"

#include <stdlib.h>
#include <string.h>

/* ---- 7720 arithmetic ------------------------------------------------------------------------------------------ */

typedef struct {
	uint16_t v;
	uint8_t s0, s1, z, c, ov0, ov1;
} acc;

static void acc_flags(acc *a, uint16_t r)
{
	a->s0 = (r & 0x8000) != 0;
	a->z = r == 0;
	if (!a->ov1)
		a->s1 = a->s0;
	a->v = r;
}

/* Logic operations and shifts clear the overflow and carry flags. */
static void acc_logic(acc *a, uint16_t r)
{
	acc_flags(a, r);
	a->c = 0;
	a->ov0 = a->ov1 = 0;
}

static void acc_arith(acc *a, uint16_t p, int subtract, int carry)
{
	uint16_t q = a->v;
	uint16_t r = subtract ? (uint16_t)(q - p - carry) : (uint16_t)(q + p + carry);
	acc_flags(a, r);
	if (subtract) {
		a->ov0 = ((q ^ r) & (q ^ p) & 0x8000) != 0;
		a->c = r > q;
	} else {
		a->ov0 = ((q ^ r) & ~(q ^ p) & 0x8000) != 0;
		a->c = r < q;
	}
	/* OV1 counts overflows modulo a return to range: S1 then keeps the sign of the true result. */
	a->ov1 = (a->ov0 && a->ov1) ? (a->s1 == a->s0) : (a->ov0 || a->ov1);
}

static void add(acc *a, int16_t p) { acc_arith(a, (uint16_t)p, 0, 0); }
static void sub(acc *a, int16_t p) { acc_arith(a, (uint16_t)p, 1, 0); }
static void clear(acc *a) { acc_logic(a, 0); }
static int16_t val(const acc *a) { return (int16_t)a->v; }

/* The SGN source: the saturated value for the true sign of an overflowed sum. It always reads accumulator A. */
static int16_t sgn(const acc *a) { return (int16_t)(0x8000 - a->s1); }

/* Saturates if the chain of additions since the last logic operation overflowed. */
static void saturate(acc *a)
{
	if (a->ov1)
		a->v = (uint16_t)sgn(a);
}

/* Q15 multiply: M is the product >> 15, N its low word << 1. */
static int16_t mul(int16_t k, int16_t l) { return (int16_t)(((int32_t)k * l) >> 15); }
static int16_t mul_lo(int16_t k, int16_t l) { return (int16_t)(((int32_t)k * l) << 1); }

/* ---- RAM layout ----------------------------------------------------------------------------------------------- */

enum {
	/* 0x00-0x0D: output FIFO slots (the model keeps only the two it ever holds, see below) */
	GLOT_AMP = 0x0E,   /* voicing amplitude for the current period (from w34/w35) */
	GLOT_PHASE_STATE = 0x0F, /* 0 opening, 2 closing, 3 (or 1) pulse finished */
	GLOT_A = 0x11, GLOT_B = 0x12, /* glottal filter coefficients (w38, w39) */
	F1_A = 0x13, F1_B = 0x14, F1_GAIN = 0x15, /* pending F1 (w14, w15, w36) */
	FN_A = 0x16, FN_GAIN = 0x17,  /* pending nasal zero (w32) and its gain (w29) */
	FRAME_MISSED = 0x18, OUT_PREV = 0x19, SILENT = 0x1A,
	FIFO_WRITE = 0x1B, FIFO_READ = 0x1C,
	GLOT_STEP = 0x1D,  /* phase increment per sample for this period (w1 scaled) */
	GLOT_CLOSE_RATE = 0x1E, GLOT_CLOSE_MODE = 0x1F,
	GLOT_GAIN = 0x20,  /* w37 */
	ISR_SAVE_B = 0x21, OUT_LATCH = 0x22,
	F2_A = 0x24, F2_B = 0x25, F2_GAIN = 0x26, /* pending F2 (w10, w11, w27) */
	OUT_OFFSET = 0x28, PAR_PREV = 0x29, NOISE_A = 0x2A, TEMP = 0x2C, GLOT_PREV = 0x2D,
	GLOT_ACC = 0x2E,   /* glottal phase accumulator */
	FRAME_CLOSE_MODE = 0x2F,
	GLOT_Y1 = 0x31, GLOT_Y2 = 0x32,
	F3_A = 0x34, F3_B = 0x35, F3_GAIN = 0x36, /* pending F3 (w8, w9, w25) */
	PAR_SCALE = 0x38,  /* init word 0 */
	NOISE_B = 0x3A,    /* init word 2 seeds it */
	FRAME_LEN = 0x3B, FRAME_COUNT = 0x3C, PERIOD_COUNT = 0x3E, HALF_COUNT = 0x3F,
	FRAME_CLOSE_RATE = 0x40, FRAME_STEP = 0x41,
	HF_A = 0x42, HF_B = 0x43, F5_A = 0x44, F5_B = 0x45, F4_A = 0x46, F4_B = 0x47, /* w2-w7, used directly */
	F3_A_NOW = 0x48, F3_B_NOW = 0x49, F2_A_NOW = 0x4A, F2_B_NOW = 0x4B,
	NP_A = 0x4C, NP_B = 0x4D, /* nasal pole, fixed (w12, w13) */
	F1_A_NOW = 0x4E, F1_B_NOW = 0x4F,
	AH = 0x50, VOICE_GAIN = 0x51, PAR_HF = 0x52, CASC_IN_GAIN = 0x53, PAR_F5 = 0x54, F5_GAIN = 0x55,
	PAR_F4 = 0x56, F4_GAIN = 0x57, PAR_F3 = 0x58, F3_GAIN_NOW = 0x59, PAR_F2 = 0x5A, F2_GAIN_NOW = 0x5B,
	PAR_BYPASS = 0x5C, FN_GAIN_NOW = 0x5D, PERIOD = 0x5E, PERIOD_NEXT = 0x5F,
	F1_GAIN_NOW = 0x63,
	CASC_STATE = 0x64, /* y1, y2 pairs of the cascade stages at 0x64, 0x66 ... 0x6E */
	PAR_STATE = 0x72,  /* y1, y2 pairs of the parallel stages at 0x72 ... 0x7A */
	FN_A_NOW = 0x7C, NZ_B = 0x7D, /* nasal zero: w32 (pending copy at 0x16) and w33 */
	AV = 0x7E, AV_NEXT = 0x7F,
};

struct prose_synth {
	uint16_t ram[128];
	uint16_t rom[512];
	prose_synth_poll poll;
	prose_synth_timeout timeout;
	void *user;
	size_t produced;
	int raw_start; /* start from zeroed output registers, as the emulator does (see prose_synth_run) */
	/* not in the DSP: an optional glottal flow period that replaces the pulse table (prose_synth_set_custom_pulse) */
	int16_t custom[PROSE_SYNTH_PULSE_LEN];
	int custom_on, custom_period;
};

#define R(a) (s->ram[(a)])
#define RS(a) ((int16_t)s->ram[(a)])

static int host_word(prose_synth *s, uint16_t *word)
{
	return s->poll && s->poll(s->user, s->produced, word);
}

/* ---- Filters -------------------------------------------------------------------------------------------------- */

/* Cascade resonator (0x11C): y = sat(2*(g*x + a*y1) - b*y2). Returns y; *y2_old gets the replaced y2, which the
   nasal zero after the nasal pole reads. */
static int16_t cascade_resonator(prose_synth *s, int16_t x, int16_t g, unsigned coef, unsigned st, int16_t *y2_old)
{
	acc a = {0};
	int16_t y1 = RS(st), y2 = RS(st + 1);
	clear(&a);
	add(&a, mul(g, x));
	add(&a, mul(RS(coef), y1));
	add(&a, val(&a));
	sub(&a, mul(RS(coef + 1), y2));
	saturate(&a);
	R(st + 1) = (uint16_t)y1;
	R(st) = a.v;
	if (y2_old)
		*y2_old = y2;
	return val(&a);
}

/* Parallel resonator (0x13C): y = sat(g*x - b*y2 + 2*a*y1). */
static int16_t parallel_resonator(prose_synth *s, int16_t x, int16_t g, unsigned coef, unsigned st)
{
	acc a = {0};
	int16_t y1 = RS(st), y2 = RS(st + 1);
	clear(&a);
	add(&a, mul(g, x));
	sub(&a, mul(RS(coef + 1), y2));
	add(&a, mul(y1, RS(coef)));
	add(&a, mul(y1, RS(coef)));
	saturate(&a);
	R(st + 1) = (uint16_t)y1;
	R(st) = a.v;
	return val(&a);
}

/* ---- Glottal source ------------------------------------------------------------------------------------------- */

/* Pulse table lookup (0x147): the phase's high byte indexes ROM[2..257]; the value is corrected by the low byte
   times ROM[|index - 128|], the slope. */
static int16_t pulse_shape(prose_synth *s)
{
	uint16_t phase = R(GLOT_ACC);
	int hi = phase >> 8, frac = phase & 0xFF;
	int16_t base = (int16_t)s->rom[(hi + 2) & 0x1FF];
	int d = hi + 2 - 128;
	int16_t slope = (int16_t)s->rom[(d < 0 ? -d : d) & 0x1FF];
	return (int16_t)(base - mul((int16_t)frac, slope));
}

/* Pitch clock (0x15C). Returns 1 when a new pitch period starts (then F1-F3 and the nasal pair take their pending
   coefficients, and the two periods swap). */
static int pitch_clock(prose_synth *s)
{
	if (R(PERIOD) == 0 && RS(PERIOD_COUNT) < 0) {
		/* unvoiced with a negative count (0x19E): both counters take the count; restart every sample */
		R(HALF_COUNT) = R(PERIOD_COUNT);
		return 1;
	}
	int16_t count = (int16_t)(RS(PERIOD_COUNT) - 1);
	if (count >= 0) {
		R(PERIOD_COUNT) = (uint16_t)count;
		R(HALF_COUNT) = (uint16_t)(R(HALF_COUNT) - 1);
		return 0;
	}
	/* period over: take the pending formant coefficients */
	R(F1_A_NOW) = R(F1_A);
	R(F1_B_NOW) = R(F1_B);
	R(F1_GAIN_NOW) = R(F1_GAIN);
	R(F2_A_NOW) = R(F2_A);
	R(F2_B_NOW) = R(F2_B);
	R(F2_GAIN_NOW) = R(F2_GAIN);
	R(F3_A_NOW) = R(F3_A);
	R(F3_B_NOW) = R(F3_B);
	R(F3_GAIN_NOW) = R(F3_GAIN);
	R(FN_A_NOW) = R(FN_A);
	R(FN_GAIN_NOW) = R(FN_GAIN);
	uint16_t period = R(PERIOD);
	R(PERIOD_COUNT) = period;
	R(PERIOD) = R(PERIOD_NEXT);
	R(PERIOD_NEXT) = period;
	R(HALF_COUNT) = (uint16_t)((int16_t)period >> 1);
	/* the new period's sign decides; periods are never negative */
	return (int16_t)~((int16_t)period >> 1) < 0;
}

/* Not in the DSP: the custom period's flow for this sample, stretched over the pitch period and scaled by the
   period's amplitude as the table's flow is. The period counts down from its length - 1 to 0; unvoiced, it is negative
   and the flow 0. */
static int16_t custom_flow(prose_synth *s)
{
	int16_t count = RS(PERIOD_COUNT);
	int len = s->custom_period;
	if (count < 0 || len <= 0)
		return 0;
	int i = len - 1 - count;
	if (i < 0)
		i = 0;
	return mul(s->custom[(long)i * PROSE_SYNTH_PULSE_LEN / len], RS(GLOT_AMP));
}

/* One glottal sample (0x019-0x075): the flow value for this sample, differentiated and scaled x8. */
static int16_t glottal_sample(prose_synth *s, int period_start)
{
	acc a = {0};
	int16_t flow;

	if (period_start) {
		R(GLOT_CLOSE_MODE) = R(FRAME_CLOSE_MODE);
		R(GLOT_CLOSE_RATE) = R(FRAME_CLOSE_RATE);
		uint16_t av = R(AV);
		R(AV) = R(AV_NEXT);
		R(AV_NEXT) = av;
		R(GLOT_AMP) = av;
		R(GLOT_STEP) = R(FRAME_STEP);
		R(GLOT_PHASE_STATE) = 0;
		R(GLOT_ACC) = 0;
		clear(&a);
		flow = 0;
		s->custom_period = RS(PERIOD_COUNT) + 1;
		if (s->custom_on)
			flow = custom_flow(s);
	} else if (s->custom_on) {
		clear(&a);
		flow = custom_flow(s);
	} else if (R(GLOT_PHASE_STATE) & 1) {
		clear(&a);
		flow = 0;
	} else {
		uint16_t before = R(GLOT_ACC);
		uint16_t after = (uint16_t)(before + R(GLOT_STEP));
		int wrapped = after < before;
		R(GLOT_ACC) = after;
		int closing = (R(GLOT_PHASE_STATE) & 2) != 0;
		if (wrapped && closing) {
			R(GLOT_PHASE_STATE) = 3;
			a.v = 0;
			flow = 0;
			goto differentiate;
		}
		if (wrapped)
			R(GLOT_PHASE_STATE) = 2;
		int16_t v = pulse_shape(s);
		if (!wrapped && !closing) {
			/* opening phase (0x069): flow = AV/2 - (AV/2)*v */
			acc h = {0};
			h.v = 0;
			add(&h, RS(GLOT_AMP));
			acc_logic(&h, (uint16_t)((int16_t)h.v >> 1));
			int16_t half = val(&h);
			a = h;
			sub(&a, mul(half, v));
			flow = val(&a);
		} else {
			/* closing phase (0x040) */
			int16_t rate = RS(GLOT_CLOSE_RATE);
			clear(&a);
			sub(&a, rate);
			add(&a, mul(rate, v));
			uint16_t t = a.v;
			if (!(R(GLOT_CLOSE_MODE) & 0x4000)) {
				a.v = t;
				add(&a, 0x7FFF);
				if (a.s0) {
					R(GLOT_PHASE_STATE) = 3;
					a.v = 0;
					flow = 0;
					goto differentiate;
				}
				int16_t level = val(&a);
				clear(&a);
				add(&a, mul(level, RS(GLOT_AMP)));
				flow = val(&a);
			} else {
				/* fine-grained closing: a 32-bit ramp, rate/256 per unit */
				acc lo = {0};
				lo.v = 0xFFFF;
				add(&lo, mul_lo(rate, v));
				a.v = t;
				acc_arith(&a, 0x007F, 0, lo.c);
				if (a.s0) {
					R(GLOT_PHASE_STATE) = 3;
					a.v = 0;
					flow = 0;
					goto differentiate;
				}
				uint16_t level = (uint16_t)(((a.v & 0xFF) << 8) | (lo.v >> 8));
				clear(&a);
				add(&a, mul((int16_t)level, RS(GLOT_AMP)));
				flow = val(&a);
			}
		}
	}

differentiate:
	/* 0x064: first difference, then x8 with saturation on each doubling */
	{
		int16_t prev = RS(GLOT_PREV);
		R(GLOT_PREV) = (uint16_t)flow;
		a.v = (uint16_t)flow;
		sub(&a, prev);
		for (int i = 0; i < 3; i++) {
			add(&a, val(&a));
			if (a.ov0) {
				a.v = (uint16_t)sgn(&a);
				break;
			}
		}
		return val(&a);
	}
}

/* Noise (0x07A): two registers, lagged XOR, then a shift-and-mix; zero is replaced by the seed pattern. */
static int16_t noise(prose_synth *s)
{
	int16_t k = (int16_t)(R(NOISE_A) ^ R(NOISE_B));
	R(NOISE_A) = R(NOISE_B);
	uint16_t v = (uint16_t)((mul(k, 0x100) & 0x1F8) | (uint16_t)mul_lo(k, 0x20));
	if (v == 0)
		v = 0xAAAA;
	R(NOISE_B) = v;
	return (int16_t)v;
}

/* ---- One sample ----------------------------------------------------------------------------------------------- */

static uint16_t compute_sample(prose_synth *s)
{
	int period_start = pitch_clock(s);
	int16_t excitation = glottal_sample(s, period_start);
	int16_t voice = mul(excitation, RS(VOICE_GAIN));
	int16_t n = noise(s);
	int16_t source = (int16_t)(mul(RS(AH), n) + voice);

	/* glottal filter (0x128): gain w37, coefficients w38/w39 */
	{
		acc a = {0};
		int16_t y1 = RS(GLOT_Y1), y2 = RS(GLOT_Y2);
		clear(&a);
		add(&a, mul(RS(GLOT_GAIN), source));
		add(&a, mul(y1, RS(GLOT_A)));
		add(&a, val(&a));
		sub(&a, mul(y2, RS(GLOT_B)));
		saturate(&a);
		R(GLOT_Y2) = (uint16_t)y1;
		R(GLOT_Y1) = a.v;
		source = val(&a);
	}

	/* cascade branch (0x090): each stage's input gain is the gain word placed before its coefficients */
	int16_t y2_old = 0;
	int16_t y = source;
	y = cascade_resonator(s, y, RS(CASC_IN_GAIN), F5_A, CASC_STATE + 0, NULL);
	y = cascade_resonator(s, y, RS(F5_GAIN), F4_A, CASC_STATE + 2, NULL);
	y = cascade_resonator(s, y, RS(F4_GAIN), F3_A_NOW, CASC_STATE + 4, NULL);
	y = cascade_resonator(s, y, RS(F3_GAIN_NOW), F2_A_NOW, CASC_STATE + 6, NULL);
	y = cascade_resonator(s, y, RS(F2_GAIN_NOW), NP_A, CASC_STATE + 8, &y2_old);
	/* nasal zero (0x136): y + b*y[n-2] + 2*a*y[n-1] over the nasal pole's output, no saturation */
	{
		int16_t y1 = RS(CASC_STATE + 9);
		y = (int16_t)(y + mul(RS(NZ_B), y2_old) + mul(RS(FN_A_NOW), y1) + mul(RS(FN_A_NOW), y1));
	}
	y = cascade_resonator(s, y, RS(FN_GAIN_NOW), F1_A_NOW, CASC_STATE + 10, NULL);
	int16_t cascade = (int16_t)(mul(RS(F1_GAIN_NOW), y) + mul(RS(F1_GAIN_NOW), y));

	/* noise for the parallel branch: halved in the second half of the pitch period (0x09F) */
	int16_t pn = n;
	if (R(PERIOD_COUNT) != 0 && (int16_t)(R(PERIOD_COUNT) ^ R(HALF_COUNT)) < 0)
		pn = (int16_t)(pn >> 1);

	/* parallel branch (0x0A7) */
	int16_t par = 0;
	par = (int16_t)(par + parallel_resonator(s, pn, RS(PAR_HF), HF_A, PAR_STATE + 0));
	par = (int16_t)(par + parallel_resonator(s, pn, RS(PAR_F5), F5_A, PAR_STATE + 2));
	par = (int16_t)(par + parallel_resonator(s, pn, RS(PAR_F4), F4_A, PAR_STATE + 4));
	par = (int16_t)(par + parallel_resonator(s, pn, RS(PAR_F3), F3_A_NOW, PAR_STATE + 6));
	par = (int16_t)(par + parallel_resonator(s, pn, RS(PAR_F2), F2_A_NOW, PAR_STATE + 8));
	par = (int16_t)(par + mul(RS(PAR_BYPASS), pn));

	/* output (0x0B1): parallel scaled and differentiated, plus cascade, through y = x - 0.23*y[n-1] */
	int16_t scaled = mul(RS(PAR_SCALE), par);
	int16_t out = (int16_t)(scaled - RS(PAR_PREV));
	R(PAR_PREV) = (uint16_t)scaled;
	out = (int16_t)(out + cascade);
	out = (int16_t)(out - mul(RS(OUT_PREV), 0x1D71));
	R(OUT_PREV) = (uint16_t)out;
	out = (int16_t)(out + RS(OUT_OFFSET));

	uint16_t dac;
	if (R(SILENT)) {
		dac = (uint16_t)(0x0FE0 + R(OUT_OFFSET));
	} else {
		int16_t biased = (int16_t)(out + 0x1001);
		if (biased < 0)
			dac = 0;
		else if ((int16_t)(0x1FFF - biased) < 0)
			dac = 0x1FFF;
		else
			dac = (uint16_t)biased;
	}
	/* 0x0D3: the offset decays to zero. The program stores the 7720's source 0 (NON), which reads as 0. */
	R(OUT_OFFSET) = 0;
	return dac;
}

/* ---- Frames --------------------------------------------------------------------------------------------------- */

static uint16_t wait_word(prose_synth *s)
{
	/* The 7720 spins here without a timeout. A host that never answers would hang the chip; give up instead. */
	uint16_t w = 0;
	for (long tries = 0; tries < (1L << 20); tries++)
		if (host_word(s, &w))
			return w;
	return 0;
}

/* Frame request (0x1A2): raise USF0 and pulse P0, then read 40 words or give up. */
static void fetch_frame(prose_synth *s)
{
	uint16_t w0;
	if (!host_word(s, &w0)) {
		if (s->timeout)
			s->timeout(s->user, s->produced);
		if (!host_word(s, &w0)) {
			/* first miss: keep the old frame; second: go silent */
			if (R(FRAME_MISSED) == 0)
				R(FRAME_MISSED) = 1;
			else
				R(SILENT) = 1;
			R(FRAME_COUNT) = R(FRAME_LEN);
			return;
		}
	}
	R(SILENT) = (w0 & 0x80) ? 1 : 0;
	R(FRAME_MISSED) = 0;
	if (w0 & 0x1F00)
		R(OUT_OFFSET) = s->rom[(0x145 + ((w0 >> 8) & 0x1F)) & 0x1FF];
	unsigned type = w0 & 0x1F;
	R(FRAME_CLOSE_MODE) = type >= 15 ? 0x4000 : 0;
	R(FRAME_CLOSE_RATE) = s->rom[0x103 + type];
	R(FRAME_STEP) = (uint16_t)mul((int16_t)wait_word(s), 0x517D);
	/* words 2-39 go to RAM[ROM[0x145 - i] >> 3] */
	for (int i = 0; i < 38; i++) {
		unsigned addr = (uint16_t)mul(0x1000, (int16_t)s->rom[0x145 - i]) & 0x7F;
		R(addr) = wait_word(s);
	}
	R(FRAME_COUNT) = R(FRAME_LEN);
}

/* ---- Public interface ----------------------------------------------------------------------------------------- */

prose_synth *prose_synth_create(const uint16_t rom[512])
{
	prose_synth *s = calloc(1, sizeof *s);
	if (s)
		memcpy(s->rom, rom, sizeof s->rom);
	return s;
}

void prose_synth_destroy(prose_synth *s) { free(s); }

void prose_synth_set_raw_start(prose_synth *s, int on) { s->raw_start = on; }

void prose_synth_set_custom_pulse(prose_synth *s, const int16_t *period)
{
	s->custom_on = period != NULL;
	if (period)
		memcpy(s->custom, period, sizeof s->custom);
}

size_t prose_synth_produced(const prose_synth *s) { return s->produced; }

void prose_synth_set_host(prose_synth *s, prose_synth_poll poll, prose_synth_timeout timeout, void *user)
{
	s->poll = poll;
	s->timeout = timeout;
	s->user = user;
}

static uint16_t rev16(uint16_t v)
{
	uint16_t r = 0;
	for (int i = 0; i < 16; i++)
		r = (uint16_t)((r << 1) | ((v >> i) & 1));
	return r;
}

void prose_synth_reset(prose_synth *s)
{
	memset(s->ram, 0, sizeof s->ram);
	s->produced = 0;

	/* reset (0x000): wait for a host word with bit 0 set, then take 8 setup words into 0x38-0x3F */
	uint16_t w;
	do
		w = wait_word(s);
	while (!(w & 1) && s->poll && s->produced == 0 && w != 0);
	for (int i = 0; i < 8; i++)
		R(0x38 + i) = wait_word(s);
	R(FIFO_READ) = 13;
	R(FIFO_WRITE) = 12;
	/* The program never initializes its output latch or the first FIFO slot it reads, so the first two samples out
	   are whatever the RAM held at power-on. The emulator's RAM starts at 0, which the DAC plays as full negative
	   scale: a click at the start of every render. Unless the raw start is asked for, start them at the bias word
	   0x1001 (the DAC's mid-scale), which is what the program outputs next for a zero signal. */
	if (!s->raw_start) {
		R(OUT_LATCH) = 0x1001;
		R(12) = 0x1001;
	}
}

size_t prose_synth_continue(prose_synth *s, int16_t *out, size_t count)
{
	/* The emulator runs the sample interrupt once per pass of the main loop: it outputs OUT_LATCH, then moves the
	   oldest FIFO sample into it. With one sample written per pass the FIFO holds one sample, so the output is the
	   sample computed two passes earlier. */
	for (size_t i = 0; i < count; i++) {
		out[i] = (int16_t)rev16(R(OUT_LATCH));
		s->produced++;
		unsigned rd = (unsigned)R(FIFO_READ);
		if (rd != R(FIFO_WRITE)) {
			rd = (rd - 1) & 0x7F;
			if ((rd & 15) == 15)
				rd = 13;
			R(FIFO_READ) = (uint16_t)rd;
			R(OUT_LATCH) = R(rd);
		}

		uint16_t sample = compute_sample(s);
		unsigned wr = (R(FIFO_WRITE) - 1) & 0x7F;
		if ((wr & 15) == 15)
			wr = 13;
		R(FIFO_WRITE) = (uint16_t)wr;
		R(wr) = sample;

		int16_t left = (int16_t)(RS(FRAME_COUNT) - 1);
		R(FRAME_COUNT) = (uint16_t)left;
		if (left < 0)
			fetch_frame(s);
	}
	return count;
}

size_t prose_synth_run(prose_synth *s, int16_t *out, size_t count)
{
	prose_synth_reset(s);
	return prose_synth_continue(s, out, count);
}

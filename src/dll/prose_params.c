/* The frame parameters (names, defaults, limits, units), raw frames, WAV files and the custom glottal pulse. */
#include "prose_int.h"

#include "prose_v1_data.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const names[PROSE_PARAM_MAX] = {
	"AV", "AF", "AH", "A2", "A3", "A4", "A5", "A6", "AB", "F1", "F2", "F3",
	"F4", "B1", "B2", "B3", "FN", "F0", "SOURCE", "SOURCE_GAIN", "JITTER", "SHIMMER",
};

/* the defaults and maxima behind ESC[l: v3.4.1 DS:61FE (bytes) and DS:60E0 (words); v1.1 DS:24A1 and DS:23B7 */
void params_defaults(int version, uint8_t out[PROSE_PARAM_MAX])
{
	memset(out, 0, PROSE_PARAM_MAX);
	if (version == PROSE_V11)
		for (int i = 0; i < 18; i++)
			out[i] = prose_v1_ds_data[0x24A1 + i];
	else
		for (int i = 0; i < 22; i++)
			out[i] = prose_ds_byte(engine_rom(), (uint16_t)(0x61FE + i));
}

int params_max(int version, int p)
{
	if (version == PROSE_V11)
		return prose_v1_ds_data[0x23B7 + 2 * p] | prose_v1_ds_data[0x23B8 + 2 * p] << 8;
	return prose_ds_word(engine_rom(), (uint16_t)(0x60E0 + 2 * p));
}

/* Raw frames go through v3.4.1's frame builder. v1.1's p0-p17 take v3.4.1's coding, except FN (2 Hz steps in v1.1,
 * 4 Hz from 192 Hz in v3.4.1), and voice 0's source settings for p18-p21, as v1.1's speech does (v1_map). */
void params_build_frame(prose_h h, const uint8_t *p, uint16_t out[40])
{
	uint8_t t[22];
	memcpy(t, p, 22);
	if (h->version == PROSE_V11) {
		int fn = (2 * p[PROSE_FN] - 192) / 4;
		t[PROSE_FN] = (uint8_t)(fn < 0 ? 0 : fn);
		t[18] = 17;
		t[19] = 8;
		t[20] = 0;
		t[21] = 0;
	}
	frame_build(&h->raw_fb, t, 0, 0);
	memcpy(out, h->raw_fb.w, 40 * sizeof *out);
}

/* ---- the exported parameter functions ---- */

static int valid(prose_h h) { return h && h->magic == PROSE_MAGIC; }

PROSE_API int PROSE_CALL prose_param_count(prose_h h) { return valid(h) ? h->nparams : PROSE_ERR_HANDLE; }

PROSE_API const char *PROSE_CALL prose_param_name(prose_h h, int p)
{
	return valid(h) && p >= 0 && p < h->nparams ? names[p] : NULL;
}

PROSE_API int PROSE_CALL prose_param_index(prose_h h, const char *name)
{
	if (!valid(h) || !name)
		return -1;
	for (int p = 0; p < h->nparams; p++) {
		const char *a = names[p], *b = name;
		while (*a && toupper((unsigned char)*a) == toupper((unsigned char)*b))
			a++, b++;
		if (!*a && !*b)
			return p;
	}
	return -1;
}

/* physical value = raw * scale + offset (REFERENCE §11.4) */
static void coding(int version, int p, double *scale, double *offset)
{
	*scale = 1;
	*offset = 0;
	switch (p) {
	case PROSE_F1:
		*scale = 4;
		break;
	case PROSE_F2:
		*scale = 8;
		*offset = 500;
		break;
	case PROSE_F3:
	case PROSE_F4:
		*scale = 16;
		break;
	case PROSE_B1:
	case PROSE_B2:
	case PROSE_B3:
		*scale = 2;
		break;
	case PROSE_FN:
		if (version == PROSE_V11)
			*scale = 2;
		else {
			*scale = 4;
			*offset = 192;
		}
		break;
	}
}

PROSE_API double PROSE_CALL prose_param_value(prose_h h, int p, int raw)
{
	double scale, offset;
	if (!valid(h) || p < 0 || p >= h->nparams)
		return 0;
	coding(h->version, p, &scale, &offset);
	return raw * scale + offset;
}

PROSE_API int PROSE_CALL prose_param_raw(prose_h h, int p, double value)
{
	double scale, offset;
	if (!valid(h))
		return PROSE_ERR_HANDLE;
	if (p < 0 || p >= h->nparams)
		return PROSE_ERR_ARG;
	coding(h->version, p, &scale, &offset);
	double r = (value - offset) / scale;
	int max = params_max(h->version, p);
	if (!(r > 0)) /* also NaN */
		return 0;
	if (r >= max)
		return max;
	return (int)(r + 0.5);
}

/* ---- WAV files ---- */

static void put16(uint8_t *b, unsigned v)
{
	b[0] = (uint8_t)v;
	b[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *b, uint32_t v)
{
	put16(b, v & 0xFFFF);
	put16(b + 2, v >> 16);
}

/* the 44-byte header of a 10 kHz 16-bit mono PCM file with `samples` samples */
void wav_header(uint8_t hdr[44], uint32_t samples)
{
	memcpy(hdr, "RIFF\0\0\0\0WAVEfmt ", 16);
	put32(hdr + 4, 36 + samples * 2);
	put32(hdr + 16, 16);
	put16(hdr + 20, 1);
	put16(hdr + 22, 1);
	put32(hdr + 24, PROSE_SAMPLE_RATE);
	put32(hdr + 28, PROSE_SAMPLE_RATE * 2);
	put16(hdr + 32, 2);
	put16(hdr + 34, 16);
	memcpy(hdr + 36, "data", 4);
	put32(hdr + 40, samples * 2);
}

/* samples as little-endian bytes, whatever the machine's order */
int wav_put_samples(FILE *f, const int16_t *pcm, size_t count)
{
	uint8_t b[512];
	while (count) {
		size_t n = count < sizeof b / 2 ? count : sizeof b / 2;
		for (size_t i = 0; i < n; i++)
			put16(b + 2 * i, (uint16_t)pcm[i]);
		if (fwrite(b, 2, n, f) != n)
			return -1;
		pcm += n;
		count -= n;
	}
	return 0;
}

int wav_write(const char *filename, const int16_t *pcm, size_t count)
{
	uint8_t hdr[44];
	if (count > 0x7FFFFFF0u / 2)
		return PROSE_ERR_ARG;
	FILE *f = fopen(filename, "wb");
	if (!f)
		return PROSE_ERR_FILE_OPEN;
	wav_header(hdr, (uint32_t)count);
	int bad = fwrite(hdr, 1, 44, f) != 44 || wav_put_samples(f, pcm, count);
	if (fclose(f) != 0)
		bad = 1;
	return bad ? PROSE_ERR_FILE_IO : 0;
}

PROSE_API int PROSE_CALL prose_save_wave(const char *filename, const int16_t *pcm, size_t count)
{
	if (!filename || (!pcm && count))
		return PROSE_ERR_ARG;
	return wav_write(filename, pcm, count);
}

static unsigned get16(const uint8_t *b) { return b[0] | b[1] << 8; }
static uint32_t get32(const uint8_t *b) { return get16(b) | (uint32_t)get16(b + 2) << 16; }

/* ---- the custom glottal pulse ---- */

/* One period from a 16-bit mono 10 kHz WAV file (at most 1 s), as PROSE_SYNTH_PULSE_LEN samples of flow from 0 to
 * 0x7FFF. A derivative is integrated first (after removing its mean, so that the period ends where it began). */
int glottal_load(const char *filename, int kind, int16_t out[PROSE_SYNTH_PULSE_LEN])
{
	FILE *f = fopen(filename, "rb");
	if (!f)
		return PROSE_ERR_FILE_OPEN;
	uint8_t hdr[12], ck[8];
	int err = 0, have_fmt = 0;
	double *x = NULL;
	size_t n = 0;
	if (fread(hdr, 1, 12, f) != 12)
		err = PROSE_ERR_WAV_FORMAT;
	else if (memcmp(hdr, "RIFF", 4) || memcmp(hdr + 8, "WAVE", 4))
		err = PROSE_ERR_WAV_FORMAT;
	while (!err) {
		if (fread(ck, 1, 8, f) != 8) {
			err = PROSE_ERR_WAV_FORMAT; /* no data chunk */
			break;
		}
		uint32_t size = get32(ck + 4);
		if (!memcmp(ck, "fmt ", 4)) {
			uint8_t fmt[16];
			if (size < 16 || fread(fmt, 1, 16, f) != 16) {
				err = PROSE_ERR_WAV_FORMAT;
				break;
			}
			unsigned tag = get16(fmt);
			if ((tag != 1 && tag != 0xFFFE) || get16(fmt + 2) != 1 || get16(fmt + 14) != 16)
				err = PROSE_ERR_WAV_FORMAT;
			else if (get32(fmt + 4) != PROSE_SAMPLE_RATE)
				err = PROSE_ERR_WAV_RATE;
			have_fmt = 1;
			size -= 16;
		} else if (!memcmp(ck, "data", 4)) {
			if (!have_fmt) {
				err = PROSE_ERR_WAV_FORMAT;
				break;
			}
			n = size / 2;
			if (n == 0 || n > PROSE_SAMPLE_RATE) {
				err = PROSE_ERR_WAV_LENGTH;
				break;
			}
			uint8_t *raw = malloc(n * 2);
			x = malloc(n * sizeof *x);
			if (!raw || !x)
				err = PROSE_ERR_MEMORY;
			else if (fread(raw, 2, n, f) != n)
				err = PROSE_ERR_FILE_IO;
			else
				for (size_t i = 0; i < n; i++)
					x[i] = (int16_t)get16(raw + 2 * i);
			free(raw);
			break;
		}
		if (size && fseek(f, (long)(size + (size & 1)), SEEK_CUR) != 0)
			err = PROSE_ERR_FILE_IO;
	}
	fclose(f);
	if (err) {
		free(x);
		return err;
	}

	if (kind == PROSE_GLOTTAL_DERIVATIVE) {
		double mean = 0, sum = 0;
		for (size_t i = 0; i < n; i++)
			mean += x[i];
		mean /= (double)n;
		for (size_t i = 0; i < n; i++) {
			sum += x[i] - mean;
			x[i] = sum;
		}
	}
	double lo = x[0], hi = x[0];
	for (size_t i = 1; i < n; i++) {
		if (x[i] < lo)
			lo = x[i];
		if (x[i] > hi)
			hi = x[i];
	}
	if (!(hi - lo > 1e-9)) {
		free(x);
		return PROSE_ERR_WAV_SILENT;
	}
	/* resample the period to the table's length (linear interpolation) and scale it to 0..0x7FFF */
	for (int k = 0; k < PROSE_SYNTH_PULSE_LEN; k++) {
		double t = (double)k * (double)n / PROSE_SYNTH_PULSE_LEN;
		size_t i = (size_t)t;
		double a = x[i], b = i + 1 < n ? x[i + 1] : x[i];
		double v = (a + (b - a) * (t - (double)i) - lo) / (hi - lo);
		out[k] = (int16_t)(v * 32767 + 0.5);
	}
	free(x);
	return 0;
}

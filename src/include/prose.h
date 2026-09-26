/* prose.h: the Prose 2000 speech DLL (prose.dll on Windows, libprose.so on Linux). See API.md.
 *
 * Audio is 16-bit signed mono at 10,000 Hz, the Prose's own rate; the library never resamples. */
#ifndef PROSE_H
#define PROSE_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#ifdef PROSE_BUILD_DLL
#define PROSE_API __declspec(dllexport)
#else
#define PROSE_API __declspec(dllimport)
#endif
#define PROSE_CALL __cdecl
#else
#if defined(PROSE_BUILD_DLL) && defined(__GNUC__)
#define PROSE_API __attribute__((visibility("default")))
#else
#define PROSE_API
#endif
#define PROSE_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define PROSE_SAMPLE_RATE 10000

typedef struct prose_handle *prose_h;

/* firmware versions for prose_open */
enum { PROSE_V341 = 341, PROSE_V11 = 11 };

/* error codes: functions that can fail return 0 or more on success */
enum {
	PROSE_OK = 0,
	PROSE_ERR_HANDLE = -1,        /* NULL, closed or unknown handle */
	PROSE_ERR_ARG = -2,           /* NULL pointer, unknown parameter or version */
	PROSE_ERR_BUSY = -3,          /* the handle is already running a job */
	PROSE_ERR_REENTRANT = -4,     /* a blocking call from inside the handle's own callback */
	PROSE_ERR_MEMORY = -5,
	PROSE_ERR_TEXT_TOO_LONG = -10,
	PROSE_ERR_FILE_OPEN = -20,
	PROSE_ERR_FILE_IO = -21,
	PROSE_ERR_WAV_FORMAT = -22,   /* not 16-bit mono PCM */
	PROSE_ERR_WAV_RATE = -23,     /* not 10,000 Hz */
	PROSE_ERR_WAV_LENGTH = -24,   /* empty or over 1 s */
	PROSE_ERR_WAV_SILENT = -25,
	PROSE_ERR_AUDIO_OPEN = -30,
	PROSE_ERR_AUDIO_WRITE = -31,
	PROSE_ERR_FIRMWARE = -40      /* the firmware's own fatal error; see prose_last_firmware_error */
};

/* frame parameters: one byte each per 10 ms frame */
enum {
	PROSE_AV, PROSE_AF, PROSE_AH,
	PROSE_A2, PROSE_A3, PROSE_A4, PROSE_A5, PROSE_A6, PROSE_AB,
	PROSE_F1, PROSE_F2, PROSE_F3, PROSE_F4,
	PROSE_B1, PROSE_B2, PROSE_B3,
	PROSE_FN, PROSE_F0,
	PROSE_SOURCE, PROSE_SOURCE_GAIN, PROSE_JITTER, PROSE_SHIMMER, /* v3.4.1 only */
	PROSE_PARAM_MAX
};

/* kinds of glottal waveform for prose_load_glottal_wave */
enum { PROSE_GLOTTAL_FLOW, PROSE_GLOTTAL_DERIVATIVE };

/* ---- callbacks: `user` is the caller's own pointer, passed back unchanged ---- */

typedef struct {
	void (PROSE_CALL *on_index)(prose_h h, int n, uint32_t position, void *user);
	void (PROSE_CALL *on_done)(prose_h h, int last_index, uint32_t total, void *user);
	void (PROSE_CALL *on_phoneme)(prose_h h, char ph, int ms, uint32_t position, void *user);
	void (PROSE_CALL *on_params)(prose_h h, const uint8_t *p, uint32_t position, void *user);
} prose_callbacks;

/* audio from prose_speak_to_buffer; return 0 to continue, nonzero to stop */
typedef int (PROSE_CALL *prose_audio_cb)(prose_h h, const int16_t *pcm, size_t count, uint32_t position,
                                         void *user);
/* after each frame of prose_render_frames; return 0 to continue, nonzero to stop */
typedef int (PROSE_CALL *prose_frame_cb)(prose_h h, int frame, const int16_t *pcm, int count, void *user);

/* ---- handles and information ---- */

PROSE_API int PROSE_CALL prose_open(prose_h *h, int version);
PROSE_API void PROSE_CALL prose_close(prose_h h);
PROSE_API int PROSE_CALL prose_get_version(prose_h h);
PROSE_API void PROSE_CALL prose_set_callbacks(prose_h h, const prose_callbacks *cb, void *user);
PROSE_API const char *PROSE_CALL prose_error_string(int code);
PROSE_API int PROSE_CALL prose_last_firmware_error(prose_h h);

/* ---- settings: take effect from the next text; out-of-range values are clamped ---- */

PROSE_API void PROSE_CALL prose_set_voice(prose_h h, int n);             /* 0-2; v3.4.1 only */
PROSE_API void PROSE_CALL prose_set_rate(prose_h h, int wpm);            /* 50-250 */
PROSE_API void PROSE_CALL prose_set_pitch(prose_h h, int n);             /* 50-200, or 0 */
PROSE_API void PROSE_CALL prose_set_volume(prose_h h, int n);            /* 0-15, larger = louder */
PROSE_API void PROSE_CALL prose_set_word_mode(prose_h h, int on);
PROSE_API void PROSE_CALL prose_set_fast_read(prose_h h, int n);         /* 0-9; v3.4.1 only */
PROSE_API void PROSE_CALL prose_set_speak_punctuation(prose_h h, int on); /* v3.4.1 only */

/* ---- speaking ---- */

PROSE_API int PROSE_CALL prose_speak(prose_h h, const char *text);      /* returns at once */
PROSE_API int PROSE_CALL prose_speak_to_wave(prose_h h, const char *filename, const char *text);
PROSE_API int32_t PROSE_CALL prose_speak_to_buffer(prose_h h, const char *text, int16_t *buf, size_t buf_samples,
                                                   prose_audio_cb on_audio, void *user); /* samples, or an error */
PROSE_API int PROSE_CALL prose_text_to_phoneme(prose_h h, const char *text, char *out, size_t out_size);
PROSE_API void PROSE_CALL prose_index(prose_h h, int n);                 /* 1-255 */
PROSE_API void PROSE_CALL prose_stop(prose_h h);
PROSE_API void PROSE_CALL prose_pause(prose_h h);
PROSE_API void PROSE_CALL prose_resume(prose_h h);
PROSE_API void PROSE_CALL prose_reset(prose_h h);

/* ---- raw synthesis ---- */

PROSE_API void PROSE_CALL prose_set_frame_param(prose_h h, int p, int value);
PROSE_API int PROSE_CALL prose_render_frames(prose_h h, int count, prose_frame_cb on_frame, void *user);
PROSE_API int PROSE_CALL prose_save_wave(const char *filename, const int16_t *pcm, size_t count);

/* ---- parameter information ---- */

PROSE_API int PROSE_CALL prose_param_count(prose_h h);                    /* 22 or 18 */
PROSE_API const char *PROSE_CALL prose_param_name(prose_h h, int p);     /* "AV" ... "F0", or NULL */
PROSE_API int PROSE_CALL prose_param_index(prose_h h, const char *name); /* -1 if unknown */
PROSE_API double PROSE_CALL prose_param_value(prose_h h, int p, int raw); /* byte -> Hz or dB */
PROSE_API int PROSE_CALL prose_param_raw(prose_h h, int p, double value); /* Hz or dB -> byte */

/* ---- custom glottal pulse ---- */

PROSE_API int PROSE_CALL prose_load_glottal_wave(prose_h h, const char *filename, int kind);
PROSE_API void PROSE_CALL prose_use_custom_glottal(prose_h h, int on);

#ifdef __cplusplus
}
#endif

#endif

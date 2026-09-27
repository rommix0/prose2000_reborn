# Prose speech library API

The speech library around the C decompilation in `src/`: `prose.dll` on Windows, `libprose.so` on Linux. The header is
[`src/include/prose.h`](src/include/prose.h); the CMake project in `src/` builds the library, `prose_say`
and the samples (README). The examples below are runnable programs in `samples/`. How the
library drives the firmware: [Implementation](#implementation).

## Conventions

- Every export starts with `prose_`. Plain `open`, `close` and `index` would clash with C runtime functions.
- `prose_h` is an opaque handle. One handle runs one job at a time; open a second handle for parallel work.
- **Audio is 16-bit signed mono at 10,000 Hz**, the Prose's own rate. The DLL never resamples.
- A handle opens either firmware: `PROSE_V341` (v3.4.1) or `PROSE_V11` (v1.1). Every function is exported for both.
  A function v1.1 has no equivalent for does nothing and returns nothing (marked **v3.4.1 only** below).
- Callbacks are functions the program writes and registers. They carry a `void *user` pointer (see below).
- **Calling convention:** every export and every callback is `__cdecl` on Windows, so C, Python's `ctypes.CDLL` and
  C# P/Invoke can all call it and pass callbacks in.
- **Errors:** functions that can fail return 0 or more on success and a negative `PROSE_ERR_*` code on failure (see
  [Error codes](#error-codes)). Setters and the other `void` functions cannot fail: they clamp or ignore bad values.

## Functions

### Handles and information

| Function | Notes |
|---|---|
| `prose_open(prose_h *h, int version)` | `PROSE_V341` or `PROSE_V11`. Returns 0 or an error |
| `prose_close(prose_h h)` | |
| `prose_get_version(prose_h h)` | the firmware the handle runs: `PROSE_V341` (341) or `PROSE_V11` (11). v3.4.1's own `ESC[E` identity is 34 |
| `prose_set_callbacks(prose_h h, const prose_callbacks *cb, void *user)` | the handle's event callbacks |
| `prose_error_string(int code)` | a short English message for an error code, e.g. `"WAV file is not 10000 Hz"` |
| `prose_last_firmware_error(prose_h h)` | the firmware's own number for its last fatal error on this handle (0x21, 0x22…), or 0 |

### Settings

Each setter queues the matching escape command ahead of the next text, so it takes effect from that point.

| Function | Escape | Range, default | Notes |
|---|---|---|---|
| `prose_set_voice(h, n)` | `nV` | 0–2, 0 | **v3.4.1 only** |
| `prose_set_rate(h, wpm)` | `nr` | 50–250 wpm; 150 (v1.1: 160) | `ESC[v` is the same control, so it has no separate function |
| `prose_set_pitch(h, n)` | `np` | 50–200; 85 (v1.1: 75) | 0 = F0 0: whispered (AV moves to aspiration) in both versions |
| `prose_set_volume(h, n)` | `na` | 0–15, larger = **louder** | inverted from `ESC[a`, where larger is quieter |
| `prose_set_word_mode(h, on)` | `nP` | off | on = word/list reading |
| `prose_set_fast_read(h, n)` | `nf` | 0–9, 0 | caps the speed. **v3.4.1 only** |
| `prose_set_speak_punctuation(h, on)` | `2N` / `2F` | off | says "comma", "period"… **v3.4.1 only** |

### Speaking

| Function | Notes |
|---|---|
| `prose_speak(h, text)` | returns at once (0 or an error); plays through the sound card on the DLL's audio thread. Calls queue up and play in order |
| `prose_speak_to_wave(h, filename, text)` | blocks until done; writes a 10 kHz WAV file. Returns 0 or an error |
| `prose_speak_to_buffer(h, text, buf, buf_samples, on_audio, user)` | blocks until done; audio through a callback (below). Returns the number of samples as an `int32_t` (enough for about 59 hours at 10 kHz), or an error |
| `prose_text_to_phoneme(h, text, out, out_size)` | writes the phoneme string (e.g. `"HeLO1 ."`); returns its full length, as `snprintf` does, so call again with a bigger buffer if the result is ≥ `out_size`; or an error |
| `prose_index(h, n)` | queues marker `n` (1–255); `on_index` fires when it is reached |
| `prose_stop(h)` | drops everything queued (`ESC[S`); `on_done` still fires |
| `prose_pause(h)` / `prose_resume(h)` | hold the output at once: the sound device pauses and synthesis waits (a blocking call waits inside). `prose_stop` and `prose_reset` also resume |
| `prose_reset(h)` | the only reset: settings to defaults, everything queued dropped |

Text may also contain the Prose's own escape sequences (`ESC[n letter`), for example an index marker in mid-text.
Each call is one utterance, spoken to its end even without final punctuation. The serial line is 7-bit, so bytes
above 0x7F (UTF-8 and the like) and control characters other than ESC, CR, LF and TAB are sent as spaces. Avoid
`ESC[x` in the text: while one is pending, the firmware ends the utterance at the next index marker (REFERENCE §8.2).
If the text has one anyway, the DLL waits for its reply before sending the rest, since text that arrives earlier is
discarded.

### Raw synthesis

| Function | Notes |
|---|---|
| `prose_set_frame_param(h, p, value)` | sets parameter `p` (table below) for the following frames |
| `prose_render_frames(h, count, on_frame, user)` | synthesizes `count` 10 ms frames of the current parameters; blocks. Returns the number of frames rendered, or an error |
| `prose_save_wave(filename, pcm, count)` | writes samples to a 10 kHz WAV file. Returns 0 or an error |

These feed the frame builder and the DSP directly, not the firmware's `ESC[l` / `ESC[g` hold (which is imprecise and
waits behind queued speech). Parameters keep their values until changed; unset ones keep their defaults. A v1.1
handle's frames also go through v3.4.1's frame builder, with FN converted to its coding and voice 0's source
settings for p18-p21, as v1.1's speech does.

### Parameter information

For the `on_params` callback and for reading or writing parameter files. `p` is a parameter number (table below).

| Function | Notes |
|---|---|
| `prose_param_count(h)` | 22 for v3.4.1, 18 for v1.1 |
| `prose_param_name(h, p)` | column name: `"AV"`, `"AF"`, … `"F0"` (the constants without `PROSE_`) |
| `prose_param_index(h, name)` | the reverse; −1 for an unknown name |
| `prose_param_value(h, p, raw)` | a raw byte in physical units: Hz for frequencies and bandwidths, dB for amplitudes; raw for p18-p21 |
| `prose_param_raw(h, p, value)` | the reverse, rounded and clamped to the parameter's range |

The conversions follow the codings in the frame-parameter table, including v1.1's different FN coding, so a file in
physical units reads the same for both versions.

### Custom glottal pulse

| Function | Notes |
|---|---|
| `prose_load_glottal_wave(h, filename, kind)` | loads one period of a glottal waveform from a WAV file |
| `prose_use_custom_glottal(h, on)` | switches between the custom pulse and the Prose's own; off by default |

- **The file:** 16-bit mono PCM at 10,000 Hz, up to 1 second long. The whole file is **one period**: it is
  normalized and resampled to 256 samples (the length of the DSP's pulse table), and the pitch still comes from F0. A longer file therefore
  holds a more finely sampled period, not several periods.
- **`kind`:** `PROSE_GLOTTAL_FLOW` for a glottal flow waveform (what the Prose's own table holds), or
  `PROSE_GLOTTAL_DERIVATIVE` for its derivative, which the DLL integrates first.
- **Returns** 0, or `PROSE_ERR_FILE_OPEN`, `PROSE_ERR_FILE_IO`, `PROSE_ERR_WAV_FORMAT` (not 16-bit mono PCM),
  `PROSE_ERR_WAV_RATE` (not 10 kHz), `PROSE_ERR_WAV_LENGTH` (empty or over 1 s) or `PROSE_ERR_WAV_SILENT`. On an
  error the custom period loaded before, if any, stays in place.
- **The Prose's own table is never changed.** The custom period is kept in a separate array, and the switch picks
  which one the DSP model reads. Loading a new file replaces only the custom period; the switch stays as it was.
- In custom mode each pitch period plays the custom period stretched to the period's length, scaled by AV. The rest
  of the model is unchanged: the excitation is still the first difference of the flow, and aspiration, the glottal
  filter and the resonators work as before. The Prose's opening and closing shape (the source parameters p18 and
  p19) has no effect while the custom period is on.
- It works for both versions, since v1.1 also plays through the v3.12 DSP model, and for speech and raw synthesis
  alike.

## Error codes

Grouped by range, so the kind of failure shows at a glance. `prose_error_string` gives a message for each.

| Code | Name | When |
|---|---|---|
| 0 | `PROSE_OK` | success |
| **Calls** | | |
| −1 | `PROSE_ERR_HANDLE` | NULL, closed or unknown handle |
| −2 | `PROSE_ERR_ARG` | NULL pointer, unknown parameter number, or unknown firmware version in `prose_open` |
| −3 | `PROSE_ERR_BUSY` | the handle is already running a job (e.g. `prose_speak_to_buffer` while `prose_speak` plays) |
| −4 | `PROSE_ERR_REENTRANT` | a blocking call made from inside one of the handle's own callbacks, which would deadlock |
| −5 | `PROSE_ERR_MEMORY` | out of memory |
| **Text** | | |
| −10 | `PROSE_ERR_TEXT_TOO_LONG` | more than 1,000,000 characters in one call |
| **Files** | | |
| −20 | `PROSE_ERR_FILE_OPEN` | cannot open or create the file |
| −21 | `PROSE_ERR_FILE_IO` | a read or write failed, or the disk is full |
| −22 | `PROSE_ERR_WAV_FORMAT` | not a PCM WAV file, or not 16-bit mono |
| −23 | `PROSE_ERR_WAV_RATE` | not 10,000 Hz (the DLL never resamples) |
| −24 | `PROSE_ERR_WAV_LENGTH` | longer than 1 second, or empty |
| −25 | `PROSE_ERR_WAV_SILENT` | every sample is zero, so there is no pulse to normalize |
| **Audio** | | |
| −30 | `PROSE_ERR_AUDIO_OPEN` | no sound device, or it refuses 10 kHz 16-bit mono |
| −31 | `PROSE_ERR_AUDIO_WRITE` | the device failed during playback, e.g. unplugged |
| **Firmware** | | |
| −40 | `PROSE_ERR_FIRMWARE` | the decompiled firmware hit one of its own fatal errors (below) |

**Not errors:**
- Out-of-range settings are clamped; the setters return nothing.
- Functions v1.1 lacks do nothing and return nothing.
- `prose_stop`, `prose_pause` and `prose_resume` with nothing playing simply return.
- A `prose_speak_to_buffer` or `prose_render_frames` stopped by its callback or by `prose_stop` returns what it
  produced so far; `on_done` still fires.
- `prose_text_to_phoneme` with a small buffer returns the full length, as `snprintf` does.
- `prose_close` from inside one of the handle's own callbacks does nothing, since it cannot wait for itself.

**Firmware fatal errors.** The firmware has its own fatal-error path, which restarts the board. The DLL does the same:
it restarts the handle's firmware (settings back to defaults), drops the current job, fires `on_done`, and keeps the
firmware's error number for `prose_last_firmware_error`. A blocking call returns `PROSE_ERR_FIRMWARE`. `prose_speak`
has already returned by then, so a program that cares checks `prose_last_firmware_error` in its `on_done`. None has
been seen on real input so far; this is for robustness and bug reports.

## Callbacks

```c
typedef struct {
	void (*on_index)(prose_h h, int n, uint32_t position, void *user);           /* marker n reached */
	void (*on_done)(prose_h h, int last_index, uint32_t total, void *user);      /* utterance finished */
	void (*on_phoneme)(prose_h h, char ph, int ms, uint32_t position, void *user); /* phoneme and its length */
	void (*on_params)(prose_h h, const uint8_t *p, uint32_t position, void *user); /* one frame's parameters */
} prose_callbacks;

typedef int (*prose_audio_cb)(prose_h h, const int16_t *pcm, size_t count, uint32_t position, void *user);
typedef int (*prose_frame_cb)(prose_h h, int frame, const int16_t *pcm, int count, void *user);
```

- `position` and `total` count samples from the start of the utterance, as a `uint32_t` (enough for about 119
  hours of audio). Events fire in order, just before the audio that contains them is delivered (buffer mode) or
  played (`prose_speak`). A frame's audio starts one frame after the firmware builds it, so the first frame of an
  utterance is at position 100 (10 ms).
- `on_done`'s `last_index` is the last index marker reached in the utterance, or 0.
- `prose_audio_cb` and `prose_frame_cb` return 0 to continue, nonzero to stop.
- `prose_speak` callbacks run on the DLL's audio thread; data they share with other threads needs locking.
- `on_phoneme` comes from the firmware's playback stage, for both versions: each phoneme with `position` where its
  segment starts and `ms` its length, so in `prose_speak` it fires as the phoneme starts to play (a lip-sync example
  is below). Pauses come as `' '`. The firmware reports a segment only once it has been played to its end, so the DLL
  dates the event back by the segment's length and holds the audio back by up to one phoneme until then (verified
  against the voicing of the parameter frames, 2026-09-27; before that fix the event came at the segment's end with
  the next phoneme's length).
  `prose_text_to_phoneme` uses the firmware's phoneme echo on v3.4.1 (with stress marks and punctuation, as
  `ESC[16N` sends it) and the played segments on v1.1, which has no echo (phonemes only).
- `on_params` fires once per 10 ms frame with the frame's raw parameter bytes (`prose_param_count(h)` of them, in
  the frame-parameter order), during speech in any mode and during `prose_render_frames`. It fires when the frame is
  built, and `position` is where that frame's audio starts. The bytes are only valid during the call.
- **For reference synthesizers** such as Klatt's klsyn: the tracks hold the Prose's variable parameters only. F5, B4,
  the nasal pole (about 250 Hz) and the parallel-branch bandwidths are fixed per voice and need adding as constants.
  The amplitudes use the Prose's dB coding; that it matches Klatt's scale is likely (MITalk lineage) but unverified.

### The user pointer

`user` is a plain `void *`, not a type from `prose.h`. The DLL stores it, passes it back unchanged, and never reads
it. It lets callbacks reach the program's own data without globals: several handles can share one callback, and an
adapter passes its own object (a SAPI engine, a Python driver through `ctypes` as `c_void_p`, C# as `IntPtr`).
`NULL` is fine when a callback needs nothing. What it points to must stay valid while callbacks can still arrive:
until `on_done`, until a blocking call returns, or until `prose_close`.

`prose_set_callbacks` takes one pointer for the handle's event callbacks. Calls with their own callback
(`prose_render_frames`, `prose_speak_to_buffer`) take their own pointer as the last argument.

## Examples

### Speaking with events

```c
typedef struct {
	int last_index;
	int done;
} app_state;

static void my_index(prose_h h, int n, uint32_t pos, void *user)
{
	app_state *s = user;
	s->last_index = n;
}

static void my_done(prose_h h, int last_index, uint32_t total, void *user)
{
	app_state *s = user;
	s->done = 1;
}

static app_state state;          /* must outlive the callbacks */

prose_h h;
prose_open(&h, PROSE_V341);
prose_set_callbacks(h, &(prose_callbacks){ .on_index = my_index, .on_done = my_done }, &state);

prose_set_rate(h, 180);
prose_set_volume(h, 12);
prose_speak(h, "Hello there.");
prose_index(h, 5);
prose_speak(h, "Second sentence.");

prose_speak_to_wave(h, "out.wav", "Saved to a file.");

char ph[256];
int need = prose_text_to_phoneme(h, "Hello.", ph, sizeof ph);

prose_close(h);
```

### Grabbing audio with `prose_speak_to_buffer`

The program lends a buffer; the DLL fills it and calls `on_audio` each time it is full, and once more at the end with
a partly filled buffer. The buffer size sets the chunk length: 100 samples is one Prose frame (10 ms); `NULL` / `0`
lets the DLL use 10 ms chunks. Synthesis runs as fast as the CPU allows, not in real time. The call returns the total
number of samples, or a negative error code.

```c
typedef struct {
	my_output *out;          /* wherever the audio goes: SAPI site, file, network */
	volatile int cancelled;  /* set by another thread to abort */
} job;

static int on_audio(prose_h h, const int16_t *pcm, size_t count, uint32_t pos, void *user)
{
	job *j = user;
	my_output_write(j->out, pcm, count);     /* copy it out: the buffer is reused for the next chunk */
	return j->cancelled;                     /* nonzero stops synthesis */
}

static void on_index(prose_h h, int n, uint32_t pos, void *user)
{
	job *j = user;
	my_output_bookmark(j->out, n, pos * 2);  /* SAPI wants a byte offset: 2 bytes per sample */
}

int16_t buf[1000];                           /* 100 ms chunks */
job j = { out, 0 };
prose_set_callbacks(h, &(prose_callbacks){ .on_index = on_index }, &j);
int32_t total = prose_speak_to_buffer(h, "Hello \x1B[1i world.", buf, 1000, on_audio, &j);
```

Stopping: return nonzero from `on_audio`, or call `prose_stop(h)` from another thread; the call returns early and
`on_done` still fires. Settings and in-text escapes apply as for `prose_speak`.

### Raw synthesis: an "ah" gliding to "ee"

```c
#define FRAMES 30                        /* 30 x 10 ms = 0.3 s */

typedef struct {
	int16_t pcm[FRAMES * 100];           /* 100 samples per frame */
} glide;

static int lerp(int a, int b, int k) { return a + (b - a) * k / FRAMES; }

/* after each frame: keep its audio, then set up the next frame */
static int on_frame(prose_h h, int frame, const int16_t *pcm, int count, void *user)
{
	glide *g = user;
	memcpy(g->pcm + frame * 100, pcm, count * sizeof *pcm);
	int k = frame + 1;
	prose_set_frame_param(h, PROSE_F1, lerp(175, 75, k));   /* 700 -> 300 Hz */
	prose_set_frame_param(h, PROSE_F2, lerp(87, 212, k));   /* 1200 -> 2200 Hz */
	prose_set_frame_param(h, PROSE_F3, lerp(156, 181, k));  /* 2500 -> 2900 Hz */
	prose_set_frame_param(h, PROSE_F0, lerp(130, 100, k));  /* pitch 130 -> 100 Hz */
	return 0;
}

static glide g;

prose_set_frame_param(h, PROSE_AV, 60);  /* frame 0: voiced "ah" */
prose_set_frame_param(h, PROSE_F1, 175);
prose_set_frame_param(h, PROSE_F2, 87);
prose_set_frame_param(h, PROSE_F3, 156);
prose_set_frame_param(h, PROSE_F0, 130);
prose_render_frames(h, FRAMES, on_frame, &g);
prose_save_wave("glide.wav", g.pcm, FRAMES * 100);
```

A fricative such as "s" sets AV to 0 and uses AF with A4–A6; silence is AV, AF and AH all 0.

### Extracting parameters to a CSV file

One row per 10 ms frame, in physical units, with the parameter names as the header. No audio is needed, so the
text goes through `prose_speak_to_buffer` with a callback that discards it.

```c
static void on_params(prose_h h, const uint8_t *p, uint32_t pos, void *user)
{
	FILE *f = user;
	fprintf(f, "%lu", (unsigned long)(pos / 10));            /* time in ms: 10 samples per ms */
	for (int i = 0; i < prose_param_count(h); i++)
		fprintf(f, ",%g", prose_param_value(h, i, p[i]));    /* Hz or dB */
	fputc('\n', f);
}

static int discard_audio(prose_h h, const int16_t *pcm, size_t count, uint32_t pos, void *user)
{
	return 0;
}

FILE *f = fopen("hello.csv", "w");
fprintf(f, "time_ms");
for (int i = 0; i < prose_param_count(h); i++)
	fprintf(f, ",%s", prose_param_name(h, i));               /* AV,AF,AH,A2,...,F1,F2,...,F0 */
fputc('\n', f);
prose_set_callbacks(h, &(prose_callbacks){ .on_params = on_params }, f);
prose_speak_to_buffer(h, "Hello there.", NULL, 0, discard_audio, NULL);
fclose(f);
```

The first row is the silent frame before speech (v3.4.1):

```
time_ms,AV,AF,AH,A2,A3,A4,A5,A6,AB,F1,F2,F3,F4,B1,B2,B3,FN,F0,SOURCE,SOURCE_GAIN,JITTER,SHIMMER
10,0,0,0,60,60,60,60,60,0,400,1396,2400,3296,140,90,110,248,100,16,8,0,0
```

### Synthesizing from a CSV file

The same file, read back one row per frame through the raw path. The header decides which column sets which
parameter, so a file may hold any subset of the columns, in any order.

```c
typedef struct {
	FILE *csv;
	int col[32];                 /* parameter number of each column after time_ms, or -1 to skip it */
	int n;                       /* number of those columns */
	int16_t *pcm;                /* where the audio goes */
} reader;

/* load the next row into the frame parameters; 0 at the end of the file */
static int next_row(prose_h h, reader *r)
{
	double v;
	if (fscanf(r->csv, " %*lf") == EOF)                       /* skip time_ms */
		return 0;
	for (int i = 0; i < r->n; i++)
		if (fscanf(r->csv, ",%lf", &v) == 1 && r->col[i] >= 0)
			prose_set_frame_param(h, r->col[i], prose_param_raw(h, r->col[i], v));
	return 1;
}

static int on_frame(prose_h h, int frame, const int16_t *pcm, int count, void *user)
{
	reader *r = user;
	memcpy(r->pcm + (size_t)frame * 100, pcm, count * sizeof *pcm);
	return !next_row(h, r);                                  /* nonzero stops at the end of the file */
}

reader r = { fopen("hello.csv", "r") };
char line[1024], *name;
fgets(line, sizeof line, r.csv);                             /* the header */
strtok(line, ",\r\n");                                       /* time_ms */
while ((name = strtok(NULL, ",\r\n")) && r.n < 32)
	r.col[r.n++] = prose_param_index(h, name);

r.pcm = malloc(max_frames * 100 * sizeof *r.pcm);
if (next_row(h, &r))                                         /* row 0 is the first frame */
	prose_render_frames(h, max_frames, on_frame, &r);
```

`max_frames` is an upper bound (the number of data lines in the file); rendering stops at the last row.

### A custom glottal pulse

`pulse.wav` holds one period of a glottal flow waveform (16-bit mono, 10 kHz, at most 1 s).

```c
if (prose_load_glottal_wave(h, "pulse.wav", PROSE_GLOTTAL_FLOW) == 0)
	prose_use_custom_glottal(h, 1);

prose_speak_to_wave(h, "custom.wav", "This voice uses a custom glottal pulse.");

prose_use_custom_glottal(h, 0);                              /* back to the Prose's own pulse */
prose_speak_to_wave(h, "original.wav", "This one uses the original.");
```

A period for testing can be made in code, for example a Rosenberg pulse: 40 % rising, 16 % falling, then closed.

```c
enum { N = 100 };                                            /* one 10 ms period at 10 kHz */
int16_t period[N];
for (int i = 0; i < N; i++) {
	double t = (double)i / N, open = 0.40, close = 0.16;
	double g = t < open ? 0.5 * (1 - cos(M_PI * t / open))
	         : t < open + close ? cos(M_PI / 2 * (t - open) / close) : 0;
	period[i] = (int16_t)(g * 32000);
}
prose_save_wave("pulse.wav", period, N);
```

### Lip sync with `on_phoneme`

`on_phoneme` in `prose_speak` fires as each phoneme starts to play, which is what an animated face needs. The sample
(`samples/lip_sync.c`) maps the Prose phonemes (below) to the ten Preston Blair mouth shapes that lip-sync tools such
as Papagayo use, and redraws a mouth in the terminal. Without a sound device it prints the timeline of shapes instead.

```c
enum { REST, MBP, FV, L, WQ, U, O, E, AI, ETC };

static int viseme_of(char ph)
{
	switch (ph) {
	case 'P': case 'B': case 'M': case 'm': return MBP;
	case 'F': case 'V': return FV;
	case 'L': case 'j': case 'l': return L;
	case 'W': case 'h': return WQ;
	case 'u': case 'b': case 'U': case 'c': return U;
	case 'O': case 'w': case 'g': case 'y': case 'f': return O;
	case 'E': case 'A': case 'e': case 'k': case '3': return E;
	case 'a': case 'o': case 'v': case '@': case 'i': case '|': case 'I': case 'r': case '4': return AI;
	case ' ': return REST;
	default: return ETC; /* the other consonants */
	}
}

/* on the DLL's audio thread: a GUI program posts the shape to its UI thread here */
static void on_phoneme(prose_h h, char ph, int ms, uint32_t pos, void *user)
{
	show_mouth(user, viseme_of(ph));
}

prose_set_callbacks(h, &(prose_callbacks){ .on_phoneme = on_phoneme, .on_done = on_done }, &state);
prose_speak(h, "Hello, my friend. How are you today?");
```

For an animation made offline, call `prose_speak_to_buffer` instead: the events then come at once, and `pos` (in
samples) and `ms` give each shape's start and length.

### Python

`samples/python/` has each example above in Python, byte-for-byte the same output as the C samples. `prose.py` there
declares the exports and callback types for `ctypes`, so the calls read as in C:

```python
import ctypes
from prose import *                       # lib, the constants, prose_callbacks, ON_INDEX ... FRAME_CB

h = prose_h()
lib.prose_open(ctypes.byref(h), PROSE_V341)
cb = prose_callbacks(on_done=ON_DONE(lambda h, last, total, user: print(total, "samples")))
lib.prose_set_callbacks(h, ctypes.byref(cb), None)
lib.prose_speak_to_wave(h, b"out.wav", b"Hello from Python.")
lib.prose_close(h)
```

- Text and file names are `bytes`. The 7-bit rule of Speaking still applies.
- Keep every callback object (and the `prose_callbacks` structure) referenced while it can fire; `ctypes` does not,
  and a collected callback crashes the process.
- `user` is unnecessary in Python, where a closure or bound method carries the state: pass `None`.
- Python and the library must have the same bitness (README).

## Frame parameters

One byte each per 10 ms frame, in the Prose's own coding (REFERENCE §11.4). Defaults and limits are the ROM tables
behind `ESC[l`.

| p | Constant | Name | Coding | v3.4.1 default / max | v1.1 default / max |
|---|---|---|---|---|---|
| 0 | `PROSE_AV` | voicing amplitude | dB | 60 / 80 | 60 / 60 |
| 1 | `PROSE_AF` | frication amplitude (turns on the parallel branch) | dB | 0 / 80 | 0 / 60 |
| 2 | `PROSE_AH` | aspiration amplitude | dB | 0 / 80 | 0 / 60 |
| 3–7 | `PROSE_A2`–`PROSE_A6` | parallel formant amplitudes | dB | 0 / 80 | 0 / 80 |
| 8 | `PROSE_AB` | bypass amplitude | dB | 0 / 80 | 0 / 80 |
| 9 | `PROSE_F1` | first formant | Hz / 4 | 100 (400 Hz) / 255 | same |
| 10 | `PROSE_F2` | second formant | (Hz − 500) / 8 | 113 (1404 Hz) / 255 | same |
| 11 | `PROSE_F3` | third formant | Hz / 16 | 144 (2304 Hz) / 255 | same |
| 12 | `PROSE_F4` | fourth formant | Hz / 16 | 206 (3296 Hz) / 255 | same |
| 13–15 | `PROSE_B1`–`PROSE_B3` | formant bandwidths | Hz / 2 | 50, 50, 70 / 204 | 50, 50, 70 / 255 |
| 16 | `PROSE_FN` | nasal zero | (Hz − 192) / 4 | 14 (248 Hz) / 255 | **Hz / 2**: 125 (250 Hz) / 255 |
| 17 | `PROSE_F0` | pitch | Hz | 50 / 255 | 50 / 255 |
| 18 | `PROSE_SOURCE` | voicing source type | raw | 16 / 31 | — |
| 19 | `PROSE_SOURCE_GAIN` | voicing source gain | raw | 8 / 15 | — |
| 20 | `PROSE_JITTER` | jitter (low nibble), attenuation (high nibble) | raw | 0 / 15 | — |
| 21 | `PROSE_SHIMMER` | shimmer (low nibble), voice (high nibble) | raw | 0 / 127 | — |

v1.1 has only p0–p17, and its FN coding differs; `prose_param_value` and `prose_param_raw` convert it, so FN in Hz
means the same for both.

## Phonemes

The Prose's one-character codes, used by phoneme input (`ESC[1I`) and returned by `on_phoneme` and
`prose_text_to_phoneme` (REFERENCE §12.3a, decoded from v3.4.1; v1.1's set is not yet checked code by code).

**Vowels (23):**

| Code | IPA | Example | Code | IPA | Example | Code | IPA | Example |
|---|---|---|---|---|---|---|---|---|
| `E` | i | beat | `a` | æ | bat | `f` | aʊ | bout |
| `i` | ɪ | bit | `o` | ɑ | pot | `y` | ɔɪ | boy |
| `A` | eɪ | bait | `w` | ɔ | bought | `U` | ju | cubic |
| `e` | ɛ | bet | `O` | oʊ | boat | `3` | ɝ / ɚ | bird, butter |
| `v` | ʌ | but | `u` | ʊ | book | `r` | ɑr | car |
| `@` | ə | about | `b` | u | boot | `g` | ɔr | core |
| `\|` | ɨ | roses | `I` | aɪ | bite | `k` | ɛr | fair |
| `4` | ɪr | fear | `c` | ʊr | poor | | | |

**Consonants (34) and silence:**

| Code | IPA | Example | Code | IPA | Example |
|---|---|---|---|---|---|
| `P` `B` | p b | pin, bin | `F` `V` | f v | fin, vim |
| `T` `D` | t d | tin, din | `X` `x` | θ ð | thin, then |
| `K` `G` | k g | kin, go | `S` `Z` | s z | sin, zoo |
| `t` | ɾ flap | water | `s` `z` | ʃ ʒ | shin, measure |
| `q` | glottalized t | button | `H` | h | hat |
| `C` | tʃ closure (+ `s`) | chin = `Cs…` | `d` | ɦ voiced h | ahead |
| `J` | dʒ closure (+ `z`) | gin = `Jz…` | `h` | ʍ wh (phoneme input only) | which |
| `M` `N` | m n | man, nap | `m` `n` | syllabic m, n | rhythm, button |
| `~` | ŋ | sing | `Q` | ʔ glottal stop | uh-oh |
| `L` | l before a vowel | let | `j` | ɫ dark l | feel |
| `l` | syllabic l | bottle | `W` | w | wet |
| `Y` | j | yet | `R` | r | red |
| `p` | release after a final consonant | `Tp` | space | silence | |

`h`, `Q` and `p` are inferred, not confirmed.

**Marks:** stress follows the vowel (`1` primary, `2` secondary, `"` emphatic); boundaries are `, . ? \ ]`; in
phoneme input, `&` starts a content word and `%` a function word.

## Escape commands left out

`t` (developer phoneme test), `K` (input mute), `q` (stop without a reply), the `A`/`D` flags (meaning unknown),
mode flags 10 and 15 (serial-protocol details), `L` and `b` (board reboot and baud rate), `X` and `c` (not supported
on the Prose), `w` and `W` (covered by `prose_reset`), `E` and `Q` (covered by `prose_get_version` and the DLL's own
copy of the settings).

## Implementation

The code is in `src/dll/`, apart from a few hooks in the decompiled files; it follows `src/tests/pipeline_play.c`
and `v1_pipeline_play.c`, the programs that were checked against the emulator.

| File | Role |
|---|---|
| `prose_api.c` | the exports: handles, settings, jobs, the audio thread, the sinks (sound device, WAV file, buffer), raw synthesis |
| `prose_engine.c` | runs one handle's firmware and DSP model: text in, replies and events out |
| `prose_eng3.c`, `prose_eng1.c` | the version drivers: the synthesis loop's scheduling and the firmware hooks |
| `prose_params.c` | parameter tables and units, raw frames, WAV files, the glottal pulse loader |
| `prose_sys.c` | threads and locks; the sound device (winmm; PulseAudio or ALSA through `dlopen`) |

- **Firmware state.** The decompiled firmware keeps its state in one data-segment image per version. Each handle
  keeps its own copy of the RAM part (0x3000 bytes) and its own DSP model, and the engine swaps a handle's RAM in
  under one global lock, 10 ms at a time. Several handles therefore speak at once, each exactly as a lone one does.
- **Timing.** The DSP model pulls: every 100 samples it asks for a frame, and the engine runs the firmware's loop until
  it would idle, then its frame interrupt. Synthesis runs as fast as the CPU allows (about 600 times real time for
  v3.4.1 and 90 for v1.1 on a 2020s PC); `prose_speak` paces it to the sound device.
- **Ending an utterance.** The text goes to the firmware's serial input, pausing while the firmware has sent XOFF,
  and ends with a CR rather than `ESC[x` (see Speaking). When the text is in, the pipeline is idle and the DSP has had
  no frame for 3 requests, the DLL sends `ESC[C` if the text does not end with `.` `?` or `!`: its phrase boundary
  releases a last phrase the firmware holds back for more text. At the next such point it sends `ESC[x`, and the
  utterance ends 3 frames after its reply. `prose_stop` sends `ESC[S` and runs the firmware until it is idle.
- **Sound device.** Windows: winmm. Linux: PulseAudio's simple API, else ALSA, loaded at run time; `PROSE_AUDIO=pulse`
  or `alsa` picks one. The device gets at most 150 ms ahead of what it has played, so stop and pause act at once.
- **Hooks in the decompiled code** (not firmware behaviour): `prose_synth_reset` / `prose_synth_continue` (the DSP
  model in pieces) and `prose_synth_set_custom_pulse`; `pg_segment_hook` and `v1_segment_hook` (a segment has been
  played); `v1_params_hook` (a v1.1 frame's parameter bytes); `pr_f0_hook` (the parts of each F0 target, for
  `pitch_trace`, PITCH_SYSTEM.md); `pg_trace_hook` (what the parameter generator does for each segment, for
  `formant_trace`, FORMANT_SYSTEM.md). The replay tests give the same results with them.
- **Checked** (2026-09-26): `prose_speak_to_wave` gives the same samples as `pipeline_play` / `v1_pipeline_play` for
  the same text (up to the end, where the DLL stops sooner); two handles of each version in four threads give the
  same audio as one handle alone; stop, pause, busy and re-entrant calls, settings, reset, parameter units and the WAV
  errors behave as described here. The output is identical on 64-bit and 32-bit Windows (MinGW; 64-bit checked
  2026-09-27) and 64-bit Linux (gcc), and the samples run on both systems, including speaker output through winmm,
  PulseAudio (WSLg) and ALSA.

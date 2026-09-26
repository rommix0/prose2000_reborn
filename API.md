# Prose DLL API (draft)

The planned DLL around the C decompilation in `src/`. Nothing here is implemented yet.

## Conventions

- Every export starts with `prose_`. Plain `open`, `close` and `index` would clash with C runtime functions.
- `prose_h` is an opaque handle. One handle runs one job at a time; open a second handle for parallel work.
- **Audio is 16-bit signed mono at 10,000 Hz**, the Prose's own rate. The DLL never resamples.
- A handle opens either firmware: `PROSE_V341` (v3.4.1) or `PROSE_V11` (v1.1). Every function is exported for both.
  A function v1.1 has no equivalent for does nothing and returns nothing (marked **v3.4.1 only** below).
- Callbacks are functions the program writes and registers. They carry a `void *user` pointer (see below).

## Functions

### Handles and information

| Function | Notes |
|---|---|
| `prose_open(prose_h *h, int version)` | `PROSE_V341` or `PROSE_V11` |
| `prose_close(prose_h h)` | |
| `prose_get_version()` | firmware version (v3.4.1's `ESC[E` answers 34) |
| `prose_set_callbacks(prose_h h, const prose_callbacks *cb, void *user)` | the handle's event callbacks |

### Settings

Each setter queues the matching escape command ahead of the next text, so it takes effect from that point.

| Function | Escape | Range, default | Notes |
|---|---|---|---|
| `prose_set_voice(h, n)` | `nV` | 0–2, 0 | **v3.4.1 only** |
| `prose_set_rate(h, wpm)` | `nr` | 50–250 wpm; 150 (v1.1: 160) | `ESC[v` is the same control, so it has no separate function |
| `prose_set_pitch(h, n)` | `np` | 50–200; 85 (v1.1: 75) | 0 = F0 0: v1.1 whispers, v3.4.1 is unvoiced |
| `prose_set_volume(h, n)` | `na` | 0–15, larger = **louder** | inverted from `ESC[a`, where larger is quieter |
| `prose_set_word_mode(h, on)` | `nP` | off | on = word/list reading |
| `prose_set_fast_read(h, n)` | `nf` | 0–9, 0 | caps the speed. **v3.4.1 only** |
| `prose_set_speak_punctuation(h, on)` | `2N` / `2F` | off | says "comma", "period"… **v3.4.1 only** |

### Speaking

| Function | Notes |
|---|---|
| `prose_speak(h, text)` | returns at once; plays through the sound card on the DLL's audio thread |
| `prose_speak_to_wave(h, filename, text)` | blocks until done; writes a 10 kHz WAV file |
| `prose_speak_to_buffer(h, text, buf, buf_samples, on_audio, user)` | blocks until done; audio through a callback (below) |
| `prose_text_to_phoneme(h, text, out, out_size)` | writes the phoneme string (e.g. `"HeLO1 ."`); returns its full length, as `snprintf` does, so call again with a bigger buffer if the result is ≥ `out_size` |
| `prose_index(h, n)` | queues marker `n` (1–255); `on_index` fires when it is reached |
| `prose_stop(h)` | drops everything queued (`ESC[S`); `on_done` still fires |
| `prose_pause(h)` / `prose_resume(h)` | `ESC[H` / `ESC[C` |
| `prose_reset(h)` | the only reset: settings to defaults, everything queued dropped |

Text may also contain the Prose's own escape sequences (`ESC[n letter`), for example an index marker in mid-text.
After an utterance, the DLL waits for the firmware's done reply before sending more, because text that arrives
earlier is discarded.

### Raw synthesis

| Function | Notes |
|---|---|
| `prose_set_frame_param(h, p, value)` | sets parameter `p` (table below) for the following frames |
| `prose_render_frames(h, count, on_frame, user)` | synthesizes `count` 10 ms frames of the current parameters; blocks |
| `prose_save_wave(filename, pcm, count)` | writes samples to a 10 kHz WAV file |

These feed the frame builder and the DSP directly, not the firmware's `ESC[l` / `ESC[g` hold (which is imprecise and
waits behind queued speech). Parameters keep their values until changed; unset ones keep their defaults.

## Callbacks

```c
typedef struct {
	void (*on_index)(prose_h h, int n, uint64_t position, void *user);           /* marker n reached */
	void (*on_done)(prose_h h, int last_index, uint64_t total, void *user);      /* utterance finished */
	void (*on_phoneme)(prose_h h, char ph, int ms, uint64_t position, void *user); /* phoneme and its length */
} prose_callbacks;

typedef int (*prose_audio_cb)(prose_h h, const int16_t *pcm, size_t count, uint64_t position, void *user);
typedef int (*prose_frame_cb)(prose_h h, int frame, const int16_t *pcm, int count, void *user);
```

- `position` and `total` count samples from the start of the utterance. Events fire in order, just before the audio
  that contains them is delivered (buffer mode) or played (`prose_speak`).
- `prose_audio_cb` and `prose_frame_cb` return 0 to continue, nonzero to stop.
- `prose_speak` callbacks run on the DLL's audio thread; data they share with other threads needs locking.
- v1.1 has no phoneme echo in its firmware; the DLL takes its phonemes from its own playback instead.

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

static void my_index(prose_h h, int n, uint64_t pos, void *user)
{
	app_state *s = user;
	s->last_index = n;
}

static void my_done(prose_h h, int last_index, uint64_t total, void *user)
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

static int on_audio(prose_h h, const int16_t *pcm, size_t count, uint64_t pos, void *user)
{
	job *j = user;
	my_output_write(j->out, pcm, count);     /* copy it out: the buffer is reused for the next chunk */
	return j->cancelled;                     /* nonzero stops synthesis */
}

static void on_index(prose_h h, int n, uint64_t pos, void *user)
{
	job *j = user;
	my_output_bookmark(j->out, n, pos * 2);  /* SAPI wants a byte offset: 2 bytes per sample */
}

int16_t buf[1000];                           /* 100 ms chunks */
job j = { out, 0 };
prose_set_callbacks(h, &(prose_callbacks){ .on_index = on_index }, &j);
long total = prose_speak_to_buffer(h, "Hello \x1B[1i world.", buf, 1000, on_audio, &j);
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

v1.1 has only p0–p17, and its FN coding differs; the DLL could hide that by taking FN in Hz.

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

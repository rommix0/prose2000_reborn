# The pitch (F0) system of the Prose 2000 v3.4.1

How the firmware decides the voice's fundamental frequency, from the host's pitch setting to the pitch periods the
DSP runs. It covers the declination line, the contour types, the accents and the hat pattern, the phrase endings,
and how the per-phoneme targets are smoothed into a 10 ms track.

Everything here comes from the C decompilation in `src/` (byte-exact against the emulator, REFERENCE §14). Tags:
**[verified: code]** is read from the decompiled code; **[verified: trace]** is also seen in a run of the C
pipeline (numbers below come from such runs); **[I]** is inferred. Addresses are Prose 2000 linear addresses and
`DS:x` offsets (DS = F410).

## 1. Overview

F0 is decided in four places:

| Layer | Code | Output |
|---|---|---|
| Host settings | escape parser, `stage_run_command` `D9D2B` | pitch `p`, voice `V`, word mode `P`, monotone flag, given F0 values, word-type marks |
| **Prosody stage** | `f0_target` `DBC84`, with `prosody_scan_phrase` `D9492`, `phrase_breaks` `DA101`, `accent_context` `DBA35` | one F0 **target per phoneme**, in node `+8` as F0/2 (so 2 Hz steps) |
| Parameter generator | `paramgen_segment` `DD703`, `pg_apply_loci` `E1C1A`, `pg_amplitude_boundaries` `E2535`, `param_emit_segment` `D3D2F` | the **F0 track**: one byte per 10 ms frame, in Hz (parameter p17, REFERENCE §11.4) |
| Frame builder and DSP | `dsp_build_frame` `D89CB`; DSP pitch clock (REFERENCE §13) | two jittered pitch periods per frame, in samples at 10 kHz |

The design is a declining **phrase line** from `pitch + fx` down to `pitch`, plus **accents** on stressed syllables,
a raised **hat** from the first accent to the phrase's **nucleus** (its last primary-stressed vowel), and a phrase
ending chosen by the punctuation: a fall for `.`, a slight rise for `,`, a jump up for a yes/no question. This is the
plan of MITalk's F0 rules (hat pattern with declination) [I]. The patent [216] says the prosodics rules depend on
"the phonetic form and the part of speech of the words", the rate and the prosody type selected [patent-claimed]. In
v3.4.1 the part of speech acts through the lexicon's word classes (function words lose stress, phrase breaks) and
through the word-type marks of §4.2.

## 2. Settings that reach the F0 rules

All are in-band: they apply from their position in the text on (REFERENCE §8.2). The prosody stage keeps its own
copy in its stage record `DS:C24A` (pitch at `+14` = `DS:C25E`, voice at `+20`, phrase kind at `+10`, mode flags at
`+1A`) [verified: code].

| Setting | Effect on F0 |
|---|---|
| `ESC[n p`, pitch 50-200 (default 85) | the bottom of the phrase line; everything else scales with it. **`0`** makes every target 0: the generator then moves AV's level to AH, so the speech is **whispered** (§5.4) [verified: trace] |
| `ESC[n V`, voice 0-2 | loads the voice's default pitch from `DS:5328` (**85, 75, 110** Hz) over the current pitch, and selects the excursion factor `DS:5378` (**1.0, 0.80, 0.90**, Q15) and the jitter depth (§6) [verified: code, trace]. So `V` must come before `p`. The default pitch goes only into the stages' copies: the escape parser keeps just the voice in the host settings (`[EE44]`), so after the utterance ends (`ESC[x`, whose loop reset refills the stages from the host settings) the pitch is the `p` setting again while the voice stays [verified: code, trace] |
| `ESC[12N` (mode flag 12, `0x800`) | **monotone**: every target = the pitch setting [verified: trace] |
| `ESC[0P` (word mode) | every word is spoken as its own accent domain: each gets a rise on the consonant before its stressed vowel and falls back on the vowel, over one declining line for the sentence [verified: trace] |
| Phoneme input `/p;d/`, `!p;d!` | a given F0 replaces (`/p/`, 50-200 Hz) or shifts (`!p!`, adds *p* − 100 Hz, result clamped to 50-200 Hz) the rule target of one phoneme (REFERENCE §15.4) [verified: code] |
| Word-type marks (two-letter phoneme input) | select the contour types and the other word-level effects of §4.2 |
| Rate `r`/`v` | no direct effect on the targets. It changes durations, and so the timing of the track, and phrase breaks: none at speed 19 and above, extra `)` breaks below 9 (REFERENCE §15.2) |

Emphasis and stress also matter. Node stress levels are 3 emphatic, 2 primary and 1 secondary, and 0 is none
(REFERENCE §15.3). `~word` in text or `"` in phoneme input gives level 3. Function words lose a level (all of it
from speed 9), so they carry no accent at normal rates.

## 3. The span of the line: sentences, scans and phrases

**The line spans a whole scan, not a comma phrase** [verified: code, trace]. `prosody_scan_phrase` reads ahead to a
`.` or `?`, a `C` command, or a length limit, counting phonemes into `PHRASE_LEN` (`DS:DC8A`). Commas, semicolons,
colons, dashes and brackets (which the text rules turn into `,`) do not end the scan. They only get a `P` command
each (below), so the declination runs on across them, and `F0_POS` (`DS:DD54`, the count of phonemes timed so far)
keeps counting.

- **Where the line ends.** At a sentence end the scan stops counting at the **last vowel**: the consonants after it
  are subtracted, so the line reaches `pitch` on the last vowel and goes on falling below it for the final
  consonants.
- **Long sentences.** A scan stops at 180 phonemes (70 for the first scan after a reset, below speed 19; 5 in
  fast-start mode) or 240 nodes. Then `CARRY` = `PHRASE_LEN`/4 + 4 is added to the length, so the line is drawn as
  if the sentence were a quarter longer. On the next scan the carry is taken off again and `F0_POS` continues, so
  the slope is recomputed for the longer total, which can make a small step in the line [verified: code].
- **Resets.** `accent_reset` `DBC51` clears `F0_POS` and the accent state at a sentence end (and at a comma in word
  mode).

**Phrases.** `phrase_breaks` `DA101` puts a `P` command before each stretch of words that ends at a punctuation mark,
and before the breaks it inserts into long stretches (§15.2). The command's `+6` is the **phrase kind** and its
`+8` is 0 for the first phrase of a sentence, else 1. `stage_run_command` loads them into the stage's `+10` word
(low byte kind, high byte "not first"):

| Kind | Given for | Phrase ending (§4.6) |
|---|---|---|
| 1 | `.` (also `!`); a question that starts with a wh-word (lexicon class 13) | fall |
| 2 | a stretch ended by a `C` command or the length limit | none |
| 3 | `,` (also `;` `:` `-` `(` `)`); an inserted `,` or `\` break | slight rise |
| 4 | `?` otherwise (yes/no questions) | jump up |
| 6 | forced by contour types 8 and 15 (§4.2) | none, and no nucleus |

For a wh-question the first vowel-bearing symbol of the wh-word also gets secondary stress and the stressed-syllable
flag (`question_start`), so "What is it?" falls like a statement [verified: trace].

## 4. The target of one phoneme (`f0_target` `DBC84`)

`segment_prosody` `DC1EA` calls `accent_context`, then `f0_target`, then the duration rules, for each symbol.
`f0_target` works in Hz on 16-bit integers (divisions truncate) and stores `di / 2` in node `+8`. Pauses (` `) count
as phonemes here. Word boundaries (`&` `%`) only set the flags of §4.2.

In the formulas, `P` is the pitch setting, `S` the voice's factor (`DS:5378[V]`), `pos` = `F0_POS` (1 for the first
phoneme of the scan) and `len` = `PHRASE_LEN`.

### 4.1 Shortcuts

In this order [verified: code]:
1. `P` = 0: the target is 0.
2. Monotone (mode flag 12): the target is `P`.
3. `F0_HOLD` (`DS:DD50`) ≠ 0: the target is `F0_HOLD`. It is set by the phrase ending (§4.6), so everything after the
   phrase's last vowel, up to the punctuation, repeats that vowel's value.

### 4.2 The phrase line and the contour types

`fx = (P / 3) · S`, which is **27 Hz** for voice 0 at pitch 85, **20** for voice 1 at 75 and **32** for voice 2 at
110. `top = P + fx`. The default line (contour 0) is

    line = top − fx · pos / len

It falls from `top` at the start to `P` at the last vowel [verified: trace].

**Word types.** Other contours are chosen by where words of certain **types** fall in the scan. A word's type is the
low byte of `+6` of its boundary node (`&`/`%`). It is **not** the lexicon's word class, which is in `+8`
(REFERENCE §9.6) [verified: code]. Ordinary text always gives type 0. Types come only from marks in **two-letter
phoneme input**: with `ESC[6A`, text in `[ ]` is phoneme input (REFERENCE §15.4), and a mark character directly
before the `]` gives the next word its type (`phoneme_spelling` `D4DB0`). For example,
`ESC[6AThe old man [\]walked slowly.` makes "walked" a type-6 word [verified: trace].

| Mark | Type | Effect |
|---|---|---|
| `\` | 6 | contour 6 (rise to this word, then fall) |
| `/` | 7 | contour 7 (fall to this word, then rise) |
| `/\` | 8 | contour 8 (like 6, with no phrase ending) |
| `{` | 12 | the rest of the sentence **8 % higher** (`LOWER_F0`, despite its name) |
| `_` | 17 | sets `DECL_LOW` (§4.6: no phrase ending) and a late hat (§4.4) |
| `=` `` ` `` `-` | 1, 2, 5 | the word gets secondary stress (lexical stage) |
| `!` `"` `~` | 9 | the word gets emphatic stress |
| `'` `*` `:` `#` `$` | 3, 4, 10, 11, 13 | lexical and text-rule effects only (13 skips the allophone and function-word rules); no F0 rule tests them |
| two digits | — | a word class 0-63 into `+8` (read by the phrase-break rules) |

The scan records the phoneme position of the last word of type 6, 7 and 8 (`POS_6` `DC80`, `POS_7` `DC7E`,
`POS_8` `DC7C`; −1 for none) and of type 17 (`POS_17` `DC7A`, 0 for none). Written `p6`, `p7` and `p8`, these
positions select the contour `[DC82]`:

| Words present | Contour |
|---|---|
| neither 6 nor 7 | 8 if there is a type-8 word, else 0 |
| only 6 | 6, or 0 if `p6` = 0 |
| only 7 | 7, or 0 if `p7` = `len` |
| 6 before 7 | 16 if `p6` ≠ 0, else 7 (0 if `p7` = `len`) |
| 7 before (or at) 6 | 15 if `p7` ≠ 0, else 6 (0 if `p6` = 0) |

The contours are made of straight pieces between `top` and `P` [verified: code; 0, 6, 7, 8, 15 and 16 seen in
traces]:

| Contour | Shape (`pos` from 1 to `len`) | Also |
|---|---|---|
| 0 | `top` → `P` | |
| 6 | `P` at the start, rising to `top` at `p6`, then falling to `P` at the end | |
| 7 | `top` falling to `P` at `p7`, then rising to `top` at the end | |
| 8 | as 6, around `p8` | phrase kind forced to 6: no ending, no nucleus |
| 16 | rise to `top` at `p6`, fall to `P` at `p7`, rise to `top` at the end | phrase kind forced to 3: a comma ending |
| 15 | fall to `P` at `p7`, rise to `top` at `p6`, fall to `P` at the end | phrase kind forced to 6 |

Contours 8, 15 and 16 overwrite the whole kind word, so the "not first" byte becomes 0 as well. A mark at the
very start (`p7` = 0) turns contour 7 into a line rising from `P` to `top` [verified: trace].

**Type 12:** from its word to the end of the sentence, the line value is multiplied by 108/100, before the accents
are added [verified: code, trace].

### 4.3 The nucleus (`accent_context` `DBA35`)

At each vowel, when no nucleus is set, the routine looks ahead to the next punctuation mark and takes the **last
vowel of stress level 2 or 3** there as the nucleus, `ACCENT_NODE` `DD46`. Once an emphatic (level 3) vowel is
chosen, only a later emphatic vowel replaces it. If no punctuation lies within the scanned text there is no
nucleus. A punctuation mark clears the nucleus, the hat state and the phrase-final flags [verified: code].

The same routine sets `PHRASE_FINAL` (`DD4C`) at the phrase's **last vowel**, when a punctuation mark comes before
any further vowel. The flag stays set until the punctuation [verified: code, trace].

### 4.4 Accents and the hat

With `di` = the line value (after the ×1.08):

- `accent = (di · S) / 4`, about a quarter of the line (27 Hz at 110 Hz for voice 0).
- `acc = accent − accent/4`, the local accent (about 20 Hz). For the first stressed vowel of an accent domain
  (`ACCENT_DONE` `DD48` still 0), `acc` is 3/4 as large in a continuation phrase or while `DECL_LOW` is set, and
  1/16 larger otherwise.
- If the nucleus is emphatic, `acc` is halved for the whole phrase.

Then [verified: code; values checked by hand against traces]:

| Where | Adds |
|---|---|
| a vowel with stress level 1 | `acc/2` |
| a vowel with stress level 2 | `acc` |
| a vowel with stress level 3 | `3·acc` |
| **the nucleus** | nothing: it is marked (flags bit 7, which no later stage reads) and ends the hat |
| the segment just before the nucleus (unless the kind is 6) | `acc`, or `3·acc` for an emphatic nucleus |
| every segment while the **hat** (`PEAK` `DD52`) is up | `accent` |

**The hat** goes up at the segment before the phrase's first stressed vowel (the next phoneme has stress level 2 or
3 and the nucleus has not been reached). It also goes up at the first phoneme of a scan if that phoneme is a vowel in
a stressed syllable, and at the phrase-final syllable after a type-17 word. It comes down at the nucleus, so the
nucleus falls back to the bare line. The rise that marks the nucleus is on the segment before it. After a plateau of
`line + accent` with bumps on the stressed vowels, the phrase drops at its last accented syllable [verified: trace].

### 4.5 Segment effects

- **Voiced dip.** A voiced segment (feature 0.04, vowels included) loses **3 Hz**, and a voiced obstruent **8 Hz**.
  This applies when the segment is outside a stressed syllable and the next phoneme is an unstressed consonant, and
  always for the nucleus. The code also excludes a "stop-like" class whose three feature tests no phoneme passes
  (dead code) [verified: code].
- **High segments** (`4 E U W Y b c h i u |`, feature 200.01) get **+3 Hz** except in the phrase-final syllable:
  the intrinsic pitch of high vowels [I: role].

### 4.6 The phrase ending

At the phrase's last vowel (`PHRASE_FINAL`), **provided `DECL_LOW` (`DS:DD7C`) is 0**, the kind of the phrase
decides [verified: code, trace]:

| Kind | Ending |
|---|---|
| 1 (statement) | `− accent/4`, and 4 Hz more if that last vowel is unstressed and directly followed by `.` |
| 3 (comma) | `+ accent/8` |
| 4 (yes/no question) | the target becomes **`2·P − P/8`** (160 Hz at pitch 85), whatever the line |
| 2, 6 | unchanged |

The result is clamped to 50-240 Hz and kept in `F0_HOLD`, so the consonants after the vowel get the same target.

**`DECL_LOW` and the power-up quirk.** A type-17 word sets `DECL_LOW`. A later word boundary clears it when an
accent has already passed in the phrase and the boundary is not in the phrase-final syllable. **Nothing
initialises it.** The RAM test at power-up fills RAM with `0x55` (`in_boot.c`), so it starts as `0x5555`, which
counts as set. Until the first sentence with a word after an accent clears it, the phrase endings are skipped and
first accents are 3/4 size. So after power-up (or when a DLL handle is opened), "Are you ready?" alone ends at
85 Hz with no rise. The same question after "The old man walked slowly to the store." rises to 160 Hz
[verified: trace; follows from the firmware's RAM test, so the board would do the same]. A type-17 word in the
last word of a sentence leaves `DECL_LOW` set into the next sentence in the same way [verified: trace].

### 4.7 Clamping and storage

Other targets are clamped to **50-240 Hz** as well and stored as `di/2`, so a target is an even number of Hz up to
240. Given values (§2) are applied after the rules by `given_values` `D9BE6`: an absolute F0 replaces the target, and
a relative one is added and clamped to 50-200 Hz [verified: code].

### 4.8 Worked example

"The old man walked slowly to the store." spoken as a session's second sentence, voice 0, pitch 85 (`fx` 27,
`top` 112, `len` 24):

    x 111  E 140  O 158  j 131  D 125  M 132  a 151  N 125  W 130  w 145  K 124  T 123
    S 122  L 121  O 137  L 117  E 116  T 114  b 113  x 112  @ 108  S 109  D 124  g 77

- `E` (pos 2): the next phoneme is stressed, so the hat goes up: line 110 + accent 27 = 137, plus 3 for a high vowel
  = 140.
- `a` in "man" (pos 7): line 112 − 27·7/24 = 105; accent 26, acc 20; 105 + 20 + 26 = 151.
- `D` in "store" (pos 23): the segment before the nucleus. Line 87 + acc 16 + hat 21 = **124**. It is in a stressed
  syllable, so no voiced dip.
- `g` (pos 24, the nucleus): line 85, no accent, hat down, nucleus dip −3, statement ending −21/4 = −5, so **77**.

## 5. From targets to the F0 track (parameter generator)

The generator writes 22 byte tracks, one byte per 10 ms frame (REFERENCE §12.4). For F0 [verified: code, trace]:

### 5.1 One segment

- **Target** = node `+8` × 2 (`paramgen_load_targets` `DD1C0`).
- **Locus** `L` = the last F0 value written, taken by `paramgen_advance` `DDC70` (at the start of an utterance, the
  track default 100 Hz). After a **voiceless** segment, `paramgen_run` sets `L` to that segment's own rule target
  (`[EAFA]` → `[EBA0]`), so F0 resumes where the line would have been, not from 0.
- **Weight** `W` = 0: `paramgen_segment_setup` `DEB3A` zeroes F0's weight (`[EB74]`), so the onset is
  `W·target + (1−W)·L` = `L`. **F0 is continuous across segments**, unlike the formants (REFERENCE §12.5a).
- **Transition**: type 6, a ramp from the onset to the target along ramp *k* = the segment's length (at least 7,
  at most 20 frames and the look-ahead), then held. The ramps (`DS:9B9E`) are **256·(1 − i/k)²**, so F0 moves fast
  at first and settles slowly. In segments shorter than *k* frames F0 does not reach its target, which smooths the
  contour.

So the track follows the targets with a lag. With the same sentence spoken first in a session (where `O` in "old"
has target 152), the track climbs 140 → 152 over the vowel's 15 frames. `D` in "old" (3 frames) moves only from 130
toward its 124.

### 5.2 Special cases

| Case | Code | F0 |
|---|---|---|
| **Voiceless segment** | `paramgen_segment` | target 0, held: the track is 0 there, and AV is 0 too |
| After a stop, the release into a sonorant | `pg_release_onset` `E30C1` | the voice-onset frames get F0 0, then the locus is the stop's rule target |
| Voiced onset consonant → vowel, both in the same stressed syllable, no glottal onset | `pg_locus_weights` `E1024`, `pg_apply_loci` | `W` = 0.5 and type 7: the vowel starts halfway between `L` and its target, and the consonant's frames are pulled back toward that value over its whole length. The accent rise starts in the onset consonant |
| **Glottal onset** (flags bit 6: a vowel after a pause or in hiatus, REFERENCE §15.3) | `paramgen_segment` | a second pass pulls the first frames toward **40 Hz** (ramp 3) |
| Glottalized `q` after a sonorant | `pg_amplitude_boundaries` | a dip to 40 Hz (durF 5, durB 3) |
| `Z` between non-vowels, not before a pause | `paramgen_segment_setup` | target **80 Hz**, `W` 0.5, 4-frame transitions [I: a devoicing dip] |
| **Pre-pausal fall**: a pause after a voiced segment other than `Z` | `pg_amplitude_boundaries` | the last 2/3 of the voiced segment is pulled toward **58 Hz**, and AV toward 48 dB. The length is capped at 20 frames and by how far back the ring still holds frames (10-12 in the traces) |
| Target 0 (pitch 0) | `paramgen_segment` | AV's target moves to AH (whisper) and F0 stays 0 |

The pre-pausal fall acts on every phrase ending, the question rise included. After a sentence, "Are you ready?"
climbs toward its 160 Hz target and reaches 138 Hz. The fall then pulls the last frames down to 95 Hz:

    … 88 88 97 106 114 121 128 134 137 138 135 130 121 110 95   (then silence)

## 6. Frames and the DSP

`dsp_build_frame` `D89CB` turns p17 into the DSP's pitch periods (REFERENCE §11.3, §13) [verified: code]:

- F0 0 gives source word 0, periods 0 and AV 0: no voicing.
- The p17 values 69, 74, 79, 84 and 90 are bumped by 1 first (the same test is in TruVoice).
- **Jitter**: two values `F0 + F0·D·J`, where `D` = `DS:53E8[p20 & 15]` (steps of 3.3 %) and `J` =
  `DS:5428[LFSR & 63]` (−0.16 to +0.16). The LFSR (`[DC26]`) steps twice per frame. p20's low nibble is **0 for
  voices 0 and 2 and 5 for voice 1** (`DS:5318`), so only voice 1 jitters, by up to about ±2.7 %. The attenuation
  `a` is in p20's high nibble and has no effect on jitter.
- **Periods**: w30 and w31 = `(20000 / F) / 2` = `10000 / F` samples at 10 kHz.
- **The DSP** (`15C`) swaps w30/w31 at every period start, so successive periods alternate between the two jittered
  values. At the same moment it loads the pending F1-F3 and nasal-zero coefficients (pitch-synchronous formant
  updates) and swaps the AV pair w34/w35 (shimmer).

## 7. Quirks

- **`DECL_LOW` starts set** after power-up (§4.6): no phrase endings, and so no question rise, until a sentence
  clears it. For a clean start, speak a sentence with an accent followed by another word, such as "The old man
  walked.".
- **Type-17 words leave `DECL_LOW` set** when they stand in the phrase-final syllable, which carries over to the
  next sentence.
- The "stop-like" exclusion in the voiced dip is dead: no phoneme has all three features.
- `LOWER_F0` raises F0 by 8 %.
- Contours 8, 15 and 16 overwrite the stage's whole phrase-kind word, which also clears its "not first" byte.
- `mark_break_neighbour` `DA811` gives the phoneme after an inserted phrase break a glottal onset only when the
  **pitch setting** is 0 (REFERENCE §15.2). With a normal pitch, inserted breaks get no 40 Hz dip.
- `ESC[V`'s default pitch lasts only until the end of the utterance (§2): a later utterance has the new voice at the
  old `p` pitch.
- `accent_context`'s search stops only at a punctuation mark. A scan cut by the length limit has no nucleus until
  its punctuation arrives in a later scan.

## 8. Reproducing and graphs

- **`pitch_trace`** (`src/cli/pitch_trace.c`, built with the project) speaks a text and prints, for each phoneme as
  it is played, its start and length, the F0 target and the phrase-line value, the contour and phrase kind, and each
  contribution of §4 (from the trace hook `pr_f0_hook` in `f0_target`, NULL in normal use), plus the F0 track frame by
  frame. `-W` speaks a sentence first so that `DECL_LOW` is clear.
- **[pitch_graphs/](pitch_graphs/)** has annotated PNGs of the examples in this document (statement, questions,
  commas, emphasis, the power-up quirk, every contour type, types 12 and 17, word mode, voices and pitch settings).
  `pitch_graphs/make_graphs.py` makes them from `pitch_trace` with matplotlib; its README explains the markers.
- **The track** alone: `samples/python/params_export.py` (or `samples/params_export.c`) writes every frame's
  parameters, F0 included, as CSV.
- The library starts each handle from the C power-up, so the `DECL_LOW` quirk shows in its first sentence.

## 9. v1.1 (1983) for comparison

v1.1's `f0_set` `F0A45` (REFERENCE §16) follows the same plan with fixed sizes instead of fractions of the line
[verified: code]:
- a declining offset (35 Hz at a sentence start, 95 % of it after each vowel) rather than a straight line;
- a hat of +30 Hz from just before the first stressed vowel to the last one;
- accents of 20, 10 and then 6 Hz, and +15 Hz before an emphatic vowel;
- −10 Hz (−5) segment dips;
- endings of −15 Hz before `.` and +15/+20 Hz before other boundaries, with no question jump;
- clamped to 50-200 Hz; the default pitch is 75.

It has no contour types and no `DECL_LOW`.

## 10. Addresses

| Item | Address | Role |
|---|---|---|
| `f0_target` | `DBC84` | the per-phoneme target (§4) |
| `accent_context` / `accent_reset` | `DBA35` / `DBC51` | nucleus, phrase-final flags / reset per sentence |
| `segment_prosody` | `DC1EA` | accent context → F0 → durations |
| `prosody_scan_phrase` | `D9492` | phrase length, word-type positions, contour choice |
| `phrase_breaks` | `DA101` | `P` commands and their kinds |
| `given_values` | `D9BE6` | F0 given with phoneme input |
| `phoneme_spelling` | `D4DB0` | word-type marks in two-letter phoneme input |
| `paramgen_segment_setup` / `paramgen_segment` | `DEB3A` / `DD703` | F0 weight 0, voiceless, glottal onset |
| `pg_apply_loci` / `pg_amplitude_boundaries` | `E1C1A` / `E2535` | onset, durF, pre-pausal fall, `q` dip |
| `param_emit_segment`, ramps | `D3D2F`, `D345F` `D3532` `D357B` | writing the track |
| `dsp_build_frame` | `D89CB` | jitter and periods |
| `DS:5328`, `DS:5378` | per voice | default pitch; excursion factor (Q15) |
| `DS:9B9E` | 21 pointers | ramp tables |
| `DS:53E8`, `DS:5428`, `DS:5318` | | jitter depth, jitter values, per-voice p20 |
| `DS:DC7A-DC82` | RAM | `POS_17`, `POS_8`, `POS_7`, `POS_6`, `CONTOUR` |
| `DS:DD40`, `DD46`-`DD54`, `DD7C` | RAM | `LOWER_F0`; `ACCENT_NODE`, `ACCENT_DONE`, `ACCENT_HERE`, `PHRASE_FINAL`, `CLAUSE_FINAL`, `F0_HOLD`, `PEAK`, `F0_POS`; `DECL_LOW` |

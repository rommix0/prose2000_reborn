# VOICE_CONTEXTS.md — the contexts a new Prose voice must cover

This file is for a **data preparer** building a new voice for the v3.4.1 synthesizer: new formant targets and
trajectories measured from a speaker. It lists every context the firmware distinguishes when it picks a segment's
parameter values, so that recordings and measurements can cover each one.

Everything here comes from the decompiled parameter generator (`src/paramgen/`, REFERENCE §12) and the lexical stage
(`src/lexical/`, REFERENCE §15.3), and from the ROM tables in `src/data/`, decoded on 2026-09-26. It is *verified*
from code and tables unless marked **[I]** (inferred). Codes are the Prose's one-character phonemes (API.md
"Phonemes", REFERENCE §12.3a). ␣ stands for the pause/silence code.

## 1. How one segment gets its parameters

The parameter generator walks the phoneme list with a window **prev, cur, next** (plus next+1 and prev−1 in a few
places) and, for each segment:

1. **Context-free targets** of cur: F1-F4, B1-B3, AV; AF, AH, A2-A6, AB for consonants; FN; the offglide targets of a
   diphthong-capable vowel (§5.1).
2. **Stop release row** if cur is a stop: burst AV/AF/AH, burst length, voice onset time (VOT) and AB, chosen by
   the **class of next** (§5.2).
3. **Rule table**: the group for the pair (**cur, prev**) is selected, and its rules are tried in order; the first
   whose condition holds overrides targets, weights and burst values and names the routines to run (§5.3).
4. **Context routines**: hard-coded tests on prev/cur/next set transition types and lengths, VOT, voicing,
   frication, loci and further target changes (§5.4).
5. **Locus model** (computed, §6): every transition starts from `W·target + (1 − W)·L`, where L is the value the
   previous segment actually left on the track (or a fixed consonant locus) and W a weight chosen by class and place.

A new voice replaces the data of steps 1-3. The numbers in step 4 are constants in code, and step 5 needs no data.

## 2. The phoneme inventory the generator sees

The generator receives **58 codes**: 23 vowels, 34 consonants and the pause. Several of them are **allophones**
written by the lexical stage's allophone rules (`src/lexical/lx_allophone.c`, REFERENCE §15.3), so each is a separate
target set in the voice, and it occurs only in the contexts below. A voice needs data for all 58, except `h` and `Q`,
which reach the generator only through phoneme input.

**Allophones and the contexts that produce them** (speed 13 = 150 wpm is the default; "stressed" means node bit 5,
in a stressed syllable):

| Code | Sound | Produced when |
|---|---|---|
| `t` | flap ɾ | T or D between a vowel and an unstressed `3 @ \| E O` (speed > 6, not word mode; also across words: "water", "city", "at all"); D between vowels before an unstressed vowel, at any speed ("ladder", "had it"); D after a stressed sonorant before a schwa + N ("sudden") |
| `q` | glottalized t | T after a stressed sonorant before schwa + N ("button"); T after a sonorant before `M`, `x D L N`, or a word boundary followed by a sonorant consonant ("that man", "at the"); word-final T after a nasal in content words before a vowel ("want it") |
| `D B G` | unaspirated t p k | T P K after `S` before a stressed syllable ("strength", "spy", "scare"). `D` also replaces `x` (ð) after T/D above speed 19 |
| `C s`, `J z` | tʃ, dʒ as closure + release | every `C`/`J` gets `s`/`z` inserted after it; T and D before `R` become `C`/`J` ("tree" `CsR`, "dream" `JzR`), except after `S` |
| `j` | dark ɫ | L after a vowel or syllabic consonant, unless the next vowel is stressed ("feel", "silly"; "hello" keeps `L`) |
| `l` | syllabic l̩ | `@`/`\|` + L at a word end or before a reduced vowel ("bottle", "little") |
| `n` | syllabic n̩ | `@`/`\|`/`p` + N after an obstruent, before an obstruent or pause ("button", "seven") |
| `m` | syllabic m̩ | lexicon and letter-to-sound only (e.g. -ISM); never by rule |
| `3 4 k r g c` | ɝ ɪr ɛr ɑr ɔr ʊr | a vowel + R not followed by a stressed vowel: `v \| @ i` → `3`, `E` → `4`, `a e A` → `k`, `o` → `r`, `O w` → `g`, `b u` → `c`; after `I f y` the R becomes `3` ("fire" `FI3`). Also in lexicon entries |
| `~` | ŋ | N before `G K ~` across a morph boundary `[` (speed > 9); lexicon |
| `d` | ɦ | H after a voiced sound (speed > 6, also across words: "ahead", "saw him"); H is deleted before a non-vowel, and in unstressed function words after a consonant above speed 13 |
| `W` | w | also `h` (ʍ) after a voiced sound |
| `Y` | j | also `E` between `j` and `@`; inserted before `b` for letter-to-sound U ("cute" `KYb`) |
| `p` | release vocoid | inserted after a final stop (not an affricate) or `M N ~` before `] , . ?` below speed 19 ("friend." `NDp`); the prosody stage also inserts it after a closure (not `J C`) or `x X V z` before a phrase break it adds in a long sentence |
| `S`, `F` | s, f | Z before a voiceless sound in the word; V of "of"/"have" before `F X S s` |
| `@` | ə | vowels of function words at speed > 6 ("an", "the" before a consonant); unstressed vowels of letter-to-sound words |
| ␣ | silence | a pause before a stressed `A E b` after `~` ("going east"); `Q` at a word end |

**Glottal onset (node bit 6)** is set on a word-initial vowel after a pause (and after a vowel or sonorant at slow
speeds), on a stressed initial `j L W R h` after a pause below speed 19, on consonants after `q`, and with the
inserted silence above. In the generator it makes F0 dip to 40 Hz at the segment start, counts as "released" for a
stop, and blocks the aspirated release of the next segment. It is a context the preparer should record (vowel-initial
words after a pause) but it needs no separate targets.

**Also changed before the generator:** geminates merge above speed 6 (identical codes, and `S Z` + `s`, `Z` + `S`,
`V` + `F`, `D` + `T`, `B` + `P`), and T or D are dropped in clusters above speed 13 ("last night", "and then").
Such contexts therefore never reach the generator at normal speeds.

## 3. Context dimensions (the summary)

| Dimension | Values distinguished | Where |
|---|---|---|
| **cur** phoneme | 58 codes | targets; rule-group row; nearly every routine |
| **prev** phoneme | per cur row: vowel-quality groups, single phonemes, consonant classes (§4.2) | rule group; `after_closure` (transition length by prev place); all routines |
| **next vowel class** | V0-V3 (§4.1) | rule conditions (flags 6-9); stop release row; routines |
| **next** phoneme identity | e.g. `3 R L W l Y s p n Z a i` after stops; `bcu`/`EiU` after `N`; `@` after `s` | rule conditions (223 terms); routines |
| **next** class (non-vowel) | glide/liquid, `p`, fricative, `d h H`, stop/nasal, pause | stop release row; rule feature terms |
| **next+1** | vowel class, front/back/high, `R`, pause | only `C s …`, `J z …` (affricates), `n ␣ ␣` |
| **prev−1** | `S`/`j` before `T`; `C` before `s R` | "st", "lt" (no aspiration); "tr" (`C s R`) loci. Never in the rule table |
| **cur released** (R) | cur is a stop and next is a closure or pause, or cur has a glottal onset (node bit 6) | rule flag 5 |
| **stress** | node bit 5 = "in a stressed syllable" (the vowel and its onset) of cur, prev or next | rule flag 10 (next); AV ±, VOT lengthening, burst decay, F0, AV timing. The stress *level* is not used |
| **cur voiced** | voiced / voiceless | rule flag 0; F0 and AV handling |
| **prev class** | vowel / closure (stop, nasal, flap, glottal) / sonorant / other | default weights; rule flags 1-4; `after_closure` |
| **duration** of cur (and prev, next) | frames | vowel reduction, diphthong timing, VOT cap, AV/F0 transition lengths |
| **pause adjacency** | prev, next or next+1 = ␣ | prevoicing, pre-pausal fall, release, fricative levels |
| **voice** `ESC[V` | 0-2 | per-voice tables only (F4 offset and cap, formant scale, source p18-p21). **The rules are voice-independent** |
| speed / fast response | speed ≥ 19 or mode flag 14 | shortens the look-ahead from 20 to 4 frames, capping every transition |
| F0 = 0 | whisper | AV moves to AH |

## 4. The classes contexts are stated in

### 4.1 Next-vowel classes (`DS:98C2`)

The rule flags 6-9 and the stop release rows sort the *next* sound into these classes:

| Class | Members | Description |
|---|---|---|
| **V0** | `4 E U` (ɪr i ju) | high front |
| **V1** | `A a e i k \|` (eɪ æ ɛ ɪ ɛr ɨ) | non-high front |
| **V2** | `O b c g u w y` (oʊ u ʊr ɔr ʊ ɔ ɔɪ) | back rounded |
| **V3** | `I f r 3 @ o v` (aɪ aʊ ɑr ɝ ə ɑ ʌ) | central / low |
| 4 | `j l L W Y R` | glide or liquid |
| 5 | `p` | release vocoid (pre-pausal release) |
| 6 | `V F x X Z S z s` | fricative |
| 7 | `d h H` | h-like |
| 8 | stops, nasals, `t Q q` | closure: no release row loaded |
| 9 | ␣ | pause: no release row loaded |

### 4.2 Previous-sound groups (the rule-table rows)

The rule group is a matrix indexed by **cur phoneme × prev phoneme** (`DS:9376`, 88 groups). The rows split prev as
follows (full list in Appendix A):

| cur | prev groups |
|---|---|
| vowels, `p` | `K` / other closures / everything else |
| `j l L W Y` | closure / other |
| `R` | `K` / `P` / other closures / other |
| `d H`, `h` | closure / other |
| `B` | `AEIaeiy\|Y` front / `uw@ovp` central-back / `4cgkr3R` r-coloured / `OUbfW` w-offglide / `S` / other |
| `P` | as `B`, plus ␣ as its own group |
| `G`, `K` | `AE` / `Iaeiy\|` / `OUbf` / `uw@ov` / `4cgkr3` / other |
| `S` | as `K`, with closures separate from the rest |
| `s` | as `S` |
| `D` | `S` / ␣ / `j` / other |
| `T` | `Z S` / ␣ / other |
| `t Q` | one group |
| `J q`, `C` | one group each |
| `V F` | `AEIaeiy\|Y` front / other vowels and `W R p d h H ␣` / `l L` and fricatives / `j` / closures |
| `x`, `X`, `Z`, `z` | ␣ (not `Z`, `z`) / closures / vowels and `p` / other |
| `M m ~` | `AEIaeiy\|Y` / `4cgkr3R` / other |
| `N n` | one group |
| ␣ | closures / other / ␣ |

In words: **a stop or fricative is shaped by the quality of the vowel before it** (front, high front, central
or back, r-coloured, w-offglide), and by a few specific predecessors (`S`, ␣, `j`).

### 4.3 Feature sets used in conditions

From the feature table `DS:00A8` (plane.bit): front vowel 100.20 = `4 A E U a e i k |`; back/rounded 180.20 =
`O U b c g u w y W h`; high 200.01 = `4 E U b c i u | W Y h`; syllabic 0.01 = vowels and `l m n`; sonorant 0.02;
stop/affricate 0.20 = `B P D T J C G K q`; fricative 0.40; closure 100.01 = stops, nasals, `t Q q`; alveolar 100.04 =
`D T t q Z S N n`; labial 100.10 = `W h B P V F M m`; velar 100.80 = `G K ~`; lateral 100.40 = `j l L`; rhotic 100.08
= `3 R`; palatal 180.04 = `Y J C z s`; aspirated 200.80 = `P T C`.

## 5. The data a voice supplies, by context

### 5.1 Context-free, per phoneme (58 codes)

| Data | Tables | For |
|---|---|---|
| F1, F2, F3, F4 target | `944C 9486 94C0 94FA` | all 58 |
| B1, B2, B3 target | `9534 956E 95A8` | all 58 |
| AV target | `95E2` | all 58 |
| AF, AH, A2-A6, AB | via `9774-9854` | consonants (idx ≥ 29); vowels and `j l L W Y R` get 0/0/60…60/0 |
| FN (nasal zero) | `98C0` | `M m N n ~` (448 Hz); others 248 Hz |
| **Offglide** F1-F3, B1-B3; transition length; onglide hold fraction | `9690-96EA`, `9744`, `9720` | the 18 vowels `4 A E I O U a b c e f g i k r u w y`; `3 @ o v \|` have one target |
| Per voice: F4 offset and cap, formant scale, source type/gain, jitter/shimmer | `5348 5358 52F8 5308-5320` | voices 0-2 |

So every vowel except `3 @ o v |` needs **two targets** (onglide and offglide), not one: even `E`, `a`, `e`, `i`
move slightly.

### 5.2 Stop release, by next class (9 stops × 8 classes = 72 cells)

For cur `B P D T t J C G K` and next class 0-7 of §4.1 (V0, V1, V2, V3, glide/liquid, `p`, fricative, `d h H`):
burst AV, burst AF, burst AH, burst length (frames), VOT (frames), AB override (tables `99DD 9905 994D 9A25 9A6D
9995`, row = `DS:98DB[stop]`). Next = closure or pause loads no row: the burst AV/AF/AH are then 0 (reset for every segment), while the burst
length and VOT keep the values of the last stop that loaded a row **[I: probably unintended]**.

### 5.3 Rule contexts (88 groups, 621 rules)

Within a (cur, prev) group the rules are tried in order and the **first** match applies. Its actions set targets
(mostly the parallel amplitudes A2-A6, AB, AF and F2-F4 of the consonant, i.e. its spectrum in that context), locus
weights and burst values. The recurring context lists are:

| cur | contexts, in priority order |
|---|---|
| `B` | released; next ∈ V0, V1, V2, V3 (not `3`); next `Z`; after `S` also `L` |
| `P` | released; next `EiU`; next front non-high; next `p` (after back vowels); next `S`, `s`, `R`, `L`; after ␣ or `S` also `i` |
| `D` | next `p`; unstressed sonorant or vowel next (after a voiced sound, ␣ or `j`); V0; V1; V2; `3`; V3; `R`; `n`; released |
| `T` | next `p`; unstressed vowel next; V0; `a`; V1; `3`; `R`; V2; V3; `W`; `n`; released |
| `G` | V0; V1; V2; `3`; V3; `R`; `p`; `L`; `W`; `Y`; released; non-syllabic next |
| `K` | V0; V1; V2; `3`; V3; `R`; `p`; `L`; `W`; `l`; `Y`; `s`; other fricative; stop; released |
| `C` | `s` + next+1: high front vowel, non-high front vowel, back vowel, `3`, other vowel, `R`, ␣; not released |
| `J q` | `J z R`; not released |
| `t Q` | none |
| `V F` | cur voiced × next front/back; next front; next back; after `j` also `S` |
| `x` | next not a vowel |
| `X` | V0; V1; V2; `3`; V3; `R` |
| `Z` | V0; V1; V2; `3`; V3; ␣; stop; non-vowel |
| `S` | V0; V1; V2; `3`; V3; closure; ␣; fricative |
| `z` | back/rounded next; non-vowel next |
| `s` | (after a closure: `@ \|`); V0; V1; V2; `3`; V3; voiceless before back |
| `M m ~` | cur `M m` vs `~` |
| `N n` | next `b c u`; next `E i U`; after a vowel |
| `R` | after `K`; after `P` before a stressed vowel |
| `H d` | `H` before a front vowel; before a non-back vowel; `H`; `d` |
| vowels | `E U Y 4` after `K` (A2 A3 = 0) |

Appendix A lists every group with its prev set, its conditions and the parameters each rule sets. One rule never fires:
in group 31 (`D` after `j`), "prev voiced and next unstressed sonorant" follows "next unstressed" and is shadowed
by it. `voice/contexts.tsv` lists the same contexts as explicit patterns (§8).

### 5.4 Hard-coded contexts in the routines

These contexts are tested in code with fixed numbers, not table data. A voice built only by replacing tables
inherits them. They mark contexts where the original speaker's trajectories were hand-tuned, so they are worth
recording and checking (source lines in `src/paramgen/`).

**Transition timing and loci by place:**
- After a closure, formant transition length by prev place: labial 6 frames, alveolar 8, palatal/velar 10 (F1 half).
  Into a closure the same by cur place, 5 after an aspirated `P T C`.
- Fixed consonant loci: alveolar F2 1600 / F3 2620 Hz (F2 1050 after a lateral, F3 2300 after a rhotic); `~` F2
  900-2100 / F3 2176; `N n` F3 2600, F4 3400-3600, F2 1670 after a high non-front sound; `M m` F2 700 after a
  non-front sound; `W` after `D T q` 1200/2050/2500; after `r`-coloured sounds F2 1700 / F3 1900; `R` after an
  alveolar F3 2100, after a labial 1700.
- Weights W by pair (e.g. F2 0.2 after `S` into a vowel, 0.1 into a central vowel; F2/F3 0.1 from a stop burst into a
  vowel; 0.35 after a w- or y-offglide vowel).

**Specific pairs and triples** (cur after prev, or cur before next):
- Vowels: `E` after `G K` (F3 +300, F4 −200); `b` after `R` or before a vowel (F2 1164); front vowels after `3 R`
  and before laterals (F2 −300); `a e i |` before `G K ~` (no F2 offglide); `U` before `D T q` (offglide F2 +400);
  `A E I y` before alveolars (offglide F2 −150); `u j`; `K p`; `P o`; `E C`; `R E`; `W E`; `D E`, `D o`; `B` + `b c u`,
  `E`, `o`; `G` + back or central vowels; `T 3`.
- Sonorants: `R` after velars, before V2, `P R` + unstressed vowel, `C s R` ("tr", F2 1292 / F3 1760); `L` after
  velars (F2 876); `K W` (F2 800); `G Y` (F2 2036 / F3 2592); `W` after ␣ before `E`; `n ␣ ␣`; `D n`, `T n`.
- Obstruents: "st"/"lt" (`S T`, `j T`: no aspiration), `T 3`, `T n`, `P T`, `K` + closure, `S B`, `S G R`, `S D` +
  stressed vowel, `G Z`, `D p`, `J z` + V1, `C s R`, `C s ␣`, `s @`, `K p`, `X R`, `F` after ␣, `Z` next to
  non-vowels, `S`/`Z` + unstressed vowel.
- Release after `K` by next class: aspiration 31 dB for V0/V2 and consonants, 48 for `a`, 0 for V1, the last value
  for V3; frication 50-60 dB; `W` VOT 7, `Y` VOT 5.

**Stress, duration and pauses:**
- A voiceless stop before a stressed vowel lengthens that vowel by VOT/2.
- A stressed vowel's AV is +2 dB, an unstressed one −3; `o a` are always a further −3 (and `@ w` too among the
  reduced vowels **[I: only `@` can reach that branch]**).
- Burst decay per frame: `K` 1, others 3, or 6 after a nasal; unstressed only.
- Voiced stops after a pause are prevoiced (`D`: 2-3 frames at 43-45 dB; `B`: 3 frames).
- The AV transition takes cur duration/4 before a pause or in a stressed syllable, and /2 otherwise.
- Into a pause after a voiced sound, F0 falls to 58 Hz and AV to 48 dB over 2/3 of the last segment (the pre-pausal
  fall).

## 6. Computed, not supplied

These depend on context but need no data. They are listed so that a preparer can tell them apart from measured
trajectories:
- **Coarticulation by the locus model**: onsets and boundaries are W-weighted mixes of the new target and the last
  value on the track (REFERENCE §12.5a). Formants must keep F(n+1) ≥ F(n) + 200 Hz.
- **Vowel reduction by duration**: all vowels except `3 @ |` are pulled toward F1/F2/F3 = 490/1450/2500 Hz by 0.88
  (1 frame) down to 0.02 (24+ frames). Reduced `@ | p` take F3 as the mean of their own, the last and the next.
- **Diphthong timing**: the onglide is held for a fraction of the segment (`DS:9720`), then moves to the offglide.
  The transition length scales with duration, and r-coloured vowels start closer to the offglide when unstressed.
- **Durations and F0**: the prosody stage (Klatt's duration rules, REFERENCE §15.2), not the voice data.

## 7. Recording checklist

A minimal set of contexts to measure for a complete voice:

1. **Vowels (23)**: the steady target of each; for the 18 diphthong-capable vowels the onglide and offglide
   targets. Record in stressed, long contexts so that reduction does not bias the targets.
2. **Consonant spectra (34)**: the context-free targets (formants, bandwidths, AF AH A2-A6 AB).
3. **Stop releases**: each of `B P D T G K` (and the affricates `J C` before `z`/`s`) released into V0, V1, V2, V3, a
   glide/liquid, a fricative and `h`, plus word-final release (`p`) and unreleased before a closure/pause. This covers
   the 72 release cells and the next-context rules.
4. **VC transitions**: each of `B P G K S s M m ~ V F` after a front vowel (`AE` vs `Iaeiy|` for `G K S s`), a
   central/back unrounded vowel, an r-coloured vowel and a w-offglide vowel (`O U b f`), and after ␣ and `S`.
5. **Consonant-specific next contexts** from §5.3: `K`/`G`/`P`/`B` + `R L W l Y s`; `T D` + `3 R W n a` and before
   unstressed vowels; `S Z X` + each vowel class; `N n` + `b c u` and `E i U`; `H` + front/back vowels; `R` after
   `K`/`P`.
6. **The pairs of §5.4** that the code tunes by hand.
7. **Per voice**: the source settings (p18-p21), the formant scale and the F4 limit.

## 8. Checking a corpus: `voice/`

These files support checking whether an aligned speech corpus covers the contexts above:

| File | What |
|---|---|
| `voice/contexts.tsv` | Every context as a pattern over Prose codes, one row per context. Generated by `voice/gen_contexts.py` from the ROM tables in `src/data/` |
| `voice/classes.tsv` | The named phoneme classes the set columns of `contexts.tsv` use, with their members (also generated) |
| `voice/arpabet_to_prose.tsv` | ARPABET (CMUdict, with the TIMIT extras) → Prose codes, including the allophone contexts of §2 |
| `voice/check_coverage.py` | Maps a phone-aligned corpus with `arpabet_to_prose.tsv` and counts the matches of every row of `contexts.tsv` |

```
python voice/check_coverage.py phones.txt -o coverage.tsv
```

`phones.txt` holds one utterance per line: ARPABET phones separated by spaces, vowels with CMUdict stress digits,
and `sil`, `sp` or `spn` for pauses (e.g. exported from the phone tier of a Montreal Forced Aligner TextGrid). The
script prints the coverage per row kind and the rows no token matched, and writes `coverage.tsv`: `contexts.tsv` with
a count column. `--show-prose` prints each utterance in Prose codes, `'` marking a stressed vowel.

### `contexts.tsv`

Tab-separated, ASCII, one header line. Columns:

| Column | Meaning |
|---|---|
| `id` | `R<group>.<rule>[a-z]` a rule of the rule table (Appendix A; letters when the rule's contexts take several rows); `REL.<stop>.<class>` a stop release cell (§5.2); `C.<routine>.<name>` a context tested in code (§5.4) |
| `kind` | `rule`, `release` or `code` |
| `prev2`, `prev`, `cur`, `next`, `next2` | the sounds two before, before, the segment itself, after and two after, as a set (below) |
| `stress` | `*`, or tokens `cur+`/`cur-`, `prev+`/`prev-`, `next+`/`next-`: that sound is / is not in a stressed syllable (node bit 5: a stressed vowel and its onset consonants) |
| `sets` | what the context sets: parameters (`AV` … `F4` are targets), `.durF`/`.durB`/`.type` transitions, `W[p]` weights, `L[p]` loci, burst values; `-` = nothing of its own (the routines still run) |
| `note` | the rule's condition, or a short description |

**Sets** are written with class names from `classes.tsv` and single Prose codes, space-separated: the set is the
union of the plain tokens minus the union of the tokens marked `!`. `_` is the pause and `any` means any sound, the
pause included. Examples: `high-front-vowel` = `4 E U`; `closure !K` = every closure but `K`; `vowel p
!high-front-vowel`; `alveolar !fricative` = `D T t q N n`. The generator picks the shortest such form, and checks that
it expands back to the exact set. Besides the phonetic classes (vowel quality, manner, place, voicing, features of
the feature table), `classes.tsv` names the rule table's previous-sound groups of §4.2 (`front-ending`,
`tense-front-vowel`, `other-front-ending`, `central-back-vowel`, `w-ending`).

A token matches a row when every column matches. The three kinds differ:
- **rule** rows (633) come from running every (prev, cur, next) triple, with next stressed or not and, for `C` and
  `J q`, every sound after next, through the rule engine; each triple lands in exactly one rule row. 620 of the 621
  rules are reachable: rule 3 of group 31 (`D` after `j`) is shadowed by rule 1 and can never fire. Rows whose
  `sets` is `-` choose no data of their own; a preparer can skip them.
- **release** rows (72) are the cells of §5.2. `sets` leaves out the fields whose ROM value is 0x7F ("keep"),
  e.g. most of `t`, `J` and `C`.
- **code** rows (218) are overlapping, hand-transcribed from the routines. They mark contexts that get fixed values
  in code, so covering them matters only for checking or retuning those constants.

Contexts that no English speech can produce (for instance `t` after a pause) will stay uncovered; the counts show
which rows matter for a corpus.

### `arpabet_to_prose.tsv`

Columns `arpabet`, `prose`, `context`, `note`. Rows are tried **in file order**, and the first whose phones and
context match at the current position wins and consumes those phones:
- `arpabet`: one phone or a sequence (`T AH0 N`). A phone without a stress digit matches any stress.
- `prose`: the Prose codes it becomes, space-separated; `-` = deleted, `_` = pause.
- `context`: empty, or conditions joined by spaces (all must hold): `before=` the phone after the sequence, `after=`
  the phone before it. A value is an ARPABET phone or a class, alternatives joined with `|`, `!` negates. Classes:
  `vowel`, `nonvowel` (a consonant or a pause), `stressed` / `unstressed` (vowel with stress 1-2 / 0, or a reduced
  vowel `AX IX AXR EL EM EN`), `stressed-syl` (a stressed vowel, or `L R W Y` before one), `consonant`, `obstruent`,
  `sonorant`, `voiced`, `voiceless`, `pause`.

The contextual rows implement the main allophone rules of §2 on phones: r-coloured vowels, `CH`/`JH` and `T R`/`D R`
as closure + release, flaps, glottalized `t` and syllabic `n l m`, unaspirated stops after `S`, dark `l`, voiced
and deleted `h`, the release vocoid after a final stop or nasal before a pause, and devoiced `Z`. They approximate the
firmware: its rules see word boundaries, speed and punctuation, which a phone string does not. The release vocoid in
particular is inserted only at punctuation, not at every pause. Checked against the firmware's own output for
test words ("fear" `F4`, "very" `kRE`, "button" `Bvqn`, "water" `Wwt3`, "tree" `CsRE`, "cute" `KYbTp`); the
remaining differences come from CMUdict itself (e.g. `AH0` where the Prose's lexicon has `|` or `e`).

## Appendix A: every rule group

Generated from the ROM tables in `src/data/`. For each cur set: the prev groups (one row each) and, in priority
order, the conditions of its rules and the parameters each rule sets (targets unless marked `W[..]` for a locus
weight, `.durF`/`.type` for a transition, or burst values: `burstAF`, `burstAH`, `burstAV`, `burstLen`, `VOT`;
`aspShift`/`aspAH` = how many frames aspiration starts early, and its level). Eighteen rules also write the word
`DS:EBB0`, which nothing reads; it is left out. "otherwise" is the rule without a
condition. `cur released (R)`: cur is a stop and next is a closure or pause, or it has a glottal onset (node bit 6). "next ∈ Vn"
uses the classes of §4.1. The routine lists are omitted; they follow the table in REFERENCE §12.5.

**cur `4AEIOUabcefgikruwy3@ov\|p`**

| prev group | contexts in priority order → parameters the rule sets |
|---|---|
| `K` | 1. cur = {EUY4} → A2 A3<br>2. otherwise → — |
| `BPDTtJCGQqMmNn~` | 1. otherwise → — |
| `4AEIOUabcefgikruwy3@ov\|jlLWYRpdhHVFxXZSzs␣` | 1. otherwise → — |

**cur `jlLWY`**

| prev group | contexts in priority order → parameters the rule sets |
|---|---|
| `BPDTtJCGKQqMmNn~` | 1. otherwise → — |
| `4AEIOUabcefgikruwy3@ov\|jlLWYRpdhHVFxXZSzs␣` | 1. otherwise → — |

**cur `R`**

| prev group | contexts in priority order → parameters the rule sets |
|---|---|
| `K` | 1. otherwise → A2 A3 AV AF AH VOT F2 F4 A5 A6 A4 |
| `P` | 1. next in stressed syllable and next is vowel → A2 VOT F1 F2 F3 F4<br>2. otherwise → — |
| `BDTtJCGQqMmNn~` | 1. otherwise → — |
| `4AEIOUabcefgikruwy3@ov\|jlLWYRpdhHVFxXZSzs␣` | 1. otherwise → — |

**cur `dH`**

| prev group | contexts in priority order → parameters the rule sets |
|---|---|
| `BPDTtJCGKQqMmNn~` | 1. cur = {H} and next is front vowel → p18 p19 F1 F2 F3 AH<br>2. cur = {H} and next is vowel and not next is back/rounded → p18 p19 F1 F2 F3 AH<br>3. cur = {H} → p18 p19 F1 F2 F3<br>4. otherwise → p18 p19 F1 F2 F3 |
| `4AEIOUabcefgikruwy3@ov\|jlLWYRpdhHVFxXZSzs␣` | 1. cur = {H} and next is front vowel → p18 p19 F1 F2 F3 AH<br>2. cur = {H} and next is vowel and not next is back/rounded → p18 p19 F1 F2 F3 AH<br>3. cur = {H} → p18 p19 F1 F2 F3<br>4. otherwise → p18 p19 F1 F2 F3 |

**cur `h`**

| prev group | contexts in priority order → parameters the rule sets |
|---|---|
| `BPDTtJCGKQqMmNn~` | 1. otherwise → — |
| `4AEIOUabcefgikruwy3@ov\|jlLWYRpdhHVFxXZSzs␣` | 1. otherwise → — |

**cur `B`**

| prev group | contexts in priority order → parameters the rule sets |
|---|---|
| `AEIaeiy\|Y` | 1. cur released (R) → p18 p19 W[F2] W[F3]<br>2. next ∈ V0 → p18 p19 W[F2] W[F3] F1 F2 F3 F4 B2 A2 A3 A4 A5<br>3. next ∈ V1 → p18 p19 F2 A2 W[F2] W[F3]<br>4. next ∈ V2 → p18 p19 A2 AB B2 F2 F3 F4 F1<br>5. not next = {3} and next ∈ V3 → p18 p19 F2 F3 F4 F1 A3 A4 A5 A2<br>6. next = {Z} → p18 p19 W[F2] W[F3] burstAF A2 AB<br>7. otherwise → p18 p19 W[F2] W[F3] |
| `uw@ovp` | 1. cur released (R) → p18 p19 W[F2] W[F3]<br>2. next ∈ V0 → p18 p19 W[F2] W[F3] F1 F2 F3 F4 B2 A2 A3 A4 A5<br>3. next ∈ V1 → p18 p19 F2 A2 W[F2] W[F3]<br>4. next ∈ V2 → p18 p19 A2 AB B2 F2 F3 F4 F1<br>5. not next = {3} and next ∈ V3 → p18 p19 F2 F3 F4 F1 A3 A4 A5 A2<br>6. next = {Z} → p18 p19 W[F2] W[F3] burstAF A2 AB<br>7. otherwise → p18 p19 W[F2] W[F3] |
| `4cgkr3R` | 1. cur released (R) → p18 p19 W[F2] W[F3] F3<br>2. next ∈ V0 → p18 p19 W[F2] W[F3] F1 F2 F3 F4 B2 A2 A3 A4 A5<br>3. next ∈ V1 → p18 p19 F2 A2 W[F2] W[F3] F3<br>4. next ∈ V2 → p18 p19 A2 AB B2 F2 F3 F4 F1<br>5. not next = {3} and next ∈ V3 → p18 p19 F2 F3 F4 F1 A3 A4 A5 A2<br>6. next = {Z} → p18 p19 W[F2] W[F3] F3 burstAF A2 AB<br>7. otherwise → p18 p19 W[F2] W[F3] F3 |
| `OUbfW` | 1. cur released (R) → p18 p19 W[F2] W[F3]<br>2. next ∈ V0 → p18 p19 W[F2] W[F3] F1 F2 F3 F4 B2 A2 A3 A4 A5<br>3. next ∈ V1 → p18 p19 F2 A2 W[F2] W[F3]<br>4. next ∈ V2 → p18 p19 A2 AB B2 F2 F3 F4 F1<br>5. not next = {3} and next ∈ V3 → p18 p19 F2 F3 F4 F1 A3 A4 A5 A2<br>6. next = {Z} → p18 p19 W[F2] W[F3] burstAF A2 AB<br>7. otherwise → p18 p19 W[F2] W[F3] |
| `jlLdhHBPDTtJCGKQqVFxXZzsMmNn~␣` | 1. cur released (R) → p18 p19<br>2. next ∈ V0 → p18 p19 W[F2] W[F3] F1 F2 F3 F4 B2 A2 A3 A4 A5<br>3. next ∈ V1 → p18 p19 F2 A2<br>4. next ∈ V2 → p18 p19 A2 AB B2 F2 F3 F4 F1<br>5. not next = {3} and next ∈ V3 → p18 p19 F2 F3 F4 F1 A3 A4 A5 A2<br>6. next = {Z} → p18 p19 burstAF A2 AB<br>7. otherwise → p18 p19 |
| `S` | 1. cur released (R) → p18 p19<br>2. next ∈ V0 → p18 p19 W[F2] W[F3] F1 F2 F3 F4 B2 A2 A3 A4 A5<br>3. next ∈ V1 → p18 p19 F2 A2<br>4. next ∈ V2 → p18 p19 A2 AB B2 F2 F3 F4 F1<br>5. not next = {3} and next ∈ V3 → p18 p19 F2 F3 F4 F1 A3 A4 A5 A2<br>6. next = {Z} → p18 p19 burstAF A2 AB<br>7. next = {L} → p18 p19 burstAF A2<br>8. otherwise → p18 p19 |

**cur `P`**

| prev group | contexts in priority order → parameters the rule sets |
|---|---|
| `AEIaeiy\|Y` | 1. cur released (R) → W[F2] W[F3]<br>2. next = {EiU} → F2 W[F2] W[F3]<br>3. next is front vowel and not next is high {4EUbciu\|WYh} → F2 W[F2] W[F3]<br>4. next = {S} → W[F2] W[F3] burstAF A2<br>5. next = {s} → A2 A3 A4 A5<br>6. next = {R} → A2 A5 AB F1 F2 F3 F4 burstAF burstAH A3 A4 A6<br>7. next = {L} → A2 A4 AB burstAF A3 F1 F2 F3<br>8. otherwise → W[F2] W[F3] |
| `uw@ovp` | 1. cur released (R) → W[F2] W[F3]<br>2. next = {EiU} → F2 W[F2] W[F3]<br>3. next is front vowel and not next is high {4EUbciu\|WYh} → F2 W[F2] W[F3]<br>4. next = {p} → W[F2] W[F3] burstAF burstAH<br>5. next = {S} → W[F2] W[F3] burstAF A2<br>6. next = {s} → A2 A3 A4 A5<br>7. next = {R} → A2 A5 AB F1 F2 F3 F4 burstAF burstAH A3 A4 A6<br>8. next = {L} → A2 A4 AB burstAF A3 F1 F2 F3<br>9. otherwise → W[F2] W[F3] |
| `4cgkr3R` | 1. cur released (R) → W[F2] W[F3] F3<br>2. next = {EiU} → F2 W[F2] W[F3] F3<br>3. next is front vowel and not next is high {4EUbciu\|WYh} → F2 W[F2] W[F3] F3<br>4. next = {S} → W[F2] W[F3] F3 burstAF A2<br>5. next = {s} → A2 A3 A4 A5<br>6. next = {R} → A2 A5 AB F1 F2 F3 F4 burstAF burstAH A3 A4 A6<br>7. next = {L} → A2 A4 AB burstAF A3 F1 F2 F3<br>8. otherwise → W[F2] W[F3] F3 |
| `OUbfW` | 1. cur released (R) → W[F2] W[F3]<br>2. next = {EiU} → F2 W[F2] W[F3]<br>3. next is front vowel and not next is high {4EUbciu\|WYh} → F2 W[F2] W[F3]<br>4. next = {p} → W[F2] W[F3] burstAF burstAH<br>5. next = {S} → W[F2] W[F3] burstAF A2<br>6. next = {s} → A2 A3 A4 A5<br>7. next = {R} → A2 A5 AB F1 F2 F3 F4 burstAF burstAH A3 A4 A6<br>8. next = {L} → A2 A4 AB burstAF A3 F1 F2 F3<br>9. otherwise → W[F2] W[F3] |
| `␣` | 1. cur released (R) → —<br>2. next = {i} → A2 A3 A5 A4 AB VOT F2 F3 W[F2] W[F3]<br>3. next = {EiU} → F2<br>4. next is front vowel and not next is high {4EUbciu\|WYh} → F2<br>5. next = {S} → burstAF A2<br>6. next = {s} → A2 A3 A4 A5<br>7. next = {R} → A2 A5 AB F1 F2 F3 F4 burstAF burstAH A3 A4 A6<br>8. next = {L} → A2 A4 AB burstAF A3 F1 F2 F3<br>9. otherwise → — |
| `jlLdhHBPDTtJCGKQqVFxXZzsMmNn~` | 1. cur released (R) → —<br>2. next = {EiU} → F2<br>3. next is front vowel and not next is high {4EUbciu\|WYh} → F2<br>4. next = {S} → burstAF A2<br>5. next = {s} → A2 A3 A4 A5<br>6. next = {R} → A2 A5 AB F1 F2 F3 F4 burstAF burstAH A3 A4 A6<br>7. next = {L} → A2 A4 AB burstAF A3 F1 F2 F3<br>8. otherwise → — |
| `S` | 1. cur released (R) → —<br>2. next = {i} → A2 A3 A5 A4 AB VOT F2 F3 W[F2] W[F3]<br>3. next = {EiU} → F2<br>4. next is front vowel and not next is high {4EUbciu\|WYh} → F2<br>5. next = {S} → burstAF A2<br>6. next = {s} → A2 A3 A4 A5<br>7. next = {R} → A2 A5 AB F1 F2 F3 F4 burstAF burstAH A3 A4 A6<br>8. next = {L} → A2 A4 AB burstAF A3 F1 F2 F3<br>9. otherwise → — |

**cur `D`**

| prev group | contexts in priority order → parameters the rule sets |
|---|---|
| `S` | 1. next = {p} → A5 A6 A4 A2 A3 F1 F2 F3 F4 AV p18 p19<br>2. next ∈ V0 → p18 p19 A2 A3 A4 A5 A6<br>3. next ∈ V1 → p18 p19 AB<br>4. next ∈ V2 → p18 p19 A5 A6<br>5. next = {3} → p18 p19 AB A3 A4 A6 A5<br>6. next ∈ V3 → p18 p19 A2 A3 A4<br>7. next = {R} → p18 p19 A3 A6 A5<br>8. next = {n} → burstAV F2 F3 F4 p18 p19 A3 A5 A6<br>9. cur released (R) → p18 p19<br>10. otherwise → p18 p19 |
| `␣` | 1. next = {p} → A5 A6 A4 A2 A3 F1 F2 F3 F4 AV p18 p19<br>2. not next in stressed syllable and next is sonorant → burstAV A3 A4 F2 AB burstAF<br>3. next ∈ V0 → p18 p19 F2 B3 A2 A3 A4 A5 A6<br>4. next ∈ V1 → p18 p19 A3 A4 A2 F2 F3<br>5. next ∈ V2 → p18 p19 A5 A6 F3<br>6. next = {3} → p18 p19 AB A3 A4 A6 A5<br>7. next ∈ V3 → p18 p19 A2 A3 A4 F2 F3 F4<br>8. next = {R} → p18 p19 A3 A6 A5<br>9. next = {n} → burstAV F2 F3 F4 p18 p19 A3 A5 A6<br>10. cur released (R) → p18 p19<br>11. otherwise → p18 p19 |
| `j` | 1. not next in stressed syllable → F2 F3 F4 burstAF burstAV A2 A3 A5 A6 A4<br>2. next = {p} → A5 A6 A4 A2 A3 F1 F2 F3 F4 AV p18 p19<br>3. prev voiced (vowel, sonorant or nasal) and not next in stressed syllable and next is sonorant → burstAV A3 A4 F2 burstAF<br>4. next ∈ V0 → p18 p19 F2 B3 A2 A3 A4 A5 A6<br>5. next ∈ V1 → p18 p19 A3 A4 A2 F2 F3<br>6. next ∈ V2 → p18 p19 A5 A6 F3<br>7. next = {3} → p18 p19 AB A3 A4 A6 A5<br>8. next ∈ V3 → p18 p19 A2 A3 A4 F2 F3 F4<br>9. next = {R} → p18 p19 A3 A6 A5<br>10. next = {n} → burstAV F2 F3 F4 p18 p19 A3 A5 A6<br>11. cur released (R) → p18 p19<br>12. otherwise → p18 p19 |
| `4AEIOUabcefgikruwy3@ov\|lLWYRpdhHBPDTtJCGKQqVFxXZzsMmNn~` | 1. next = {p} → A5 A6 A4 A2 A3 F1 F2 F3 F4 AV p18 p19<br>2. prev voiced (vowel, sonorant or nasal) and not next in stressed syllable and next ∈ V0 → burstAV A3 A4 F2 burstAF<br>3. next ∈ V0 → p18 p19 F2 B3 A2 A3 A4 A5 A6<br>4. next ∈ V1 → p18 p19 A3 A4 A2 F2 F3<br>5. next ∈ V2 → p18 p19 A5 A6 F3<br>6. next = {3} → p18 p19 AB A3 A4 A6 A5<br>7. next ∈ V3 → p18 p19 A2 A3 A4 F2 F3 F4<br>8. next = {R} → p18 p19 A3 A6 A5<br>9. next = {n} → burstAV F2 F3 F4 p18 p19 A3 A5 A6<br>10. cur released (R) → p18 p19<br>11. otherwise → p18 p19 |

**cur `T`**

| prev group | contexts in priority order → parameters the rule sets |
|---|---|
| `ZS` | 1. next = {p} → burstAF A3 A4 A5 A6 burstLen F4<br>2. not next in stressed syllable and next is vowel → burstLen burstAF burstAH A3 A5 A6 AB<br>3. next ∈ V0 → burstLen A2 A3 A4 A5 A6 F2 F3 F4<br>4. next = {a} → VOT A5 A6<br>5. next ∈ V1 → burstLen burstAH F2 F3 A2 A3 A4 A5 A6<br>6. next = {3} → AB A2 A3 A5 A6 F1 F2 F3 F4 A4 burstLen burstAF<br>7. next = {R} → burstLen F3 A3 A6 A5<br>8. next ∈ V2 → A3 F1 F3 F4<br>9. next ∈ V3 → burstLen A2 A3 A5 A6 F1 F2 F3 F4<br>10. next = {W} → A4 A5 A6 AB<br>11. next = {n} → burstAF A3 A6 F4 A4 A5<br>12. cur released (R) → —<br>13. otherwise → — |
| `␣` | 1. next = {p} → burstAF A3 A4 A5 A6 burstLen F4<br>2. not next in stressed syllable and next is vowel → burstLen burstAH burstAF VOT A2 A3 A4 A5 A6 F1 F2 F4<br>3. next ∈ V0 → burstLen A2 A3 A4 A5 A6 F2 F3 F4<br>4. next = {a} → VOT A5 A6<br>5. next ∈ V1 → burstLen burstAH F2 F3 A2 A3 A4 A5 A6<br>6. next = {3} → AB A2 A3 A5 A6 F1 F2 F3 F4 A4 burstLen burstAF<br>7. next = {R} → burstLen F3 A3 A6 A5<br>8. next ∈ V2 → A3 F1 F3 F4<br>9. next ∈ V3 → burstLen A2 A3 A5 A6 F1 F2 F3 F4<br>10. next = {W} → A4 A5 A6 AB<br>11. next = {n} → burstAF A3 A6 F4 A4 A5<br>12. cur released (R) → —<br>13. otherwise → — |
| `4AEIOUabcefgikruwy3@ov\|jlLWYRpdhHBPDTtJCGKQqVFxXzsMmNn~` | 1. next = {p} → burstAF A3 A4 A5 A6 burstLen F4<br>2. prev voiced (vowel, sonorant or nasal) and not next in stressed syllable and next is vowel → burstLen burstAF burstAH A4 AB<br>3. next ∈ V0 → burstLen A2 A3 A4 A5 A6 F2 F3 F4<br>4. next = {a} → VOT A5 A6<br>5. next ∈ V1 → burstLen burstAH F2 F3 A2 A3 A4 A5 A6<br>6. next = {3} → AB A2 A3 A5 A6 F1 F2 F3 F4 A4 burstLen burstAF<br>7. next = {R} → burstLen F3 A3 A6 A5<br>8. next ∈ V2 → A3 F1 F3 F4<br>9. next ∈ V3 → burstLen A2 A3 A5 A6 F1 F2 F3 F4<br>10. next = {W} → A4 A5 A6 AB<br>11. next = {n} → burstAF A3 A6 F4 A4 A5<br>12. cur released (R) → —<br>13. otherwise → — |

**cur `tQ`**

| prev group | contexts in priority order → parameters the rule sets |
|---|---|
| `4AEIOUabcefgikruwy3@ov\|jlLWYRpdhHBPDTtJCGKQqVFxXZSzsMmNn~␣` | 1. otherwise → AV |

**cur `Jq`**

| prev group | contexts in priority order → parameters the rule sets |
|---|---|
| `4AEIOUabcefgikruwy3@ov\|jlLWYRpdhHBPDTtJCGKQqVFxXZSzsMmNn~␣` | 1. cur = {J} and next = {z} and next+1 = {R} → p18 p19 A2 A3 burstAF F2<br>2. not cur released (R) → p18 p19<br>3. otherwise → p18 p19 |

**cur `C`**

| prev group | contexts in priority order → parameters the rule sets |
|---|---|
| `4AEIOUabcefgikruwy3@ov\|jlLWYRpdhHBPDTtJCGKQqVFxXZSzsMmNn~␣` | 1. next = {s} and next+1 is high {4EUbciu\|WYh} and next+1 is front vowel → A3 A4 A6 F3 A5 AB<br>2. next = {s} and not next+1 is high {4EUbciu\|WYh} and next+1 is front vowel → A2 A4 F3 A5 A6 B3 AB<br>3. next = {s} and not next+1 is front vowel and next+1 is back/rounded → A3 A2 A4 A5 A6 F3 AB<br>4. next = {s} and next+1 = {3} → A3 A2 A4 A5 A6 F3 AB<br>5. next = {s} and not next+1 is front vowel and not next+1 is back/rounded and next+1 is vowel → A3 A2 A4 F3 A5 A6 AB<br>6. next = {s} and next+1 = {R} → A3 A4 A5 A6 AB burstLen burstAF burstAH F2 F3<br>7. next = {s} and next+1 = {␣} → A3 A5 A6 AB A4 F2 F3 F4<br>8. not cur released (R) → —<br>9. otherwise → — |

**cur `G`**

| prev group | contexts in priority order → parameters the rule sets |
|---|---|
| `AE` | 1. next ∈ V0 → p18 p19 A2 A3 A4 A6 A5 F2 F3 F4<br>2. next ∈ V1 → AV A2 A3 A4 A5 p18 p19 F1 F2 F3 F4<br>3. next ∈ V2 → p18 p19 burstAF A2 A3 A4 A5 A6 B2 F1 F2 F3 F4<br>4. next = {3} → p18 p19 F2 F3 A5 A2 A3<br>5. next ∈ V3 → p18 p19 A2 A3 A5 burstAF F2 F3 F4<br>6. next = {R} → p18 p19 A4 A5 A6 A2 A3 F2 F3 AB burstAH<br>7. next = {p} → p18 p19 A4 A2 A3 A5 A6<br>8. next = {L} → p18 p19 F2 F3 A5 A6 A2 A3 A4 burstAF burstAH AB<br>9. next = {W} → p18 p19 burstAF A2 A3 A4 A5 A6 B2 F1 F2 F3 F4 AB<br>10. next = {Y} → p18 p19 AB AV A2 A3 A5 F2 F3<br>11. cur released (R) → p18 p19<br>12. not next is syllabic → p18 p19<br>13. otherwise → p18 p19 A4 A2 A3 A5 A6 |
| `Iaeiy\|` | 1. next ∈ V0 → p18 p19 A2 A3 A4 A6 A5 F2 F3 F4<br>2. next ∈ V1 → AV A2 A3 A4 A5 p18 p19 F1 F2 F3 F4<br>3. next ∈ V2 → p18 p19 burstAF A2 A3 A4 A5 A6 B2 F1 F2 F3 F4<br>4. next = {3} → p18 p19 F2 F3 A5 A2 A3<br>5. next ∈ V3 → p18 p19 A2 A3 A5 burstAF F2 F3 F4<br>6. next = {R} → p18 p19 A4 A5 A6 A2 A3 F2 F3 AB burstAH<br>7. next = {p} → p18 p19 A4 A2 A3 A6 A5 F2 F3<br>8. next = {L} → p18 p19 F2 F3 A5 A6 A2 A3 A4 burstAF burstAH AB<br>9. next = {W} → p18 p19 burstAF A2 A3 A4 A5 A6 B2 F1 F2 F3 F4 AB<br>10. next = {Y} → p18 p19 AB AV A2 A3 A5 F2 F3<br>11. cur released (R) → p18 p19<br>12. not next is syllabic → p18 p19<br>13. otherwise → p18 p19 A4 A2 A3 A6 A5 F2 F3 |
| `OUbf` | 1. next ∈ V0 → p18 p19 A2 A3 A4 A6 A5 F2 F3 F4<br>2. next ∈ V1 → AV A2 A3 A4 A5 p18 p19 F1 F2 F3 F4<br>3. next ∈ V2 → p18 p19 burstAF A2 A3 A4 A5 A6 B2 F1 F2 F3 F4<br>4. next = {3} → p18 p19 F2 F3 A5 A2 A3<br>5. next ∈ V3 → p18 p19 A2 A3 A5 burstAF F2 F3 F4<br>6. next = {R} → p18 p19 A4 A5 A6 A2 A3 F2 F3 AB burstAH<br>7. next = {p} → p18 p19 A2 A3 A4 A5 A6 F2 F3<br>8. next = {L} → p18 p19 F2 F3 A5 A6 A2 A3 A4 burstAF burstAH AB<br>9. next = {W} → p18 p19 burstAF A2 A3 A4 A5 A6 B2 F1 F2 F3 F4 AB<br>10. next = {Y} → p18 p19 AB AV A2 A3 A5 F2 F3<br>11. cur released (R) → p18 p19 F2 F3<br>12. not next is syllabic → p18 p19 F2 F3<br>13. otherwise → p18 p19 A2 A3 A4 A5 A6 F2 F3 |
| `uw@ov` | 1. next ∈ V0 → p18 p19 A2 A3 A4 A6 A5 F2 F3 F4<br>2. next ∈ V1 → AV A2 A3 A4 A5 p18 p19 F1 F2 F3 F4<br>3. next ∈ V2 → p18 p19 burstAF A2 A3 A4 A5 A6 B2 F1 F2 F3 F4<br>4. next = {3} → p18 p19 F2 F3 A5 A2 A3<br>5. next ∈ V3 → p18 p19 A2 A3 A5 burstAF F2 F3 F4<br>6. next = {R} → p18 p19 A4 A5 A6 A2 A3 F2 F3 AB burstAH<br>7. next = {p} → p18 p19 F2 F3<br>8. next = {L} → p18 p19 F2 F3 A5 A6 A2 A3 A4 burstAF burstAH AB<br>9. next = {W} → p18 p19 burstAF A2 A3 A4 A5 A6 B2 F1 F2 F3 F4 AB<br>10. next = {Y} → p18 p19 AB AV A2 A3 A5 F2 F3<br>11. cur released (R) → p18 p19 F2 F3<br>12. not next is syllabic → p18 p19 F2 F3<br>13. otherwise → p18 p19 F2 F3 |
| `4cgkr3` | 1. next ∈ V0 → p18 p19 A2 A3 A4 A6 A5 F2 F3 F4<br>2. next ∈ V1 → AV A2 A3 A4 A5 p18 p19 F1 F2 F3 F4<br>3. next ∈ V2 → p18 p19 burstAF A2 A3 A4 A5 A6 B2 F1 F2 F3 F4<br>4. next = {3} → p18 p19 F2 F3 A5 A2 A3<br>5. next ∈ V3 → p18 p19 A2 A3 A5 burstAF F2 F3 F4<br>6. next = {R} → p18 p19 A4 A5 A6 A2 A3 F2 F3 AB burstAH<br>7. next = {p} → p18 p19 A3 A4 A2 A5 A6<br>8. next = {L} → p18 p19 F2 F3 A5 A6 A2 A3 A4 burstAF burstAH AB<br>9. next = {W} → p18 p19 burstAF A2 A3 A4 A5 A6 B2 F1 F2 F3 F4 AB<br>10. next = {Y} → p18 p19 AB AV A2 A3 A5 F2 F3<br>11. cur released (R) → p18 p19 F2 F3<br>12. not next is syllabic → p18 p19 F2 F3<br>13. otherwise → p18 p19 A3 A4 A2 A5 A6 |
| `jlLWYRpdhHBPDTtJCGKQqVFxXZSzsMmNn~␣` | 1. next ∈ V0 → p18 p19 A2 A3 A4 A6 A5 F2 F3 F4<br>2. next ∈ V1 → AV A2 A3 A4 A5 p18 p19 F1 F2 F3 F4<br>3. next ∈ V2 → p18 p19 burstAF A2 A3 A4 A5 A6 B2 F1 F2 F3 F4<br>4. next = {3} → p18 p19 F2 F3 A5 A2 A3<br>5. next ∈ V3 → p18 p19 A2 A3 A5 burstAF F2 F3 F4<br>6. next = {R} → p18 p19 A4 A5 A6 A2 A3 F2 F3 AB burstAH<br>7. next = {p} → p18 p19<br>8. next = {L} → p18 p19 F2 F3 A5 A6 A2 A3 A4 burstAF burstAH AB<br>9. next = {W} → p18 p19 burstAF A2 A3 A4 A5 A6 B2 F1 F2 F3 F4 AB<br>10. next = {Y} → p18 p19 AB AV A2 A3 A5 F2 F3<br>11. cur released (R) → p18 p19<br>12. not next is syllabic → p18 p19<br>13. otherwise → p18 p19 |

**cur `K`**

| prev group | contexts in priority order → parameters the rule sets |
|---|---|
| `AE` | 1. next ∈ V0 → A2 A3 A4 A6 A5 F2 F3 F4<br>2. next ∈ V1 → A4 A3 A5 A2 F2 F3 F4<br>3. next ∈ V2 → A2 A3 A4 A5 B3 F4 burstAF<br>4. next = {3} → A4 A2 A3 A5 A6 F2 F3 F4<br>5. next ∈ V3 → A2 A5 A6 A3 A4 AB F2 F3 F4<br>6. next = {R} → W[F4] F2 F3 F4 A3 A4 AB A5<br>7. next = {p} → A5 AB A3 F2 F3 F4<br>8. next = {L} → burstAF A2 A3 F2 F3 AB A5<br>9. next = {W} → burstAF A3 A4 A5 A2 AB F2 F3<br>10. next = {l} → A2 A3 A4 A6 A5 F2 F3 AB<br>11. next = {Y} → A5 AB A2 A4 A6 AV burstAF burstAH F2 F3<br>12. next = {s} → burstAF A3 AB F2 F3 A4 A5<br>13. next is fricative and not next = {s} → A3 A4 F2 F3 A5 AB<br>14. next is stop/affricate → A4 F2 F3<br>15. cur released (R) → A5 AB A3 F2 F3 F4<br>16. otherwise → A5 AB A3 F2 F3 F4 |
| `Iaeiy\|` | 1. next ∈ V0 → A2 A3 A4 A6 A5 F2 F3 F4<br>2. next ∈ V1 → A4 A3 A5 A2 F2 F3 F4<br>3. next ∈ V2 → A2 A3 A4 A5 B3 F4 burstAF<br>4. next = {3} → A4 A2 A3 A5 A6 F2 F3 F4<br>5. next ∈ V3 → A2 A5 A6 A3 A4 AB F2 F3 F4<br>6. next = {R} → W[F4] F2 F3 F4 A3 A4 AB A5<br>7. next = {p} → A2 A3 A5 AB F2 F3<br>8. next = {L} → burstAF A2 A3 F2 F3 AB A5<br>9. next = {W} → burstAF A3 A4 A5 A2 AB F2 F3<br>10. next = {l} → A2 A3 A4 A6 A5 F2 F3 AB<br>11. next = {Y} → A5 AB A2 A4 A6 AV burstAF burstAH F2 F3<br>12. next = {s} → burstAF A3 AB F2 F3 A4 A5<br>13. next is fricative and not next = {s} → A3 A4 F2 F3 A5 AB<br>14. next is stop/affricate → A4 F2 F3<br>15. cur released (R) → A2 A3 A5 AB F2 F3<br>16. otherwise → A2 A3 A5 AB F2 F3 |
| `OUbf` | 1. next ∈ V0 → A2 A3 A4 A6 A5 F2 F3 F4<br>2. next ∈ V1 → A4 A3 A5 A2 F2 F3 F4<br>3. next ∈ V2 → A2 A3 A4 A5 B3 F4 burstAF<br>4. next = {3} → A4 A2 A3 A5 A6 F2 F3 F4<br>5. next ∈ V3 → A2 A5 A6 A3 A4 AB F2 F3 F4<br>6. next = {R} → W[F4] F2 F3 F4 A3 A4 AB A5<br>7. next = {p} → AB A2 A5 burstAF burstAH A3 A4 F2 F3<br>8. next = {L} → burstAF A2 A3 F2 F3 AB A5<br>9. next = {W} → burstAF A3 A4 A5 A2 AB F2 F3<br>10. next = {l} → A2 A3 A4 A6 A5 F2 F3 AB<br>11. next = {Y} → A5 AB A2 A4 A6 AV burstAF burstAH F2 F3<br>12. next = {s} → burstAF A3 AB F2 F3 A4 A5<br>13. next is fricative and not next = {s} → A3 A4 F2 F3 A5 AB<br>14. next is stop/affricate → A4 F2 F3<br>15. cur released (R) → AB A2 A5 burstAF burstAH A3 A4 F2 F3<br>16. otherwise → AB A2 A5 burstAF burstAH A3 A4 F2 F3 |
| `uw@ov` | 1. next ∈ V0 → A2 A3 A4 A6 A5 F2 F3 F4<br>2. next ∈ V1 → A4 A3 A5 A2 F2 F3 F4<br>3. next ∈ V2 → A2 A3 A4 A5 B3 F4 burstAF<br>4. next = {3} → A4 A2 A3 A5 A6 F2 F3 F4<br>5. next ∈ V3 → A2 A5 A6 A3 A4 AB F2 F3 F4<br>6. next = {R} → W[F4] F2 F3 F4 A3 A4 AB A5<br>7. next = {p} → A5 A3 A4 F2 F3<br>8. next = {L} → burstAF A2 A3 F2 F3 AB A5<br>9. next = {W} → burstAF A3 A4 A5 A2 AB F2 F3<br>10. next = {l} → A2 A3 A4 A6 A5 F2 F3 AB<br>11. next = {Y} → A5 AB A2 A4 A6 AV burstAF burstAH F2 F3<br>12. next = {s} → burstAF A3 AB F2 F3 A4 A5<br>13. next is fricative and not next = {s} → A3 A4 F2 F3 A5 AB<br>14. next is stop/affricate → A4 F2 F3<br>15. cur released (R) → A5 A3 A4 F2 F3<br>16. otherwise → A5 A3 A4 F2 F3 |
| `4cgkr3` | 1. next ∈ V0 → A2 A3 A4 A6 A5 F2 F3 F4<br>2. next ∈ V1 → A4 A3 A5 A2 F2 F3 F4<br>3. next ∈ V2 → A2 A3 A4 A5 B3 F4 burstAF<br>4. next = {3} → A4 A2 A3 A5 A6 F2 F3 F4<br>5. next ∈ V3 → A2 A5 A6 A3 A4 AB F2 F3 F4<br>6. next = {R} → W[F4] F2 F3 F4 A3 A4 AB A5<br>7. next = {p} → A2 A3 A5 AB F2 F3<br>8. next = {L} → burstAF A2 A3 F2 F3 AB A5<br>9. next = {W} → burstAF A3 A4 A5 A2 AB F2 F3<br>10. next = {l} → A2 A3 A4 A6 A5 F2 F3 AB<br>11. next = {Y} → A5 AB A2 A4 A6 AV burstAF burstAH F2 F3<br>12. next = {s} → burstAF A3 AB F2 F3 A4 A5<br>13. next is fricative and not next = {s} → A3 A4 F2 F3 A5 AB<br>14. next is stop/affricate → A4 F2 F3<br>15. cur released (R) → A2 A3 A5 AB F2 F3<br>16. otherwise → A2 A3 A5 AB F2 F3 |
| `jlLWYRpdhHBPDTtJCGKQqVFxXZSzsMmNn~␣` | 1. next ∈ V0 → A2 A3 A4 A6 A5 F2 F3 F4<br>2. next ∈ V1 → A4 A3 A5 A2 F2 F3 F4<br>3. next ∈ V2 → A2 A3 A4 A5 B3 F4 burstAF<br>4. next = {3} → A4 A2 A3 A5 A6 F2 F3 F4<br>5. next ∈ V3 → A2 A5 A6 A3 A4 AB F2 F3 F4<br>6. next = {R} → W[F4] F2 F3 F4 A3 A4 AB A5<br>7. next = {p} → A2 A5 A3 A4 F2 F3<br>8. next = {L} → burstAF A2 A3 F2 F3 AB A5<br>9. next = {W} → burstAF A3 A4 A5 A2 AB F2 F3<br>10. next = {l} → A2 A3 A4 A6 A5 F2 F3 AB<br>11. next = {Y} → A5 AB A2 A4 A6 AV burstAF burstAH F2 F3<br>12. next = {s} → burstAF A3 AB F2 F3 A4 A5<br>13. next is fricative and not next = {s} → A3 A4 F2 F3 A5 AB<br>14. next is stop/affricate → A4 F2 F3<br>15. cur released (R) → A2 A5 A3 A4 F2 F3<br>16. otherwise → A2 A5 A3 A4 F2 F3 |

**cur `VF`**

| prev group | contexts in priority order → parameters the rule sets |
|---|---|
| `AEIaeiy\|Y` | 1. cur voiced and next is back/rounded → p18 p19 p18.durF p19.durF F2 W[F2] W[F3]<br>2. cur voiced → p18 p19 p18.durF p19.durF F2 W[F2] W[F3]<br>3. next is back/rounded → F2 W[F2] W[F3]<br>4. otherwise → F2 W[F2] W[F3] |
| `4OUbcfgkruw3@ovWRpdhH␣` | 1. cur voiced and next is front vowel → p18 p19 p18.durF p19.durF F2 W[F2] W[F3]<br>2. cur voiced and next is back/rounded → p18 p19 p18.durF p19.durF F2 W[F2] W[F3]<br>3. cur voiced → p18 p19 p18.durF p19.durF W[F2] W[F3]<br>4. next is front vowel → F2 W[F2] W[F3]<br>5. next is back/rounded → F2 W[F2] W[F3]<br>6. otherwise → W[F2] W[F3] |
| `lLVFxXZSzs` | 1. cur voiced and next is front vowel → p18 p19 p18.durF p19.durF F2<br>2. cur voiced and next is back/rounded → p18 p19 p18.durF p19.durF F2<br>3. cur voiced → p18 p19 p18.durF p19.durF<br>4. next is front vowel → F2<br>5. next is back/rounded → F2<br>6. otherwise → — |
| `j` | 1. cur voiced and next is front vowel → p18 p19 p18.durF p19.durF F2 W[F2] W[F3]<br>2. cur voiced and next is back/rounded → p18 p19 p18.durF p19.durF F2 W[F2] W[F3]<br>3. cur voiced → p18 p19 p18.durF p19.durF W[F2] W[F3]<br>4. next is front vowel → F2 W[F2] W[F3]<br>5. next is back/rounded → F2 W[F2] W[F3]<br>6. next = {S} → AH A2<br>7. otherwise → W[F2] W[F3] |
| `BPDTtJCGKQqMmNn~` | 1. cur voiced and next is front vowel → p18 p19 p18.durF p19.durF F2<br>2. cur voiced and next is back/rounded → p18 p19 p18.durF p19.durF F2<br>3. cur voiced → p18 p19 p18.durF p19.durF<br>4. next is front vowel → F2<br>5. next is back/rounded → F2<br>6. otherwise → — |

**cur `x`**

| prev group | contexts in priority order → parameters the rule sets |
|---|---|
| `␣` | 1. not next is vowel → p18 p19 p18.durF p19.durF F2 A6 AV<br>2. otherwise → p18 p19 p18.durF p19.durF F2 A6 |
| `BPDTtJCGKQqMmNn~` | 1. not next is vowel → p18 p19 p18.durF p19.durF AV<br>2. otherwise → p18 p19 p18.durF p19.durF |
| `4AEIOUabcefgikruwy3@ov\|p` | 1. not next is vowel → p18 p19 p18.durF p19.durF W[F2] W[F3] AV<br>2. otherwise → p18 p19 p18.durF p19.durF W[F2] W[F3] |
| `jlLWYRdhHVFxXZSzs` | 1. not next is vowel → p18 p19 p18.durF p19.durF AV<br>2. otherwise → p18 p19 p18.durF p19.durF |

**cur `X`**

| prev group | contexts in priority order → parameters the rule sets |
|---|---|
| `␣` | 1. next ∈ V0 → F1.type F1 F2 F3 AF AH AB A2 A3 A4 A5 A6<br>2. next ∈ V1 → F1.type F1 F2 F4 AF AH AB A2 A3 A4 A5 A6<br>3. next ∈ V2 → F1 F2 AF AH AB A2 A3 A4 A5 A6<br>4. next = {3} → F1.type F1 F2 AF AH AB A2 A3 A4 A5 A6<br>5. next ∈ V3 → AF AH AB A2 A3 A4 A5 A6<br>6. next = {R} → AF.durF AH.durF F1 F3 AF AH AB A2 A3 A4 A5 A6<br>7. otherwise → — |
| `BPDTtJCGKQqMmNn~` | 1. next ∈ V0 → F1.type F1 F2 F3 AF AH AB A2 A3 A4 A5 A6<br>2. next ∈ V1 → F1.type F1 F2 F4 AF AH AB A2 A3 A4 A5 A6<br>3. next ∈ V2 → F1 F2 AF AH AB A2 A3 A4 A5 A6<br>4. next = {3} → F1.type F1 F2 AF AH AB A2 A3 A4 A5 A6<br>5. next ∈ V3 → AF AH AB A2 A3 A4 A5 A6<br>6. next = {R} → AF.durF AH.durF F1 F3 AF AH AB A2 A3 A4 A5 A6<br>7. otherwise → — |
| `4AEIOUabcefgikruwy3@ov\|p` | 1. next ∈ V0 → F1.type F1 F2 F3 AF AH AB A2 A3 A4 A5 A6<br>2. next ∈ V1 → F1.type F1 F2 F4 AF AH AB A2 A3 A4 A5 A6<br>3. next ∈ V2 → F1 F2 AF AH AB A2 A3 A4 A5 A6<br>4. next = {3} → F1.type F1 F2 AF AH AB A2 A3 A4 A5 A6<br>5. next ∈ V3 → AF AH AB A2 A3 A4 A5 A6<br>6. next = {R} → AF.durF AH.durF F1 F3 AF AH AB A2 A3 A4 A5 A6<br>7. otherwise → W[F2] W[F3] |
| `jlLWYRdhHVFxXZSzs` | 1. next ∈ V0 → F1.type F1 F2 F3 AF AH AB A2 A3 A4 A5 A6<br>2. next ∈ V1 → F1.type F1 F2 F4 AF AH AB A2 A3 A4 A5 A6<br>3. next ∈ V2 → F1 F2 AF AH AB A2 A3 A4 A5 A6<br>4. next = {3} → F1.type F1 F2 AF AH AB A2 A3 A4 A5 A6<br>5. next ∈ V3 → AF AH AB A2 A3 A4 A5 A6<br>6. next = {R} → AF.durF AH.durF F1 F3 AF AH AB A2 A3 A4 A5 A6<br>7. otherwise → — |

**cur `Z`**

| prev group | contexts in priority order → parameters the rule sets |
|---|---|
| `4AEIOUabcefgikruwy3@ov\|p` | 1. next ∈ V0 → p18 p19 p18.durF p19.durF B1 F2 W[F2] W[F3] A5 F3 AF F1<br>2. next ∈ V1 → p18 p19 p18.durF p19.durF AV AF A5 F2 F3 F4 F1<br>3. next ∈ V2 → p18 p19 p18.durF p19.durF AV F1 F2 F3 F4 AF A6 A4 A5<br>4. next = {3} → p18 p19 p18.durF p19.durF AV AF A5 A6 F1 F2 F3 F4<br>5. next ∈ V3 → p18 p19 p18.durF p19.durF A2 A3 AF F1 F2 F3 F4 A4 A5<br>6. next = {␣} → p18 p19 p18.durF p19.durF AV AF A5 AF.durF AH A4 F1 F4 A3 A6<br>7. next is stop/affricate → p18 p19 p18.durF p19.durF AF F1 AV AH AF.durF<br>8. not next is vowel → p18 p19 p18.durF p19.durF AV AF F1 A5 A6 A3<br>9. otherwise → p18 p19 p18.durF p19.durF W[F2] W[F3] AV AF A5 |
| `BPDTtJCGKQqMmNn~` | 1. next ∈ V0 → p18 p19 p18.durF p19.durF B1 F2 W[F2] W[F3] A5 F3 AF F1<br>2. next ∈ V1 → p18 p19 p18.durF p19.durF AV AF A5 F2 F3 F4 F1<br>3. next ∈ V2 → p18 p19 p18.durF p19.durF AV F1 F2 F3 F4 AF A6 A4 A5<br>4. next = {3} → p18 p19 p18.durF p19.durF AV AF A5 A6 F1 F2 F3 F4<br>5. next ∈ V3 → p18 p19 p18.durF p19.durF A2 A3 AF F1 F2 F3 F4 A4 A5<br>6. next = {␣} → p18 p19 p18.durF p19.durF AV AF A5 AF.durF AH A4 F1 F4 A3 A6<br>7. next is stop/affricate → p18 p19 p18.durF p19.durF AF F1 AV AH AF.durF<br>8. not next is vowel → p18 p19 p18.durF p19.durF AV A4 AF.durF AF A3 A5 A6<br>9. otherwise → p18 p19 p18.durF p19.durF AV AF A5 |
| `jlLWYRdhHVFxXZSzs␣` | 1. next ∈ V0 → p18 p19 p18.durF p19.durF B1 F2 W[F2] W[F3] A5 F3 AF F1<br>2. next ∈ V1 → p18 p19 p18.durF p19.durF AV AF A5 F2 F3 F4 F1<br>3. next ∈ V2 → p18 p19 p18.durF p19.durF AV F1 F2 F3 F4 AF A6 A4 A5<br>4. next = {3} → p18 p19 p18.durF p19.durF AV AF A5 A6 F1 F2 F3 F4<br>5. next ∈ V3 → p18 p19 p18.durF p19.durF A2 A3 AF F1 F2 F3 F4 A4 A5<br>6. next = {␣} → p18 p19 p18.durF p19.durF AV AF A5 AF.durF AH A4 F1 F4 A3 A6<br>7. next is stop/affricate → p18 p19 p18.durF p19.durF AF F1 AV AH AF.durF<br>8. not next is vowel → p18 p19 p18.durF p19.durF AV A4 AF.durF AF A3 A5 A6<br>9. otherwise → p18 p19 p18.durF p19.durF AV AF A5 |

**cur `S`**

| prev group | contexts in priority order → parameters the rule sets |
|---|---|
| `AE` | 1. next ∈ V0 → AF A2 A3 A4 A5 A6 AH AB F2 F3 F4<br>2. next ∈ V1 → AF A2 A3 A4 A5 A6 AB AH B1 B3 F2 F3 F4<br>3. next ∈ V2 → AF A2 A3 A4 A5 A6 AB AH F2<br>4. next = {3} → AF A4 A5 A6 AB AH A2 A3 F3 F4<br>5. next ∈ V3 → AF A2 A3 A4 A5 A6 AB AH F2 F3<br>6. next is closure → A2 A3 A4 A5 A6 AB AF<br>7. next = {␣} → A6 AF AH A4 A5<br>8. next is fricative → AF AH A4 A5 AB A6<br>9. otherwise → AF AH A4 A5 A6 |
| `Iaeiy\|` | 1. next ∈ V0 → AF A2 A3 A4 A5 A6 AH AB F2 F3 F4<br>2. next ∈ V1 → AF A2 A3 A4 A5 A6 AB AH B1 B3 F2 F3 F4<br>3. next ∈ V2 → AF A2 A3 A4 A5 A6 AB AH F2<br>4. next = {3} → AF A4 A5 A6 AB AH A2 A3 F3 F4<br>5. next ∈ V3 → AF A2 A3 A4 A5 A6 AB AH F2 F3<br>6. next is closure → A2 A3 A4 A5 A6 AB AF<br>7. next = {␣} → A6 AF AH A4 A5<br>8. next is fricative → AF AH A4 A5 AB A6<br>9. otherwise → AF AH A4 A5 A6 |
| `OUbf` | 1. next ∈ V0 → AF A2 A3 A4 A5 A6 AH AB F2 F3 F4<br>2. next ∈ V1 → AF A2 A3 A4 A5 A6 AB AH B1 B3 F2 F3 F4<br>3. next ∈ V2 → AF A2 A3 A4 A5 A6 AB AH F2<br>4. next = {3} → AF A4 A5 A6 AB AH A2 A3 F3 F4<br>5. next ∈ V3 → AF A2 A3 A4 A5 A6 AB AH F2 F3<br>6. next is closure → A2 A3 A4 A5 A6 AB AF<br>7. next = {␣} → A6 AF AH A4 A5<br>8. next is fricative → AF AH A4 A5 AB A6<br>9. otherwise → AF AH A4 A5 A6 |
| `uw@ovp` | 1. next ∈ V0 → AF A2 A3 A4 A5 A6 AH AB F2 F3 F4<br>2. next ∈ V1 → AF A2 A3 A4 A5 A6 AB AH B1 B3 F2 F3 F4<br>3. next ∈ V2 → AF A2 A3 A4 A5 A6 AB AH F2<br>4. next = {3} → AF A4 A5 A6 AB AH A2 A3 F3 F4<br>5. next ∈ V3 → AF A2 A3 A4 A5 A6 AB AH F2 F3<br>6. next is closure → A2 A3 A4 A5 A6 AB AF<br>7. next = {␣} → A6 AF AH A4 A5<br>8. next is fricative → AF AH A4 A5 AB A6<br>9. otherwise → AF AH A4 A5 A6 |
| `4cgkr3` | 1. next ∈ V0 → AF A2 A3 A4 A5 A6 AH AB F2 F3 F4<br>2. next ∈ V1 → AF A2 A3 A4 A5 A6 AB AH B1 B3 F2 F3 F4<br>3. next ∈ V2 → AF A2 A3 A4 A5 A6 AB AH F2<br>4. next = {3} → AF A4 A5 A6 AB AH A2 A3 F3 F4<br>5. next ∈ V3 → AF A2 A3 A4 A5 A6 AB AH F2 F3<br>6. next is closure → A2 A3 A4 A5 A6 AB AF<br>7. next = {␣} → A6 AF AH A4 A5<br>8. next is fricative → AF AH A4 A5 AB A6<br>9. otherwise → AF AH A4 A5 A6 |
| `BPDTtJCGKQqMmNn~` | 1. next ∈ V0 → AF A2 A3 A4 A5 A6 AH AB F2 F3 F4<br>2. next ∈ V1 → AF A2 A3 A4 A5 A6 AB AH B1 B3 F2 F3 F4<br>3. next ∈ V2 → AF A2 A3 A4 A5 A6 AB AH F2<br>4. next = {3} → AF A4 A5 A6 AB AH A2 A3 F3 F4<br>5. next ∈ V3 → AF A2 A3 A4 A5 A6 AB AH F2 F3<br>6. next is closure → A2 A3 A4 A5 A6 AB AF<br>7. next = {␣} → A6 AF AH A4 A5<br>8. next is fricative → AF AH A4 A5 AB A6<br>9. otherwise → AF AH A4 A5 A6 |
| `jlLWYRdhHVFxXZSzs␣` | 1. next ∈ V0 → AF A2 A3 A4 A5 A6 AH AB F2 F3 F4<br>2. next ∈ V1 → AF A2 A3 A4 A5 A6 AB AH B1 B3 F2 F3 F4<br>3. next ∈ V2 → AF A2 A3 A4 A5 A6 AB AH F2<br>4. next = {3} → AF A4 A5 A6 AB AH A2 A3 F3 F4<br>5. next ∈ V3 → AF A2 A3 A4 A5 A6 AB AH F2 F3<br>6. next is closure → A2 A3 A4 A5 A6 AB AF<br>7. next = {␣} → A6 AF AH A4 A5<br>8. next is fricative → AF AH A4 A5 AB A6<br>9. otherwise → AF AH A4 A5 A6 |

**cur `z`**

| prev group | contexts in priority order → parameters the rule sets |
|---|---|
| `BPDTtJCGKQqMmNn~` | 1. next is back/rounded → p18 p19 p18.durF p19.durF F2 F3<br>2. not next is vowel → p18 p19 p18.durF p19.durF AV<br>3. otherwise → p18 p19 p18.durF p19.durF |
| `4AEIOUabcefgikruwy3@ov\|p` | 1. next is back/rounded → p18 p19 p18.durF p19.durF F2 F3 W[F2] W[F3]<br>2. not next is vowel → p18 p19 p18.durF p19.durF W[F2] W[F3] AV<br>3. otherwise → p18 p19 p18.durF p19.durF W[F2] W[F3] |
| `jlLWYRdhHVFxXZSzs␣` | 1. next is back/rounded → p18 p19 p18.durF p19.durF F2 F3<br>2. not next is vowel → p18 p19 p18.durF p19.durF AV<br>3. otherwise → p18 p19 p18.durF p19.durF |

**cur `s`**

| prev group | contexts in priority order → parameters the rule sets |
|---|---|
| `BPDTtJCGKQqMmNn~` | 1. next = {@\|} → AH A4 A5 A6 F3 F4 A3 AB<br>2. next ∈ V0 → A5 A4 AH AF A3 A6 F3<br>3. next ∈ V1 → A5 A4 AF A3 A6<br>4. next ∈ V2 → AH A6 A5 A4 AF F3 F4<br>5. next = {3} → AH A6 A5 A4 AF F3<br>6. next ∈ V3 → A5 A4 AH F4 A3 A6<br>7. not cur voiced and next is back/rounded → F2 F3<br>8. otherwise → A4 A5 A6 A3 |
| `AE` | 1. next ∈ V0 → A5 A4 AH AF A3 A6 F3<br>2. next ∈ V1 → A5 A4 AF A3 A6<br>3. next ∈ V2 → AH A6 A5 A4 AF F3 F4<br>4. next = {3} → AH A6 A5 A4 AF F3<br>5. next ∈ V3 → A5 A4 AH F4 A3 A6<br>6. not cur voiced and next is back/rounded → F2 F3<br>7. otherwise → A5 A6 B3 |
| `Iaeiy\|` | 1. next ∈ V0 → A5 A4 AH AF A3 A6 F3<br>2. next ∈ V1 → A5 A4 AF A3 A6<br>3. next ∈ V2 → AH A6 A5 A4 AF F3 F4<br>4. next = {3} → AH A6 A5 A4 AF F3<br>5. next ∈ V3 → A5 A4 AH F4 A3 A6<br>6. not cur voiced and next is back/rounded → F2 F3<br>7. otherwise → A5 A6 F3 F4 A3 A4 |
| `OUbf` | 1. next ∈ V0 → A5 A4 AH AF A3 A6 F3<br>2. next ∈ V1 → A5 A4 AF A3 A6<br>3. next ∈ V2 → AH A6 A5 A4 AF F3 F4<br>4. next = {3} → AH A6 A5 A4 AF F3<br>5. next ∈ V3 → A5 A4 AH F4 A3 A6<br>6. not cur voiced and next is back/rounded → F2 F3<br>7. otherwise → F2 A2 A3 A4 A5 A6 |
| `uw@ovp` | 1. next ∈ V0 → A5 A4 AH AF A3 A6 F3<br>2. next ∈ V1 → A5 A4 AF A3 A6<br>3. next ∈ V2 → AH A6 A5 A4 AF F3 F4<br>4. next = {3} → AH A6 A5 A4 AF F3<br>5. next ∈ V3 → A5 A4 AH F4 A3 A6<br>6. not cur voiced and next is back/rounded → F2 F3<br>7. otherwise → A4 A5 A6 F4 |
| `4cgkr3` | 1. next ∈ V0 → A5 A4 AH AF A3 A6 F3<br>2. next ∈ V1 → A5 A4 AF A3 A6<br>3. next ∈ V2 → AH A6 A5 A4 AF F3 F4<br>4. next = {3} → AH A6 A5 A4 AF F3<br>5. next ∈ V3 → A5 A4 AH F4 A3 A6<br>6. not cur voiced and next is back/rounded → F2 F3<br>7. otherwise → A3 F3 F4 A5 A6 |
| `jlLWYRdhHVFxXZSzs␣` | 1. next ∈ V0 → A5 A4 AH AF A3 A6 F3<br>2. next ∈ V1 → A5 A4 AF A3 A6<br>3. next ∈ V2 → AH A6 A5 A4 AF F3 F4<br>4. next = {3} → AH A6 A5 A4 AF F3<br>5. next ∈ V3 → A5 A4 AH F4 A3 A6<br>6. not cur voiced and next is back/rounded → F2 F3<br>7. otherwise → A4 A5 A6 A3 |

**cur `Mm~`**

| prev group | contexts in priority order → parameters the rule sets |
|---|---|
| `AEIaeiy\|Y` | 1. cur = {Mm} → W[F2] W[F3]<br>2. otherwise → — |
| `4cgkr3R` | 1. cur = {Mm} → F3 W[F2] W[F3]<br>2. otherwise → — |
| `OUbfuw@ovjlLWpdhHBPDTtJCGKQqVFxXZSzsMmNn~␣` | 1. cur = {Mm} → W[F2] W[F3]<br>2. otherwise → — |

**cur `Nn`**

| prev group | contexts in priority order → parameters the rule sets |
|---|---|
| `4AEIOUabcefgikruwy3@ov\|jlLWYRpdhHBPDTtJCGKQqVFxXZSzsMmNn~␣` | 1. next = {bcu} → AV B3 F2<br>2. next = {EiU} → F2 F3<br>3. prev vowel → F2 B2 B3<br>4. otherwise → — |

**cur ␣ (pause)**

| prev group | contexts in priority order → parameters the rule sets |
|---|---|
| `BPDTtJCGKQqMmNn~` | 1. otherwise → aspShift aspAH |
| `4AEIOUabcefgikruwy3@ov\|jlLWYRpdhHVFxXZSzs` | 1. otherwise → aspShift aspAH |
| `␣` | 1. otherwise → — |

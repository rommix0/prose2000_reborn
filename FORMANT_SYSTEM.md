# Formant movements and coarticulation in the Prose 2000 v3.4.1

How the firmware's parameter generator turns a string of timed phonemes into moving formants: the targets each
phoneme starts from, the transition machinery that joins neighbouring segments, the locus model that decides where
each transition starts, and the context rules for stops, fricatives, nasals, glides, liquids and vowels. The graphs
in [formant_graphs/](formant_graphs/README.md) show these rules at work on traced examples.

Everything here comes from the C decompilation in `src/paramgen/` (byte-exact against the emulator, REFERENCE §12.7,
§14). Tags: **[verified: code]** is read from the decompiled code; **[verified: trace]** is also seen in a run of the C
pipeline with `formant_trace` (numbers below come from such runs); **[I]** is inferred. Addresses are Prose 2000
linear addresses and `DS:x` offsets (DS = F410). The phoneme codes are the Prose's one-character alphabet
(REFERENCE §12.3a): `E` /i/, `o` /ɑ/, `T` /t/, `s` /ʃ/, `~` /ŋ/ and so on.

## 1. Overview

The prosody stage gives every phoneme a **duration in frames** (10 ms) and an **F0 target** (PITCH_SYSTEM.md). The
parameter generator `paramgen_run` `DCB00` then takes the phonemes one at a time, with a window of the previous and
the next one, and writes **22 parameter tracks** (one byte per 10 ms frame, REFERENCE §11.4): the voicing,
frication and aspiration amplitudes AV AF AH, the parallel-branch amplitudes A2-A6 and AB, the formants F1-F4, the
bandwidths B1-B3, the nasal resonator FN, F0, and four source bytes. The frame builder converts each frame into
resonator coefficients for the DSP, which is a Klatt-style cascade/parallel formant synthesizer (REFERENCE §13).

It is a **target-and-transition** system in the style of Klatt (1980) and MITalk [I]:

| Step | Routine | What it decides |
|---|---|---|
| 1. Targets | `paramgen_load_targets` `DD1C0` | each parameter's steady-state value for this phoneme (§2) |
| 2. Context rules | `paramgen_apply_rules` `DE96E` and the routines of its lists (§5) | changes to targets by the neighbours; the consonant spectra; closure, burst and voice onset timing (§6-§7) |
| 3. Loci | `pg_finalize` `E2FB8`: `pg_locus_weights`, `pg_consonant_loci`, `pg_apply_loci`, `pg_amplitude_boundaries` | where each transition starts (**onset**), what the previous segment's tail bends toward (**locB**), and how long both transitions are (§4) |
| 4. Settings | `paramgen_segment` `DD703` | `ESC[a` attenuation of AV/AF/AH; `ESC[V` formant scaling (§8) |
| 5. Track writing | `param_emit_segment` `D3D2F` | the frames: a forward ramp from the onset to the target, and a blend of the previous segment's last frames toward locB (§3) |

Coarticulation in this design is **local**: a segment sees its previous phoneme, the next one and, in rule conditions,
the one after next and the one before the previous (REFERENCE §12.5). It reaches its neighbours in two ways: the
**forward** transition of each segment starts from a value derived from the previous segment (carry-over), and the
**backward** blend reshapes the end of the previous segment toward the current one (anticipation). Some targets are
also chosen from the class of the next vowel (§6.1, §7.3).

## 2. Targets

`paramgen_load_targets` loads the phoneme's row of the target tables (58 phonemes, indexed through `DS:93CA`)
[verified: code]:

| Parameter | Table | Coding | Notes |
|---|---|---|---|
| F1, F2, F3, F4 | `DS:944C`, `9486`, `94C0`, `94FA` | ×4, ×8 + 500, ×16, ×16 + per-voice `DS:5348` | |
| B1, B2, B3 | `DS:9534`, `956E`, `95A8` | ×2 | |
| AV | `DS:95E2` | dB | 60 for vowels, 0 for voiceless consonants, 47-58 for voiced ones |
| AF AH A2-A6 AB | pointers `DS:9774-9854` | dB | consonants only; vowels and glides get AF = AH = AB = 0 and A2-A6 = 60 |
| FN | `DS:98C0` for the nasals `M m N n ~`, else 248 Hz | ×4 + 192 | |
| F0 | node `+8` ×2 | Hz | from the prosody stage |
| offglide F1-F3, B1-B3 | `DS:9690`, `96A2`, `96B4`, `96C6`, `96D8`, `96EA` | as above | vowel indices 0-17 (§7.2) |
| next phoneme's F1-F3 (B1-B3 if a consonant) | the same tables | | kept in `EB2A-EB36` for anticipation |

`formant_trace -T` prints these tables. Graph `15_vowel_space` plots the vowel targets and offglides. A few examples
(voice 0, Hz):

| Phoneme | F1 | F2 | F3 | B1 | B2 | B3 | Phoneme | F1 | F2 | F3 | B1 | B2 | B3 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `E` /i/ | 272 | 2220 | 2880 | 50 | 116 | 238 | `W` /w/ | 300 | 628 | 2336 | 80 | 60 | 60 |
| `a` /æ/ | 724 | 1652 | 2352 | 176 | 178 | 274 | `R` /r/ | 360 | 964 | 1296 | 60 | 70 | 70 |
| `o` /ɑ/ | 680 | 1100 | 2640 | 110 | 94 | 138 | `L` /l/ | 340 | 1044 | 2992 | 70 | 60 | 60 |
| `b` /u/ | 372 | 1412 | 2112 | 40 | 60 | 110 | `Y` /j/ | 240 | 2068 | 3008 | 40 | 250 | 400 |
| `@` /ə/ | 532 | 1348 | 2352 | 48 | 70 | 128 | `M` /m/ | 520 | 1044 | 2096 | 160 | 150 | 140 |

Consonants have formant targets too, which matter for the transitions into and out of them even where the cascade
is silent (a stop's closure, a voiceless fricative). Many are replaced by the context rules (§6).

## 3. Segments and transitions

### 3.1 The segment struct

Each of the 22 parameters has a struct at `DS:DE22 + 14·p` (REFERENCE §12.2) [verified: code]:

| Field | Meaning |
|---|---|
| `type` | how the segment is written (below); 7 by default |
| `len` | frames in this segment (the node's duration, at most 60) |
| `target` | the steady-state value |
| `onset` | the value the forward transition starts from |
| `durF` | frames of the forward transition, default `DS:98A4[p]`: AV 2, AF 4, AH 2, A2-AB 3, F1-F3 6, F4 5, B1-B2 5, B3 6, FN 12, F0 12, source bytes 2 |
| `locB` | the value the *previous* segment's last frames are pulled toward |
| `durB` | frames of that backward blend |

`pg_apply_loci` sets `durB = durF`, clamps `durF` to the look-ahead (`[EAE0]`: 20 frames, 4 in fast-response mode)
and `len` to 60; `pg_finalize` clamps both lengths to 20. The context rules change all of these per parameter.

### 3.2 Transition types

`param_emit_segment` writes a segment according to its type [verified: code]:

| Type | Forward part | Backward part |
|---|---|---|
| 4 | hold the target from the first frame (a jump) | none |
| 5 | as 4 | blend the previous segment's end toward locB |
| 6 | ramp from the onset to the target over durF frames, then hold | none |
| 7 | as 6 | blend the previous segment's end toward locB |
| 2 | pull the frames already in the track toward the onset (used by diphthongs, §7.2) | none |
| 3 | as 2 | blend back |

An odd type becomes even when there is nothing behind it to blend (`gap <= 0`). The previous segment's frames can
still be changed because playback runs at least `[EAE4]` = 12 frames behind the generator (`RING_LAG`).

### 3.3 The ramp shape

The ramps `DS:9B9E[k]` are the fractions of the remaining distance, in 256ths, frame by frame [verified: code,
`formant_trace -T`]. Ramp *k* is **(1 − i/k)²**: for k = 4 it is 255, 144, 64, 16, and for k = 6 it is 255, 178,
114, 64, 28, 7. So a forward transition leaves the onset quickly and settles into the target (an ease-out parabola),
reaching it after *k* frames. The backward blend uses the same ramp from its second entry, walking backwards from the
boundary: the last frame of the previous segment moves 144/256 of the way to locB (k = 4), the one before 64/256, and
so on, so the anticipation fades in over the end of the previous segment.

## 4. The locus model

### 4.1 Formants, bandwidths, FN and F0

For p9-p17, `pg_apply_loci` `E1C1A` computes [verified: code]:

**onset = locB = W·target + (1 − W)·L**

- **L** (`DS:EB7E + 2p`) is the **locus**. After every segment `paramgen_advance` sets it to the last value written
  on the track, so by default a transition starts from where the previous segment ended. Rules replace it with
  fixed consonant loci (§6).
- **W** (`DS:EB52 + 2p`, q15) is how far the onset jumps toward the new target at once. The default is `DS:9864` by
  the class of the previous and the current phoneme (0 vowel, 1 voiced consonant, 2 other, 3 closure):

  | prev \ cur | vowel | voiced | other | closure |
  |---|---|---|---|---|
  | vowel | 0.50 | 0.75 | 0.50 | 0.35 |
  | voiced | 0.25 | 0.50 | 0.25 | 0.35 |
  | other | 0.50 | 0.75 | 0.50 | 0.50 |
  | closure | 0.65 | 0.65 | 0.50 | 0.50 |

  `pg_locus_weights`, `pg_sonorant_onset`, `pg_after_closure` and others override it by place and manner. W = 1
  means the formant jumps to the target (for example F2 and F3 going into an alveolar or velar closure from a vowel);
  W = 0 means the onset is the locus itself (F2 and F3 of every vowel after /t d k g/, §6.1).

Because onset = locB, the backward blend and the forward ramp meet at the same value: the boundary value is a
weighted mean of the two sides, and both segments move toward it. At the end `pg_finalize` keeps **F(n+1) ≥ F(n) +
200 Hz** on onsets and loci (targets too while the segment is voiced), except for /t/ before /ɝ/.

A traced check ("see.", REFERENCE §12.5a): /s/ ends at F2 = 1852 Hz and W(F2) after `S` is 0.2, so /i/ starts at 0.2 ·
2220 + 0.8 · 1852 = 1926 Hz, and its first frame is 1924 Hz [verified: trace].

### 4.2 Amplitudes and source bytes

For the other parameters, `pg_amplitude_boundaries` `E2535` uses the mean: **onset = locB = (L + target)/2**, but for
AV, AF and AH no more than *d* dB below either side: *d* is 9, 15 going into a pause, 10 after a voiceless sound, 0
for AH otherwise, and 20 for AV of a nasal after a pause; A2-AB use *d* = 0 [verified: code]. So an amplitude never
dips at a boundary by more than *d*. The same routine times voicing at voicing changes (AV durB = the previous
segment's length / 4, durF = this one's / 2, or / 4 before a pause or in a stressed syllable) and makes the
**pre-pausal fall** (AV toward 48 dB and F0 toward 58 Hz over the last 2/3 of the last voiced segment).

## 5. The rule engine

`paramgen_apply_rules` picks a **rule group** from the pair (current phoneme, previous phoneme) through the matrix
`DS:9376`, and applies the **first** rule in the group whose condition holds (REFERENCE §12.3, §12.5; VOICE_CONTEXTS.md
lists the groups) [verified: code]. There are 88 groups and 621 rules. A condition is a byte code over context
flags (voicing, the previous phoneme's class, the class of the next vowel, the next syllable's stress), feature bits
and phoneme lists of the current, next, next-but-one and before-previous phoneme. A rule's actions mostly *set*
values: the parallel amplitudes of consonants, burst values, and some formant targets. It then calls a list of
routines, always ending with `pg_finalize` (§4):

| Current phoneme | Routines |
|---|---|
| vowel | [`pg_after_closure` after a stop, nasal or `Q`] → `pg_sonorant_onset` → `pg_vowel` |
| glide, liquid, `h` `H` `d` | [`pg_after_closure`] → `pg_sonorant_onset` → `pg_sonorant_consonant` |
| nasal | `pg_sonorant_onset` → `pg_sonorant_consonant` → `pg_closure_types` |
| stop | [`pg_voiceless_onset` if voiceless] → `pg_obstruent_voicing` → `pg_closure_types` → [`pg_stop_burst`] |
| fricative | [`pg_voiceless_onset`] → `pg_obstruent_voicing` → `pg_fricative_amps` |
| pause | `pg_shift_aspiration` [→ `pg_after_closure`] |

`formant_trace` prints the group, the rule's index and the routines for every segment; the graphs show them in their
bottom strip (for example `g28/2` = group 28, rule 2).

## 6. Consonants

### 6.1 Stops (plosives)

A stop is one segment of the node list, but the generator writes it as **closure + burst**, and the following
sonorant begins with a **voice onset delay** [verified: code and trace; graphs 01-06, 12]:

1. **Transition into the closure** (`pg_closure_types`, `pg_locus_weights`). AV-F1 get type 5 and F2-FN type 7: the
   end of the previous vowel is blended toward the stop's loci. Going from a vowel into a non-labial closure other
   than /t/, W(F2) = W(F3) = 1, so F2 and F3 head straight for the stop's targets, and the preceding vowel's tail
   bends toward them (the VC transition); a /d/ or /n/ after a vowel also gets the alveolar targets F2 1600, F3 2620
   Hz. W(F1) is 0.5 (0.99 for /g/, 1.0 for /k ŋ/). The formant transition
   length is `DS:9856[place]`, by the place of articulation `DS:[AEA8]`: **labial 6 frames, dental and alveolar 8,
   palatal and velar 10**, glottal 8; F1 takes half as many + 1.
2. **Burst data by the next vowel.** For the stops before a vowel, `paramgen_load_targets` looks up six values by
   `DS:98DB[stop] + class of the next vowel` (high front, other front, back rounded, central/low): burst AV, AF and AH
   (`EBC6-EBCA`), the burst length `EBB6`, the voice onset delay `EBB4`, and AB. The rules then adjust them
   (`P`+`T`, `K` before a closure, `G` before an unstressed vowel, `D`/`B`/`G` after `S` …).
3. **The split** (`pg_stop_burst` `E08EA`). The AV/AF/AH frames of the closure are filled with the stop's own
   targets (AV 0 for a voiceless stop, a **voice bar** of 30-50 dB for a voiced one, see §6.2), and the last `EBB6`
   frames become the burst, with the burst values as targets. The burst values drop by 1 dB for /k/, 3 for the
   others, 6 after a nasal, except in a stressed syllable, for /b/ and /g/, and for /d/ and /t/ before a vowel. After a pause, /b/ and /d/ are
   **prevoiced**: the last 2-3 closure frames get AV (43-45 dB for /d/). **For /p/ and /k/, `pg_stop_burst` runs
   twice** (`pg_closure_types` calls it, then the rule list does), so the burst is split a second time; for "key"
   this makes a 1-frame burst plus a 1-frame decayed burst [verified: trace].
4. **The release** (`pg_release_onset` `E30C1`, from the next segment's `pg_sonorant_onset`). The first `EBB4` frames
   of the vowel get **AV = 0**, **AH** at the aspiration level `EBC4` (3 dB less after an unstressed stop) and
   **B1 = 150 Hz**. When the stop is in a stressed syllable (and in the clusters /pl tw kw kj kl tn/ and unstressed
   /tə/), frication continues through these frames at a level set by place (AF 50-65 dB) and F0 is 0 until voicing
   starts. For /k/ AV comes back 2 frames before the end (55 dB). A stressed vowel after a voiceless stop is lengthened by half the
   delay. Meanwhile the formants already move: the aspirated frames show the transition, excited by AH only.
5. **The CV transition** (`pg_after_closure`, `pg_locus_weights`, `pg_apply_loci`). After a stop F2 and F3 are type 6
   (no blend back into the burst) with the transition length of the stop's place, and:

   | After | W(F2) | W(F3) | So the vowel starts … |
   |---|---|---|---|
   | /b/ /p/ | 0.1 before a front vowel, else 0.2 | 0.2 front, else 0.7 | 10-20 % of the way from the labial locus to the target |
   | /d/ | 0 | 0 | at the alveolar locus: **F2 1600 Hz after a vowel (1924 before a high front vowel), F3 2620 Hz** |
   | /t/ | 0 | 0 | at /t/'s own F2 1796 Hz and F3 2688 Hz (2700 before a high front vowel) |
   | /g/ /k/ | 0 | 0 | at the velar stop's own F2/F3, which the rules set **by the vowel's class** |

   Graph `04_locus_equations` plots the onset against the target for 12 vowels [verified: trace]. The /d/ points lie
   on one horizontal line (a locus-equation slope of 0: every vowel starts from 1595 Hz); the /g/ points form steps,
   one per vowel class (F2 2252 Hz before /i/, 2060 before the other front vowels, 1676 before the back rounded ones,
   1548 before the central/low ones: the classic front/back velar allophones); the /b/ points follow the targets with
   a slope near 0.1-0.2 from loci of 1000-1200 Hz.
6. **F1** rises from the closure: F1 targets of stops are low (200-400 Hz), and W(F1) is 0.5 after a closure.

Traced timings, phoneme input `@CV1` (frames of 10 ms) [verified: trace]:

| Stop | before /i/ | before /ɑ/ | before /u/ | Burst AF / AH (dB) |
|---|---|---|---|---|
| /p/ | 7 closure + 1 burst, 7 voice onset | 7 + 1, 6 | 7 + 1, 5 | 63 / 51 |
| /t/ | 2 + 6, 1 | 4 + 4, 6 | 6 + 2, 4 | 56-60 / 48-51 |
| /k/ | 5 + 2, 7 | 5 + 2, 5 | 6 + 1, 7 | 55-58 / 0-51 |
| /b/ /d/ | 5 + 1, none | 5 + 1, none | 5 + 1, none | 55-65 / 0-40 |
| /g/ | 5 + 2, none | 5 + 2, none | 5 + 2, none | 52-58 / 0 |

So the aspiration of /t/ is mostly a long burst of frication, and that of /p/ and /k/ a long stretch of AH after a
short burst: 70-100 ms of voiceless sound after the closure in each case.

**Allophones of stops:**
- After /s/, /p t k/ are written `B D G` by the lexical rules (REFERENCE §12.3a), so they get no voice onset delay;
  the rules set a 2-frame burst and no voice bar (graph `12_clusters`, "spy").
- **Flap** `t` (water, little): `pg_apply_loci` sets F1-F3 to the mean of the previous value, the flap's target and
  the next phoneme's target, and AV to type 4 (graph `13_flap_glottal`).
- **Glottalized t** `q` (button): a voiced closure (AV 30 dB) whose F0 is pulled to 40 Hz (PITCH_SYSTEM.md).
- Intervocalic /t/: the end of the preceding vowel's AV is blended toward 40 dB over 6 frames, so voicing fades
  instead of stopping at once.
- A released stop before a pause gets a release vocoid `p` with a short frication burst by place (/t/ 55, /k/ 52,
  /p/ 65 dB).

### 6.2 Voicing of obstruents

`pg_obstruent_voicing` `DFE87` [verified: code]:
- A voiced stop not before a vowel loses 20 dB of AV; its closure AV is 0 after a voiceless sound (30 for /dʒ/), or 30
  dB below its target after a voiced obstruent. Before a vowel, /b/ has a 50 dB voice bar, /d/ 43 dB before a
  stressed vowel. Voiced stops before an unstressed sound lose A3 and A4, and so does a /d/ between a vowel and an
  unstressed vowel (flap-like) [I: the purpose].
- Before a sonorant, a fricative's frication overlaps the next segment's first `EBB8` frames (2, or fewer before a
  very short sonorant; 0 for stops, /s/, and /θ/ before /r/).
- `pg_voiceless_onset`: a voiceless fricative or /h/ after a sonorant starts its aspiration 1 frame early (AF 2 frames
  early), by moving the start of the AH track back (`pg_shift_aspiration`); B1 widens over those frames.

### 6.3 Fricatives

Fricative spectra live in the **parallel branch**: AF drives A2-A6 and AB, the amplitudes of resonators at F2-F6 and a
bypass (REFERENCE §13) [verified: code]. The rule table supplies most of them (A5 is set by 250 rule actions, A2 224,
A4 173, AB 158). Graph `08_fricative_spectra` shows the traced targets before /i/ and /aɪ/: /s/ puts 77 dB on A4-A6,
/ʃ/ peaks at A3 (70 dB, the F3 region), /f/ uses the bypass and A2 (64 dB each), /θ/ only A5-A6 [verified: trace].
`pg_fricative_amps` `E01C0` adds the context: AF +1 dB per missing frame for a fricative shorter than 8 frames outside
a cluster, −12 dB for /s/ (and /z/ before a voiceless sound or a pause; unless bit 0 of `[C288]`), AH 31 dB for most
voiceless fricatives (before a vowel only before a front one, and not for /s θ/), a special spectrum for /s/ before an
unstressed vowel, voicing for /z/ by context, and AV +6 dB for a voiceless fricative before a pause.

Vowels follow fricatives slowly: from a fricative into a vowel W(F2) = W(F3) = 0.2 (0.1 for /s/ before a central or
low vowel), so the vowel starts near the fricative's F2 and F3. After /s z/ they then move over 8 frames (10 after
/z/) and blend back 3 frames into the fricative; after labial and dental fricatives they blend back 2-3 frames
(graph `07_fricatives`).

### 6.4 Affricates

/tʃ/ and /dʒ/ are two segments each: a closure `C` or `J` and a fricative release `s` or `z` (REFERENCE §12.3a).
`pg_obstruent_voicing` and `pg_fricative_amps` give the release its own frication (/dʒ/ before a voiced sound: a
voiced burst at F1-F4 284/1876/2528/3232 Hz). "tr" is written `C s R` and gets an alveolar-postalveolar release with
F2 1396, F3 1936 Hz (graph `12_clusters`, "try") [verified: code, trace].

### 6.5 Nasals

`M N ~` (and syllabic `m n`) are closures with the cascade open [verified: code; graph `09_nasals`]:
- **FN**, the nasal resonator, has its own target (the other phonemes leave it at 248 Hz). Into a nasal after a
  sonorant W(FN) = 0.55 over 10 frames; out of it, 0.6 over 9 frames.
- **B1** starts from a 300 Hz locus with W = 0 in both directions, so the first formant is wide at the nasal
  boundaries. F1 moves three quarters of the way at once into the nasal (W = 0.75) and a quarter out of it (0.25).
- Loci by place: into /n/ F3 2600, F4 3600 Hz (F2 1670 after a high non-front vowel), out of it F3 2600, F4 3400;
  into /m/ F2 700 Hz after a non-front vowel; /ŋ/ before a vowel F2 900, F3 2176 Hz ("singer"), and out of /ŋ/ F2
  starts from 2100 Hz.
- AV: a nasal after a pause may start 20 dB below its target; after a nasal the next voiced sound's AV jumps (type 4).

### 6.6 Glides and liquids

`pg_sonorant_consonant` `DFB5B` [verified: code; graph `10_glides_liquids`]:
- Formants move over **7 frames** into a glide or liquid (5 from another glide or liquid; 9 into a nasal or /h/,
  which also run this routine); /r/ moves F2-F4 over 6. Out of a glide or liquid, `pg_sonorant_onset` gives the next
  sound 7 frames (11 after /j/ into a back vowel) and F3 9 after /r/. Between a /w/ or /r/ and a front vowel these
  are among the largest voiced movements the system makes (graph `10_glides_liquids`: F2 of "way" rises by about
  1250 Hz).
- **/r/**: F2 is moved a quarter (q15 0x2008) of the way toward the next phoneme's F2, and F3 = F2 + 400 Hz, which
  gives the low F3 of American /r/. Before a back rounded vowel F2 = 1140, F3 = 1440 Hz; after a velar F2 1140, F3
  1400. In "tr" F2 1292, F3 1760 Hz.
- **/l/**: F2 a tenth of the way toward the next phoneme's (876 Hz after a velar), AV −3 dB.
- **/w/** after /k/: F2 800 Hz. **/j/** after /g/: F2 2036, F3 2592 Hz.
- After a pause a voiced glide, liquid or nasal starts at AV 56 dB.

## 7. Vowels

### 7.1 Context shifts of the targets

`pg_vowel` `DF245` [verified: code; graph `14_anticipation`]:
- **After /r/** (and other rhotics): F3 a quarter of the way down to 1800 Hz; front vowels also move F2 a quarter of
  the way to 1400 Hz. The offglide moves with them.
- **Before a lateral**: F2 −300 Hz (the dark /l/ of "bell").
- A lax front vowel before a velar keeps its F2 in the offglide; /u/ after /r/ or before a vowel has F2 1164 Hz;
  /ʊ/ before /l/ −150 Hz.
- After a velar stop, /i/ gets F3 +300 and F4 −200 Hz.
- **Stress:** AV +2 dB in a stressed syllable, −3 dB otherwise, and /ɑ æ/ a further −3 dB.
- A reduced vowel's F3 is the mean of its target, the previous value and the next phoneme's F3.

### 7.2 Onglide and offglide

Every vowel with index below 18 (`4 A E I O U a b c e f g i k r u w y`: the diphthongs, but also /i ɪ ɛ æ ɔ ʊ u/
and the r-coloured vowels) has an **offglide** target, and `pg_vowel` always moves to it [verified: code and trace].
`diphthong_onglide` writes F1-F3 and B1-B3 as:

1. the onglide target held for `[EBBC]` = F1's length × `DS:9720[vowel]` frames (for "tea" 11 of 25 frames),
2. a transition to the offglide over `[EBBA]` frames (`DS:9744`, scaled by the vowel's length against its inherent
   duration), starting from the midpoint of onglide and offglide (+200 Hz on F2 for /aɪ/, −200 on F3 for /aɪ ɔɪ/),
3. then the normal emit pulls the whole stretch toward the segment's onset (type 3), so the held part is not quite
   flat at its start.

Unstressed r-coloured vowels start a quarter nearer the offglide. Graph `11_diphthongs` shows /aɪ ɔɪ aʊ oʊ/ after
/b/, and `15_vowel_space` all the targets with their offglides. For /i/ the offglide differs mainly in F3 (2704 against
2880 Hz), for the r-coloured vowels in F3 (down to 1500-1650 Hz: the r-colouring comes at the end of the vowel).

### 7.3 Vowel reduction

A vowel's F1-F3 targets and its offglide are pulled toward the **neutral vowel 490 / 1450 / 2500 Hz** by
`DS:9884[duration × 10 / 16]` (q15): 88 % for a 10 ms vowel, 51 % at 40 ms, 23 % at 80 ms, 6 % at 160 ms, 2 % at 240 ms
and above [verified: code; graph `16_vowel_reduction`]. Rhotic and schwa-like vowels are not reduced. Since the
durations come from the prosody stage, **faster speech and unstressed syllables reduce vowels toward schwa**, and a
formant never reaches a far target in a very short vowel. The transition lengths (§3) do not shrink with the rate,
so at fast rates the transitions take a larger part of each vowel [I: effect on perception].

## 8. Voice and attenuation

- `ESC[V` scales F1-F3 targets by `DS:52F8[V]`: voice 1 ×0.95, voice 2 ×1.05 (traced on /ɑ/: F1 676 → 642 / 709 Hz).
  F4 moves by F3's change, then is capped by `DS:5358[V]` (REFERENCE §12.7). Loci and onsets follow, since they are
  computed from the scaled targets of the neighbouring segments.
- `ESC[a` subtracts from AV/AF/AH targets, onsets and locB after the rules, and from the closure values in
  `pg_stop_burst`.

## 9. Findings

Checked with `formant_trace` (2026-09-27):
- `DS:[AEA8]`, which `pg_after_closure` and `pg_closure_types` use to index `DS:9856`, is the **place of
  articulation** per phoneme char (the prosody stage's `PLACE_TABLE`), not a second phoneme map as the C comment said
  (fixed). `DS:9856` is therefore an 8-entry table of transition lengths by place: labial 6, dental 8, alveolar 8,
  palatal 10, velar 10, glottal 8 frames.
- **`pg_stop_burst` runs twice for /p/ and /k/**: once from `pg_closure_types` and once from the rule's list. The
  second call splits the burst of the first again.
- **Every vowel with index < 18 moves to its offglide**, not only the diphthongs (§7.2).
- **After /d/ and /g/ the vowel's F2 and F3 onset weight is 0**, so the onset is the stop's locus: a constant 1595 Hz
  after /d/ (1924 before /i/), and one of four values by vowel class after /g/ (§6.1).

## 10. Tools

- `formant_trace` (`src/cli/`, built with the C project): `formant_trace [-v VOICE] [-r WPM] [-W] "text"` prints every
  segment's rule, routines, events and parameter structs, and the tracks as played; `formant_trace -T` prints the
  target, reduction, weight and ramp tables. The output format is at the top of `src/cli/formant_trace.c`. It uses
  a trace hook in the generator (`pg_trace_hook`, `pg.h`), which is NULL in normal use; synthesis output is unchanged.
- [formant_graphs/](formant_graphs/README.md): 16 graphs made from these traces by `make_graphs.py`.

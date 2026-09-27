# Formant graphs

Plots of the Prose 2000 v3.4.1's formant movements and coarticulation, made from the firmware's own rules as
described in [FORMANT_SYSTEM.md](../FORMANT_SYSTEM.md). Each example is spoken through the C pipeline by
`formant_trace`, which reports what the parameter generator did for every segment (the rule, the routines, their
events, and each parameter's target, onset and locus) and the 10 ms parameter tracks the synthesizer plays.

## Reading a time graph

Each column is one utterance; time runs left to right in ms from the start of speech. Letters at the top are the
Prose phoneme codes with their IPA (REFERENCE §12.3a), `ˈ` marks a stressed vowel, and the dotted vertical lines are
the segment boundaries.

**Top panel**, formants in Hz:

| Element | Meaning |
|---|---|
| solid line | F1 (blue), F2 (orange), F3 (green), as played; FN (dashed yellow) and B1 (dotted blue) in `09_nasals` |
| faint line | the same track where the cascade is not excited (AV = AH = 0: closures, voiceless frication, pauses) |
| dashed segment | the segment's target after the context rules |
| ○ | the onset: where the segment's forward transition starts, W·target + (1 − W)·L (FORMANT_SYSTEM.md §4) |
| ✕ | the locus L of that formula, where it is not the value the track shows just before: a fixed consonant locus, or a value that was later blended back toward the next segment |
| yellow band | a vowel's onglide stretch ([EBBC] frames) before it moves to its offglide |

**Middle panel**, amplitudes in dB: AV (voicing), AF (frication), AH (aspiration), with bands for the events of the
routines: **closure** and **burst** (`pg_stop_burst`), **voice onset** (frames of AV 0 after a voiceless stop,
`pg_release_onset`), and **early AH** (aspiration moved into the previous segment, `pg_shift_aspiration`).

**Bottom strip:** the rule applied (`g28/2` = group 28 of `DS:9376`, rule 2) and the routines of its list
(`pg_finalize`, which ends every list, is left out), plus the vowel reduction applied (`reduce 10%`).

## The graphs

| File | Shows |
|---|---|
| `01_stop_anatomy` | "a tea": transitions into the closure, closure, a 6-frame burst, voice onset, the CV transition from the /t/ locus, the onglide of /i/ |
| `02_voicing_b_p` | /b/ has a voice bar and no voice onset delay; /p/ a silent closure, 1-frame burst and 7 frames of aspiration |
| `03_place_b_d_g` | /b d g/ before /ɑ/: F2 and F3 start from the labial, alveolar and velar loci |
| `04_locus_equations` | the onset of F2 and F3 against the target for 12 vowels after /b d g/: /d/ constant, /g/ by vowel class, /b/ slope 0.1-0.2 |
| `05_velar_front_back` | /k/ before /i/ and /u/: burst, aspiration and loci chosen by the vowel |
| `06_voiceless_stops` | /p t k/ before /ɑ/: burst lengths and aspiration by place |
| `07_fricatives` | /s ʃ f θ/: frication and slow vowel onsets (W = 0.2) |
| `08_fricative_spectra` | the parallel amplitudes A2-A6 and AB of /s ʃ f θ/ |
| `09_nasals` | /m n ŋ/: FN, the wide B1 at the boundaries, the loci of /ŋ/ |
| `10_glides_liquids` | /w r l j/: 7-frame transitions, the low F3 of /r/ |
| `11_diphthongs` | /aɪ ɔɪ aʊ oʊ/: onglide, then the move to the offglide |
| `12_clusters` | "pie", "spy" (unaspirated), "try" (the affricate `C s R`) |
| `13_flap_glottal` | the flap of "water" and the glottalized /t/ of "button" |
| `14_anticipation` | "bet", "bell" (F2 −300 Hz before /l/), "red" (F3 lowered after /r/) |
| `15_vowel_space` | the vowel targets and offglides from the ROM tables; r-coloured vowels as F3 against F2 |
| `16_vowel_reduction` | the pull toward the neutral vowel by duration, and where five vowels' targets go |

The examples run "warm" (`formant_trace -W`), as the pitch graphs do. Examples written `ESC[1I…` use phoneme input,
which gives exact contexts without the letter-to-sound rules.

## Making them again

Build the C project (README), which also builds `formant_trace`, then:

```
python formant_graphs/make_graphs.py
python formant_graphs/make_graphs.py --only 01 04 --tool build/formant_trace.exe
```

It needs matplotlib. The examples are the `EXAMPLES` list in `make_graphs.py`. `formant_trace` on its own prints the
trace as tab-separated lines (the format is at the top of `src/cli/formant_trace.c`):

```
build/formant_trace -W "a tea."
build/formant_trace -T
```

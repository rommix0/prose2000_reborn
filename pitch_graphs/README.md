# Pitch graphs

Plots of the Prose 2000 v3.4.1's pitch contours, made from the firmware's own rules as described in
[PITCH_SYSTEM.md](../PITCH_SYSTEM.md). Each graph runs a text through the C pipeline with `pitch_trace`, which
reports every phoneme's F0 target, the rules that built it, and the 10 ms F0 track the synthesizer plays.

## Reading a graph

**Top panel**, F0 in Hz over time:

| Element | Meaning |
|---|---|
| red line | the F0 track, one value per 10 ms frame, as the DSP plays it; gaps are unvoiced frames |
| blue bars | each phoneme's F0 target from the prosody stage (grey dashed: the target of a voiceless phoneme, which is not played) |
| dotted line | the phrase line: the contour before any accent (declination, or the shape of contour types 6-16) |
| yellow band | the hat is up (the plateau between the first accent and the nucleus) |
| grey band | unvoiced or pause |
| ▲ green | accent on a stressed vowel |
| ◆ orange | the rise on the segment just before the nucleus |
| ★ red | the nucleus: the phrase's last primary-stressed (or emphatic) vowel, where the hat ends |
| ▼ grey | voiced dip (−3 Hz, −8 for a voiced obstruent) |
| red note | the phrase ending: fall, comma rise or question jump, with the result |
| ✖ purple | the phrase ending was skipped because `DECL_LOW` was set |
| purple *g* / *q* | glottal onset or glottalized t: the track dips toward 40 Hz |
| brown dashed arrow | the pre-pausal fall: the end of the last voiced segment before a pause is pulled toward 58 Hz |
| letters at the bottom | the phonemes (Prose codes, REFERENCE §12.3a); `ˈ` primary, `ˌ` secondary, `"` emphatic stress; thin vertical lines are word starts |
| grey small text | where the phrase kind or the contour type changes |

**Bottom panel:** for each phoneme, how many Hz each rule added to (or took from) the phrase line: accent, rise
before the nucleus, hat, high segment, voiced dip, phrase ending, type-12 raise and clamping. Target = line + the
bars (then halved to 2 Hz steps).

## The graphs

| File | Shows |
|---|---|
| `01_statement` | declination, the hat, accents, nucleus, final fall and pre-pausal fall |
| `02_yes_no_question` | the question jump to 2·pitch − pitch/8 |
| `03_wh_question` | a wh-question falls like a statement |
| `04_comma_list_question` | comma phrases end with a slight rise; one line across the sentence |
| `05_comma_statement` | a comma phrase and a statement, then a new sentence |
| `06_emphasis` | emphatic stress: 3× accent before it, other accents halved |
| `07_power_up_quirk` | cold start: `DECL_LOW` is set after the RAM test, so the question stays flat |
| `08`-`12` | contour types 6, 7, 8, 16 and 15, selected with marks in two-letter phoneme input |
| `13_type12_register` | the type-12 raise of 8 % |
| `14_type17_carryover` | a type-17 word leaves `DECL_LOW` set, so the next question is flat too |
| `15_word_mode` | word mode: each word is its own accent domain |
| `16_long_sentence` | inserted phrase breaks and one declination line for the whole sentence |
| `17_voices` | voices 0-2 at their default pitch |
| `18_pitch_settings` | pitch 60, 85 and 150 |

All but `07` start "warm" (`pitch_trace -W`): a sentence is spoken first so that `DECL_LOW` has been cleared, as on a
board that has been talking for a while.

## Making them again

Build the C project (README), which also builds `pitch_trace`, then:

```
python pitch_graphs/make_graphs.py
python pitch_graphs/make_graphs.py --only 02 07 --tool build/pitch_trace.exe
```

It needs matplotlib. The examples are the `EXAMPLES` and `OVERLAYS` lists in `make_graphs.py`. `pitch_trace` on its
own prints the trace as tab-separated lines (the format is at the top of `src/cli/pitch_trace.c`):

```
build/pitch_trace -W "Are you ready?"
```

# Structured tables for the v3.4.1 data segment

Date: 2026-09-28. Status: approved design, not yet implemented. Plan: `docs/superpowers/plans/2026-09-28-ds-structured-tables.md`.

Found while planning (verified from the dump): text-rule lists are arrays of pattern pointers, each replacement
following its pattern; LTS rule fields +6/+8 point to word pairs (33E2-37FD); `LTS_RULES` has 28 entries (52C0-52F7);
the affix area is suffix pool / 94 records / lists / `SUFFIXES`, then the same for 60 prefixes.

## Goal

Replace the raw byte dump `prose_ds_data` (DS:0000-AEA9, 0xAEAA bytes, in `src/data/prose_data.c`) with named,
typed C tables, one per documented ROM table, so the data can be read (and later edited) in the source. The image
the decompiled C runs on stays **byte-identical**; no stage code changes.

## Constraints

- Many tables hold DS pointers: into themselves (text-rule lists, LTS rules, paramgen rules and action lists,
  affix records, ramp pointers, INH/MIN/PLACE base pointers), into RAM (paramgen actions address DE2E-EBCA), or
  far code pointers (paramgen routine lists 783E-7995). The C also compares absolute record addresses (affix records
  A25C, A310, A450, A4B4, A4F0, A54A, A55E; LTS pattern 2E3E; affix patterns 9C65, 9C97, 9CA0, 9D0B). So every table
  must sit at its original offset with its original bytes.
- The build needs no Python and no ROM files; that stays true (the generated C is committed).
- `tools/` is git-ignored; `tools/rom_extract.py` stays there.

## Scope

In: every byte of v3.4.1 DS:0000-AEA9.
Out: the lexicon segment (`prose_lexicon`), the DSP data ROM, the ROM lane sums, v1.1 (`prose_v1_data.c`),
changing any reader to typed access, disassembling the text-rule program or the paramgen condition byte code.

## Design

### Representation (approach A)

Each table is its own `const` array with a real C type:

- flat tables: `uint8_t` / `int16_t` / `uint16_t` arrays, e.g. `prose_ds_t_cos[512]`;
- record tables: arrays of structs made only of `uint8_t` / `uint16_t` fields, e.g.
  `struct prose_lts_rule { uint16_t left, output, pattern, cond, pass_on; }`;
- pools (text-rule patterns 03D0, LTS strings 21AC-33E1, the affix pools 9BE8 and A6CA, ramps, demo text):
  `uint8_t` arrays with one item per line (split at every pointer target), printable bytes as character literals,
  each line commented with its DS offset. Word lists inside a pool (the affix next-lists) are written with
  `PROSE_W(expr)`, which gives the two bytes of a 16-bit value;
- byte code: the paramgen conditions 626C form a pool with one condition per line; the text-rule program 146E
  is a plain `uint8_t` array, 16 bytes per line, because rule boundaries need a disassembler;
- undocumented ranges (0000-000B, 001C-001F, 0030-0047, 03CF, 52F6-52F7, 5338-5347, 7246-725D, F-fill or
  unexplained bytes inside 9BE8-AD4F, ...): small `uint8_t` arrays named `prose_ds_XXXX` with an *opaque* comment.

Every byte of 0000-AEA9 belongs to exactly one table.

`static_assert(sizeof(struct ...) == N)` for every record type, so there is no padding. Tables are copied into the
image with `memcpy`, which assumes a little-endian host (x86, x64, ARM); `prose_ds_tables.h` has an `#error` for a
big-endian target.

### Layout list and assembly

The header declares `const prose_ds_span prose_ds_layout[]` = `{offset, const void *data, size}` in offset order.
`prose_rom_builtin` (`src/common/prose_rom.c`) copies each span to `PROSE_DS_DATA_BASE + offset` instead of
copying `prose_ds_data`. `prose_ds_data` is removed from `prose_data.c` / `.h`.

### Files (`src/data/`, generated once)

| File | Tables |
|---|---|
| `prose_ds_tables.h` | struct types, `extern`s, `PROSE_DS_<NAME>` offset macros, the layout list declaration, the FNV-1a (64-bit) hash of the original DS bytes |
| `ds_layout.c` | the layout list |
| `ds_boot.c` | header words 0000, lane sums 000C, stage accept masks 0020, curve 0030, `TR_ATTR_PTR`, INT 8 stub, speed caps, identity byte |
| `ds_textrules.c` | pattern pool, list pairs, `TR_LISTS`, word chars, program, strings |
| `ds_lexical.c` | class masks, feature planes, letter / phoneme states, entry adjust, LTS pool, LTS rules and lists, affix area, INH / MIN / PLACE |
| `ds_frame.c` | voice tables 52F8-5367, per-voice frame words, jitter / shimmer, voicing gain, AV scaling, DSP boot block, frame template, nasal-zero gain, exp / cos / dB, parallel corrections |
| `ds_prosody.c` | pause scale, speed %, break masks, cluster %, `l` maxima and defaults, track defaults, `t` rows, AV correction, demo text |
| `ds_paramgen.c` | conditions, actions, action lists, routine lists, rules, group pointers and counts, pair index, phoneme map, targets, offglides, onglides, consonant blocks, loci, reduction, ramps, bit masks |

`CMakeLists.txt`: the `prose_data` library gains these sources.

### Names and pointers

- Names follow the macros the stage headers already use (`FEATURES`, `CLASS_MASKS`, `T_COS`, `LTS_RULES`,
  `TRACK_DEFAULT`, `VOICE_SCALE`, ...): array `prose_ds_<lower>`, offset `PROSE_DS_<UPPER>`. Tables without a C
  name get `prose_ds_XXXX`. Each table has a one-line comment with its format, its REFERENCE section and its status
  (*full*, *partial*, *opaque*).
- Pointers stay numeric DS offsets. A pointer to a table start (or a fixed offset into it) is written as
  `PROSE_DS_<NAME> + k`; other pointers are hex with a comment decoding the target (the string, the rule number).
  Far pointers are written as `{offset, segment}` fields with the target routine named in a comment.
- Biased base pointers (INH 96 bytes at AD84 with pointer AD64, consonant blocks 9774, nasal FN 98C0) are written
  as `PROSE_DS_<NAME> - bias` with the bias explained.

### Source of truth

`rom_extract.py` generates these files once, through `tools/ds_gen.py` (machinery) and `tools/ds_spec.py` (the table
list), both local like the extractor. From then on the C files are the source: their header comment says
they may be edited (renaming fields, splitting a table as its structure is understood) and that `ds_image_check`
guards the bytes. The extractor can still regenerate them from the dumps.

## Verification

1. **Generator self-check:** before writing, `rom_extract.py` serializes its table list back to bytes and compares
   with the dump's DS:0000-AEA9; it also checks full coverage, no overlap, and each record size. On any failure it
   writes nothing.
2. **`ds_image_check`** (new test program, built in both `PROSE_VERSION` configurations): builds the image with
   `prose_rom_builtin`, checks the FNV-1a hash of DS:0000-AEA9 against the embedded value (a SHA-1 in C would cost more code for no gain
   here: the check guards against edits, not attacks), and checks that the layout
   list is sorted, contiguous and covers 0000-AEA9.
3. **Changeover:** before `prose_ds_data` is deleted, a one-off comparison of the old array with the assembled
   image, byte for byte.
4. **Regression:** Windows 64-bit gcc and WSL gcc 13 builds with no new warnings; `boot_check`; `pipeline_play -s`
   on a few texts (frames and host output compared before and after); every locally available replay capture.
5. **Docs:** REFERENCE §14 (data files, how the image is built, the "Next" line); the stale §9.3 rows (544E =
   jitter table, 612B = `t` rows, 94C1 = F3 targets, AE64 = place table); the `src/` row of AGENTS.md.

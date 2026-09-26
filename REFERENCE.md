# REFERENCE.md — Prose 2000 on-demand reference

Detail store for [AGENTS.md](AGENTS.md). Read only the section you need
(`grep -n '^## ' REFERENCE.md` gives the table of contents with line numbers).

Sources are tagged: **[877]** = US4659877A, **[583]** = US4716583A,
**[216]** = US4979216A, **[MAME]** = `prose_v3/tsispch.cpp`, **[cgrm]** = the TruVoice SDK header `cgrm.h` (removed from the repository; in its git history)
(TruVoice SDK header). Everything from a patent is
a *design description*, not a verified fact about the dumped firmware — confirm
it in the ROMs before relying on it.

---

## 1. Source documents

| File | Title | Filed / granted | Inventors | Relevance |
|---|---|---|---|---|
| `docs/US4659877A.*` | Verbal computer terminal system | 1983-11-16 / 1987-04-21 | Dorsey, Williams, Vyas, Groner | TTS channel hardware (T/VIS board), CPU↔DSP 10 ms frame protocol, index markers. |
| `docs/US4716583A.*` | Verbal computer terminal system (continuation of 877) | 1986-10-22 / 1987-12-29 | same | Text is nearly identical to 877: typo fixes, UART figure reference dropped, and "initiating calls" added to the abstract and summary. Skip it unless you are quoting claims. |
| `docs/US4979216A.*` | TTS synthesis using context-dependent vowel allophones | 1989-02-17 / 1990-12-18 | Malsheen, Groner, L. Williams | Names the **Prose 2000** directly. Describes the TTS pipeline, formant parameters, vowel-allophone tables and codebooks. **Most relevant to firmware RE.** |
| `cgrm.h` (removed; see git history) | Centigram **TruVoice** SDK header (`TV_ENG32.DLL`), community-annotated (@rommix0) | c. 1996 | — | TruVoice is the successor to Prose. The header lists the `ESC [ n X` host command set, ranges, defaults and voice names. Its Prose-compatibility claims were tested in §8. Tagged **[cgrm]**. |
| `..\tv-decomp-main\` (not in this repo) | **OpenTV**: a byte-exact C decompilation of TruVoice `CGRM_EN.DLL` 5.0.0.51 (SAPI 4, Oct 1997) | 2026 | third party | Its `docs/` and `src/engine/` describe the TruVoice back end, which turns out to be a port of the Prose one (§11.3, §12.6). `frame.c` = our `dsp_build_frame`; `generate.c` (`Synth_Generate`) is a C port of the filter that the Prose runs on the µPD7720. Its addresses are `CGRM_EN.DLL`, not `TV_ENG32.DLL`. Tagged **[tvd]**. |

Notes on the files:
- **`docs/*.txt` are the files to read.** They are text extracts of the HTML covering
  metadata, abstract, description and claims, with the citation and legal-event lists
  dropped. In `US4979216A.txt`, the Table 5 IPA symbols were restored by hand from
  the PDF scan.
- The HTML files are Google Patents pages saved with SingleFile. Their text is
  clean, but **IPA symbols in Table 5 of [216] are missing** (see §6.4 for the
  version recovered from the PDF). The PDFs are scans with an OCR layer, and
  that OCR is also poor for IPA and tables.
- Extracting text: strip `<script>`, `<style>` and `<svg>` with Python's
  `html.parser`; the useful part lies between `Description` and `Claims (`.
  For a PDF page image, run `magick -density 300 "docs/X.pdf[N]" out.png`
  (N is 0-based; ImageMagick is installed, poppler's `pdftoppm` is not).
  `pdftotext` exists at `/mingw64/bin`.
- In [216], PDF page 14 (index 13) holds Table 5, and page 13 holds Table 4.
- Cited background: G. Groner et al., "A Real-Time Text-to-Speech Converter,"
  *Speech Technology* 1(2):73-76, Apr 1982 (the Prose design paper; not in
  `docs/`). D. Klatt, "Review of text-to-speech conversion for English," JASA
  82(3), 1987.

---

## 2. Board hardware (from MAME `tsispch.cpp`)

**CPU:** Intel 8086 at 8 MHz (24 MHz / 3, "VERIFIED" clock), running in min mode.
**DSP:** NEC µPD77P20 (7720 family) at 8 MHz (16 MHz / 2). MAME runs it on its
µPD7725 core by repacking the 24-bit opcodes.
**UART:** i8251A at U15, with a 153600 Hz baud clock. **PIC:** 8259A at U4/U5.
**DAC:** 12-bit, fed from the 7720's **serial output (SO/SCK)**. MAME does not
implement this path; this project's `native/prose_dsp.cpp` does.

### 8086 memory map (20-bit; I/O space unused)
| Range | Mirror | Device |
|---|---|---|
| `00000-02FFF` | `0x34000` | SRAM (2×6264, only ¾ used) |
| `03000-03003` | `0x341FC` | i8251 UART (umask `0x00FF`, even bytes) |
| `03200-03203` | `0x341FC` | 8259 PIC (even bytes) |
| `03400` (read) | `0x341FE` | DIP switch S4 |
| `03401` (write) | `0x341FE` | Peripheral/param register (see below) |
| `03600` | `0x341FC` | 7720 data register (r/w) |
| `03602` | `0x341FC` | 7720 status register (read only) |
| `C0000-FFFFF` | — | ROM (4 × 64 KB, interleaved even/odd) |

### ROM interleave (v3.4.1)
| File | Load address | Byte lane |
|---|---|---|
| `v3.4.1__2000__2.u22` | `C0000` | even |
| `v3.4.1__2000__3.u45` | `C0001` | odd |
| `v3.4.1__2000__0.u21` | `E0000` | even |
| `v3.4.1__2000__1.u44` | `E0001` | odd |
| `v3.12__8-9-88__dsp_prog.u29` | 0x600 bytes = 512 × 24-bit words, packed | DSP program |
| `v3.12__8-9-88__dsp_data.u29` | 0x400 bytes = 512 × 16-bit words, **word-swapped** | DSP data ROM |

The MAME 7720→7725 bit shuffle for OP/RT, JP and LD opcodes is documented in
`prose_v3/tsispch.cpp:246-281`.

### Peripheral register `0x3401` (write)
Values in MAME's "high byte" numbering (bits 8-F), with the bit index within the byte in brackets:
- bit 8 [0]: 7720 P0 → 8259 IR0 mask/enable (probably)
- bits 9-C [1-4]: LEDs 6/5/4/3 (0 = on). LED 6 is the "Talking" LED.
- bit E [6]: 7720 RESET (0 = held in reset, 1 = running)
- bits D [5] and F [7]: unknown

At idle, LEDs 5 and 3 are on. On each character received from the 8251, bit 8 is cleared and then set again.

### 8259 IRQs
IR0 = 7720 P0 (masked by param bit 8) · IR1 = 8251 RxRDY · IR2 = 8251 TxEMPTY ·
IR3 = 8251 TxRDY · IR4-7 = unknown.

### DIP S4
S4-7 on enables the test mode, which repeats "This is version 3.4.1 test mode, activated by
switch s4 dash 7". S4-8 (0x80) is checked at boot (`D3109`).

### v3.4.1 boot (MAME notes, then decompiled 2026-09-25: `src/input/in_boot.c`, §14)
MAME's notes:
`D3109` checks S4-8 → `D3123` → RAM test at `D32B0` (shift pattern) and
`D32E6` (word rotate) → `D312E` → fill 0x55 at `D314E` → `E3987` **init
µPD7720** → `D33D2` ROM checksum → `D5E14` init 8259. `F44B4` is the general
LED status write. Hardware revisions: a 1981 TSI board without U82 and a 1986 Speech Plus
board with U82.

**Decompiled** [verified: code; the normal path matches the emulator's RAM byte for byte, `boot_check`, §14]:
- **`boot_reset` `D3100`** (the reset vector jumps here):
  - **DIP S4-8 off (normal):** latch 0x1C, then `ram_test` `D32B0` over linear 0-2BFF. Each byte is walked from FF down
    to 0, then from the top back up to FF. The latch shows 0x0A on a pass, 0x12 on a failure.
  - It fills 0-2BFF with 0x55; **2C00-2FFF is not cleared**.
  - It sets DS = SS = F410 and SP = C200 (linear 0300), `[DB90]` = EF00 (the I/O page 0300:0000 as a DS offset),
    `[DB8C]` = 1 (power-up, not restart) and `[C200]` = 0 (or 0x10 after a RAM error).
  - Then it calls `synthesis_loop(0x12)`.
  - **DIP S4-8 on:** factory loops. With S4-4..6 = off, on, on (`(port & 0x38) == 0x30`) the RAM test repeats
    forever; otherwise the ROM checksum repeats.
- **`loop_entry` (§15.6), code 0x12, hardware part:**
  - **`self_test` `D3377`** → `[EE78]`, reported as `ESC[nR`: 0x10 after a RAM error, else **`rom_checksum`
    `D33B3`**. That sums each 32 KB byte lane of D0000-FFFFF (lanes 2-7: D000 even/odd, E000, F000) against the words
    at `DS:0010-001A`, and returns the first failing lane. The latch shows 0x08 on a pass, 0x10 on a failure.
    C0000-CFFFF is not checked.
  - **`pic_init` `D5E14`:** 8259 ICW1 0x17, vector base 0x20, ICW4 0x0F, all masked. Vectors 0-0x27 go to
    `default_isr` `D63A4` (`fatal_error(0x25)`). **Vector 8 → `0000:0024`**, where 15 bytes from `DS:03AA` are copied:
    `cli; mov es,0300; mov es:[0402],al; jmp FFFF:0000`. So `ESC[nL` writes *n* to port 3402 and cold-boots (§8.2).
  - **`latch_init` `D5DF6`:** latch 0x5E.
  - **`uart_init` `D6006`:** the 8251 is reset (0, 0, 0, 0x40). The mode byte is `0x4A | (~DIP & 0x0D) << 2`: 1 stop
    bit, x16 clock; **S4-1 on = 8 data bits (off 7), S4-3 on = parity, S4-4 on = even parity** [8251 bit meanings
    inferred from the data sheet]. The command byte is 6 (receiver on, RTS), or 4 with DIP S4-5 or S4-6. The vectors
    are 0x21 = `uart_rx_isr` `D6260` and 0x22/0x23 = `uart_tx_isr` `D631B`; IR1 is unmasked.
  - **`dsp_boot` `D5EE3`:** it pulses the DSP reset (latch bit 6) and sends the 9-word boot block from `DS:5644`, each
    word after the status says so (0x20 first, then RQM 0x80, at most 100 polls, else `fatal_error(0x29)`). Then it sets
    latch bit 5. The vectors are 0x20 and 0x27 = `dsp_irq_service` `D615F`; IR0 and IR7 are unmasked.
- **Interrupts:**
  - IR0: `dsp_irq_service` runs `dsp_frame_tick` when the DSP status has 0x20. Otherwise it counts in `[DB76]`
    (20 → `fatal_error(0x23)`); the emulated DSP raises one such interrupt after its reset. Decompiled, §11.1.
  - IR1: `uart_rx_isr` → `host_rx_char` (§8.1).
  - IR2/3: `uart_tx_isr` → `tx_send` `D734B` → `tx_next_byte` `DE3AE` (§15.5).

### Other sets in repo
- `prose_v1/`: v1.1 (1983) set, 10 × 8 KB 2764s. In MAME it hangs waiting for 7720 status `== 0x20`. Compared with v3.4.1 in §16.
- `prose_v3_4001/`: Prose 4001 ISA card. **80188** CPU, D77P20 DSP, 3 × 27C512
  (load order u3, u2, u1), 256 kbit SRAM, 8251A, and 3 DIP banks. Can run standalone
  over serial. See `prose4k1.cpp`. It runs the **same v3.4.1 firmware**, rebuilt for the 80188, and is
  in Ghidra as the second program (§10).
- **DSP ROM note (user instruction, 2026-09-23):** the only DSP dump we have, `v3.12__8-9-88__dsp_*.u29`, comes
  from the **Prose 2000**. The 4001's DSP ROMs were never dumped (MAME lists `v3.12_5_04_90`, `NO_DUMP`), but they
  are the same v3.12 version. When the DSP program is decompiled, treat the 2000 dump as the reference and
  assume it is **probably compatible with the 4001 firmware** too. Confirm by checking that the 4001's
  CPU→DSP frame format matches the 2000's before relying on it.

---

## 3. CPU ↔ DSP protocol (from [877], T/VIS channel board)

The T/VIS channel board is the multi-line telephone product from the same Speech Plus
TTS lineage, not the Prose 2000 board itself. It describes the interface the firmware
probably shares:

- The TTS CPU does text analysis and generates *amplitude, frequency and bandwidth
  parameters of a time-varying digital filter network* (a formant synthesizer).
  The signal processor turns these into audio.
- **The DSP interrupts the CPU (INT0) every 10 ms** when it has finished the
  current frame. The ISR sends the next **74-byte parameter packet** over the
  PD bus. Interrupts are edge-triggered.
- **When no data is ready**, the CPU sends a special *silence packet* once, then
  sends nothing. The DSP keeps reusing the last packet, which is silence, until
  new data arrives.
- On Prose 2000 this probably maps to 7720 P0 → 8259 IR0, the ISR writing
  parameters to `0x3600` and polling status `0x3602`, and a 10 kHz output rate.
  At 10 kHz, a 10 ms frame is 100 samples. The packet size on the Prose board may
  differ from 74 bytes, so measure it.
  **Measured and decompiled (§11):** the Prose 2000 frame is **80 bytes (40 words)** every 10 ms, sent on IR0 when the
  DSP raises USF0. The silence-packet behaviour is confirmed.
- Host (central CPU) ↔ TTS CPU traffic is byte-wise through latches, with
  INT1 = host wrote a byte and INT2 = host read a byte.

---

## 4. TTS host-facing behaviour hints ([877])

- **Index markers:** special markers placed in the text are not spoken. Each one is
  echoed back to the host after the preceding text has been spoken. Up to **256
  unique markers** are supported, and they are used for synchronization.
- Error codes are reported back for hardware failures and bad commands.
- Speaking rates quoted for the e-mail application: 90, 115, 165, 190 and 205
  wpm, with a default of **140 wpm** and a step of about 25 wpm. The list as
  printed omits 140. A rate change takes effect at the next punctuation mark.
- Speech is flushed or interrupted at punctuation boundaries. **Verified
  2026-09-23:** with `prose_cli`, `"This is a test."` alone produced silence
  (all 15 bytes were read, but the DSP only got its init writes). Adding a
  trailing **CR (`\r`)** made it speak. `ESC [ x` also flushes (§8).
  **Refined (§8.1):** the receive handler maps CR, LF and TAB to a space, and `"This is a test. "`
  (trailing space, no CR) also speaks [verified]. So the rule is that the final punctuation must be
  followed by *some* character (space, CR or LF) or by `ESC[x`.
- Index markers are **`ESC[n i`**. The firmware echoes `ESC[n i` when that point is spoken, and the
  sentence-done reply `ESC[n x` carries the last marker value [verified, §8.2].

---

## 5. TTS pipeline (from [216], FIG. 1, describing the Prose 2000)

1. **Text normalization (20):** expands abbreviations, numbers, money,
   punctuation and non-alphabetic characters into words.
2. **Dictionary lookup (22):** exception words, typically 3000-5000 entries,
   stored as phoneme strings.
3. **Word-to-phoneme rules (24):** letter-to-sound rules for all other words.
4. **Word-level stress assignment (26).**
5. **Allophonics (28):** context-dependent allophones for *consonants*
   (e.g. aspirated vs unaspirated /p/).
6. **Sentence-level prosodics (30):** duration and F0 contour. Depends on
   phonetic form, part of speech, speech rate and the selected prosody type.
7. *(The patent's addition goes here: vowel-allophone assignment.)*
8. **Parameter generator (40):** "constructive synthesis" by rule that produces
   time-varying parameters **every 10 ms**.
9. **Formant synthesizer (42):** the DSP, followed by the DAC, a low-pass filter
   and an amplifier.

Stock Prose 2000 vowels are **static formant targets smoothed toward
neighbouring phonemes**. The vowel-allophone library is described as a
*reprogrammed Prose 2000*. It is unknown whether v3.4.1 includes it (see §6.7).

Phoneme inventory: **57 phonemes = 23 vowels + 33 consonants + silence**.
The patent says "3 consonant", which is a typo; 34 consonant indices include
silence.

**Exception dictionary in v3.4.1 (verified by string scan):** it sits at about
`F44D8-F6225` in the linear 8086 image. Entries are phoneme strings in a
one-character-per-phoneme ASCII alphabet. `&` marks the start of a word,
digits `1`/`2` are stress marks after the stressed vowel, and `@`, `|`, `3`
and similar are vowel symbols. Examples: `&TRi1jY@N` (trillion),
`&SePTe1MB3` (September), `&Se1K@ND` (second). Orthographic number words
(`thousand`, `thirteen` …) appear at `F5223-F5277`. In DS terms the dictionary is
`DS:03C8…` (DS = `F410`). The same entries sit in TruVoice at `1010D728…` (§9.3). The main
lexicon is a separate packed structure in segment `E900` (§9.4). The same alphabet is
what `ESC [ 1 I` phoneme mode accepts (verified, §8.3). The full alphabet is decoded in §12.3a. `D3000` holds `Copyright 1987 SPI`. Every
character appears twice in the interleaved image because both the even and
the odd ROM contain the string.

---

## 6. Vowel allophone system ([216]) — data structures to look for in ROM

### 6.1 Formant parameters per allophone (Tables 1-2)
Four formants, f1-f4. Each formant has:
- `bb`: frequency at the back boundary (start)
- `v1..v4` at times `t1..t4`: intermediate trajectory
- `fb`: frequency at the forward boundary (end)
- `b3`: bandwidth 30 ms after start; `b5`: bandwidth at 50% of duration; `b7`: bandwidth at 70% of duration

f1-f3 have all three bandwidths. f4 has no bandwidths (it uses a constant bandwidth).

### 6.2 Scaling (Table 3)
| Param | Bits | Encoding |
|---|---|---|
| bb1, fb1, v·1 | 8 | Hz / 4 |
| bb2, fb2, v·2 | 8 | (Hz − 500) / 8 |
| bb3, fb3, v·3 | 8 | Hz / 16 |
| bb4, fb4, v·4 | 8 | Hz / 16 |
| b3 | 6 | Hz / 8 |
| b5 | 5 | Hz / 12 |
| b7 | 5 | Hz / 12 |
| t·· | 4 | % of allophone duration / 2 |
| FX1 / FX2 / FX3 / FX4 | 10 / 9 / 7 / 6 | codebook index |

Without codebooks, one allophone takes 38 bytes. With codebooks, one Allophone
Data Table record takes **19 bytes**: FX = 32 bits (4 bytes), bb/fb = 8 bytes,
bandwidths = 3 × 16 bits (6 bytes), and LLRR = 1 byte.

### 6.3 Codebooks
- There are four codebooks, one per formant. Each row holds `(v1,t1)…(v4,t4)`, which is
  4 × 8-bit frequency + 4 × 4-bit time = **6 bytes per row**.
- One example build used sizes n1 = 741, n2 = 451, n3 = 127 and n4 = 81, a total of 1400. The
  total can also be 4000. The index widths (10/9/7/6 bits) fit these sizes.
- The codebooks were built by minimax vector quantization, with distance = area between
  trajectories × 1/F. This happens offline only; the runtime just looks rows up.

### 6.4 Context index (FIG. 7A)
- The **Phoneme Index Table** maps any phoneme to 0..33 (33 consonants + silence).
  Vowel entries map to substitute consonants (Table 5):

| Neighbouring (outer) vowel | Substitute index |
|---|---|
| /ej/, /ij/, /ai/, /ɔi/ | /j/ |
| /ou/, /juw/, /uw/, /ɔ/, /au/ | /w/ |
| /ɝ/, /ir/, /er/, /ur/, /ɔr/, /ar/ | /r/ |
| /ə/, /a/, /ʌ/, /æ/, /ɛ/, /I/, /ɨ/, /U/ | /ʔ/ (glottal stop) |

The table lists 23 vowels, which matches the vowel count.

- `CVC = I2 + 34 * I1`, where I1 is the index of the preceding phoneme and I2 the index of the
  following phoneme. The range is 0..1155.

### 6.5 Per-vowel decoder (FIG. 7C, 8, 9, 10) — one set of each table per vowel (×23)
- **Allophone Context Table:** 1156 mask bits, which is about **145 bytes** per vowel.
  `Mask = 1` marks a distinct allophone. For those, `CI = popcount(mask[0..CVC])`, which is
  1-based. `CIMAX(V)` is the number of distinct allophones.
- **Duplicate Context Table:** 4-byte records `(oldCVC:u16, newCVC:u16)`, with
  **24-111 records per vowel**. If `mask == 0`, look up oldCVC and restart with newCVC.
  If it is not found, use a default context.
  - Variants: store CI directly in place of newCVC, or use a flat u16 CI table (1156 × 2 bytes per vowel).
- **Allophone Data Table:** `CIMAX(V) + 16` records of 19 bytes. The last 16 are the
  *Extended* table for LCVC/CVCR contexts.
- **LLRR byte** in each record: low nibble `LLRRx` (0 = no extended context)
  and high nibble `LLRRd`.
- **LLRR Context Table:** 15 entries (for LLRRx = 1..15), each `(LRI, CC)`. LRI = 0 compares the
  phoneme to the left of the C-V-C window with CC; LRI = 1 compares the phoneme to the right.
  On a match, `CI = CIMAX(V) + LLRRd`.
- Size estimate: about 10,000 distinct allophones × 19 bytes ≈ 190 KB. The design target was
  ≤ 256 KB for all vowel data. A table this size would dominate a 256 KB ROM set,
  so if it is present it is easy to spot.

### 6.6 Parameter stream generation
Frames are output every 10 ms for each formant:
- **quadratic** smoothing from bb to the first codebook target
- **linear** smoothing between targets t1..t4
- linear smoothing from the last target to fb

Bandwidth is interpolated linearly: previous phoneme → b3 → b5 → b7 → next boundary.
Back-smoothing into the preceding consonant is done **except** after nasals
(m, n, ng) and stops (p, t, k, b, d, g), which keep a discontinuity in F2-F4.

### 6.7 RE heuristics
- Search the 8086 ROM for runs of 23 bitmaps of 145 bytes each, and for tables of
  4-byte (u16, u16) pairs whose values are all < 1156.
- Search for 6-byte codebook rows. The high nibbles are t values of 0..15, which
  should be monotonic within a row.
- Search for a 57-entry phoneme table whose values fall in 0..33.
- A multiply by 34 (`0x22`) in 8086 code is a strong CVC-index signature.
- If none of these turn up, v3.4.1 probably uses the older static-vowel synthesis by rule.

---

## 7. T/VIS system context ([877]) — low relevance

This section covers the multi-channel IBM PC host, six channels each with TTS + UART + DTMF/DAA,
and the e-mail-by-phone application. It includes a host utility API (SPEAK, GET-TTS, WAIT-TTS,
DIAL and others) and PAL-generated bus strobes (CH-RD/WT, CPU-RD/WT, STAT6/7-CLK).
Not needed for Prose 2000 firmware work beyond §3-4.

---

## 8. Host command protocol: `ESC [ n X` ([cgrm] + emulator tests)

Commands are sent inline in the serial text as `ESC [ <decimal> <letter>`,
e.g. `\x1B[150p`.

**Tests (2026-09-23).** Emulator: `native/` built out-of-tree with
`cmake -G Ninja`. The stale `native/CMakeCache.txt` names "Unix Makefiles",
so configure into a fresh build directory. Command:
`prose_cli prose_v3 110000000 "<ESC seq>This is a test of the speech system.\r" out.wav`.
Results were compared against an `ESC[0V`-prefixed baseline: start and end
of speech, peak RMS, and median F0 by autocorrelation. Baseline: speech from
1.16 to 2.97 s, median F0 about 132 Hz.

| Seq | [cgrm] meaning, range | Prose v3.4.1 result | Status |
|---|---|---|---|
| `nV` | voice | **3 voices.** 0: F0 about 132 Hz (default), 1: about 111 Hz, 2: about 167 Hz. Values 3 and 7 give output byte-identical to 2, so they are clamped. Voices also differ in level; whether formants differ is not yet measured. | **verified** |
| `na` | amplitude 0-16, **higher = quieter** | `1a` peak RMS 6605; `12a` peak RMS 1835 | **verified** |
| `np` | pitch 50-400 | `150p` → F0 213 Hz; `60p` → 95 Hz | **verified** |
| `nr` | rate 50-250 wpm | `250r` → 1.01 s of speech; `80r` → 3.18 s (baseline 1.81 s) | **verified** |
| `nv` | "speed" 0-25, default 13 | `25v` gives the same timing and F0 statistics as `250r`, though the output is not byte-identical. `1v` gives 3.5 s. It looks like a coarse rate scale of roughly n × 10 wpm. | **verified** effect; units inferred |
| `nP` | 0 = word (list) mode, 1 = regular prosody | `0P` → 3.7 s, flat F0 around 100 Hz | **verified** |
| `ns` | pause in 1/100 s | `200s` delays the start of speech by 1.64 s (expected 2.0 s) | **verified**; units approximate |
| `nI` | 0 = text, 1 = phoneme input | `1I` gives different output (English text read as phoneme codes) | **verified** |
| `x` | end of sentence | flushes without a CR. The firmware replies **`ESC [ 0 x`** on the UART when that point is spoken, which is the index/sync marker from §4. | **verified** |
| `nf` | fast read 0-9 | `8f` → **no audio** within 110 M cycles | effect unclear |
| `nX` | context mode (e-mail) | no audible change; firmware replies **BEL (`0x07`)** | **not supported** on Prose |
| `nc` | 0 normal / 1 whisper / 2 monotone | no audible change; replies **BEL** | **not supported** on Prose |
| `nt` | phoneme test (developer) | not tested | [cgrm] claim only |

**Firmware → host messages** (UART output): `ESC [ 0 R` + `XON (0x11)` after
reset (probably "ready"), `ESC [ 0 x` for a sentence or marker reached, and
`0x07` for an unsupported command.

**Static search note:** the v3.4.1 code has no `cmp al,1Bh` or `cmp al,5Bh` immediates. The parser
compares bytes against memory (`cmp byte [bp-1],1Bh`) instead. It is decompiled in §8.1.

### 8.1 Parser internals (decompiled 2026-09-23; 4001 program, 2000 addresses in brackets; in C 2026-09-25, byte-exact, §14)

| Linear 4001 [2000] | Name | Role |
|---|---|---|
| `D6FB8` [`D713F`] | `host_rx_char(ch, status)` | Called by the serial interrupt per received byte. Counts the 8251 error bits (parity, overrun, framing, break; dword counters [`EE82-EE90`]; a break becomes NUL). Masks the byte to 7 bits and maps **CR/LF/TAB → space**. `^R` (`0x12`) restarts the synthesis loop (`restart_cold` [`D31B5`], reply `ESC[0R`, §15.6). NUL and DEL are dropped. XOFF/XON `0x13/0x11` pause/resume *output* ([`EE12`]). Any other control char except ESC → **BEL**. Everything else goes into the 16-byte RX ring (2000: `DS:EBF0`, read [`EC00`], write [`EC02`]; a byte that does not fit is lost). If the ring is not already being drained ([`DB94`]), it drains it through the parser with interrupts on. With DIP S4-5 and S4-6 on, input is ignored while the host holds DSR low. |
| `D6462` [`D658C`] | `host_escape_parser` | Takes one char from the ring. **CS = `D62C`** [2000: `D63E`], with jump tables at `CS:01E2` (state) and `CS:0324` (command) [2000: `D65CE`, `D6715`]. The state is in `[DB9A]` [`DB96`]; the 16 parameters at [`DB9C`], the index byte [`DB98`], the overflow flag [`DB9A`]. |
| `DDE17` [`DE345`] | `input_put(ch)` | One byte into the input stage's ring `DS:EC04` (§15.5); sends XOFF when fewer than 0x50 bytes are free, returns 0 when full. |
| `DDF4E` [`DE4C6`] | `host_send(kind, ch, n, params)` | Host transmit into the 256-byte TX ring `DS:ED0E` (`[EE10]`). `kind 0` sends one char (XON/XOFF; BEL only if mode flag 10 is set). `kind 1` sends the report `ESC [ p1;p2… ch`, plus CR if mode flag 15 is set. |
| `D6C54` [`D6D8C`] | `host_status_report(n)` | Answers `Q`, through a 14-entry table at `DS:09EA`. |

Parser states:
- **1 (text):** the char goes to the text pipeline (`DDB1:0307`) unless K-mode (`[EE6C]`) is on.
- **2:** after ESC. `[` → state 3 and resets 16 params (`DS:DB9F…`) to −1; anything else → BEL.
- **3:** digits accumulate (×10; > 255 sets an overflow flag); `;` moves to the next param (max 16). A
  parameter not given stays −1 (`ESC[;;5v` has three, the first two −1). Any other byte is the **command letter**,
  dispatched by a 56-entry switch on `ch − 'A'` (`A`…`x`). Overflow or more than 16 parameters make it an error.
- An ESC in any state starts a new sequence.

After the handler, the tail at `D6BE6` acts on its result:
- **−2:** fully handled; nothing further.
- **−1:** error → **BEL**. This covers an unknown letter, out-of-range values, too many params, or overflow.
- ***n* ≥ 0:** the command is **re-inserted in-band** into the text stream as
  `ESC, letter, n, p1…pn` (only if the input ring has *n* + 3 bytes free; otherwise it is silently dropped).
  With K1 nothing is passed on and no BEL is sent. It therefore takes effect at that point in the speech rather than
  immediately; this is how rate, pitch and similar changes land mid-sentence.

### 8.2 Command table [decompiled; ✓ = also verified in emulation]

Settings live at `DS:EE32-EE44`. `w` resets them all and copies them into five 34-byte records at `DS:C208` [2000: `C206`]. Table addresses are the 4001's; the 2000's are in brackets where they differ. **Commands that are not in-band act when the character arrives**, which is before the text sent ahead of them has been spoken, and how far the stages have got by then depends on timing. `w` in mid-text, for example, rewrites the stage records under text already taken in, so whether earlier in-band `V`/`p` settings still apply to it varies [verified: the emulator and the C pipeline, which takes text faster, differ there; 2026-09-25].

| Cmd | Params → effect | Range / default | Where | In-band? |
|---|---|---|---|---|
| `V` | voice | 0-2, clamped (3+ → 2) ✓ | `[EE44]` | yes |
| `a` | amplitude, higher = quieter | 0-15 ✓, def 0 | `[EE3C]` | yes |
| `p` | pitch (baseline) | **50-200** (cgrm says 50-400), def **85** ✓; `0` allowed | `[EE3A]` | yes |
| `v` | speed | 0-25, def **13** ✓; capped by the fast-read table `DS:03A4[f]` [`03BA`]. Sets rate = `v·8 + 50` wpm | `[EE36]`, `[EE38]` | yes |
| `r` | rate in wpm | < 50 → 50 (def **150** ✓). **Converted to `v = (wpm − 46) >> 3`** and then handled as `v`, so `r` and `v` are one control ✓. **`ESC[0r` sets speed 0, the slowest, while the rate reported by `Q` becomes 150** ✓ (2026-09-25). TruVoice uses the same formula into a 26-row table (46-253 wpm) but does not clamp: it crashes below 46 and reads past the table above 253 [tvd] | `[EE38]` | yes |
| `f` | fast-read level | 0-9, def 0; lowers the current speed to the cap `DS:03A4[f]` [`03BA`]: 25, 20, 16, then 13 for levels 3-9 | `[EE3E]` | yes (2 params) |
| `P` | 0 = word mode, 1 = prosody | 0-1, def 1 ✓ | `[EE34]` | yes |
| `I` | 0 = text, 1 = phoneme input (2 = two-letter phoneme names: only through `[` with `ESC[6A`, §15.5) | 0-1, def 0 ✓ | `[EE32]` | yes |
| `s` | pause | 1-255, def 100. **About 10 ms per unit above 49**: speech starts 1 frame later per unit from 50 to 250 (809 → 1020 in 10 ms samples), but only about 0.25 frame per unit below 50 ✓ (2026-09-24). This matches [tvd]'s TruVoice reading, "hundredths of a second biased by 49" | — | yes |
| `g` | **hold the `l` frame** ✓: speaks one held frame built from the `l` values. It lasts about *n* − 100 frames (`150g` 55 frames, `250g` 151); *n* ≤ 100 gives only a ~70 ms minimum (2026-09-24). Same as TruVoice's `ESC[100g` [tvd] | 1-255, def 100 | — | yes |
| `i` | **index marker** | 1-255 ✓: echoed as `ESC[n i` when spoken. `ESC[i` = 1, `ESC[0i` = error | — | yes |
| `x` | end of sentence / flush | reply `ESC[n x` with the last marker ✓. When playback reaches it, the loop **resets the whole pipeline and both host rings** (`loop_reset`, §15.6), so **text sent after `ESC[x` that arrives before the reply is discarded** ✓ (2026-09-25). Wait for `ESC[n x` before sending more. **`x` also sets the end request `[EE70]` as soon as it arrives, and while that is set the generator ends its stretch at the next index marker node** (`pg_run.c`; v1.1 the same): the marker gets flag 0x20, playback stops there with the marker's number, and the rest is discarded. So `Hello ESC[1i world.ESC[x` sent at once speaks only "Hello" (reply `ESC[1i ESC[1x`), while with a CR in place of the `x` all of it is spoken ✓ (2026-09-26). A last phrase without final punctuation is held back until `x` or `ESC[C`; a late `ESC[C` (its `]` boundary) releases it without ending at a marker ✓. The DLL ends texts that way (API.md, Implementation) | `[EE70]`, `[DBD0]` [`DBCC`] = 0 | yes |
| `W` | **restart**: `restart_warm` [`D31DA`] resets the stack and enters the synthesis loop with code 0x77 (`loop_entry`, §15.6): speech stops at once, all settings go back to their defaults, both host rings are emptied, reply `ESC[W` + XON ✓ (2026-09-25). `ESC[nW` with a value is an error | — | — | no |
| `C` / `H` | **H holds speech at once**: it sets `[DBD0]` [`DBCC`], and `dsp_frame_tick` then sends the DSP no frames. C clears it (at once, and it is also passed in-band) ✓ (an `H` … `C` 2 s apart pauses the audio for 2 s, 2026-09-25). `<` and `>` in phoneme input set and clear the same flag (§15.4) | — | `[DBD0]` [`DBCC`] | C yes, H no |
| `K` | K1: text and forwarded commands are dropped (mute input); values above 1 are ignored | 0-1 | `[EE6C]` | no |
| `S` | **stop**: sets `[EE6E]`; when the loop next reaches its top it resets the whole pipeline (`loop_reset`, §15.6), dropping everything queued, and replies `ESC[S`, or `ESC[n x` if an `x` had been received ✓ (2026-09-25) | — | — | no |
| `q` | **quit**: sets `[EE76]`; the loop then stops speaking (`quit_reset` `E3CE7`, §15.6) without a reply, keeping the host rings and the settings ✓ (stop and no reply verified 2026-09-25) | — | — | no |
| `l` | `l n;val`: sets low-level parameter *n* to a **raw track byte** in the §11.4 list ✓. Used only by `g`: `ESC[0;60l ESC[9;100l ESC[17;120l ESC[250g` gives frames with AV 60, F1 byte 100 and F0 120, and the others at their defaults (2026-09-24) | *n* 0-21. Default value from `DS:61E8[n]` [`61FE`], max from `DS:60CA[n]` [`60E0`]; stored at `DS:EE46[n]`. `w` does not reset them; the loop entry does | 22 params | yes |
| `t` | **test mode** (the user recalls it has at least 10 entries; the code accepts *p1* **0-9**, so exactly 10, and ≥ 10 = error). *p2* 1-255 (def 100), *p3* 30-243 (def 50). The frames come from `paramgen_hold` `DCFA2` mode `t`: 22-byte rows at `DS:6122`, where `FF` entries take AV from 60 − attenuation and F0 or F1-F3 from the phoneme node; the F1-F3 case also flags the frame for the 10 Hz formant coding (§11.3). Role of *p2*/*p3* still [I] | — | — | yes |
| `b` | **4001 only**: 1-6, calls `D5CD:0551` with interrupts off. Probably serial baud select [I]. The 2000 rejects it | — | — | no |
| `L` | **2000 only**: 0-5, executes `INT 8` with the value in AL (`D3457`). Vector 8 points to a stub the boot code copies to `0000:0024` (§2): it writes the value to **port 3402** and jumps to the reset vector, so the board **cold-boots** (reply `ESC[0R`) ✓ (2026-09-25). What port 3402 selects is unknown; it is not in MAME's map [I] | — | — | no |
| `N` / `F` | turn **mode flags** on / off: each param *k* (1-16) is bit *k*−1 of `[EE40]` | def on: 7,8,9,10,11,13 ✓ (`0x17C0`). **Flag 2 = speak punctuation** ✓ ("Hi, there." echoes `Ko1M@` "comma" and `P41E@Dp` "period"; [tvd] reports the same for TruVoice). Flag 10 = send BEL on errors; flag 15 = CR after reports; **flag 16 = phoneme echo** ✓ (§8.3) | `[EE40]` | yes (2 bytes) |
| `A` / `D` | turn **A-flags** on / off: *k* 1-7 in `[EE42]` | def on: 1,7 ✓ (`0x0041`) | `[EE42]` | yes (2 bytes) |
| `E` | identify → **`ESC[34E`** ✓ | — | `DS:03B8` [`03CE`] | no |
| `Q` | query. `Q`/`0Q` = dump all settings as reports ending `ESC[0Q` ✓; `nQ` (1-12) = one setting (`3Q` → `ESC[1;7A`, `12Q` → `ESC[0V`) ✓; `255Q` = `ESC[w`, then only the settings that differ from the defaults (no `r`), then `ESC[255Q` ✓. Other values are errors | 0-12, 255 | table `DS:09EA` | no |
| `w` | reset all settings to defaults, also in the five stage records at once (not the `l` values) | — | — | no |
| **not supported** | `B G J M O R T U X Y Z [ \ ] ^ _ `` ` `` c d e h j k m n o u` → **BEL** ✓ (`X`, `c`); also `b` on the 2000 and `L` on the 4001 | | | |

Full dump from `ESC[Q` (verified, defaults):
`ESC[7;8;9;10;11;13N ESC[1;2;3;4;5;6;12;14;15;16F ESC[1;7A ESC[2;3;4;5;6D ESC[0I ESC[0a ESC[85p ESC[0f
ESC[13v ESC[1P ESC[150r ESC[0V ESC[0Q`.

Firmware → host messages seen: `ESC[0R` + XON at reset, `ESC[n x` sentence done, `ESC[n i` marker reached,
`ESC[34E` identity, the `Q` reports above, and BEL for errors.

**A trailing escape does not change the last word** ✓ (2026-09-24, phoneme echo). [tvd] found that in TruVoice any escape
after a lone final "a" makes it the article (`%@`) instead of the letter name, which trips NVDA's trailing index
marks. On the Prose, the final punctuation decides this instead: `a` + `ESC[x` → `@1`, and `a` + `ESC[5i` + `ESC[x` →
`@1` too, while `a.` → `A1` with or without a trailing `ESC[5i`. With a trailing marker the done reply carries its
number (`ESC[5x`).

**Consequence for this project.** `README.md` says the firmware has one
voice and only Rate, Pitch and Volume. That is out of date: voices 0-2,
word mode, phoneme input and sync markers are all reachable over serial.

### 8.3 Phoneme echo: `ESC[16N` (decoded and verified 2026-09-24)
Mode flag 16 (in-band copy `[C286]` bit 15 on the 2000, `[C288]` on the 4001) makes the firmware **send each
phoneme to the host as it is spoken**.
- **Writer:** in `paramgen_advance` DDC70 (at `DDD20`) [4001 `DD828`]. It logs to the 128-byte ring `DS:EA5E`
  (write `[EA5C]`, read `[EA5A]`) [4001: `DS:EA60`, `[EA5E]`/`[EA5C]`]. For each segment it logs:
  - the phoneme char
  - a stress mark from node `+4` bits 3-4: 1 → `2`, 2 → `1`, 3 → `"`
  - any preceding boundary nodes (kind 3) whose char has feature `+200:08`, i.e. `, . ? \ ]`
- **Reader:** `phoneme_echo_drain` D64E1 [4001 D63BA], called from `dsp_frame_tick` with a budget = segments
  played (`[DD9A]` delta, carried in `[EA56]`). It sends with `host_send(0, ch)`. After a two-phoneme lead-in
  (`[EA58]`), each phoneme costs one budget unit, so the echo stays in step with the audio. Marks and punctuation
  are sent immediately.
- **Verified in emulation:** `ESC[16N` + "Hello, my friend." sends `HeLO1 ,MIFRe1NDp  . `. Without the flag
  nothing is sent.
- **Decompiled** (2026-09-25, `src/input/in_dsp.c`) [verified: byte-exact on captured frame interrupts, and the
  host output of `pipeline_play` equals the emulator's for 11 texts with `ESC[16N`]. The reader loop, per log byte:
  if the lead-in count `[EA58]` is 0, or 1 while playback is not held (`[DD80]` = 0), it sends at once; otherwise a
  byte without feature `+200:08` (not a mark or punctuation) waits while the budget is ≤ 0. After a send, a phoneme
  (feature `+0:80`) raises the lead-in count up to 2, and after that costs one unit of budget. It stops when
  `host_send` fails (output ring full). The budget left is kept in `[EA56]` for the next frame.
- `ESC[w` in mid-text resets the N flags in the stage records at once, so the echo can stop before anything is sent
  (a timing race, like the other immediate commands, §14).
- **Uses:** a direct way to get the firmware's letter-to-sound output for any text, which helps decode the
  phoneme alphabet. That was done with it: §12.3a. For NVDA
  it could drive progress or lip-sync.

### TruVoice-only details ([cgrm], not applicable to Prose hardware)
- `TV_ENG32.DLL` API: `tts_Open(dev, opts, &h)`, `tts_Speak`, `tts_SaveFile`
  (WAVE/RAW/AU), `tts_Phoneme` (text → phoneme string), `tts_CallBack`,
  `tts_Close`, plus Pause/Resume/Stop/Reset/UserDic.
- Output: PCM or A-law (`0x00030000`), 8 kHz or 11 kHz. Prose is 10 kHz.
- Defaults: rate 150, pitch 85, volume 5, voice 0.
- Ten voices: 0 Peter, 1 Sidney, 2 Eddie, 3 Douglas, 4 Biff, 5 Amos,
  6 Melvin, 7 Alex, 8 Wanda, 9 Julia. The Prose 2000 names for voices 0-2
  are unknown. Prose voice 0 has the same fixed 5th resonator as Peter (3970 Hz / 200 Hz, §11.3). Prose voices 1
  and 2 (≈ 3700 / 500 and ≈ 4400 / 500) match none of TruVoice's (Sidney 5970 / 200, Eddie 4100 / 280) [tvd].
- Error codes: −601 to −623 (engine/Dialogic), −1000 to −1026 (wave I/O).

---

## 9. Lineage cross-reference: TruVoice (`TV_ENG32.DLL`) and DECtalk (DTC-01)

Sibling projects: `C:\Users\abart\Desktop\TruVoice_disasm` (Centigram TruVoice, 1997 Win32 DLL, image
base `0x10000000`; its REFERENCE.md §1-§6 maps the text front end) and `C:\Users\abart\Desktop\DTC-01`
(DECtalk v1.8, 68000; its `docs/REFERENCE.md` §14 covers MITalk lineage). Lineage: MITalk → **Prose**
(Speech Plus / TSI → Centigram, later **TruVoice**) and MITalk → **DECtalk** (Klattalk). Work done
2026-09-23; scratch scripts not kept, method below.

### 9.1 Method
Common-substring scan of the interleaved v3.4.1 image (`C0000-FFFFF`) against the DLL's
memory-mapped image: 8-byte seeds (≥ 3 distinct byte values), extended greedily, runs ≥ 12 bytes, then
clustered by constant ROM→DLL offset. Both are little-endian x86, so byte and 16-bit data compare
directly. The DLL widens every 16-bit pointer to 32 bits, so pointer-bearing tables match at 70-95 %,
with gaps where the pointers are.

### 9.2 Result summary
- **Prose ↔ TruVoice: 38,408 bytes (≈ 15 % of the ROM) are verbatim in the DLL.** The linguistic data
  was carried over nearly unchanged. **Verified.**
- **Prose ↔ DECtalk: only English number words** (`one two … nineteen`, `hundred thousand million
  billion trillion`) and the phrase ` is version `. There are no shared tables, which confirms two independent MITalk forks. **Verified.**
- **DSP:** none of the 7720 data-ROM constants appear in the DLL, as 16-bit or 32-bit, in either byte
  order. *Revised 2026-09-24:* this does not mean the synthesizer was rewritten. TruVoice builds the same 40-word
  frame with the same formulas (§11.3, from [tvd]), and its C filter consumes that frame. The constants differ
  because TruVoice runs at 8 and 11.025 kHz instead of 10 kHz. The filter was most likely ported and rescaled
  **[I]**.

### 9.3 Shared regions (ROM linear ↔ DLL VA)

The prose DS segment is **`F410`**. Seen in the emulator (`ds=f410`) and loaded at `D315E/D31B6/D31DB`,
so `DS:xxxx` = `F4100 + xxxx`.

| ROM | DS / seg | DLL | What | Status |
|---|---|---|---|---|
| `E9200-F26xx` (≈ 37 KB) | seg **`E900`** | `100E4480` header, `100E4A6C` payload … `100F2AD0` | **Main pronunciation lexicon.** Two-level 16-bit index (first letter → word length → bucket) at `E900:0200`, then packed entries. The DLL keeps the same payload bytes, adding entries and using 32-bit pointers. | verified (both lookups decompiled) |
| `F41A8-F44A8` | `DS:00A8` (+`0x80`, `0x100`, `0x200` planes) | `100A2C50` (285 code refs) | **Phoneme/letter feature table**, indexed by 1-char phoneme or letter code. Bits: vowel `0x87`, voiced stop `0xA4`, voiceless stop `0xA0`, fricative `0xC0/0xC4`, nasal `0x96`, liquid/glide `0x86`. Other planes are used as `(c|0x80)&0x20` (vowel letter?) and `(c|0x100)&2` (syllable boundary). | verified (usage), bit names inferred |
| `F555E-F5E06`, `F6153-F61F8` | `DS:145E…` | `1010D728…`, `1010E340` (used by `1000F2D0`, `1000F8BE`) | Exception dictionary and number words **spelled in the 1-char phoneme alphabet** (§5), e.g. `&Mi1jY@N`. About 32-100 % match, so TruVoice edited some entries. | verified |
| `F6271-F62A5` | `DS:2171` | `1010080F` (table proper at `DS:2172` ↔ `10100810`) | Packing length-adjust table (`DS:2172` in `lex_lookup`) | verified |
| `F70AC-F7432` | `DS:2FAC…` | `100AEEC8-100AF305` (268 pointers from `100AF000-100B3FFF`) | Context-pattern strings with flag bytes. Endings are stored reversed (`LLI`, `SS`, `NIR`), and class codes are ≥ `0x80`. | inferred: affix/LTS contexts |
| `FA22B-FA2FE` | `DS:612B` | `100A1B59` (used near `10012139`, `1005C99D`) | 28-byte records ending `0E FF 10 08 00 00 FF`, with bytes like `64 70 96 CE 46 2D 37` | unknown; possibly parameter-target records |
| `FD5C1-FD9FD`, `FEF64-FEFA8` | `DS:94C1…`, `DS:AE64` | `100D9B8A-100DA15A`, `100DA4B4` (per-phoneme byte tables `100DA098`, `100DA158`) | Per-phoneme byte arrays. One looks like durations or percentages (`19 1D 19 57 … 64 64 96`), another like small class numbers `0-9`. | inferred |
| `FDCE8-FE843` | `DS:9BE8-A743` | `100CF854-100D1167` (989 internal pointers; code users `1002A1AD-1002A64A`) | **Affix tables.** Suffix lists per last letter at `DS:A696` and prefix lists per first letter at `DS:AD50` (`ANTI`, `AUTO`, `BE`, `CIRC` …), plus cluster contexts (`SCH`, `SHR`, `SPL`, `THR`). | verified (Prose side decompiled) |
| `F954E-F9580` | `DS:544E` | `100BC214` | 16-bit constants (`0x0528` ×8, `0xFAD8`, `0xF5B0` …) | unknown |

Not shared: the ROM `C0000-E91FF` (8086 code) and the DLL `.text` (i386), as expected.

### 9.4 Prose functions identified through the match (Ghidra, program `prose2k_v341_C0000.bin`)

Ghidra shows real-mode addresses as `d000:xxxx` / `e000:xxxx`; linear = seg·16 + off.

| Linear | Name | Evidence |
|---|---|---|
| `D3225` | `lex_index_word(a,b)` | far; `DS=E900`; returns `word[word[a*2] + b*2]` |
| `D3245` | `lex_read_byte(off)` | far; returns `byte[E900:off]` |
| `D738A` | `lex_lookup` | **same algorithm as DLL `10074f90`**. It packs letters with the state table `DS:212E` (12-byte records; TruVoice uses 24-byte records at `10100788`), limited to 16 letters (TruVoice 20). It indexes by `(first−'A', len)`. The entry header's low nibble is the phoneme count and the high nibble the word class (→ `[EEA3]`). Phonemes are 6-bit codes decoded with `DS:2152`, where `code|0x40` gives the phoneme char. `0`/`1`/`2` are stress codes. Full format: §9.6. |
| `E4F57` | `lex_lookup_with_affixes` | whole word, then suffix list `DS:A696`, then prefix list `DS:AD50`, retrying `lex_lookup` after each strip |
| `E5152` | `affix_match` | matches an affix string and its context pattern, then inserts a `'['` boundary node |
| `E5264` | `stem_respell_and_lookup` | stem repair (adds `E`, `I`→`Y`, undoubles letters), then retries |
| `D81AB` | `match_context_pattern` | interprets the flag+letter/class pattern bytes in the tables above. Class masks at `DS:0048` are tested against the `DS:00A8` feature planes. |
| `D3614` / `D3805` / `D38B8` / `D36D2` | `node_insert` / `node_delete` / `node_next` / `node_prev` | word or phoneme doubly-linked list. Node: `+0` next, `+2` prev, `+4` flags, `+9` char. `node_next`/`node_prev` return 0 at the end/start of the current stage's window (§15.1). The list head and tail are at `[C232]`/`[C234]`. (`D38B8` was first named `node_prev`; renamed 2026-09-24 because it follows `+0`.) |

Word-list globals: `[EE98]`/`[EE96]` = start/end of the current word, and `[EE9A]` = current word record.
These functions belong to the lexical stage; the whole stage is decompiled and described in §15.3.
`FUN_d000_6400(code)` is the fatal-error routine. It is called with codes `0x1E`, `0x1F`, `0x20` and `0x2A`.

### 9.5 Consequences / next steps
- **TruVoice is a direct descendant of the Prose 2000 v3.x data set.** Names and structures found in one project transfer to the other. The TruVoice REFERENCE.md still calls `10074f90` "homograph stress"; it is the lexicon lookup.
- The DLL's `100E4440` string `@|ObfUAEIyaeivowu3rg5k4c` sits next to the lexicon header and is
  **the vowel set** of the alphabet (§12.3a): Prose's 23 vowels plus TruVoice's new `5`, which sits among the
  r-coloured vowels `r g 5 k 4 c`.
- Next steps:
  - ~~Decode the lexicon entry format and write a dumper~~: done, §9.6 and `tools/lexicon_dump.py`.
  - ~~Map the 1-char phoneme alphabet~~: done, §12.3a.
  - ~~Follow `lex_lookup_with_affixes`'s caller into the LTS fallback~~: done, the lexical stage (§15.3).

---

### 9.6 Lexicon entry format (decoded 2026-09-24) [verified: decoder output matches `lex_lookup` and the phoneme echo]
Decoder: **`tools/lexicon_dump.py`** (`--word X` looks up one word; `--tsv` writes a full dump). The dump is ROM
data, so keep it local and uncommitted.

**Index** (segment `E900`, ROM `E9000`):
- `word[L·2]` (L = first letter − 'A', 0-25; 26 = end sentinel) points to a row of 16-bit bucket offsets indexed
  by *n*, the number of letters **after** the first.
- Bucket (L, *n*) runs from `row[n]` to `row[n+1]`.
- Rows overlap: a row's length runs up to the next letter's row start, so letter A has *n* 0-12. `lex_lookup` guards
  this with `< index(L+1, 0)` and `< index(26, 0)`.
- The payload is `E92D2-F2672`. It holds **4,466 entries**: 2,973 with pronunciations and 1,493 stress-only. Word
  lengths are 1-14 letters (the lookup allows up to 17).

**Entry** = header byte + key + pronunciation, with no separators:
- **Header:** the low nibble is the phoneme-code count *k*; the high nibble is the **word class** (→ `[EEA3]`).
- **Word classes:**

  | Class | Meaning | Class | Meaning |
  |---|---|---|---|
  | 0 | content word | 8 | negative (NOT, CAN'T, NEVER) |
  | 1 | pronoun (HE, HIS, IT, EVERYONE) | 9 | DO/DID/DOES |
  | 2 | BE, CAN, HAVE, THE | 10 | preposition (AT, BY, IN, TO) |
  | 3 | weak function word (A, AN, OF, FROM) | 11 | modal (MAY, MUST, MIGHT) |
  | 4 | adverbial preposition (ABOUT, AFTER) | 12 | auxiliary (IS, WAS, WOULD) |
  | 5 | subordinator / discourse adverb (IF, BECAUSE, HOWEVER) | 13 | wh-word |
  | 6 | verb | 14 | coordinator (AND, BUT, OR, NOR) |
  | 7 | quantifier (ALL, EACH, MANY) | 15 | -ly adverb |

  When a whole word matches with a class other than 0, 6 or 15, `lex_lookup` rewrites the word record's char to `%`,
  which marks a function word. The affix path (`E5021-E5037`) assigns classes 6, 15 and 8 to derived words.
  **[I]** The prosody stage uses these classes to de-stress function words.
- **Key:** letters 2..*n*+1 as 5-bit codes (`letter & 0x1F`; code 0 = apostrophe), packed in a 3-state cycle by
  the table `DS:212E`, so 3 letters fit in 2 bytes:
  - state 0: byte bits 0-4
  - state 1: letter bits 2-4 → bits 5-7 of the same byte, letter bits 0-1 → bits 0-1 of the next byte
  - state 2: bits 2-6

  The last key byte is compared under the mask `1F`/`03`/`7F` of the final state. Its unused high bits already hold
  the first phoneme bits.
- **Pronunciation:** *k* 6-bit codes, 4 per 3 bytes (table `DS:2152`). The phase starts after the key:
  - phase 0: `b & 3F`
  - phase 1: `(b & C0) >> 2 | (b' & 0F)`
  - phase 2: `(b & F0) >> 2 | (b' & 03)`
  - phase 3: `(b & FC) >> 2`

  A code maps to a char as `code | 0x40`; codes `0x20` and `0x1C-0x1F` map to `4`, `0`, `1`, `2`, `3`. The alphabet
  is §12.3a.
- **Stress codes** in the pronunciation come **before** the vowel they mark. `1` gives primary stress (node level
  2, or 3 if `[EEA6]`, or 1 if `[EEA4]`); `2` gives secondary (level 1); `0` keeps the next vowel from getting the default stress (unused in
  v3.4.1's entries). An entry with no digit gets default stress on its first full vowel. For example `wjM2OST` (ALMOST),
  `2AVE1oN|K` (AVIONIC).
- **Stress-only entries** (*k* = 0): header + key + one byte, copied to `[EEA8]`. The **letter-to-sound rules**
  supply the phonemes, and this byte gives the **stress pattern**: 2 bits per syllable, right-aligned (last
  syllable = bits 0-1). `11` = primary, `10` = secondary, `01` = unstressed full vowel, `00` = reduced. Examples:
  AGO `0011`, ATOM `1100`, AVENUE `110001`, ENGINEER `100011`, AGITATION `10001100`. Readers of `[EEA8]`:
  `D7964`, `D7CC2`, `D806D`, `D8786`, `D87B3`, `E4E71`.
- **Entry size:** `(key bytes − 1) + DS:2172[state·17 + k]`, or `(key bytes − 1) + 3` for *k* = 0. Zero-length keys
  start at state 2.
- **Check:** decoded SALMON `SaM@N`, YOSEMITE `YOS1eM|tE`, COLONEL `K3Nl` and CHOIR `KWI3` match the phoneme echo
  (`Sa1M@Np`, `YOSe1M|tE`, `K31Nl`, `KWI13`). The differences are later allophone rules (`J` → `Jz`, post-vocalic
  `L` → `j`).
- **TruVoice** (`tools/lexicon_dump.py --dll …/TV_ENG32.DLL`, verified 2026-09-24): the same entry format and
  state machines (24-byte records at `0x10100788`, adjust table `0x10100810`). The index is 32-bit VAs: 27 row
  pointers at `[0x10100844]` = `0x100E4480`, and each row is an array of bucket VAs.
  - **Renumbered codes:** `0x1C-0x20` = `1 2 3 4 5` (the Prose has `0 1 2 3 4`, where `0` is unused). This frees
    `0x20` for the new vowel `5` = /aɪr/.
  - **Size:** **6,304 entries** (4,601 with pronunciations) against the Prose's 4,466. Words run to 15 letters.
  - **Shared words:** of 4,299 distinct words in both, **3,974 are byte-identical**. The rest: 136 differ only in
    word class (mostly verb 6 → content 0), 50 only in homograph order, 105 in pronunciation or stress (e.g. ACROSS
    `@KRoS` → `@KRwS`, CHAPEL `CaP@j` → `CaPl`, many stress-only entries given full pronunciations), and 34 in both.
  - **Other changes:** 33 Prose words were dropped (e.g. FIRE, which TruVoice's rules now cover with `5`), and
    about 1,845 new words were added.
  - **Lookup changes:** TruVoice's lookup adds a context-based **homograph chooser** (verb vs noun entry by the
    previous word's tag: article, preposition, TO, BE/HAVE, possessive…). The Prose lookup only has the
    record-flag rule (`[EE9A]+4` bit 5 with `+6` = 4 skips the first match). Details: TruVoice REFERENCE §7.8.
  - Stress-only bytes print as four fields in the tool (`0001` = last syllable primary), because leading `00`s
    can be real reduced syllables.

## 10. Prose 4001 (ISA card, 80188) image — second Ghidra target (2026-09-23)

Same firmware version as the Prose 2000 (v3.4.1; DSP v3.12, not dumped but reported identical). This is a
rebuild for the **80186/188 instruction set** on different glue hardware.

### 10.1 ROM layout [verified]
The 80188 has an 8-bit bus, so the ROMs are **linear, not interleaved**: `u3` → `D0000`, `u2` → `E0000`,
`u1` → `F0000` (`rom_order.txt`, MAME `prose4k1.cpp`), giving a 192 KB image `D0000-FFFFF`. The Prose 2000's
`C0000-D2FFF` is all `FF`, so both boards really hold about 180 KB from `D3000` up.

Match against the Prose 2000 image:
- **u1 (`F0000`)** is 61 % identical at the **same addresses**. These are the data tables; some sub-blocks
  are shifted by a few bytes.
- **u2 (`E0000`)** has a 28,681-byte identical run `E8FFF-EFFFF` (the lexicon tail), and the rest is code
  shifted by `+0x700…0x900`.
- **u3 (`D0000`)** is code shifted by about `±0x300`.

### 10.2 Differences from the 2000 build [verified]
- **Instruction set:** it uses 186 opcodes — 135 `ENTER`, 233 `LEAVE`, `PUSHA`, immediate shifts. The 2000
  build has only a few `ENTER`-like byte patterns, which are data. Most functions are far (`RETF` ≈ 800).
  Function starts look like `56 57 C8 nn 00 00` (push si, push di, enter).
- **Same segments:** main entry is `D310:0000`, `DS = F410`, lexicon segment `E900`.
- **Data layout shifted:** the feature table is at `DS:00A2` (linear `F41A2`), 6 bytes lower than on the
  2000. Word-list globals move up by 2: `[C234]/[C236]` instead of `[C232]/[C234]`; `[EEA9]/[EEAB]`.
  Everything in §9.3 still applies after these small shifts.
- **Boot stub** at `FFC00` (`boot_188_init`) runs before `D3100`:
  - `OUT FFFE,1040h` relocates the 80188 peripheral control block to **memory `04000h`**.
  - UMCS `F03C` = 64 K at `F0000` (u1).
  - LMCS `03F8` = 16 K RAM (`00000-03FFF`; MAME maps 32 K).
  - MMCS `C1FC` / MPCS `A0FA` = 4 × 64 K mid-range blocks from `C0000`, so u3 = MCS1 (`D0000`) and
    u2 = MCS2 (`E0000`). Peripherals are memory-mapped; 7 PCS lines.
  - PACS `033A` = peripherals at `3000h`, **PCS0-6 at `3000/3080/3100/3180/3200/3280/3300`**.
- **Peripheral mapping found so far:**

  | Function | Prose 2000 | Prose 4001 |
  |---|---|---|
  | DIP read (`D3109`) | `3400` | `3090` (PCS1) |
  | control/LED latch (the boot stub writes `FF`) | `3401` | `3180` (PCS3) |
  | 8251 UART | `3000` | not yet traced; **not** PCS0, which is the DSP |
  | 7720 DSP data/status | `3600/3602` | **`3000/3002` (PCS0)** [verified: `dsp_write_frame` `D3293`, §11] |
  | interrupt controller | 8259 at `3200` | not yet traced; probably the internal 80188 controller at PCB `04022-0403E` [I] |

  Runtime accesses mostly go through pointer or segment variables, so trace them in Ghidra or an emulator.

### 10.3 Ghidra program `prose4001_v341_D0000.bin` [verified]
The image is imported raw as `x86:LE:16:Real Mode` with base `d000:0000`.

Functions were seeded with inline scripts at every far-call target, every `push bp; mov bp,sp` or
`ENTER` prologue, and every "`RET`/`RETF`, then prologue" boundary in `D3000-E8FFF`. That gives 192 functions,
covering 73 % of `D3000-DFFFF` and 51 % of `E0000-E8FFF` as code. The remainder is inline data (jump tables,
strings) or code not yet reached.

Names carried over from the 2000 program by matching body bytes (prologues differ, so match at +4…+40):

| 4001 | 2000 | Name |
|---|---|---|
| `D3257` | `D3225` | `lex_index_word` |
| `D3277` | `D3245` | `lex_read_byte` |
| `D3640` | `D3614` | `node_insert` |
| `D3829` | `D3805` | `node_delete` |
| `D38DA` | `D38B8` | `node_next` |
| `D62E2` | `D6400` | `fatal_error` |
| `D7140` | `D738A` | `lex_lookup` |
| `D7F0F` | `D81AB` | `match_context_pattern` |
| `E4669` | `E4F57` | `lex_lookup_with_affixes` |
| `E4854` | `E5152` | `affix_match` |
| `E4951` | `E5264` | `stem_respell_and_lookup` |
| `FFC00` | — | `boot_188_init` |
| `D6462` | ESC test at `D65AA` | `host_escape_parser` (§8.1). Its CS context was set to `D62C` in Ghidra. |
| `D6FB8` | — | `host_rx_char` |
| `D6C54` | — | `host_status_report` |
| `DDF4E` | reply at `DE6C4` | `host_send` |

**Ghidra caveat (jump tables):** Ghidra assumes CS equals the segment it shows in the address (`d000`), so it resolves
`jmp cs:[bx+…]` wrongly. For `host_escape_parser` I set the CS register context to `D62C` and added the jump
references by script. The **decompiler still shows the wrong switch**, so read the handlers in the listing, or in
REFERENCE §8.2. To find the real CS of any routine that has a jump table, subtract the table's `CS:` offset from
the table's linear address.

When carrying a name over, a candidate that starts with `5E CB` (the previous function's `pop si; retf`)
is 2 bytes early.

The decompiler output is about as readable as the 2000's. The 186 `ENTER`/`LEAVE` frames decompile
cleanly, and the linear ROM avoids the interleave step. Prefer the 4001 program for code reading. Use
the 2000 program for board I/O (8259, `0x36xx` DSP) until the 4001 PCS mapping is traced.

---

## 11. CPU → DSP frame path (decompiled 2026-09-23; Prose 2000 program, 4001 addresses in brackets)

### 11.1 Call chain [verified]
```
IR0 (7720 P0) → dsp_irq_service D615F [4001: not yet found — 80188 interrupts differ]
   - pulses bit 0x04 of the control latch via [DB84] (= 3401h)
   - reads DSP status via [DB78] (= 3602h): if USF0 (0x20) is set → dsp_frame_tick; else counts misses,
     and 20 in a row → fatal_error(0x23) = "DSP timeout"
   └─ dsp_frame_tick D643C (far D63E:005C)
        if frame ready [DBCE]: dsp_write_frame(0x28, DS:DBD2); failure → fatal_error(0x24); [DBCE]=0
        then advances the output/marker bookkeeping ([DD9A], [EA56-EA5C] event queue → host_send)
        └─ dsp_write_frame D3261 (far D310:0161)  [4001: D3293]
dsp_build_frame D89CB [4001: D8710] — producer: fills DS:DBD2 from the 22 parameter tracks, sets [DBCE]=1
dsp_frame_handshake D8949 (op 3 = wait/idle check, 4 = mark ready, 5 = query [DBCE], 6 = clear; other → fatal 0x22)
```

**Decompiled** (2026-09-25, `src/input/in_dsp.c`) [verified: byte-exact on 2,468 captured `dsp_frame_tick` calls,
all of RAM including the builder's working words; every instruction ran except the fatal 0x24 path]:
- **`dsp_frame_tick`** (interrupts off, latch bit 3 low while it sends): unless output is held (`ESC[H`, `[DBCC]`) or
  playback has reached the ring's hold point (`[DD80]`), it sends the frame that is ready (`[DBCE]`), or counts a miss
  in `[DB92]`. After a send, `[DD80]` = 1 if the ring position `[DD82]` has reached `[DD84]`; the generator clears it.
  Then, with interrupts on again, it **builds the next frame** (`dsp_build_frame`) and, with N-flag 16, runs the
  phoneme echo (§8.3) with the segments that build played. **So a frame reaches the DSP one interrupt after it is
  built**; the first request after silence gets no frame (the DSP repeats its old one, §13).
- **`dsp_frame_handshake` op 3**, under `irq_disable_nested`, refuses while a frame waits (`[DBCE]`) or a build is
  running (`[DBD0]`), and otherwise claims the buffer. `dsp_build_frame` needs the ring to be at least `[EAE4]`
  frames ahead of `[DD82]` (`[DD82] + [EAE4] < [DD86]`), takes the frame's mark and 10 Hz bits out of their bitsets
  (a mark counts in `[DD9A]`), copies the 22 track bytes to `DS:DC46` and builds; its working words are
  `DS:DC28-DC44` (the parallel-branch terms, the F4/F3/F2 gains at `DC3C/DC3E/DC40`, the last cosine and product
  at `DC44/DC42`).
- **`dsp_write_frame` `D3261`** returns 1 at once if the DSP is not asking (no USF0), writes the 40 words when it is,
  and returns 0 only if USF0 is still set 10 polls after the last byte.

### 11.2 Wire protocol [verified]
- **Ports:** Prose 2000 data `3600h`, status `3602h` (`ES = 0300`, offsets `0600/0602`). **Prose 4001 data `3000h`,
  status `3002h`** (`0300:0000/0002`, chip select PCS0). The transfer code is otherwise identical.
- **Handshake (µPD7720 status bits):** the DSP raises **USF0 (0x20)** when it wants a frame. The CPU waits for
  **RQM (0x80)** before each byte, writes each 16-bit word **low byte then high byte** to the data port, then waits
  up to 10 polls for the DSP to drop USF0.
- **Frame = 40 words = 80 bytes, once per 10 ms.** The patent's 74 bytes describes a different product (§3).
  Emulator check: 7,920 bytes written against 9,930 DSP samples mid-speech, which is 0.798 bytes/sample, i.e.
  **one frame per 100 samples at 10 kHz**.
- **Silence:** after 2 frames with voicing, aspiration and frication amplitudes all 0, frame word 0 gets bit `0x80`.
  This matches the patent's "silence packet".

### 11.3 Frame contents (`DS:DBD2`, word *n* at `DBD2 + 2n`) [verified 2026-09-24: decompiled to C, word-exact; roles confirmed by the DSP decompile, §13]
The 8086 does all the conversion from parameters to filter coefficients, so the DSP only runs filters. This is the
opposite split from DECtalk, where the DSP converts dB and Hz itself.

| Words | Source | Role [I] |
|---|---|---|
| w0 | p18 (`DC6A`), `|0x80` on silence | source-type / control flags |
| w1 | period × p19 × p18 → table `DS:54E8` | voicing gain normalised by pitch period |
| w4, w5, w21 | tables `DS:53B8`, `53C8`, `53D8`, indexed by p21 high nibble (8 entries each) | **fixed 5th-formant resonator per voice** (decoded 2026-09-24): V0 3970 Hz / 200 Hz, V1 ≈ 3700 / 500, V2 ≈ 4400 / 500. V0's values are exactly TruVoice voice 0's (`adj[8]` = 3970, `adj[9]` = 200) [tvd] |
| w17, w37, w38, w39 | tables `DS:5368`/`5378`, `53A8`, `5388`, `5398` by p21 high nibble | voice / source constants. V0 w37 = 11354, which is also TruVoice's `filt_coef[37]` [tvd] |
| w6, w23 | F4 = p12 through `DS:58CA` (cosine), fixed bandwidth coefficient `0x1E11`; `0x3C40 − gain` | resonator 4 (fixed bandwidth) |
| w8, w9, w25 | F3 = p11 (`DC5C`), B3 = p15 (`DC64`) | cascade resonator: coefficient pair + gain term `0x2000 − g + …` |
| w10, w11, w27 | F2 = p10 (`DC5A`), B2 = p14 (`DC62`) | resonator 2 |
| w14, w15, w36 | F1 = p9 (`DC58`), B1 = p13 (`DC60`) | resonator 1 (gain ×16) |
| w32, w29 | FN = p16 (`DC66`) through `DS:58CA`, fixed coefficient `0xE105` | nasal resonator (250 Hz for non-nasals) |
| w30, w31 | F0 = p17 (`DC68`): `20000/(F0+jitter)/2`, twice | pitch periods in samples, two jittered values (LFSR `[DC26]`). Quirk: p17 values `0x45 0x4A 0x4F 0x54 0x5A` are bumped by 1 first; TruVoice keeps the same test [tvd] |
| w34, w35 | p0 (`DC46`) through dB→lin `DS:5CCA`, scaled by the period | voicing amplitude(s) |
| w16 | p2 (`DC4A`) through `DS:5CCA` | aspiration amplitude |
| w18, w20, w22, w24, w26, w28 | p3-p8 (`DC4C-DC56`) + p1 (`DC48`) + corrections (`DS:5E74`, `5F66`) → `DS:5CCA`, multiplied by the resonator gains; **signs alternate** | parallel-branch formant amplitudes (Klatt), zeroed when p1 ≤ 0 |
| w2, w3, w7, w12, w13, w19, w33 | **never written by any code** (no references to their addresses) | constants from the **frame template `DS:5656`** (40 words), copied by `frame_reset_buffer` `D8918`. Emulator values: w2 `9C80`, w3 `4D80`, w7 `702D`, w12 `7A60`, w13 = w33 `7800`, w19 `2000`. **w12/w13/w33 are a fixed resonator of about 250 Hz / 103 Hz** in the coefficient format below. [tvd] solved TruVoice's fixed `filt_coef[12/13/33]` as 242 Hz / 102 Hz, the same resonator |

**TruVoice uses the same 40-word frame** [confirmed 2026-09-24 against [tvd] `frame.c` `Synth_Frame`]. Every slot above has
the same role there: F4 → 6/7/23, F3/B3 → 8/9/25, F2/B2 → 10/11/27, F1/B1 → 14/15/36, 5th resonator → 4/5/21,
FN → 29/32, periods → 30/31, AV → 34/35, AH → 16, parallel amplitudes → 20-28 with alternating signs, and
p18 → 0 with `0x80` for silence. Resonator format: `a = 56CA[B]·58CA[F] >> 12` = `32768·r·cos θ`, `b = 4·57CA[B]` = `32768·r²`, and
gain `= 57CA[B] − a/2 + 0x2000` = `8192·(1 − 2r cos θ + r²)`. Differences: TruVoice computes the 5th resonator as max(voice
constant, F4 + 400 Hz) and drops it above Nyquist, and its p17 track holds **F0/2** (it doubles the byte before the
division). The Prose's holds F0 in Hz.

Useful tables: `DS:5CCA` = dB→linear (the Prose equivalent of Klatt's `amptable`); `DS:56CA`/`57CA` = bandwidth →
`8192·exp(−πBT)` / `8192·exp(−2πBT)`; `DS:58CA` = frequency → `16384·cos(2πFT)`. **Steps: 4 Hz per entry for the two
bandwidth tables, 8 Hz for the cosine table** (512 entries, zero near entry 312 = 2500 Hz). These are TruVoice's
tables 6, 7 and 8 [tvd], which are exact at 8 and 11.025 kHz. At 10 kHz the Prose tables are within 1.5 LSB (exp) and
16 LSB (cos) of the formulas, so they were computed in some other way. (Corrected 2026-09-24: the first
version of this table had frequency and bandwidth swapped; the table shapes and §12's F(n+1) ≥ F(n)+200 rule settle it.) `D3501` / `D34DC` / `D3521` = fixed-point multiply helpers.

### 11.4 Parameter tracks [verified in emulation 2026-09-24]
22 parameters per 10 ms frame, stored as 22 byte-tracks in a 128-frame ring (track *p* at `DS:DF56 + 128·p`,
pointers `DS:DDF6[22]`, read index `[DD82] & 0x7F`), unpacked to `DC46-DC70` (p0-p21) for each frame.

| p | Name | Track byte coding | p | Name | Track byte coding |
|---|---|---|---|---|---|
| 0 | AV voicing amp | dB | 11 | F3 | Hz/16 |
| 1 | AF frication amp (gates the parallel branch) | dB | 12 | F4 | Hz/16 |
| 2 | AH aspiration amp | dB | 13 | B1 | Hz/2 |
| 3-7 | A2-A6 parallel formant amps | dB (60 for vowels) | 14 | B2 | Hz/2 |
| 8 | AB bypass amp | dB | 15 | B3 | Hz/2 |
| 9 | F1 | Hz/4 | 16 | FN nasal resonator | (Hz − 192)/4 |
| 10 | F2 | (Hz − 500)/8 | 17 | F0 | Hz |
| 18, 19 | source type / gain (per voice) | raw | 20 | low nibble per voice, **high nibble = attenuation `a`** | raw |
| 21 | low nibble per voice, **high nibble = voice `V`** (cleared in place by `dsp_build_frame`) | raw | | | |

Checked with a trace harness that samples `DC46-DC70` every 10 ms. The addresses wrap into RAM: `DS:DC46` = `01D46`.
For "see.", the /s/ frames show AV 0, AF 65 and A4-A6 at 77. The /i/ frames show F1 69 (276 Hz), F2 213 (2204 Hz),
F3 176 (2816 Hz), B1 25 (50 Hz) and FN 14 (248 Hz), and F0 falls from 120 to 66 Hz. With `ESC[5a`, p20 is 0x50 and
AV, AF and AH all drop by 5. With `ESC[2V`, F4 and F0 rise. The host `l` command's 22 low-level parameters
(`DS:EE46`, §8.2) index the same list as raw track bytes (verified with `l` + `g`, 2026-09-24).
**Independent check:** TruVoice's frame builder ([tvd] `frame.c`) indexes its coefficient tables with exactly these
codings: F1 ×4, F2 ×8 + 500, F3/F4 ×16, B ×2 and FN ×4 + 192. Its per-voice percentage table scales p9-p15 as
F1 B1 F2 B2 F3 B3 F4, and it reads the voice from p21's high nibble. The only difference is p17, which holds F0/2 there.

**Also verified 2026-09-24:**
- `frame_reset_buffer` `D8918` reloads the template, and `frame_reset_source` `D89B8` sets LFSR = 0x55 and the
  silence counters to 0. Both run between utterances.
- `system_reset` `E37F0` handles power-up and `w`. It loads the §8.2 defaults, and on a cold boot sends the 9-word
  **DSP boot block `DS:5644`** through `dsp_send_block` `D5EE3`: start word 1, then 8 setup words (§13.1).
- Parameter bytes and silence: after the first silent frame, `latch_set_bits(2)` `D5D8B` sets latch bit 1. The first
  sounding frame clears it and sets bit 5 (`latch_clear_bits` `D5D74`).
- F1-F3 use a **10 Hz coding** (`p·10 >> 3` into the 8 Hz cosine table) when the frame's bit in `DS:EAE8` is set.
  Only `paramgen_hold` test mode sets it.
- `w34` gets 8× the shimmer of `w35`.
- Multiply helpers: `fx_mul_q15` `D3521` = (a·b) >> 15, `fx_mul_shr12` `D3501` = (a·b) >> 12 (and >> 13 through a
  pointer), `fx_mul_shr11` `D34DC` = (a·b) >> 11.

### 11.5 Next
- ~~Decompile the DSP program~~: done, §13.
- Find the 4001's DSP interrupt path (80188 INT0-INT3 via the PCB at `04000h`) and its `dsp_irq_service` equivalent.
- ~~The parameter generator~~: done, see §12.
- When the DSP program is decompiled (see the §2 note: the 2000 dump is the reference, probably also valid for the 4001),
  map w0-w39 to the DSP's input registers to confirm the roles above. **Use [tvd] `src/engine/generate.c`
  (`Synth_Generate`) as the guide.** It consumes the same 40-word frame in 16-bit fixed point: a table-interpolated
  glottal pulse, the cascade resonators, the parallel fricative branch, de-emphasis and output scaling. It is very
  likely a C port of the µPD7720 program **[I]**.

## 12. Parameter generator (decompiled 2026-09-24; Prose 2000 program, 4001 addresses in brackets)

This stage turns the phoneme node list (after prosody) into the 22 tracks of §11.4. It is a **target-and-transition
rule system in the Klatt/MITalk style**. Each phoneme loads per-parameter targets. Rules chosen by the phoneme-class
pair adjust the targets, loci, durations and transition types. Each parameter's segment is then written into its
ring track with smoothing ramps.

### 12.1 Call chain [verified]
```
synthesis loop E39A7 → paramgen_run DCB00 (far DCB0:0000)
   window over the node list: [C26E] = prev, [C270] = current, [C272] = next (moved on with node_next)
   flow control: param_ring_ctl DCA52 [4001 DC5D7] (op1 room check, op2 rebase by 0x400, op3 look-ahead, op4 advance)
   └─ paramgen_segment DD703 [4001 DD232], once per phoneme
        ├─ paramgen_load_targets DD1C0 [4001 DCD10]  per-phoneme targets → param structs
        ├─ paramgen_segment_setup DEB3A              per-segment defaults
        ├─ context flags EE1C-EE30 (stressed, boundary, class of next …)
        ├─ paramgen_apply_rules DE96E                rule engine (§12.3)
        ├─ attenuation [C282] ('a') subtracted from AV/AF/AH, clamped at 0; voice [C28C] ('V') scales F1-F4
        │  (DS:52F8) and caps F4 (DS:5358)
        └─ param_emit_segment D3D2F [4001 D3D2A] for p = 0..21 → ring tracks (§12.4)
paramgen_reset DD6A0, param_tracks_init DC9B9 (fills each track with its default DS:610C[p]; write positions start at 10/12)
```
Node fields used: `+4` flags (bits 0-2 = node kind, §15.1; bit 4 = boundary), `+6` low byte = **duration in frames**, `+8` =
**F0 target** (×2 → Hz), `+9` = **phoneme char**.

### 12.2 Per-parameter struct (`DS:DE22 + 14·p`, p = 0..21) [verified]
| Off | Field | Notes |
|---|---|---|
| +0 | type | 2-7, set to 7 by default (§12.4) |
| +2 | durB | backward transition length in frames, ≤ 20 |
| +4 | durF | forward transition length in frames, ≤ 20; default `DS:98A4[p]` |
| +6 | len | segment length in frames (node `+6`) |
| +8 | locB | value the previous segment's tail blends toward |
| +10 | onset | value at segment start (the locus) |
| +12 | target | steady-state value |

Values are held in natural units (Hz, dB) and converted to track bytes only at emit time.

### 12.3 Targets and rules [verified structure]
- **Phoneme index:** `idx = DS:93CA[char]`. There are 58 codes. Indices 0-22 are the **23 vowels**
  `4 A E I O U a b c e f g i k r u w y 3 @ o v |`. Indices 23-56 are consonants `j l L W Y R p d h H B P D T t J C G K
  Q q V F x X Z S z s M m N n ~`, and 57 is ' ' (silence). Upper/lower pairs such as B/P, D/T, G/K, V/F, Z/S, z/s,
  J/C and x/X are voiced/voiceless pairs (confirmed by §12.3a). Diphthong-capable vowels are indices < 18 (these have second
  targets, F1 at `DS:9690` and so on, loaded into `EB44-EB50`); glides and liquids are 23-28.
  Full IPA map: §12.3a.
- **Target tables** (58 bytes each, indexed by idx): F1 `944C` ×4, F2 `9486` ×8+500, F3 `94C0` ×16,
  F4 `94FA` ×16 + per-voice `5348`, B1-B3 `9534/956E/95A8` ×2, AV `95E2`. Consonants (idx ≥ 29) have AF, AH,
  A2-A6 and AB from byte tables reached through pointers `DS:9774-9854`. Vowels get AF = AH = AB = 0 and A2-A6 = 60.
  FN = 248 Hz unless idx ≥ 52 (the nasals), which use `DS:98C0`. p18-p21 come from per-voice tables
  `5308/5310/5318/5320[V]`. Stop/vowel locus data for 32 < idx < 42 is indexed by `DS:98DB[idx]` + the class of the next phoneme,
  from tables at `DS:9905-9A6D` (→ `EBB4-EBCA`, and FN override `DS:9995`). The targets of the
  next phoneme (`EB2A-EB36`) are loaded for anticipation.
- **Rule engine** `paramgen_apply_rules` DE96E:
  - The rule group is `DS:9376[class(cur)][class(prev)]`, which indexes 88 groups: pointers at `DS:8CFE`, counts
    at `DS:8DAE`.
  - A rule is **8 bytes**: `{cond ptr, action A, far-function list ptr, action B}`.
  - Conditions are byte codes, an AND of terms (encoding in §12.5).
  - A matching rule applies A and B through `paramgen_rule_action` DE81F, masked by `1 << voice`, then calls every
    function in its list.
  - The lists are at `DS:7842-798E`, and all end with `pg_finalize` E2FB8. That routine runs
    `pg_locus_weights` E1024, `pg_consonant_loci` E1A49, `pg_apply_loci` E1C1A and `pg_amplitude_boundaries` E2535
    (§12.5a). It then enforces **F(n+1) ≥ F(n) + 200 Hz** for F1-F4 on the onset and locus values, and clamps durB and durF
    to 20.
  - The other list members are the context-rule routines of §12.5.

### 12.3a Phoneme alphabet (decoded 2026-09-24 with the phoneme echo, §8.3) [verified unless marked]
Method: about 100 test words were spoken with `ESC[16N` and the echoed codes were read back. The indices are those
of `DS:93CA`. The echo shows the *surface* output after the allophonic rules, which is why several codes are
allophones.

**Vowels** (idx 0-22):
| Code | IPA | Example | Code | IPA | Example | Code | IPA | Example |
|---|---|---|---|---|---|---|---|---|
| `E` | i | beat | `a` | æ | bat | `f` | aʊ | bout |
| `i` | ɪ | bit | `o` | ɑ | pot | `y` | ɔɪ | boy |
| `A` | eɪ | bait | `w` | ɔ | bought | `U` | ju | cubic *(dictionary)* |
| `e` | ɛ | bet | `O` | oʊ | boat | `3` | ɝ / ɚ | bird, butter |
| `v` | ʌ | but | `u` | ʊ | book | `r` | ɑr | car |
| `@` | ə | about, sofa | `b` | u | boot | `g` | ɔr | core |
| `\|` | ɨ | roses | `I` | aɪ | bite | `k` | ɛr | fair |
| `4` | ɪr | fear | `c` | ʊr | poor, cure | | | |

**Consonants** (idx 23-56) and others:
| Code | IPA | Example / note | Code | IPA | Example / note |
|---|---|---|---|---|---|
| `P` `B` | p b | pin, bin | `F` `V` | f v | fin, vim |
| `T` | t | tin | `X` `x` | θ ð | thin, then |
| `D` | d; also unaspirated t after s | din; strength = `SDRe1~X` | `S` `Z` | s z | sin, zoo |
| `t` | ɾ (flap) | water, little | `s` `z` | ʃ ʒ | shin, measure |
| `q` | glottalized t | button = `Bv1qn` | `H` | h | hat, hue |
| `K` `G` | k g | kin, go | `d` | ɦ (voiced h) | ahead = `@de1Dp` |
| `C` + `s` | tʃ (closure + release) | chin = `Csi1Np`; also tr = `CsR` | `h` | ʍ (wh) **[I]** | phoneme input only; LTS writes `W` |
| `J` + `z` | dʒ | gin = `Jzi1Np` | `Q` | ʔ **[I]** | deleted by rules in `v1Qv1` |
| `M` | m | man | `m` | m̩ | rhythm |
| `N` | n | nap, ten | `n` | n̩ | button, seven |
| `~` | ŋ | sing, ink | `L` | l (prevocalic) | let, hello |
| `j` | ɫ (post-vocalic) | feel, milk | `l` | l̩ | bottle |
| `W` | w | wet, queen | `Y` | j | yet, cute = `KYb` |
| `R` | r | red | `p` | release vocoid **[I]** | after a final consonant before a pause: `Tp`, `Np` |
| ` ` (idx 57) | silence | | | | |

Other symbols: `1` primary stress, `2` secondary stress, `"` emphatic stress (node stress level 3). These follow
the vowel. `&` marks the start of a content word and **`%` the start of a function word** in dictionary and phoneme
input. The exception dictionary has `%@V&K@Lv1MBE@` ("of Columbia") and `%@ND` ("and"), and a class string at
`F5561` begins `%&),.?@[]|~\`. [tvd]'s TruVoice trace shows the same (`&HeLO1.` hello, `%@.` article a, `%I.` I).
The echo (§8.3) does not send either marker. Boundaries are `, . ? \ ]`.
Letter-to-sound quirks seen: "rhythm" → `Ri1Xm` (θ for ð), "the" → `xE`, "cute" → `KYb` (the dictionary uses `U`).
**TruVoice** adds a 24th vowel **`5` = /aɪr/** (verified from its lexicon: FIREFLY `F15FLI`, VAMPIRE `V1aMP5`,
WIRETAP, CHOIRBOY, MCGUIRE; §9.6). The Prose writes the same sound as two codes, `I3` ("fire" → `FI13`).

### 12.4 Track writing [verified]
`param_emit_segment(p)` first converts the struct values with the codings of §11.4 (p9-p16 have their own
cases; B ≤ 0 → `fatal_error(0x30)`). It then writes the ring by **type**:

| Type | Action |
|---|---|
| 4 | hold: `track_ramp_then_hold(0 frames, len, 0 → target)` |
| 6 | ramp from onset to target over durF, then hold |
| 2 | `track_blend_fwd` toward onset over durF (no hold) |
| odd (3, 5, 7) | as type −1, but first `track_blend_back` blends the previous segment's last frames toward locB over durB |

Odd types drop to even when there is no gap. Helpers:
- `track_fill` D3F36 [4001 D3F0E]
- `track_ramp_then_hold` D345F
- `track_blend_back` D3532 and `track_blend_fwd` D357B, which move each byte *x* to *x* + (*v* − *x*)·c/256

The ramp curves `DS:9B9E[k]` are k-frame smoothing ramps (k = 0-20, 0-terminated fractions of 256). The helpers
write at position `DS:DD9E[p]`, and the previous boundary is `DS:DDCA[p]`.

### 12.5 Context rules (decompiled 2026-09-24) [structure verified; routine roles from decompile, tagged per item]

**Rule table statistics:** 88 groups, 621 rules, 533 of them conditional, and 2,032 action entries. **Every
action uses voice mask `0xFF`**, so v3.4.1 does not make the rules depend on the voice. The actions mostly *set* targets:
A5 250 times, p18 229, A2 224, A4 173, F2 167, AB 158, A3 150, AF 113, burst values `EBC8`/`EB66`, and so on. In
other words, the table mostly supplies the consonant spectra (parallel amplitudes) for each context.

**Condition byte code** (`paramgen_apply_rules` DE96E):
- A term is a head byte and its operands, and the list ends with `0x18`.
- Head bits: bit 0 = required result (1 = must hold, 0 = must fail); bits 1-2 = node (0 cur, 1 next,
  2 next+1, 3 prev−1, via `rule_cond_node_char` DE918); bits 3-4 = kind.
- **Kind 0** (`01 n`): context flag *n*, from `DS:EE1C + 2n`, which `paramgen_segment` sets:
  - 0: cur voiced
  - 1-4: previous phoneme's class (0 vowel, 3 closure, 1 voiced, 2 other)
  - 5: cur is released pre-pausally
  - 6-9: next phoneme's class
  - 10: next is stressed
- **Kind 1** (`0A`/`0B` *f*): feature bit *f* of the node, using mask and plane from `DS:0048[f]` into the feature
  table `DS:00A8`.
- **Kind 2** (`12`/`13` chars…): the node's phoneme char is in the list. For example `13 45 69 55` = "next ∈
  {E, i, U}", the front vowels, which pick a different burst spectrum.

**Rule actions** (`paramgen_rule_action` DE81F):
- A and B each point to a null-terminated list of pointers to 6-byte entries `{op, voice mask, value, address}`.
- The ops are 0 set, 1 add, 2 sub, 3 ×q15, 4 += ×q15, 5 acc = value, 6 acc = [addr], 7 [addr] = acc, and 8 fill
  *value*+1 words with acc.
- Addresses are usually param-struct fields (§12.2) or the burst and locus variables `EB64-EBCA`.

**Which routines run** is decided mainly by the *current* phoneme's category, and for some routines by the
previous phoneme's:
| Current phoneme | Function list (always ends with `pg_finalize`) |
|---|---|
| vowel (incl. `p`) | [`pg_after_closure` if prev is a stop/nasal/`Q`] → `pg_sonorant_onset` → `pg_vowel` |
| glide/liquid/`h`/`H`/`d` | [`pg_after_closure`] → `pg_sonorant_onset` → `pg_sonorant_consonant` |
| nasal | `pg_sonorant_onset` → `pg_sonorant_consonant` → `pg_closure_types` |
| stop (incl. `Q`) | [`pg_voiceless_onset` if voiceless] → `pg_obstruent_voicing` → `pg_closure_types` → [`pg_stop_burst`, unless the rule is "released pre-pausally" (flag 5)] |
| fricative | [`pg_voiceless_onset`] → `pg_obstruent_voicing` → `pg_fricative_amps` |
| silence | `pg_shift_aspiration` [→ `pg_after_closure`] |

| Linear | Name | Role [I unless noted] |
|---|---|---|
| `DEBFA` | `pg_voiceless_onset` | After a voiceless stop: `[EBB2]=1` and `pg_shift_aspiration`; sets AV/AF transition types |
| `D3FFB` | `pg_shift_aspiration` | Moves the AH/AV boundary back by `[EBB2]` frames, so aspiration overlaps the previous segment |
| `DEC87` | `pg_after_closure` | Previous = closure: formant durF from `DS:9856[prev]`, amplitude types 4/6 (hold vs ramp); special pairs K+p, s+@, T+3 |
| `DEE85` | `pg_sonorant_onset` | VOT: the stop-release delay `EBB4` lengthens every parameter's `len` and the node duration; formant durF 7/11 after glides; parallel amplitudes carried over (`EB7E`) after K/T/X… |
| `DF245` | `pg_vowel` | Stress (node `+4` bit 5) gives AV +2 / −3 (`o`, `a` a further −3). **Vowel reduction:** short vowels are pulled toward the neutral F1-F3 `DS:985E-9862` by `DS:9884[dur·10/16]`. **Diphthongs** are written as a held onglide (via `track_fill`) plus a transition to the offglide `EB44-EB48`. F2/F3 are lowered next to r-coloured phonemes; F2 −300 before `l`-class. |
| `DFB5B` | `pg_sonorant_consonant` | R/L/W: F2 is interpolated toward the next vowel's F2 (fx_mul 0x2008 / 0x0CD0), with fixed loci in some clusters (R after dental: F2 1140, F3 1400 Hz); formant durF 5/7/9 |
| `DFE87` | `pg_obstruent_voicing` | Voice-bar AV during voiced stops/fricatives (−20, or 0; 30 for `J`); release delay `EBB8` from the next vowel's duration; affricate clusters (J+z, C+s+R) |
| `E01C0` | `pg_fricative_amps` | AF/AH/A2-A6/AB for S, Z, z and s by context (pre-pausal +6 AV, before a vowel, after J/C) |
| `E067F` | `pg_closure_types` | Stops/nasals: types 5/7, formant durF `DS:9856[cur]`, P/K burst settings (`EBC6-EBCA`, `EBB6`); for P/K it calls `pg_stop_burst` itself |
| `E08EA` | `pg_stop_burst` | **[verified by trace]** Splits the stop into closure and burst+aspiration of `[EBB6]` frames. Burst AV/AF/AH come from `EBC6-EBCA` (0x7F = keep), with per-frame decay 1 (K), 3, or 6 (after a nasal). Prevoicing for B/D after a pause. "tea.": 3 silent closure frames, then 7 frames (70 ms) of AF 60 / AH 48 with AV 0, then AV ramps to 62. |

### 12.5a Boundary values: the locus model (decompiled 2026-09-24) [verified by code and trace]
The `pg_finalize` helpers decide where every transition starts. The model is **Klatt-style locus theory**:

- **Locus `L[p]`** (`DS:EB7E + 2p`, natural units). By default `paramgen_advance` DDC70 sets it after every segment
  to the *last value actually written* on track *p*, converted back to Hz or dB by `track_byte_to_value` DDC0A.
  Rules replace it with fixed consonant loci. `pg_locus_weights` and `pg_consonant_loci` set, for example,
  alveolar F2 1600 / F3 2620 Hz, W after an alveolar stop 1200/2050/2500 Hz, and `~` 900/2176 Hz.
- **Weight `W[p]`** (`DS:EB52 + 2p`, q15). The default comes from `DS:9864[prev class][cur class]`, with classes
  0 vowel, 1 voiced, 2 other and 3 closure:

  | prev \ cur | vowel | voiced | other | closure |
  |---|---|---|---|---|
  | vowel | 0.50 | 0.75 | 0.50 | 0.35 |
  | voiced | 0.25 | 0.50 | 0.25 | 0.35 |
  | other | 0.50 | 0.75 | 0.50 | 0.50 |
  | closure | 0.65 | 0.65 | 0.50 | 0.50 |

  `pg_locus_weights` E1024 overrides it by place and manner. Values are 1.0 (jump straight to target, e.g. F3
  into non-labial closures) and 0.99, 0.9, 0.8, 0.75, 0.7, 0.6, 0.55, 0.5, 0.4, 0.3, 0.25, 0.2 and 0.1 (e.g. F2
  after `s` = 0.2).
- **Formants, bandwidths, FN and F0** (p9-p17, in `pg_apply_loci` E1C1A):
  **`onset = locB = W·target + (1 − W)·L`**. The same routine caps F4 per voice (`DS:5358`), keeps targets at
  F(n+1) ≥ F(n)+200 while voiced, clamps durF to the look-ahead `[EAE0]` (durB = durF, len ≤ 60), and sets many
  place-specific transition lengths (velars before front vowels, W/R/L after alveolars…).
- **Amplitudes and source parameters** (p0-p8, p18-p21, in `pg_amplitude_boundaries` E2535):
  `onset = locB = (L + target)/2`, but no more than *d* dB below either side. *d* is 9, or 15 going into silence,
  0 for AH, 10 after a voiceless sound (AH included), 20 for AV of a nasal after a pause, and always 0 for A2-AB
  (verified by the C port, §12.7). The routine then handles AV timing: at voicing changes
  durB = prev len/4 and durF = cur len/4, or /2 before a pause or in a stressed syllable. It also handles the
  **pre-pausal fall** (into silence after a voiced sound: AV locB 48, F0 locB 58 Hz over 2/3 of the previous
  segment) and fricative/stop special cases.
- **Vowel reduction** (`pg_vowel`, §12.5): the neutral targets are F1/F2/F3 = 490/1450/2500 Hz. The pull toward
  them, `DS:9884[dur·10/16]`, falls from 0.88 for 1-frame vowels to about 0.02 for 24+ frames.
- **Trace check ("see."):** /s/ ends at F2 = 1852 Hz, and W(F2) after `s` = 0.2, so the predicted onset for /i/
  is 0.2·2220 + 0.8·1852 = 1926 Hz = byte 178. The first vowel frame reads **179**, followed by the ramp to 213.

### 12.6 Next
- **TruVoice match [confirmed 2026-09-24]:** `TV_ENG32.DLL` contains this generator ported to 32-bit C.
  - Code: `100122E0` = `paramgen_load_targets`, `10012C70` = `paramgen_segment`, `1006F910` = `param_emit_segment`,
    `10011B60` = `paramgen_run`.
  - Data: phoneme map `0x100D9A80`; the 8 target tables at `0x100D9B08 + 0x40·k`; `98A4` and `61E8` are identical.
  - TruVoice adds a phoneme `5` at index 1, and 432 of 464 target bytes are unchanged.
  - **Corrected 2026-09-24:** the rule-engine tables *are* ported. The condition byte strings sit at
    `0x1009A280-0x1009A466`, each padded to 4 bytes, which is why the ≥ 12-byte match missed them. 47 of 58 distinct
    Prose conditions are found there, including 8 of the 10 longest; a few gained or lost terms. 32-bit rule records
    and lists that point at them are at `0x1009E0B0-0x100A0A44`. [tvd] describes the same class-pair rule byte code
    in `CGRM_EN.DLL` (`Stage3_Rules`/`Stage3_Apply`). Only the coefficient tables are new, recomputed for 8 and
    11.025 kHz (§11.3).
  - [tvd] names the 16 routines the rule lists call `Stage3_Op1`-`Op16`. The Prose lists reference **11** (verified:
    the ten of §12.5 plus `pg_finalize`); with `pg_finalize`'s four helpers and `pg_release_onset` (E30C1, called
    from `pg_sonorant_onset`) there are 16 routines in all. From its descriptions: `Op9` = `pg_stop_burst`, `Op4` = nasal murmur and
    release, and `Op14` ("where every parameter starts, and the exceptions to the halfway rule") ≈
    `pg_apply_loci`/`pg_amplitude_boundaries` **[I]**. `Op16` dispatches 14 per-consonant routines. The one-to-one
    map is still open.
  - TruVoice's vowel reduction (`Stage3_Reduce`) follows packed per-neighbour-pair paths, and it has glide tables
    (`Stage3_GlideTab`). The Prose `pg_vowel` pulls toward one neutral target, so these look like TruVoice additions
    **[I]**.
  - Details: TruVoice REFERENCE §7.5 and §7.9.
- ~~The four `pg_finalize` helpers~~: done, see §12.5a.
- ~~Phoneme log reader~~: done, `ESC[16N` phoneme echo (§8.3).
- Feature-bit names used by the rules, inferred from the phoneme membership of `DS:00A8` (TruVoice `0x100A2C50`):
  - plane +0: 01 syllabic, 02 sonorant, 04 voiced, 10 nasal, 20 stop/affricate, 40 fricative, 80 any phoneme
  - plane +80: 08 affricate, 10 reduced vowel (`@ | p`), 20 non-vowel segment
  - plane +100: 01 closure (stops + nasals), 02 vowel, 04 alveolar, 08 rhotic (`3 R`), 10 labial, 20 front vowel,
    40 lateral (`j l L`), 80 velar
  - plane +180: 01 glide/liquid, 04 palatal-ish, 10 full vowel, 20 back/rounded, 40 fronting offglide
    (`A E I y`), 80 lax
  - plane +200: 80 aspirated stop (`P T C`)
  **[I]**
- 4001: only `paramgen_load_targets`, `paramgen_segment`, `param_emit_segment`, `param_ring_ctl` and `track_fill`
  are mapped so far. Its DS layout diverges from the 2000's in this region (not a flat −6 shift).


### 12.7 Decompiled to C (2026-09-24) [verified byte-exact against the emulator, §14]
The whole generator, from `paramgen_run` down, is in `src/paramgen/`. Porting it settled these points:

**Names added** (Prose 2000 program; Ghidra updated):
| Linear | Name | Role |
|---|---|---|
| `DCEEC` | `paramgen_hold_step` | Continues a held stretch (`g`/`s`/`t`) through `paramgen_hold`, as ring room allows |
| `DDE1F` | `find_segment_end` | Looks ahead from the current node for the end of what may be synthesised now; sets `[EAFE]` (1 `C`, 2 `x`/`i`, 3 pause, 4 ≥ 10 nodes, 5 ring underrun) |
| `DDAA2` | `insert_pause_pair` | Inserts two pause nodes (stress 2, then 3) where a stretch must break |
| `DDB81` | `paramgen_clear_state` | Forgets the previous segment: burst/locus variables, loci = track defaults |
| `E30C1` | `pg_release_onset` | The release into a sonorant: [EBB4] frames of AV 0, aspiration [EBC4], B1 150 Hz; after a stop also frication by place and F0 = 0 (voice onset time) |
| `D3F59` | `track_decay_back` | Adds a decaying delta to the frames before a position (amplitudes clamp at 0) |
| `D36FB` / `D3B66` | `stage_commit` / `stage_window_update` | Pipeline window of a stage record (`DS:C26C` for this stage); `D9D2B` `stage_run_command` runs in-band command nodes |
| `D6400` | `fatal_error` | Counts (`EE7A`), records the code (`EE7E`) and restarts the synthesis loop |
| `D63E5` / `D6576` | `irq_off_nested` / `irq_on_nested` | Nesting count `EE74`, latch bit 2 while off |
| `D3C25` / `D38E1` | `list_unlink` / `list_link` | Node list primitives |

**Behaviour** [verified]:
- The rule engine applies **only the first** rule of the group whose condition holds; no match is `fatal_error(0x2F)`.
- The v3.4.1 tables use action ops 0, 1, 2, 6 and 7 only. Ops 3, 4, 5 and 8 exist in the interpreter but are never used.
- `ESC[V` scaling quirk: F1-F3 targets are scaled by `DS:52F8[V]`, but F4 moves by the scaled *F3* times the same
  factor (`paramgen_segment` `DD9C4`), then is capped by `DS:5358[V]`.
- `ESC[a` is subtracted from the AV/AF/AH target, onset and locB after the rules, and again from the closure
  values in `pg_stop_burst`.
- Diphthongs: the onglide is held for `[EBBC]` = F1 length × `DS:9720[idx]` frames, then moves to the offglide
  over `[EBBA]` frames. For `y` (/ɔɪ/) the onglide midpoint is +200 Hz on every parameter except F3 (−200), which
  looks like a slip for "F2 only".
- Track writes that fall outside the RAM window are dropped (the same as on the board).
- The DSP latch is also written by `param_ring_ctl(2)` when positions are rebased by 0x400 (every ~10 s of speech).

## 13. DSP program (µPD7720, v3.12 8/9/88) (decompiled 2026-09-24) [verified sample-exact against the emulator]

**Tools:**
- `tools/dsp_disasm.py [ROM_DIR] [--rom] [--raw]` gives a register-transfer listing. It unpacks the ROMs exactly as
  `native/prose_dsp.cpp` does. Keep the listing local.
- `tools/dsp_recompile.py` writes an instruction-for-instruction C translation. The `src/` build generates it
  into the build directory.
- `src/dsp/prose_synth.c` is the readable decompilation.
- Both C models reproduce the emulator's serial output exactly: four captures, 620k samples, every host word
  consumed at the same sample (§14).

**Notation:** `RAM[x]` = data RAM word *x* (0x00-0x7F). `ROM[x]` = data ROM word *x*, in ascending order; the dump
is stored downward. The listing prints `K <- RAM[dp|40]` for the `KLM` destination and `L <- RO` for `KLR`.

### 13.1 Program map
| Addr | Role |
|---|---|
| `000-017` | Reset: wait for a host word with bit 0 set, clear RAM, raise USF0, read the **8 setup words** into `RAM[38-3F]`, set FIFO pointers `RAM[1B]`=12 / `RAM[1C]`=13, set EI and P0 |
| `018-0F0` | **Sample loop**: one sample per pass (§13.2), written to the output FIFO. `RAM[3C]` counts down, and at −1 it fetches a frame |
| `100-119` | Sample interrupt: send `RAM[22]` with `SOL` (bit-reversed serial word), then move the next FIFO sample (`RAM[00-0D]`, 14-slot ring) into `RAM[22]`. In the emulator it runs once per loop pass (at `018`), which makes the output the sample from two passes earlier |
| `11C` | Cascade resonator: y = sat(2·(g·x + a·y1) − b·y2). Gain at `dp`, coefficients at `dp^10` and `+1`, state at `dp^30` and `+1`. Leaves the old y2 in DR |
| `128` | Glottal filter, same form: gain `RAM[20]` (w37), a/b `RAM[11]`/`RAM[12]` (w38/w39), state `RAM[31]`/`RAM[32]` |
| `136` | Nasal zero: y + b·y[n−2] + 2a·y[n−1] over the nasal pole's output, no saturation |
| `13C` | Parallel resonator: sat(g·x − b·y2 + 2a·y1), summed into B |
| `147` | Glottal pulse shape: the high byte of phase `RAM[2E]` indexes `ROM[2 + hi]`, minus frac·`ROM[abs(hi + 2 − 128)]` >> 15 |
| `15C` | Pitch clock (`RAM[3E]` countdown, `RAM[3F]` half-period). At period end: copy the pending F1-F3 and nasal-zero coefficients to their working slots, swap the two periods w30/w31, and return "new period" |
| `1A2-1F5` | Frame fetch: raise USF0, pulse P0 (the 8086's IRQ). Read w0 and w1, then scatter w2-w39 to `RAM[ROM[0x145 − i] >> 3]`. On timeout (800 polls, then 40 more) keep the old frame once, then go silent |

**Setup words** (from the 8086's `DS:5646`): parallel output scale `0x184D`, `0x4000`, noise seed `0xAAAA`, frame
length−1 = 99 (100 samples per frame), initial countdown 0, `0x2710` (10000), and 0, 0.

### 13.2 Signal flow (one sample)
1. **Pitch clock** `15C`. At each period start: the AV pair w34/w35 swaps (shimmer) and the current amplitude goes to
   `RAM[0E]`. The phase step `RAM[1D]` = w1·0x517D >> 15. The closing mode and rate come from w0: `RAM[1E]` =
   `ROM[0x103 + (w0 & 31)]`, and `RAM[1F]` = 0x4000 if (w0 & 31) ≥ 15 (fine 32-bit closing ramp).
2. **Glottal flow**:
   - Opening phase: AV/2 − (AV/2)·shape.
   - After the phase accumulator wraps, the closing phase: AV·(1 − rate·(1 − shape)).
   - When it wraps again, 0 until the next period.
3. **Excitation**: first difference of the flow, ×8 with saturation, × w17.
4. **Noise** `07A`: two registers `RAM[2A]`/`RAM[3A]`. k = a ^ b, then new = ((k >> 7) & 0x1F8) | (k << 6), with 0
   replaced by 0xAAAA. Aspiration = noise × AH (w16). The source is voice + aspiration.
5. **Glottal filter** (w37-w39), then the **cascade**: F5 (w4/w5, fixed per voice) → F4 (w6/w7) → F3 → F2 → nasal
   pole (w12/w13, fixed ≈ 250 Hz) → nasal zero (FN, w32/w33) → F1. Each stage's input gain is the gain word of the
   resonator before it (w19, w21, w23, w25, w27, w29). Output ×2·w36.
6. **Parallel branch**: the noise is halved while `RAM[3F] ^ RAM[3E]` < 0 (the second half of each pitch period,
   Klatt's voicing modulation). Five resonators: fixed HF w2/w3 (≈ 4.9 kHz, 800 Hz bandwidth), F5, F4, F3, F2, with
   gains w18/w20/w22/w24/w26. Plus the bypass w28 × noise. Then × setup word 0 (0x184D), then first difference.
7. **Output**: cascade + parallel. Then y = x − 0.23·y[n−1] (0x1D71), + 0x1001 bias, clip to 0-0x1FFF (13 bits).
   Silence (w0 bit 7) outputs 0x0FE0. `RAM[28]` (a one-sample offset from w0's high byte, `ROM[0x145 + n]`) is
   stored back from source 0 every sample, which the emulator reads as `TRB` = 0. On a real 7720, source 0 is
   `NON`, which is probably also 0.

F1-F3 and FN update **pitch-synchronously** at period starts; unvoiced (period 0), that is every other sample. F4,
F5 and all gains update when the frame arrives.

**Arithmetic:** saturation is decided by the 7720's OV1/S1 flags over the whole chain since the last logic
operation, so `prose_synth.c` carries an `acc` type that reproduces them.

**Start-up click [verified 2026-09-25]:** the program never initializes its output latch `RAM[22]` or the first FIFO
slot it reads (`RAM[0C]`), so its first two serial words are whatever RAM held at power-on. The emulator zeroes RAM,
and word 0 is DAC code 0, full negative scale; after it, until the first frame, the output sits at the bias (DAC
0x800). On the board this
happens once, at reset. In the C, every render starts the DSP, so every render began with a full-scale spike.
`prose_synth_run` now presets both to the bias word 0x1001, which is what the program outputs next.
`prose_synth_set_raw_start(s, 1)` keeps the emulator's zeros; `dsp_replay` and `chain_replay` use it, and they stay
sample-exact. Silent frames still output 0x0FE0 (DAC 0x7F0), 16 codes below the speech bias, as the firmware does.

**Emulator caveat [I]:** on the board, the sample interrupt is a 10 kHz timer. The emulator (`prose_dsp.cpp`) enters
it at the loop top instead, so the loop and the output rate coincide. The C models follow the emulator.

**Match with OpenTV [tvd]:** `Synth_Generate` has the same structure: table-interpolated glottal pulse, cascade,
parallel branch and de-emphasis. The DSP now confirms the frame roles of §11.3.

## 14. C decompilation (`src/`, started 2026-09-24)
Build: `cmake -S src -B <dir> -G Ninja [-DPROSE_ROM_DIR=<abs path to prose_v3>] && cmake --build <dir>`.
`-DPROSE_VERSION=1` builds the v1.1 tree (`src/v1/`, §16) instead of v3.4.1 (default `3`).
**The ROM's data contents are built in** (2026-09-25): `tools/rom_extract.py ROMDIR` writes `src/data/prose_data.c`
and `.h`, and the programs need no ROM files. The 8086 ROM holds code at `D3000-E582D`, the lexicon segment at
`E9000-F268D` and the data segment at `F4100-FEFA9` (`DS:0000-AEA9`); the rest is FF fill, apart from the reset jump
at `FFFF0` (the script checks this layout). It extracts the two data areas (83,256 bytes), the DSP data ROM
(512 words) and the byte sums of ROM lanes 2-7, which the boot self-test (`rom_checksum`) compares with `DS:0010-001A`.
The C never reads the code. `prose_rom_builtin` builds the image from these tables (code areas FF). **The C has no
ROM loader and the programs take no ROM directory** (removed 2026-09-25); the extractor is the only code that reads
the dumps. Before the loader was removed, a test compared the tables with the dumps (equal; in the window
`DS:0000-BEFF` only the 5-byte reset jump differs, which is code), and all the results below were reproduced from the
built-in data: the 233,571 replayed calls, the frame comparisons, the phoneme echo, `boot_check`, `frame_replay` and
`dsp_replay`. The DSP program ROM is not extracted: `prose_synth.c` replaces it. Only the optional
instruction-level translation reads it (`PROSE_ROM_DIR`, generated into the build directory).

| File | Firmware | Status |
|---|---|---|
| `src/dsp/prose_synth.c` | µPD7720 program (§13) | **sample-exact** vs emulator |
| `src/frame/frame_build.c` | `dsp_build_frame` `D89CB`, `frame_reset_buffer` `D8918`, `frame_reset_source` `D89B8` | **word-exact**: 2,045 frames from 8 captures. The 10 Hz (test-mode) branch has never run |
| `src/paramgen/pg_run.c` | `paramgen_run` `DCB00` and the ring: `param_ring_ctl`, `paramgen_hold(_step)`, `find_segment_end`, `insert_pause_pair`, `paramgen_advance`, `paramgen_clear_state`, `paramgen_reset`, `param_tracks_init` | **byte-exact** (all of RAM) |
| `src/paramgen/pg_segment.c` | `paramgen_segment`, `paramgen_load_targets`, `paramgen_segment_setup`, the rule engine `paramgen_apply_rules` / `paramgen_rule_action` | **byte-exact** |
| `src/paramgen/pg_context.c` | the context routines of §12.5 and `pg_release_onset` | **byte-exact** |
| `src/paramgen/pg_loci.c` | `pg_finalize` and the locus model (§12.5a) | **byte-exact** |
| `src/paramgen/pg_tracks.c` | `param_emit_segment` and the track helpers (§12.4), `pg_shift_aspiration` | **byte-exact** |
| `src/paramgen/pg_nodes.c` | node list (`node_insert/delete/next/prev`), `stage_commit`, `stage_window_update` `D3B66`, `stage_run_command` `D9D2B`, `stage_playback_run` `D40F4` (§15.1, §15.5) | **byte-exact** |
| `src/paramgen/pg_mem.c` | the data-segment image, `fatal_error` | — |
| `src/prosody/pr_stage.c` | `prosody_run` `D9274`, `prosody_scan_phrase`, `prosody_walk`, `prosody_time_segment`, `prosody_reset`, context helpers (§15.2) | **byte-exact** |
| `src/prosody/pr_phrase.c` | `phrase_breaks`, `insert_phrase_break`, `mark_break_neighbour` | **byte-exact** |
| `src/prosody/pr_duration.c` | `segment_prosody`, `accent_context`, `duration_rules`, `klatt_duration`, `insert_pause`, `merge_geminate`, `cluster_duration` | **byte-exact** |
| `src/prosody/pr_f0.c` | `f0_target` | **byte-exact** |
| `src/lexical/lx_stage.c` | `stage_lexical_run` `E4A81`, `take_word`, `finish_span`, `take_span`, `scan_ahead`, `word_stress_marks`, `mark_stressed_syllable`, `function_word_rules`, `word_boundary` (§15.3) | **byte-exact** |
| `src/lexical/lx_lexicon.c` | `lex_lookup` `D738A`, `lex_index_word`, `lex_read_byte`, `lex_lookup_with_affixes`, `affix_match`, `stem_respell_and_lookup` | **byte-exact** |
| `src/lexical/lx_lts.c` | `lts_rules` `D78AE`, `lts_class_match`, `lts_letters_match`, `match_context_pattern`, `vowel_reduce` | **byte-exact** |
| `src/lexical/lx_allophone.c` | `allophone_rules` `E3DBF` | **byte-exact** |
| `src/textrules/tr_stage.c` | `stage_text_rules_run` `D41A4` (the rule interpreter), `text_rules_commit`, `text_rules_emit`, `match_list`, `char_test`, `not_stage_command` (§15.4) | **byte-exact** |
| `src/textrules/tr_phoneme.c` | `phoneme_spelling` `D4DB0` (phoneme names; reached with `ESC[6A` and `[ ]`) | **byte-exact** |
| `src/input/in_stage.c` | `stage_input_run` `DE09D`, `input_getc`, `input_ring_free`, `node_append`, `irq_disable/enable_nested`, latch and DIP helpers (§15.5) | **byte-exact** |
| `src/input/in_host.c` | `host_send` `DE4C6`, `output_ring_free`, 8251 command helpers, the transmitter `tx_send` `D734B` / `tx_next_byte` `DE3AE` | **byte-exact** |
| `src/input/in_escape.c` | `host_rx_char` `D713F`, `host_escape_parser` `D658C`, `host_status_report` `D6D8C`, `input_put` `DE345` (§8.1-8.2) | **byte-exact** |
| `src/input/in_boot.c` | `boot_reset` `D3100` (`power_up`), `ram_test`, `self_test`, `rom_checksum`, `pic_init`, `latch_init`, `uart_init`, `dsp_boot` (§2) | **RAM-exact** at the loop's first idle (`boot_check`) |
| `src/input/in_dsp.c` | `dsp_irq_service` `D615F`, `dsp_frame_tick` `D643C`, `phoneme_echo_drain` `D64E1`, `dsp_build_frame` `D89CB` (the ring side; calls `frame_build`), `dsp_frame_handshake` `D8949`, `dsp_write_frame` `D3261` (§11.1, §8.3) | **byte-exact** (all of RAM) |
| `src/input/in_reset.c` | `loop_reset` `E38FF`, `quit_reset` `E3CE7`, `loop_entry` `E37F0`, `node_list_init` `D3900` and the stage resets (§15.6) | **byte-exact** (`loop_entry`: frames only) |
| `src/common/prose_rom.c` | the ROM image built from `src/data` (`prose_rom_builtin`), `DS:x` table access | — |
| `src/data/prose_data.c` | generated by `tools/rom_extract.py`: lexicon segment, data segment, DSP data ROM, ROM lane sums | equal to the dumps |
| `src/v1/` (`-DPROSE_VERSION=1`) | **Prose v1.1** (§16): the memory model (`v1.h`, DS F766), board helpers, host link (`v1_host.c`: input and output rings, `host_send` `F5765`, the transmitter), receive side and escape parser (`v1_escape.c`: `host_rx_char` `EE1BB`, `host_escape_parser` `EDB32`). Data from `src/data/prose_v1_data.c` | **byte-exact** (`v1_replay`: 2,448 calls; parser and receive side 88 % covered) |
| `src/common/prose_wave.c` | not firmware: DAC word → PCM as (code − 0x7F0) × 15. The signal rests at 0x7F0 (silent frames and the mean of speech; centring on the 0x800 bias instead leaves a DC offset of about −300). A 4-bit shift (× 16) about 0x7F0 would overflow at codes 0xFF0-0xFFF, so the gain stays 15. Speech itself spans only about codes 0x310-0xC97 (peaks −4 to −7 dBFS): the synthesizer's own level, not the conversion. Playback through winmm `waveOut`, WAV writer | — |

**How the stages are written.** The generator's rule tables address its state by DS offset (an action is `{op,
voice mask, value, address}`), so the C code works on an image of the 64 KB data segment: ROM below `DS:BF00`, the
12 KB SRAM above. Accessors `rw/ww/rb/wb` do 16-bit loads and stores (`rw` is signed: compare addresses with `ruw`);
`pg.h`, `prosody.h`, `lexical.h` and `textrules.h` name the variables. The other stages use the same image; the
lexicon segment `E900` is read straight from the ROM image (`lexical_load_rom`). `paramgen_run`, `prosody_run` and
`stage_lexical_run` call `stage_window_update` themselves; `paramgen_step` / `prosody_step` / `lexical_step` are
everything after it.

**Tests** (captures come from scratch harnesses built on `native/`; they are not committed). Each takes `play` to hear
the C output through the sound card (Windows), e.g. `dsp_replay CAPTURE synth play`.
- `dsp_replay CAPTURE [synth|rec] [play]`: DSP model vs emulator samples.
- `frame_replay FRAMES.txt`: frame builder vs captured builds.
- `chain_replay FRAMES.txt CAPTURE [play]`: track bytes → `frame_build` → `prose_synth` vs emulator audio.
  **0 differing samples** on four runs (voices 0-2, `a`, `p`, `s`, `l`/`g`).
- `stage_replay CAPTURE.bin [FUNC]` (was `pg_replay`): the harness snapshots all 12 KB of RAM on entry to and
  return from each function of a stage (interrupts held off inside the stage's step), and the test runs the C
  function on the entry image and compares all of RAM except the stack. **Generator: 14 captures, 11,433 calls;
  prosody: 22 captures, 50,999 calls; lexical: 18 captures, 115,686 calls; text rules: 22 captures, 12,427 calls; input and
  `host_send`: 7 captures, 5,951 calls; playback: 173 calls; receive side and parser: 6 captures, 2,640
  `host_rx_char` calls (every command letter with 15 parameter forms, errors, control characters); loop resets:
  4 captures; frame interrupt: 3 captures, 2,468 `dsp_frame_tick` calls (phoneme echo, `H`/`C` hold); in all
  233,571 calls, 0 differ.** Mutation tests (two changed rules per stage) fail as they should. (The harness closes records in a loop: the lexical step ends at `E4AFA`, which is
  also `stage_commit`'s return address.) Generator inputs: plain sentences, voices 0-2, `a`, `p`, `v`,
  word mode, phoneme input covering all 57 phoneme codes, `l`/`g` and `s` holds, `ESC[16N`, `ESC[x`/`ESC[C` breaks,
  20 s of continuous speech. Prosody inputs: questions, commas and semicolons, speeds 0-25, voices, pitch 0 and 120,
  word mode, `N12`/`N14`, `A5`, `f`, `l`/`g`, `s`, `i`, `a`, a 38-word sentence, numbers and abbreviations.
  Lexical inputs: dictionary words, stress-only entries, affixed and respelled stems, made-up and foreign words,
  contractions, acronyms, numbers, dates, money, hyphens and quotes, speeds 1-25, `ESC[2f`, pitch 0, word mode,
  monotone, phoneme input with stress digits and `Q`, r-coloured vowels, the U glide, H after vowels.
  Text-rules inputs: numbers (ordinals, decimals, fractions, money, times, phone numbers, millions), abbreviations
  and titles, `Dr.`/`St.`/`ft.` both ways, states, compass points, symbols and punctuation, mode flags `N1-8`,
  `F7/8/13`, `A2-4`, word mode, commands inside numbers and after abbreviations, phoneme input with `!p;d!` and
  `/d;p/`, and (harness forcing `ESC[1I` to store 2) every two-letter phoneme name and mark.
- `boot_check IDLE.ram`: runs the C power-up (`power_up`: boot, self-test, hardware set-up, `loop_entry`, then
  the transmitter sends `ESC[0R` XON). It compares all of RAM except the stack with the emulator's RAM at the loop's
  first idle. **0 bytes differ.** Before comparing, it runs `dsp_irq_service` once without a frame request, as the
  emulated DSP interrupts once after its reset (`[DB76]` = 1).
- `pipeline_play [CAPTURE.bin] [-s TEXT | -t] [-w OUT.wav] [-f FRAMES.txt] [-q]`: runs **the whole pipeline in
  C** (input, text rules, lexical, prosody, generator, playback, frame interrupt and builder, DSP), scheduled as the
  synthesis loop does, then plays it and prints what the firmware sent to the host. Each frame request of the DSP model
  runs `dsp_irq_service` once (`dsp_status` = USF0), which sends the frame built at the previous request. **`-s TEXT` speaks a string, escapes included**
  (`\e` or ESC): **without a capture it starts from the C power-up**, otherwise from the first record of an input-stage
  capture (the idle firmware). The program is the host, sending each byte to `host_rx_char` and pausing while the firmware has sent XOFF. The loop's resets (§15.6)
  run as in the firmware; the idle routine hands back to the loop under the firmware's conditions (`E3B9D`), which
  matters for when an end of sentence resets. `-t` starts from a text-rules capture at the first step with the
  receive ring empty; the default starts from a prosody capture. All frame-exact against the emulator:
  **`-s` 15 texts** (11,689 frames, up to 635 characters and 42.8 s; voices, pitch, rate, amplitude, markers, `ESC[x` replies and
  the reset after them, `A6` brackets, `N`/`F`, `Q`, `l`/`g`/`t`, `K`, `v`/`f`/`r` including `ESC[0r`),
  **`-t` 8 inputs** (3,346 frames), **prosody start 8 inputs** (4,609 frames). Not comparable: commands that act on
  arrival (`w`, `S`, `q`, `W`, `H` in mid-text) and a stop at an index marker while later text is still being
  processed, where the firmware's result depends on the CPU time its stages take (it acts a frame or more later). The
  frame comparison takes the last trace line of each frame counter (the counter repeats while the DSP waits, and
  jumps at a reset or at the generator's 0x400 wrap). The emulator host must honour XOFF, or the firmware's receive
  ring overflows and text is lost.
- **Coverage:** 90% of the generator's 9,574 instructions, 79% of the prosody stage's 4,849 and 89% of the lexical
  stage's 4,300, 96% of the text-rules stage's 2,297 and 82% of the input stage and `host_send` (516) ran in these
  captures. Input code not reached: the self-test text (DIP S4-7), the 3-value command form, the DIP S4-5/6 handshake,
  a full output ring. Receive side and parser: 94% of the parser and `Q` report (707 instructions), 67% of
  `host_rx_char` (170: the 8251 error bits, the DSR handshake, `^R` and NUL were not sent). Lexical code not reached: the word types and `~`/`` ` `` marks that the
  text rules set (`word_boundary`, the homograph skip), affix codes 2-7, the T-before-C stem repair, some allophone
  contexts.
  Not reached: test mode `t` in `paramgen_hold`, the unused rule ops, `fatal_error` paths, durations and F0 given with
  phoneme input (`!p;d!`, §15.4; since verified frame-exact from text), some contours and rare phoneme contexts. That code is decompiled but untested.

**Power-up** (2026-09-25): `pipeline_play -s TEXT` needs no capture. From the C power-up, 13 texts are
frame-exact against the emulator, including the first idle frame after boot, and their replies match byte for byte.
Not tested: the RAM and ROM failure paths and the DIP S4-8 factory loops.

**Frame interrupt and phoneme echo** (2026-09-25): all firmware code on the path from host bytes to DSP frames and
back to host bytes is now C. With `ESC[16N`, 11 of 12 texts give the emulator's host output byte for byte (phonemes,
marks, punctuation, markers, replies); the twelfth has `ESC[w` in mid-text (a timing race, §8.3). The frame lists
are one line shorter than the emulator trace, which also logs the last `dsp_build_frame` call that finds the ring
empty. Audio cannot be compared sample for sample from `-s`: the DSP's noise generator runs from its reset, and the
emulator run speaks after a 3.75 s pre-roll (`chain_replay` covers the DSP).

**Next:** a library API for the DLL (speak, settings, index markers, cancel/pause, callbacks for index, done,
phoneme and audio), then split the extracted data into named tables as their structures are documented. Later: capture the lexical and prosody stages from text that
reaches their untested paths (word types, `!p;d!` values).

## 15. Synthesis loop and the pipeline stages (decompiled 2026-09-24/25; Prose 2000 program)

### 15.1 Synthesis loop and stages [verified: code; stage roles tagged]
`synthesis_loop` `E39A7` runs five stages over one doubly linked node list. Each stage has a pending flag and runs
only when enough nodes are free (`[C204]`; about 610 are free when the list is empty [verified: captures]). **Later
stages go first**, so the list drains before new text is taken in: the first stage whose flag is set runs, and if it
lacks free nodes the loop idles rather than trying an earlier stage [verified: code]. A stage that returns nonzero
sets the next stage's flag; a stage that returns 0 clears its own.

| Order | Entry | Record | Pending flag | Runs when free > | Role |
|---|---|---|---|---|---|
| 1 | `stage_input_run` `DE09D` | — | `[EE5E]` = idle (runs while 0) | `0x0F` | host text into nodes, one word per call: each character from the receive ring `DS:EC04` becomes a kind 1 node, an escape a command node (§15.5) |
| 2 | `stage_text_rules_run` `D41A4` | `C206` | `[EE60]` | `0x25` | text normalization: a byte-code interpreter over a rule program at `DS:146E` (numbers, abbreviations, symbols, phoneme input; §15.4). Hands the lexical stage **one word at a time** [verified: node list at each lexical step] |
| 3 | `stage_lexical_run` `E4A81` | `C228` | `[EE62]` | `0x25` | words → phoneme symbols: lexicon, affixes, letter-to-sound rules, stress, allophone rules (§15.3) |
| 4 | `prosody_run` `D9274` | `C24A` | `[EE64]` | `0x69` | durations and F0 (§15.2) |
| 5 | `paramgen_run` `DCB00` | `C26C` | `[EE66]`; `[EE68]` = wait for ring room | — | parameter tracks (§12) |
| 6 | `stage_playback_run` `D40F4` | `C28E` | `[EE6A]` ≠ 2 | — | runs at the top of each pass and after each generator call: reports `i` markers and `x`, frees the segments that have been played (§15.5) |

When nothing can run, the idle routine `E3B9D` sets `[EE66]` again once the ring is nearly empty (`RING_LOW −
RING_LAG − RING_READ ≤ 8`), and clears `[EE68]` once there is room.

**Stage record** (`0x22` bytes): `+0` first node of the stage's window, `+2/+4/+6` the stage's own cursors (prosody:
phrase start, node being timed, scan position), `+8` last node of the window, then **the host settings as they stand
at the stage's position in the text**: `+0E` I, `+10` P (low byte; high byte a second parameter), `+12` speed (v/r),
`+14` pitch, `+16` a, `+18` f, `+1A` N/F flags, `+1C` A/D flags, `+1E` accepted node kinds, `+20` V.
`stage_commit` `D36FB` hands the finished part of the window to the next record.

**Node kinds** (flags bits 0-2): 0 in-band **command** (char = command letter, `+6`/`+8` = its values), 1 **input text** (a host
character, before the text-rules stage; §15.4), 2 **text**
(a letter, or the word record that heads a word, before the lexical stage; §15.3), 3 **symbol** (a phoneme before prosody, word boundaries `%` `&` with the word's class in `+8`, punctuation and break
marks `, . ? \ ] )`), 4 **timed segment** (a phoneme after prosody, or a pause `' '`), 5 **hold** (the `g`, `s` and
`t` commands, converted by prosody), 6 free. Kind 7 turns up at the end of the text-rules window **[unknown]**.
(Before 2026-09-24 the C code called kinds 0 and 4 "phoneme" and "pause"; renamed.)

- `stage_window_update` `D3B66`: makes a stage current (`[C202]`) and moves its cursors past node kinds it does not
  accept (mask `+1E` against the per-kind bits at `DS:0020`), running the command nodes it passes.
- `stage_run_command` `D9D2B`: applies a command node to the current record's settings: `I P V a f r v p N A`, and
  `l` (writes `DS:EB02[n]`). Stage-specific: `C` at the text-rules stage inserts a `]` symbol; `x` there inserts a
  `C` command; `g s t` at prosody become holds (kind 5); `i` and `x` at the last record set `[EE72]`/`[EE6E]` and
  report the marker (`host_send`, when mode flag 11 is set). An unknown letter is `fatal_error(1)`.

### 15.2 Prosody stage `D9274` [verified: decompiled, byte-exact on 51k captured calls (§14)]
The stage just above the parameter generator. It turns the symbols into timed segments: node `+6` = duration in
frames (≤ 55), `+8` = F0/2, kind 4. On the way it splits sentences into phrases, inserts pauses and merges geminates.

```
prosody_run D9274 → stage_window_update(C24A)
   prosody_scan_phrase D9492   scan ahead to '.', '?', a C command or the length limit (180 phonemes; 70 for the
                               first phrase, 5 with mode flag 14); count phonemes; note the word boundaries
                               (DS:DCA2, ≤ 50) and where words of classes 6, 7, 8 and 17 fall → contour type [DC82]
     └─ phrase_breaks DA101    split long runs of words (≥ 14 / ≥ 25) from the middle out at function-word
                               boundaries (',' or '\' symbols, insert_phrase_break DA770), open the phrase with a
                               P command node (+6 = 1 '.', 2 other, 3 ',', 4 '?'; +8 = 0 for a sentence's first
                               phrase); at slow speeds (< 9) also ')' breaks, thinned by DS:60BA[speed]
   prosody_walk D9336          step PR_CUR through the phrase: 2 nodes per call before checking ring room;
                               a comma hands the phrase so far on
   prosody_time_segment D9A20  per symbol: given_values D9BE6 (durations/F0 given with phoneme input),
                               context_load D9F9A, segment_prosody DC1EA, pauses (insert_pause DC332)
      segment_prosody DC1EA:  accent_context DBA35 → f0_target DBC84 → duration_rules DA9DC → klatt_duration DB4A6
                               → merge_geminate DA8AB → cluster_duration DC57B
```
- **Durations are Klatt's rules** (MITalk 1987 ch. 9) **[verified: code structure]**: inherent and minimum durations
  per phoneme (tables `DS:[ADE4]`, `DS:[AE46]`, in 10 ms), a list of percentages multiplied pairwise on 16 bits,
  then `DUR = MIN + (INH − MIN)·PRCNT/100`, scaled by the speed table `DS:6086`. `duration_rules` holds the phrase-
  final, vowel, unstressed and consonant-context rules; `klatt_duration` the consonant-cluster ones and the formula.
  Unstressed segments halve the minimum.
- **Pauses:** punctuation `.`/`?` 39, `,` 10, `]` 12, `\` 4, other 8 units, scaled by `DS:6052[speed]/4`, split into
  ≤ 50-frame pause nodes (a short sentence pause becomes 1/3 + 2/3). Word mode and fast start use fixed short pauses.
- **F0** (`f0_target`): a line from `pitch + fx` down to `pitch` across the phrase (`fx` = pitch/3 × a Q15 voice
  factor `DS:5378[V]`), bent at the marked words by contour type (0 straight, 6/7/8/15/16 with rise-fall points).
  Stressed syllables add accents; the accented syllable of the phrase gets flags bit 7 (on input the same bit marks
  durations/F0 given with a phoneme as relative).
  The phrase end follows the P kind: 1 falls, 3 rises slightly, 4/5 (question) jumps to 2·pitch − pitch/8.
  Clamped to 50-240 Hz.
- **Settings seen here** [verified: code]: mode flag 12 (`ESC[12N`) = monotone (F0 = pitch); flag 14 = fast start
  (5-phoneme phrases, short pauses); A-flag 5 (`ESC[5A`) = every phoneme at 85 % of its minimum duration, unstressed
  halved; pitch 0 = F0 0. Above speed 19, a schwa `x` after `T`/`D` becomes the flap `D`.
- **Firmware quirks** [verified: code]: the second phrase-break rule compares a word's class with 37 (`'%'`) and can
  never fire; one duration rule compares the char with `0x5820` and never fires; `mark_break_neighbour` tests the
  *pitch* setting (`[C25E]`) where a flag seems meant.
- **Checked end to end:** from a RAM snapshot taken when the sentence has reached this stage, the C prosody,
  generator, frame builder and DSP produce the emulator's frames exactly (6 inputs, 2,616 frames; §14). The replay test covers 51k calls.

### 15.3 Lexical stage `E4A81` [verified: decompiled, byte-exact on 116k captured calls (§14)]
Turns the words from the text-rules stage into phoneme symbols with stress, then applies the allophone rules.
4001 addresses in brackets (its stage records sit 2 bytes higher: `C22A` for the 2000's `C228`).

```
stage_lexical_run E4A81 [E41AE] → stage_window_update(C228)
   scan_ahead E4CAB [E43CC]        next accepted node; gives up after 12 nodes or at a C command
   take_word E4D16 [E4437]         the next word: its word record and letters, or a run of phoneme input
     lex_lookup_with_affixes E4F57 [E4669] → lex_lookup D738A [D7140], affix_match E5152 [E4854],
                                     stem_respell_and_lookup E5264 [E4951]
     lts_rules D78AE [D7634]       the letters that are left, and the stress of stress-only entries
       → lts_class_match D80F3 [D7E57], lts_letters_match D8165 [D7EC9], match_context_pattern D81AB [D7F0F],
         vowel_reduce D8551 [D82B1]
     word_stress_marks E4E90 [E45AD] → mark_stressed_syllable E546F [E4B43], word_boundary E5736 [E4DFF]
   finish_span E4AFB [E4228]       the previous word, now that the word after it is converted:
                                     ESC[f, function_word_rules E5542 [E4C16], allophone_rules E3DBF [E352E]
   take_span E4C8D [E43AE]         the word just taken becomes the span
```
The stage works **one word behind**: a word is finished only when the next one has been converted, because the
allophone rules look across the boundary. The same structure is TruVoice's stage 1 ([tvd] `Stage1_ScanAhead`,
`Stage1_TakeSpan`).

**Words** [verified: code]. A word arrives as a **word record**, a text node (kind 2) whose `+6` low byte is a word
type set by the text rules, followed by its letters (text nodes) up to a non-letter or `&`. The record becomes the
word's boundary symbol: `&` for a content word, `%` for a function word (set when the whole word is found with a
lexicon class other than 0, 6 or 15, or by a class-8 affix), with the word class (§9.6) in `+8`. A `~` before the
word gives it emphatic stress (level 3), a `` ` `` secondary (level 1). The word types come from the text rules and
were not reached in the captures: 1, 2 and 5 lower the stress, 9 raises it; with record bit 5 set, type 4 takes the
second homograph (`lex_lookup`) and `word_boundary` rewrites the record by type (1 `$`, 2 `%` class 1, 3 `&`, 5 a
pause, 11 inserts a pause). A run of phoneme input (symbols) is taken as it is; its stress digits `1` `2` `"`
(after the vowel) become stress levels 2, 1 and 3.

**Node flags** used here: bits 3-4 stress (3 emphatic, 2 primary, 1 secondary); bit 5 on a phoneme = in a stressed
syllable (`mark_stressed_syllable`: the vowel and its onset, up to C C and an S before them; content words only);
bit 6 = glottal onset (from `allophone_rules`); bit 7 on a word boundary = a flap or glottal stop across it.

**Lexicon and affixes** (`lex_lookup_with_affixes`) [verified: code]:
- The whole word is looked up first (`lex_lookup`, §9.6). Found: the letters become the entry's phonemes. The
  primary stress goes where the entry's `1` puts it, else on the first full vowel (the first reduced vowel if there
  is no full one; a `0` code keeps the next vowel from it).
- Otherwise suffixes are stripped from the right, keeping a stem of at least three letters with a vowel. The lists
  are by last letter at `DS:A696`, then prefixes by first letter at `DS:AD50`.
  - **Affix record** (`affix_match`): `+0` string (suffixes stored reversed), `+2` context pattern next to it, `+4`
    1 = look the stem up now, `+5` word class (6 verb, 15 -ly adverb, 8 negative → `%`), `+6` code (0/1 = the
    word's stress; 2-7 → `[EEA2]`, which gives the letter-to-sound rules a class bit `0x80`-`0x1000`), `+8` the
    list to try next, so suffixes chain.
  - A `[` text node is inserted at each split.
- **Stem repair** (`stem_respell_and_lookup`): undouble a final consonant (STOPP-), add a silent E (for six suffix
  records, with context patterns `DS:9C65`/`9C97`/`9D0B`), IE or I → Y (CARRI-), add a T before a C- suffix
  (pattern `9CA0`).
- Returns 1 only for a whole-word hit. After a stem hit, the stem is phonemes and the affixes are still letters,
  so `take_word` always runs `lts_rules` then, and also for stress-only entries.

**Letter-to-sound rules** (`lts_rules`) [verified: code; phonetic roles inferred]:
- **Order.** The letters are converted **right to left**: the stem up to `[C234]`, then (if a suffix was split off)
  the suffix from the word end.
- **Rules.** For each letter the rule list `DS:52C0[letter − '@']` is tried in order. A 10-byte rule is:
  - `+0` letters to the left it also consumes;
  - `+2` its output (reversed), optionally led by control bytes: `0x10` drops the `[` before a stem the lexicon
    found; a byte `< 0x10` adds syllables to the stress count;
  - `+4` a context pattern, matched leftwards from the leftmost consumed letter;
  - `+6` a condition on two words of bits passed on by the rule to its right (`[DBC4]`/`[DBC2]`: all set, or with
    bit 15 none / any);
  - `+8` the two words it passes on.
- **Context patterns** (`match_context_pattern`, also used by the affixes): a list of elements (header byte +
  0-terminated tail), ended by an empty one. The header's bits 7-5 select:
  - `010`/`011`: a vowel letter (or none) before the syllable boundary;
  - `101`/`110`: exactly one vowel to the left (or not);
  - `001`: more than one consonant since the last vowel;
  - `100`: a doubled consonant letter;
  - otherwise letters and classes (bytes with bit 7; the class table is at `DS:0048`) on the next 1 node, or 2-5
    nodes. Bit 2 inverts the test, bit 3 negates the element, and bits 2-4 all set stop at the first hit.
- **Stress** when no lexicon entry gave it: the syllables are counted from the right. The third from the end gets
  the primary stress, or the second if the last-but-one syllable is closed by two consonants (a Latin-style
  antepenult rule). Shorter words stress their first vowel. Secondary stresses alternate leftwards from it. A
  stress-only entry gives the pattern instead: 2 bits per syllable from the right, 4 syllables at most.
- **`vowel_reduce`** runs on each vowel once the letters to its left are converted:
  - `U` (/ju/) becomes `Y` + `b`, or `b` alone after some consonants, which may palatalize (D T Z S → J C z s).
  - An unstressed vowel in an open syllable reduces to `@` or `|` (`3` for the r-coloured ones), except
    in a few contexts.

**Allophone rules** (`allophone_rules`, on each phoneme of the finished word) [verified: code; phonetic roles
inferred from the alphabet, §12.3a]. They run on text input, and on phoneme input only with `ESC[9N` (mode flag 9).
They are skipped for word type 13. Many are speed-gated (from speed 7, 10, 14, 19 or 20 up, or below 13 or 19):
- T and D become the flap `t` or the glottalized `q` between vowels, or are dropped in clusters.
- T P K after S become `D B G`.
- T and D before R become `C` and `J`, and `C` and `J` get their release `s` and `z`.
- H is dropped, or voiced (`d`) after a vowel; `h` (wh) becomes `W`.
- A post-vocalic L becomes `j`, and N before a velar across a boundary becomes `~`.
- Z devoices to S before a voiceless sound; so does the V of "of" and "have" (word classes 2, 3) across a word
  boundary.
- A release vocoid `p` is inserted after some final consonants.
- Word-initial vowels after a pause get a glottal attack.
- Reduced vowel + L or N becomes syllabic `l` or `n`; vowel + R becomes an r-coloured vowel (`3 4 k r g c`).
- In word mode (`P` 0) the main flapping rules are off. The glottal rules test the stage's pitch setting (`+14`)
  as if it were a switch, so with pitch 0 they are off (the same oddity as §15.2).

**Word-level rules** [verified: code]:
- **`ESC[f`** (`finish_span`): with *f* > 0 only every *f*-th content word (class 0, 6 or 15) is spoken. All other
  words, function words included, are **deleted**.
- **`function_word_rules`** on a `%` word:
  - classes 1-3, 5, 9, 10, 12-14 lose one stress level (primary → secondary → none), or all stress from speed 9;
  - above speed 6 the vowels of class 3 words ("a", "of") become schwa, and so does the vowel of "the" before a
    consonant;
  - from speed 22 also "to" and "for" (their second sound) and "and" (→ /ən/, the D dropped).

**Firmware quirks** [verified: code]:
- `vowel_reduce` means to skip a `[` before the vowel but reads the same node again.
- `allophone_rules` compares a local that is only set on some paths (stack garbage otherwise; the C uses 0) in the
  r-colouring rule.
- In `match_context_pattern`, a letter/class element that runs to the end of its tail without deciding the match
  leaves the pointer past its terminator, so the element after it is skipped. Whether the tables rely on this is
  unknown.

### 15.4 Text-rules stage `D41A4` [verified: decompiled, byte-exact on 12k captured calls (§14)]
Normalizes the host text for the lexical stage. The input stage leaves each host character as a **kind 1 node**
(input text) [verified: `match_list` matches only kind 1]; this stage turns them into words (kind 2 text nodes),
symbols (kind 3), or phoneme spellings it inserts itself. 4001 addresses in brackets.

```
stage_text_rules_run D41A4 [D4169] → stage_window_update(C206), then the interpreter
   text_rules_commit D4B18 [D4AB0]   hand on the nodes before TR_START (or only leading commands and ']')
   text_rules_emit   D4B81 [D4B19]   insert a node before TR_START; phonemes take the marks waiting at DS:DB04
   match_list        D4C3D [D4BD3]   is TR_MARK..TR_CUR one of a list's patterns? (TR_REPL = its replacement)
   char_test         D4CDB [D4C6E]   types 4 letter or ', 5 digit, 6 lower, 7 upper, 14 control, 19 '.'
   not_stage_command D4D76 [D4D09]   0 for the commands this stage acts on: C F I N x
   phoneme_spelling  D4DB0 [D4D43]   two-letter phoneme names → one-letter alphabet (below)
```

**Stage record `C206`** [verified: code]: `+2` hand-on point, `+4` `TR_START` (first node of the text being worked
on), `+6` `TR_CUR` (the node the rules look at), `+8` last node, `+0A` `TR_MARK` (where `match_list` starts),
`+0C` scratch cursor. The stage returns 1 (so the lexical stage runs) whenever it hands a word on.

**The interpreter** [verified: code]. The rule program is at `DS:146E` up to the string table at `DS:2053`
(about 3 KB). Its state is kept in globals, so each call runs on from where the last one stopped: `[DB72]` program
counter, `[DB70]` 1 = at a condition, `[DB6B]`/`[DB6A]` bytes left in the current action list / bytes to skip after
it, a call stack at `[DB18]` (4-byte entries `DB1A`-`DB69`, overflow `fatal_error(0xC)`, underflow `0x18`), byte
registers at `DS:DB0E`. A **rule** is a condition byte with its operands, a byte whose high nibble is the length
of the "then" actions and low nibble the length of the "else" actions, then the actions. When the list that ran
is done, the program goes on with the next rule.
- **Conditions** (0-18): phoneme input on; word mode; mode flag *n* (0-15: `ESC[n+1 N`, 16-22: `ESC[n−15 A`); the
  current node is a letter, `1`-`4` or a word-ending character (`DS:145E`); character types (via `char_test`); the
  next node of a type, skipping blanks and commands the stage ignores (20 nodes at most); register == or > value;
  current character == value; the text since `TR_MARK` is in a list (folded to lower case, or exact); step to the
  next node / the node is a command; always; current character in a string of `DS:2053`. A condition that needs a
  node the window does not have yet stops the stage (`text_rules_commit(0)`) and is tried again on the next call.
- **Actions** (0-22): insert a string as text or symbols; move on after the current node; turn `TR_START..TR_CUR`
  into a word (text nodes, letters upper case, `'` → `@`; register 2 also sets bit 6 on the window's first node)
  or into symbols; insert the replacement `match_list` found; set / increment / decrement a register; move the
  cursor forward or back, restart at `TR_START`; delete the current node or the whole span; insert a text node;
  replace a character (op 15, unused by the program); run `match_list` for its replacement only; **yield** (op 17:
  return to the synthesis loop once, then carry on); run the command node at the cursor (`stage_run_command`);
  return; read the numbers after a phoneme (ops 20/21, below); set `TR_MARK`. A byte `10xxxxxx yy` is a goto and
  `11xxxxxx yy` a call to `DS:146E + xxxxxxyy`.
- The program yields after each finished word unless word mode is on, which is why the lexical stage gets **one
  word at a time** (§15.1).

**What the program does** [verified: program disassembly (scratch `rules.py`), roles inferred]. The top-level loop
runs command nodes, then dispatches on the current character: phoneme input, a letter (words), a space, a digit
(numbers), and the punctuation and symbol lists. `match_list` has 21 lists (`DS:1434`); each entry is a pattern and
the phoneme spelling that replaces it (starting with the `&` word boundary, so these words bypass the lexicon):
digits, teens and tens, `thousand`/`million`/`billion`/`trillion`, ordinals (`1st`, `2nd`, …), symbols (`#` `%` `&` `*`
`+` `<` `=` `>` …), punctuation names (used with `ESC[2N`, speak punctuation), about 90 abbreviations, US state
codes, compass points, titles (`Mr.`, `Mrs.`, `Gov.` …), `i.e.`/`e.g.`, and three ambiguous ones decided by
context: `Dr.` (Doctor / Drive), `St.` (Saint / Street), `ft.` (Fort / feet). Mode flags 1-8 and 13 and A-flags 2-4
select variants.

**Phoneme input** (`ESC[1I`) [verified: code and captures]: letters, stress digits and the phoneme alphabet pass as
symbols. A phoneme may be followed by `!p;d!` or `/p;d/`, **pitch first, then duration** (read by ops 20/21 from
the node two after the phoneme) [verified 2026-09-25: emulator, and the C pipeline frame-exact against it]:
- `/p;d/` **absolute**: *p* = F0 in Hz, 50-200 (stored as *p*/2 in the node's `+8`; `0` gives F0 0, unvoiced); *d* =
  duration in 10 ms frames, 1-60 (`+6`). `Ha/180;40/1` gives the vowel 40 frames at F0 180.
- `!p;d!` **relative** to the rule values: *p* 0-300 adds *p* − 100 Hz to F0 (result clamped to 50-200 Hz); *d* 40-160
  adds *d* − 100 frames to the duration (result clamped to 2-60). `Ha!160;130!1` gives +60 Hz and +30 frames.
- Either number may be left out (`/180/`, `/;40/`); a value out of range makes the whole mark do nothing. The mark
  must come **straight after the phoneme, before its stress digit** (`a/180;40/1`; `a1/180;40/` has no effect).
  Only phonemes (feature bit `0x80`) take it. The prosody stage's `given_values` applies it (§15.2).

**`phoneme_spelling`** runs with input mode 2. The escape parser refuses `ESC[2I` (`I` > 1, `D6931`), but **the input
stage produces it**: with `ESC[6A`, `[` becomes an `I` command with value 2 while A-flag 7 is on (the default), and
`]` ends it (§15.5) [verified: stock firmware, `ESC[6A[HHEH1LOW …]`]. It converts two-letter names in
either case (`AA`→`o`, `AE`→`a`, `CH`→`C`, `DH`→`x`, `ER`→`k`, `IY`→`i`, `NG`→`~`, `SH`→`s`, `TH`→`X`, `ZH`→`z`, …;
the second letter's node is deleted), and marks before `ESC[0I` into the pending record at `DS:DB04` (a stress or
timing code in `+6`, a number up to 63 in `+8`), which `text_rules_emit` attaches to the next phoneme
that has feature bit 8. `<` before `ESC[0I` holds speech (`[DBCC]`, as `ESC[H`, §8.2); `>` before `ESC[0I` turns it into a `C` command, which resumes it, and ends the mode.

### 15.5 Input stage `DE09D`, playback stage `D40F4` and `host_send` [verified: decompiled, byte-exact on captured calls (§14)]
4001 addresses in brackets.

```
stage_input_run    DE09D [DDB6A]  up to one word from the receive ring into nodes (10 characters at most)
  input_getc       DE2C5 [DDD87]  next ring character, -1 if none; sends XON when a stopped ring has room again
  input_ring_free  DE2AF [DDD71]
  node_append      D35C4 [D35F1]  a node at the end of the list; starts the text-rules window if it is empty
stage_playback_run D40F4 [D40B9]  the last record (C28E): reports what playback reached, frees played segments
host_send          DE4C6 [DDF4E]  a character or an ESC[ report into the output ring, XON/XOFF
  output_ring_free DE398 [DDE60]
irq_disable_nested / irq_enable_nested D63E5 / D6576: count at [EE74], latch bit 2 (0x04) at [DB82] → 0x3401
```

**Receive ring** [verified: code and captures]: 256 bytes at `DS:EC04`, read index `[ED04]` (the input stage), write
index `[ED06]` (`input_put` `DE345`, called by the escape parser, §8.1, which sends XOFF below 0x50 free bytes and sets `[ED0A]`; the input
stage sends XON at 0x87 free). `[ED08]` holds a character read ahead (-1 = none). The interrupt stores plain text
with CR, LF and TAB as spaces, and **escape sequences already parsed**: `1B`, the command letter, a value count and
the values (one byte each). The flags commands carry the resulting flag word, not the numbers sent: `ESC[6A` is
stored as `1B 'A' 02 61 00` (0x0041 | 0x20), `ESC[3;4N` as `1B 'N' 02 CC 17`. Commands the interrupt handles itself
(`Q`, `E`, `H`, `K`, …) do not reach the ring.

**Input stage** [verified: code]: takes characters until it has passed a space and meets the next non-space (one
word), or 10 characters. A character becomes a kind 1 node; `ESC` becomes a command node whose `+6`/`+8` get the
values (count 1: `+6`; 2: `+6`, `+8`; 3: node flags (stress bits 0-1, bit 2 → flag 0x20, bit 3 → 0x40), then `+6`,
`+8`). It remembers the last `A` and `N` values it passed on (`[EBEA]`/`[EBEC]`). **With A-flag 6 (`ESC[6A`) and
N-flags 1 and 2 off, `[` and `]` become `I` commands**: `]` is `ESC[0I`, `[` is `ESC[1I`, or **`ESC[2I` (phoneme
names, §15.4) when A-flag 7 is on, which is the default**. The loop runs the stage while `[EE5E]` is 0; it sets
`[EE5E]` when the stage returns 0, and the idle routine clears it when the ring holds text (or in self-test).
With DIP S4-7 on, `input_getc` reads the demo text at `[626A]` instead of the ring (the self-test loop, §2).

**Playback stage** [verified: code and captures]: `dsp_build_frame` counts the frames that end a segment (the ring's
mark bit) in `[DD9A]`. The loop runs `stage_playback_run` at the top of each pass while `[EE6A]` ≠ 2, and after each
generator call, and keeps its result in `[EE6A]`. It walks record `C28E`: a command node runs
`stage_run_command` (index markers `i` are reported with `host_send` when N-flag 11 is on; `x` sets `[EE6E]`, the
end-of-sentence reply); a timed segment is passed only while `[DD9A]` > 0 (decremented), and `stage_commit` frees
everything passed. Returns 0 at a segment still playing, 1 at a command that must wait, 2 when done. **This is what
returns nodes to the free list**; without it a long text runs out of nodes.

**`host_send(kind, ch, count, params)`** [verified: code and captures]: output ring of 256 bytes at `DS:ED0C`, write
`[EE0E]`, read `[EE0C]` (the transmit interrupt).
- kind 0: one character; returns 0 if the ring is full. XON / XOFF set or clear `[ED0A]` and RTS (8251 command
  bit 1). **With DIP S4-2 they are queued for the transmitter** (bits 0/1 of `[EE10]`; a queued XON and XOFF cancel
  out); without it no flow characters are sent. (Corrected 2026-09-25: this was read as "counted once both were seen".)
  BEL (7) is sent only with N-flag 10 (it counts in `[EE18]`).
- kind 1: `ESC[` *p1*`;`*p2*… *ch* with up to 16 byte values in decimal (`fatal_error(0x1D)` above), plus CR with
  N-flag 15; needs `4·count + 3` free bytes.
- Then the transmitter is started (`[EE14]`, 8259 mask copy `[DB88]`, 8251 command copy `[DB7C]`) unless it is
  running, output is stopped (`[EE12]`), or with DIP S4-5 and S4-6 on the host holds DSR low.

**Transmitter** [verified: decompiled, byte-exact on 406 captured calls]: `uart_tx_isr` `D631B` calls `tx_send`
`D734B` when the 8251 has room. **`tx_next_byte` `DE3AE`** picks the byte in this order:
1. a CR after an XON, XOFF or BEL just sent (N-flag 15, `[EE1A]`);
2. a queued XON, then XOFF;
3. a BEL from `[EE18]`;
4. the output ring, unless the host sent XOFF (`[EE12]`). A byte ≥ 0x80 reads as negative and ends the sending.

With nothing to send, `tx_stop` `D6387` clears TxEN and masks IR2/3, and `[EE14]` becomes 0. Sending XOFF also
clears the output hold `[DBCC]` (§8.2 `H`). With DIP S4-5 and S4-6 on and DSR low, it stops instead. The firmware
waits for this ring to empty in `loop_entry` and before and after `loop_reset` (§15.6).

### 15.6 Loop resets and the idle routine [verified: decompiled, byte-exact on captured calls; timing in emulation]
`synthesis_loop` `E39A7` first calls `loop_entry` `E37F0` with its argument, then checks two flags at the top of every
pass (`E39B5`):
- `[EE76]` (`ESC[q`): `quit_reset` `E3CE7` deletes every node (`node_list_flush` `D3A97`) and resets the stages.
  The host rings and settings stay.
- `[EE6E]` (`ESC[S`, or playback reaching an `x` or an index marker flagged as the end, §15.5): the loop waits for the
  output ring to empty and calls `loop_reset` `E38FF`. Then it replies `ESC[n x` (*n* = `[EE72]`, the marker) if
  `[EE70]` (`x` received) was set, else `ESC[S`, waits again and sends XON.
- `loop_reset` resets the loop flags (`[EE5C-EE76]`; `[EE6A]` = 2) and the receive side (`D641F`). It rebuilds the
  node pool (`node_list_init` `D3900`: 0x26A nodes of 10 bytes at `C2E0`, free list between sentinels `C2D6`/`C2CC`,
  empty list `C2B4`/`C2BE`, the five stage records refilled from the settings). It also resets the parameter tracks
  (`DC9B9`), the frame sender (`D8918`, `D89B8`), **both host rings and the transmitter** (`host_link_reset` `DE024`;
  the input side is left "XOFF sent", so the input stage sends XON at its next read), the generator (`DD6A0`), prosody
  (`D9900`), lexical (`E3D94`) and text rules (`D4185`).
- `loop_entry(code)` sets the default settings and the 22 `l` values, clears the error counters (if `[DB8C]`), and
  calls `loop_reset`. For code 0x12 (power-up and `^R`) it runs the self-test and hardware set-up (§2) before
  `loop_reset` and replies `ESC[` `[EE78]` `R`; for code 0x77 (`ESC[W`) it replies `ESC[W`. It waits for the output ring to empty, sends XON, and sets the latch. `restart_cold` `D31B5` /
  `restart_warm` `D31DA` reset DS, SS and SP and jump in, so the interrupted code never returns.
- **Idle** (`loop_idle` `E3B9D`): it spins with interrupts on until the loop has work, and returns at once when any
  of these holds:
  - the input stage may run;
  - the generator is pending and not waiting;
  - `[EE6E]` or `[EE76]` is set;
  - playback returned 0 and a segment has been played (`[DD9A]`);
  - playback returned 1 and the output ring has more than 6 bytes free;
  - the frame ring runs low (generator restart).

  While it spins it also wakes the input stage when the receive ring has text, releases the generator's wait, and
  starts the transmitter when needed.
- **Timing** [verified in emulation]: a stop takes effect only when the loop gets back to its top. With the loop idle,
  as after a final `ESC[x`, the reset comes right after the last frame of the sentence. While a stage call is still
  running, as at a stop marker with more text queued, it comes a frame or more later. `ESC[S` sent 25 characters
  into a sentence still let 1.26 s of it be spoken. Everything queued when the reset comes is lost: that is why text sent right after `ESC[x`
  is dropped (§8.2).

## 16. Prose 2000 v1.1 (1983) compared with v3.4.1 (2026-09-25)

**Image.** The ten 2764s of `prose_v1/` merge as five even/odd pairs at `EC000`, `F0000`, `F4000`, `F8000`, `FC000`
(MAME `prose2ko`): 80 KB, `EC000-FFFFF`. Ghidra program **`prose2k_v11_cs.bin`** (`x86:LE:16:Real Mode`): the
64 KB code segment `EC00` (`EC000-FBFFF`) imported at **`0000:0000`**, so a Ghidra address is the IP offset in segment
`EC00` (linear = `EC000` + offset). It has 146 functions, from the near-call targets and the interrupt vectors.
An import of the whole image at base `ec00:0000` does not work: Ghidra re-segments every computed address at or
above `F0000` as `f000:xxxx`, so near calls from code past `F0000` resolve wrongly. The merged file is a scratch
product and is not kept in the repository. Below, v1.1 addresses are linear.

**Layout** [verified: code]:
- The reset jump goes to `EC00:0000`.
- The code is at `EC000-F754F` (46 KB) in **one segment `EC00`**, using near calls only (no `retf`). v3.4.1 has 75 KB of
  code in several far segments.
- **DS = `F766`**: data at `F7660-FFD72` (34 KB). RAM is reached through DS in the same way: SP `8CA0` is linear `0300`,
  so RAM starts at `DS:89A0` (v3.4.1: `F410`, SP `C200`, RAM at `DS:BF00`).
- There is **no lexicon segment**: v3.4.1's 38 KB lexicon at `E9000` has no counterpart. There is a copyright string,
  "Copyright 1982 TSI".

**Same design, different program** [verified: code, byte and instruction comparison]:
- **Shared data:** 17 % of v1.1's 16-byte data chunks occur in v3.4.1. These are the feature table (v3.4.1 `DS:00A8`),
  parts of the text rules and the phoneme-spelled exception words, and the frame builder's filter tables (v3.4.1
  `DS:56CD-5F67`: bandwidth, cosine, parallel corrections).
- **Code:** only 8 % of v1.1's instruction-mnemonic 10-grams occur in v3.4.1. It was recompiled and largely rewritten.
- **Same board and structure:** the same ports, latch bits, 8259 and 8251 set-up, the same fatal codes 0x23/0x24/0x25,
  and the same routines:

  | v1.1 | v3.4.1 |
  |---|---|
  | `boot_reset` `EC000` | `D3100` |
  | `dsp_boot` `EC135` | `D5EE3` |
  | `uart_init` `EC181` | `D6006` |
  | `dsp_irq_service` `EC1E0` | `D615F` |
  | `dsp_write_frame` `EC236` | `D3261` |
  | `uart_rx_isr` `EC2C8` | `D6260` |
  | `uart_tx_isr` `EC341` | `D631B` |
  | `fatal_error` `ED9B6` | `D6400` |
  | `dsp_frame_tick` `EDA11` | `D643C` |
  | `host_escape_parser` `EDB32` | `D658C` |
  | `host_rx_char` `EE1BB` | `D713F` |
  | `dsp_frame_handshake` `EE49E` | `D8949` |
  | `dsp_build_frame` `EE564` | `D89CB` |

  `host_rx_char` has the same 16-byte ring, CR/LF/TAB to space, ^R restart, XON/XOFF and BEL. `dsp_frame_tick` has
  the same hold flags and handshake, but no phoneme echo, and it counts misses per length.
- **Different synthesizer interface:** v1.1 sends a **37-word frame** and a **15-word DSP boot block** (v3.4.1: 40 and 9).
  Its frame builder has the same resonator, nasal-zero and parallel-branch arithmetic (the constants `0x1E11`, `0x3C40`,
  `0xE105`, +0x1E … +0x25, −0x2C, −0xEB), but only **16 parameters**, no voice tables and no jitter or shimmer.
  Its DSP program (MAME `s140025`) was **never dumped**, and v3.12's DSP program expects the 40-word frame.

**Running v1.1** (2026-09-25): `native/prose_board` has `load_cpu_image` (any merged image) and **`enable_dsp_stub(boot,
frame)`**, which stands in for the missing DSP. The stub implements only the host side of the µPD7720: the status
always shows RQM, and USF0 while the stub wants words. After a reset it takes `boot` words (USF0 after the first),
then every 10 ms it raises USF0 and an IR0 request (gated by latch bit 0) and records the `frame` words it gets. v1.1
boots on it (`enable_dsp_stub(15, 37)`) and sends frames that follow the speech [verified]. The reset reply is
**`ESC[1R`**: v1.1's self-test `EC277` always returns 1, and the reply takes its value from the byte at `DS:48C8`
(v1.1 has no ROM checksum). The capture harnesses take `V1_IMAGE=<merged image>`.

**Parameters and a first audio approximation** (2026-09-25):
- v1.1's frame builder `EE564` reads **18 tracks**: p0-p17, with pointers at `DS:9742 + 2·i`, the ring position at
  `[96DE]`, and the mark and 10 Hz bitsets at `DS:96E6` and `DS:A20E`.
- **Same codings as v3.4.1's p0-p17** [verified: the builder's table offsets]:
  - AV and AH are looked up at +0x8C and +0x69 in the dB table (`DS:1E4F`, v3.4.1 `5CCA`).
  - AF gates the parallel branch.
  - The formant and bandwidth indexing is the same, including the F2 offset 0x3F.
  - p17 is F0 in Hz; v1.1 sends it to its DSP as `0x1800 | p17 << 1`.
- **Except FN:** v1.1's nasal zero is at **2·FN Hz** (cosine index FN >> 2), v3.4.1's at 4·FN + 192 Hz. v1.1's
  typical 125 (250 Hz) is v3.4.1's typical 14.
- v1.1 has no p18-p21 (source shape, jitter, shimmer, voice).
- **Rendering** (scratch tools `v1params` and `v1render`): v1.1's p0-p17 are sampled at `EE58F` in the emulator.
  FN is converted to v3.4.1's coding, (2·FN − 192) / 4 floored at 0, and p18-p21 = 17, 8, 0, 0 (v3.4.1 voice 0).
  The frames then go through `frame_build` and `prose_synth`, one per 10 ms. **With the FN conversion the levels
  match v3.4.1's; without it the audio clips.** This replaces v1.1's lost DSP with v3.12's, so it is an approximation. Superseded by the word-level mapping in `v1_map.c` (below).

**Main loop** `synthesis_main` `F7204` is v3.4.1's `synthesis_loop` [verified: code]:

| Role | v1.1 | v3.4.1 | Nodes needed (v1.1 / v3.4.1) |
|---|---|---|---|
| input stage | `F53A0` | `DE09D` | 0x10 / 0x0F |
| text rules | `ECBAC` | `D41A4` | 0x26 / 0x25 |
| lexical | `F5A7B` | `E4A81` | 0x26 / 0x25 |
| prosody | `EEB9C` | `D9274` | 0x15 / 0x69 |
| generator | `F1659` (rule routines under `F21BF`) | `DCB00` | — |
| playback | `ECACC` | `D40F4` | — |
| idle / reset / entry | `F73F0` / `F716E` / `F7083` | `E3B9D` / `E38FF` / `E37F0` | — |

The pending flags are `A55A-A566`, the stop request is `A568`, the end request `A56A` and the free-node count `8CA8`.

**Host link** [verified: decompiled, byte-exact]:
- The receive ring is at `A23E` (16 bytes), the input ring at `A252` (256), the output ring at `A35A` (128).
- `host_send` `F5765` takes its reply parameters as **bytes** (v3.4.1: words).
- The transmitter `EE3A3` counts every byte it sends by kind.
- v1.1 counts most events in 32-bit counters (`A3EA-A4BC`).
- **Commands (18)**, from the letter table at `DS:176F` (letters `C`-`w`):

  | Command | Effect |
  |---|---|
  | `C` | output hold off |
  | `H` | hold now |
  | `S` | stop |
  | `x` | end of sentence |
  | `i` | index marker, 1-255 |
  | `I` | phoneme input, 0-1 |
  | `P` | word mode, 0-1 |
  | `a` | amplitude, 0-15 |
  | `r` | rate, 50-250 (default 160) |
  | `p` | pitch, 0 or 50-200 (default 75) |
  | `s`, `g` | 1-255 (default 10), in-band |
  | `t` | three values: 0-9, 1-255, 30-255 |
  | `l` | entry below 18 of the table at `A57C`, with a maximum per entry at `DS:23B7` and a default at `DS:24A1` |
  | `F` / `N` | clear / set flags 1-14 |
  | `T` | restart; the code must be below 7 |
  | `D` | no-op, no reply |

  Everything else gets BEL. Missing compared with v3.4.1: `V` voice, `v`/`f` speed, `E`, `Q`, `R`, `W`, `w`, `q`, `K`,
  `A`, `b`/`L`, `c`, `X`. The parser's data is at `9596-95BF` (state, index, overflow, 16 values).

**Node list and stage windows** (`src/v1/v1_nodes.c`) [verified: decompiled, byte-exact on captured calls]:
- **Nodes** are 8 bytes: next, prev, a flags word, a byte and the character. The flags word holds the
  kind in bits 0-2 (the same kinds as v3.4.1: 0 command, 1 input text, 2 text, 3 symbol, 4 timed segment, 5 hold,
  6 free, 7 sentinel), flag bits 3-7, and the first command value in its high byte. The second command value is at +6.
- **Pool:** 250 nodes at `DS:8D5E`, with the free count at `8CA8`. The free list has sentinels `8D56` (start) and
  `8D4E` (end), reached through `[8D4C]` and `[8D4A]`. The main list has sentinels `8D42` (start) and `8D3A` (end),
  reached through `[8D38]` and `[8D36]`.
- **Routines:**

  | Routine | Address | Notes |
  |---|---|---|
  | `node_insert` | `EC417` | Takes the first free node and links it after or before a node. Fatal 0x1E when the pool is empty, 0x1F for a null node. |
  | `node_append` | `EC3B4` | The input stage's output: appends at the list end and extends the text-rules window. |
  | `node_prev` | `EC4F1` | Stops at the window's edge. |
  | `node_next` | `EC735` | Stops at the window's edge. |
  | `node_free` | `EC658` | Fatal 0x20 for a null node. |
  | `nodes_reset` | `EC783` | |

- **Stage records** are 0x1C bytes (v3.4.1: 0x22), one per stage after the input stage: text rules `8CAA`, lexical
  `8CC6`, prosody `8CE2`, generator `8CFE`, playback `8D1A`. The current record is `[8CA6]`.
- **Record layout:**
  - +0 is the window's first node, +2 the first unfinished one, +4 the cursor, +6 a look-ahead cursor, +8 the last node.
  - +0E-+18 are the stage's copies of the settings from `I`, `P`, `r`, `p`, `a` and `N`.
  - +1A is a mask of the node kinds the stage works on: text rules 0x17, lexical 2, prosody 0x1C, generator 0x28,
    playback 0x3F. Kind 4 also needs feature bit 0x80 of its phoneme in the table at `DS:0090`.
- **Stage flow:**
  - `stage_begin` `EC9D6` opens a stage's window and runs the command nodes at its cursor through `command_apply` `F12F5`.
  - `stage_commit` `EC512` hands the finished part of the window to the next record; for the playback stage it frees
    the nodes instead.
- **In-band commands** (`F12F5`, a 13-letter table at `DS:237F`):
  - `I`, `P`, `r`, `p`, `a` and `N` update the current stage's record.
  - `l` stores its value at `DS:A1F2 + p0`.
  - `s`, `t` and `g` become kind 5 (hold) in the prosody stage.
  - `C` makes the lexical stage insert a `)` symbol node.
  - In the playback stage, `i` sends `ESC[n i` (with N-flag 11) and `x` raises the stop request with index 0.
  - Any other letter is fatal error 1. No host command reaches it: the parser forwards only letters in the table.

**Input and playback stages** (`src/v1/v1_input.c`) [verified: byte-exact]:
- The input stage `F53A0` works like v3.4.1's. It moves up to 10 bytes per call and stops after a run of spaces.
- An in-band command becomes one kind-0 node, with the letter as its character.
- With three values (only `t`), the first value (0-9) goes into flag bits 3-7 and the other two into the value bytes.
- The playback stage `ECACC` runs the command nodes that reach it. It counts finished timed segments against
  `[96F6]` (incremented by the frame builder) and frees what has been played.

**Power-up and resets** (`src/v1/v1_loop.c`) [verified: the loop reset is byte-exact; power-up as noted]:
- **`boot_reset` `EC000`** fills RAM with 0x55 and sets `[8CA4]` = 1. It then joins `loop_restart` `EC021` after the
  instruction that clears `[8CA4]`.
- **`loop_restart`** sets the interrupt vectors in RAM:
  - All 40 point to `EC00:00C7`, which is fatal error 0x25.
  - IR0 and IR7 point to `dsp_irq_service` `EC1E0`, IR1 to `uart_rx_isr` `EC2C8`, IR2 and IR3 to `uart_tx_isr` `EC341`.

  It also sets the latch to 0 and the 8259 mask to 0x7C, then calls `synthesis_main`.
- **`loop_entry` `F7083`**:
  - Sets the host defaults: `I` 0, `P` 1, rate 160, pitch 75, amplitude 0, N flags 0x7C0.
  - Copies the `l` defaults to `A57C`.
  - Clears the event counters. After a reset (`[8CA4]` set) it also clears the error counters `A3EA-A415`.
  - Runs the 8251 set-up and sends the DSP boot block (15 words from `DS:1805`).
  - Runs `loop_reset`, replies `ESC[1R`, waits for the output to drain, then sends XON.
- **`loop_reset` `F716E`** runs at power-up and after each utterance. It resets:
  - the escape parser;
  - the node pool and the stage records;
  - the 18 parameter tracks (12 frames of their rest values from `DS:23DB`);
  - the frame buffer (37 words from `DS:1823`);
  - the host rings;
  - every stage's state.

  It returns the index that ended the utterance, or −1.
- **Check:** `v1_boot_check` compares the C power-up with the emulator's RAM at `F7210`: **all of RAM matches**
  (the stack excluded). The DSP stub raises one frame interrupt while `loop_entry` waits for `ESC[1R` to go out, so
  the check runs that interrupt in C too (`v1_dsp_irq_service`).
- **Transmit interrupt:** `uart_tx_isr` sets latch bit 2 while it runs `tx_send`.

**Text-rules stage `ECBAC`** (`src/v1/v1_textrules.c`) [verified: decompiled, byte-exact on 603 captured calls;
88 % of its instructions ran, and nearly all the rest are fatal-error branches]:
- **Same interpreter as v3.4.1's** (§15.4). The program runs from `DS:057E`, and its strings are at `DS:0F56 + offset`.
  Each rule is a condition with its operands, a byte holding the then/else list lengths, and the two action lists.
- **Interpreter state:**

  | Item | Address |
  |---|---|
  | program counter | `952E` |
  | at-condition flag | `9530` |
  | yielded flag | `9532` |
  | replacement pointer | `9534` |
  | count | byte `9536` |
  | skip | byte `9538` |
  | call stack | `953A` (20 entries), pointer `958A` |
  | byte registers | `958C` |

  Stage record `8CAA`: +4 is the text start, +6 the current node, +0A the match mark, +0C a scratch cursor.
- **18 conditions** (jump table `DS:0390`) and **22 actions** (`DS:03B4`). v1.1's numbering matches v3.4.1's for
  conditions 0-17 and actions 0-19, with these differences:
  - Condition 0 is `INPUT == 1`, not 2, and there is no two-letter phoneme spelling.
  - Condition 2 tests N flags only.
  - Condition 3's extra word characters are `DS:056E` (`"$%&),.?@[]|~`).
  - Condition 8 skips only spaces.
  - There is no exact-case list match (v3.4.1 condition 18). The lists are at `DS:0552`.
  - Action 3 has no register-2 flag. Action 4 also converts command nodes.
  - Emitted phonemes take no waiting stress or timing marks.
  - Action 20 (phoneme numbers) accepts only the form `phoneme/pitch;duration/`, with a closing `/` (rule `62B`): the
    pitch goes to node byte +6 (halved) and the duration to byte +5.
  - Action 21 sets the match mark (v3.4.1's action 22).
  - Action 15 (replace a character) exists but no rule uses it.
- **Fatal errors** 2-0x19, 0x27 and 0x28 guard null cursors and bad opcodes.
- **Quirk in `fatal_error` `ED9B6`:** it means to store the error code at `A3EE`, but stores the caller's SI instead
  (`xchg ax,si` where the code is in DI). The C stores the code.
- **Rule program:** 373 rules; a linear decode works, because each rule's length follows from its condition's operands
  and its length byte. Rule `000` sets the registers. The dispatcher at `008` then sends the text by condition to the
  handlers for commands, phoneme input, words, spaces, numbers and the pattern lists 0-3.

**Lexical stage `F5A7B`** (`src/v1/v1_lexical.c`) [verified: decompiled, byte-exact on 22k captured calls of the
stage and its routines, apart from the firmware bug below, which the C fixes; 96 % of its instructions ran]:
- **Flow:** one word per call. The word is a word record (`&`), optionally followed by `~` (emphasis), then its letters.
  1. `affixes` `F5C12` looks the word up. If that fails, it strips suffixes (lists by last letter, `DS:2BE4`) and
     prefixes (by first letter, `DS:2F1C`), each with a context pattern.
  2. After an affix whose record asks for it, `stem_repair` `F5F40` retries the stem: undouble a final consonant, add
     E, or change I to Y.
  3. Whatever is left goes through `lts_rules` `F6069`, and so does a word whose entry gives only a stress pattern.
  4. The word record gets kind 3, with its class at +6.
- **Affix records:** +0 → pattern, +2 → context pattern, +4 = 1 to look the stem up afterwards, +5 a class (6 is kept
  as the word's class), +6 → the next list. `affix_match` `F5E49` puts a `[` boundary node at the match.
- **Lexicon in the data segment** (v3.4.1: segment `E900`), looked up by `lex_lookup` `F6B9F`:
  - **Index:** `[DS:4890]` = `49A0` holds 27 pointers, one index array per first letter, indexed by the number of
    letters after the first. Entry *n* to *n*+1 is the bucket. The lexicon runs to `DS:8712`.
  - **Keys:** the letters after the first are packed by the letter-state machine at `DS:47E8`, as in v3.4.1 (§9.6).
    Each state record is 12 bytes: two masks, the next state, a shift, the phoneme state it starts, and a compare mask.
  - **Entries:** the first byte holds the class (bits 5-7) and a phoneme count (bits 0-4), followed by the key.
    - Count 0: a stress pattern only, in the next byte (2 bits per vowel from the right, read by the letter-to-sound
      rules). The entry is the key plus 3 bytes.
    - Otherwise the phonemes follow, decoded by the phoneme-state machine at `DS:480C` (8-byte records). The entry size
      comes from `DS:482C`, indexed by state × 33 + count.
  - **Decoded phonemes:** `1` sets primary stress (level 3 with `~`) on the next vowel, `2` secondary. With no mark,
    the first full vowel gets level 2.
  - **Class:** a nonzero class other than 6 (`C0`) renames the word record to `%`.
- **Letter-to-sound rules** `F6069`:
  - They walk the word **right to left**, the suffix part first and then the stem.
  - Rules are 10-byte records per letter at `DS:4730`: +0 the letters to the left, +2 the phonemes, +4 the left context
    pattern, +6 the required state bits, +8 the new state bits. Bit 0x4000 of the new bits means "do not reduce".
  - A leading phoneme byte below 0x20 is a syllable count, or 0x10: drop the prefix boundary.
  - **Stress:** the third vowel from the end gets primary stress (level 2, 3 with `~`) unless the lexicon gave a
    pattern, and later vowels alternate secondary.
  - `vowel_finish` `F6A5C`:
    - U after a cluster, R, L or X becomes `b`; unstressed, it also palatalizes the consonant (D→J, T→C, S→s, Z→z,
      N+Y).
    - A reducible unstressed vowel becomes `|` (E, e, i) or `@`, and U also gets a Y glide.
- **Context patterns** `F673A`: each alternative is 0-terminated, and its first byte selects the test:
  - 0x40: a vowel before a boundary.
  - 0x20: two or more consonants.
  - 0x80: a doubled consonant.
  - Otherwise up to 3 nodes are tested. Bits 2-3 are the mode: 0 and 12 fail on the first entry that does not hold;
    4 and 8 decide on the first that does. Bit 4 means one entry per node, and bit 2 negates the result.
    Classes come from the `DS:0030` masks over the feature table.
- **Firmware bug, fixed in the C** (2026-09-25): `lex_lookup` never checks the length against the size of the first
  letter's index array (the distance to the next letter's array; Z's has 3 entries, ending at the 27th pointer, which
  holds `8712`).
  - A length past the array reads the next letters' arrays. The firmware's own check (the bucket must start before
    the next letter's entries and before `DS:8712`) rejects those.
  - For X, Y and Z the reads run past the index into the lexicon at `DS:4C1E`, whose bytes pass as bucket pointers:
    X+15, Y+12/14 and Z+4/6/8/10/12/13/14/15 (letters after the first; 15 is the most a word can have). Z+12 and
    Z+14 scan other ROM data; the rest run past `DS:8712` into RAM (Z+13 and Z+15 wrap), including the live stack.
    "ZEBRA" (Z+4) scans `DS:3661-9382`, and the firmware "finds" it in stack bytes; in the captured run the stage
    left its letters unconverted. The result depends on call frames and interrupt timing.
  - **Fix:** the C treats a length past the letter's array as not in the lexicon (no word that long with that letter
    is in it), so such words go through the affixes and letter-to-sound rules. ZEBRA becomes `ZeBR@`. Only the
    lookups whose bucket pointers come from past the index change; the C counts them in `v1_lex_bound_rejects`,
    and `v1_replay` reports those records as the fixed bug instead of as differences.
  - `prose_v1_data.c` still covers `DS:0000-899F` (the padding and the reset jump at `DS:8990` too), which the
    firmware's scans could reach.

**Prosody stage `EEB9C`** (`src/v1/v1_prosody.c`) [verified: decompiled, byte-exact on 52k captured calls of the
stage and its 21 routines in 19 captures; 96 % of its instructions ran]:
- **Output:** each phoneme symbol becomes a timed segment (kind 4): the duration in frames (≤ 55) in the flags word's
  high byte, F0/2 at +6, stress bits cleared. A different program from v3.4.1's (§15.2) with the same plan: Klatt
  durations, a declining F0 with accents, and phrase breaks.
- **Flow** (one call works through the window, then `stage_commit`):
  1. `phrase_scan` `EED9F` runs ahead (record +6) to a `.`, `?` or `,` symbol or a C command. Blanks get 10 units.
     An in-band `x` gets a C after it. When the scan reaches the window's end with 15 or more non-symbol nodes in the
     window, it appends a C. `Q`/`q` mark the next symbol (flags bit 6); a `Q` with a node before it is deleted.
  2. With `ESC[1P` (the default) it also handles the word records. When a word follows a function word (`%`, class
     at +6), `word_reduce` `F0F5F` weakens its stress (class 1: primary → secondary → none; other classes lose it
     when the last `r` command's rate is above 100). Above that rate 160 a word-initial `H` after a non-pause symbol
     is dropped. Above rate 100 class-3 words get `@` vowels and class-2 words lose an `A`'s glide or an `H`; class-3
     `T…` and class-2 `x…` words ("to", "the") take their vowel from the next word's first sound (`@`/`b`;
     `v`/`@`/`E`). After more than 5 words (`[9680]`) a `,` goes in before a function word of class 3 or 4 that
     follows a content word not of class 6, or before one of class 5. When the last `r` rate is below 150, a `)`
     break goes before a function word after a content word when the rate's bit set (`DS:21D7`) meets a rotating
     mask (`DS:48A6`).
  3. `phrase_walk` `EF161` moves the stress digits onto the vowels before them (`stress_digit` `F0DA0`: `"` level 3,
     `1` level 2, `2` level 1) and marks the stressed syllable (`mark_syllable` `F0D15`: the vowel and up to three
     consonants before it, flags bit 5). The cursor may go on once the phrase is complete or 20 symbols lie ahead.
  4. For each symbol: `given_values` `EF663` saves a duration and F0 given with phoneme input; `context_load` `F11BC`
     loads the neighbours and the feature bits (`96BC-96DA`); `allophones` `EF6F6`; `time_segment` `F0E6A`
     (`vowel_context` `F08F6`, `f0_set` `F0A45`, `duration_rules` `F0082`, `duration_set` `F050A`,
     `merge_geminate` `EFFC1`); then the given values are put back.
  5. `cursor_advance` `EEC6E` hands on the finished segments, keeping 7 behind the cursor (none with mode flag 14).
     After `.`, `?`, or any phrase without `ESC[1P`, the next call starts a sentence (`sentence_reset` `EF488`).
- **Allophone rules** (skipped for phoneme input unless mode flag 9 (0x100, set by default) is on). The conditions
  test feature bits of the neighbours; the sound classes named here are inferred from the phonemes involved:
  - Vowels: some vowels after a consonant get flags bit 6, by the segment before, rate and stress (bit 6 later adds
    20 % to the duration and stops a geminate merge). `@L` before an unstressed vowel becomes syllabic `l`; `l` after
    some consonants becomes `@` + `j`. A vowel before an R that is not followed by a stressed vowel becomes
    R-coloured (`@ | v i`→`3`, `E`→`4`, `e A`→`k`, `o`→`r`, `O w`→`g`, `b u`→`c`, `U`→`Y` with the R made `c`;
    after `I f y` the R becomes `3`); `3` after `3` gets an `R` segment between.
  - Consonants: `L` after a vowel and before an unstressed symbol → `j`; `H` after a vowel-like sound → `d`;
    `T P K` after `S` and before a stressed syllable → `D B G`; `T` before `C` is dropped; `C`/`J` get their release
    `s`/`z`; above rate 100 with `ESC[1P`, `T`/`D` between sonorants before an unstressed vowel → flap `t`; `T` before
    a syllabic-nasal context → `q` + `n`; `T` before a boundary and a stressed syllable → `q`; below rate 200 a
    release vocoid `p` before a pause. Above rate 100 an `x` after `T`/`D` becomes `D`.
- **Pauses:** a pause symbol (feature plane 0x80, bit 0x40) gets `.`/`?` 30, `,` 18, others 10 units ×
  `DS:21F5[rate]`/100 (125 % at rate 160), in segments of ≤ 50 frames. `)` breaks are dropped above rate 99. Above
  rate 199 or with mode flag 14 every pause is one 10-frame (flag 14: 4-frame) segment. Blanks get their units ×
  the same scale, at least 4 frames.
- **Durations** (Klatt's rules, MITalk ch. 9): inherent and minimum durations per character (pointers at `DS:22B9`
  and `DS:231B`, in 10 ms). `duration_rules` builds a list of percentages at `96A0`: clause-final lengthening (a
  pause before the next vowel within 6 symbols: 140 % vowels, 150 % or 280 % consonants), non-phrase-final
  shortening 60 % and non-word-final shortening 85 % for vowels (`vowel_context` sets the two flags `968E`/`9690`),
  polysyllabic shortening 80-85 %, unstressed shortening 75/70/50 % with the minimum halved, emphasis 130 %, and the
  consonants after a vowel by class (`DS:224F`: 100, 70, 160, 120 %, adjusted). The percentages are multiplied
  pairwise, keeping each product's low word (unsigned). `duration_set` adds a consonant-context percentage, scales
  by `DS:221D[rate]` (200 % at rate 50, 100 % at 160, 20 % at 250) and applies **DUR = MIN + (INH − MIN)·PRCNT/100**,
  then (DUR + 9)/10 frames, at most 55. `merge_geminate` joins a doubled consonant into one segment. The rate index
  is (rate − 46) >> 3.
- **F0** (`f0_set`): the pitch setting plus a declining offset (35 Hz at a sentence start, 95 % of it after each
  vowel), and a **hat pattern**: +30 Hz from just before the first stressed vowel to the last one of the phrase.
  Each stressed vowel under the hat adds an accent (20 Hz at a sentence start, 10 after a boundary, then 6); at the
  last one the hat ends and the segment before it moves by half the accent. +15 Hz just before an emphatic vowel
  (the accent halves around it). Segments with feature bit 4 get −10 Hz (−5 with bit 2); in the last syllable before
  a boundary, −15 Hz before `.` and +15/+20 Hz before other boundaries. Clamped to 50-200 Hz; pitch 0 gives 0; mode
  flag 12 (0x800) gives the pitch setting throughout.
- **Rate tables past their ends** [verified: code]: the break table (`DS:21D7`, 15 entries) and the pause table
  (`DS:21F5`, 20 entries) are indexed by rates up to 250 (index 25). Above rate 165 the break bits come from the pause
  table, and above rate 205 a blank's scale comes from the duration table.
- **Not reached by the captures:** the scan's added C, an in-band `x` or C inside the scan, pauses over 50 frames from
  a blank, and a few clamps.

**Parameter generator `F1659`** (`src/v1/v1_paramgen.c`) [verified: decompiled, byte-exact on 87k captured calls of
the stage and its 37 routines in 12 captures, apart from one capture artifact (below); 96.5 % of its instructions ran]:
- **Same design as v3.4.1's** (§12): per segment, targets go into one **14-byte struct per track** at `DS:9766 + 14·p`
  with v3.4.1's layout (type, durB, durF, len, locB, onset, target; §12.2), rules adjust them, and `emit` `F34D5`
  writes each track by type (§12.4: 4 hold, 6 ramp, 2 blend, odd types first blend back). **18 tracks**, p0-p17 =
  AV AF AH A2-A6 AB F1-F4 B1-B3 FN F0, 128-byte rings (pointers `9742`), write positions `96FA`, previous boundaries
  `971E`, current values `A180`, locus weights (Q15) `A1A4`, the next segment's F1-B3 `A1E0-A1EC`, a diphthong's
  second F1-F3 `A1C8-A1CC`. Values are kept in Hz and dB; `emit` converts them (F1 /4, F2 (−500)/8, F3-F4 /16,
  B1-B3 and FN /2), and `scale` `F26A1` converts back.
- **No rule table:** v1.1's context rules are **hard-coded routines**, chosen by feature bits (planes 0, 0x80-0x200
  of `DS:0090`) of the previous, current and next segment. `segment` `F21BF` runs: `load_targets` `F1D06`
  (per-vowel targets through `[DS:2551]`, per-place consonant targets through `[DS:237D]`, locus figures, weights
  by class pair `DS:25FD`), `setup` `F387D`, voiced `F3983` / voiceless `F3A32` (aspiration moved back over the
  segment before, `F3782`), after-stop `F3AEE`/`F3ACA`, vowels (`F3B9A` with the stop release `F4F17`; `F3DD9`:
  coarticulation, short vowels pulled toward a neutral vowel `DS:25F7`, diphthong glides), glides and liquids
  `F45CE` (voiced H `d` between voiced sounds), obstruent spectra `F485D` (A2-A6, AB), fricatives `F4B9C`, stops
  `F4C4B`/`F4D00`, nasals `F4EA4`, then the loci `F2A0A`/`F2DC5`, `finish` `F2FDC` (F(n+1) ≥ F(n) + 200 Hz,
  onset = current·(1 − w) + target·w), the amplitude onsets `F3208`, and `emit` for all 18 tracks. A glottal stop
  (flags bit 6) adds an F0 dip; a velar pinch between two segments softens F2/F3 (`F3418`).
- **Settings:** ESC[a is subtracted from AV, AF and AH. A segment with F0 0 (pitch 0) is **whispered**: AV's
  target goes to AH. Voiceless segments set F0 0 and carry it over. Mode flag 14 shortens the transition limit and
  the look-ahead (20/15/12 frames → 4/3/2).
- **Holds** (kind 5 nodes from `s`, `t`, `g`; `F1A38`/`F1B0E`): all 18 tracks hold values for the node's value × 10
  frames: `s` the rest values (`DS:23DB`), `g` the ESC[l values (`A1F2`), `t` a row of `DS:23ED` chosen by the
  node's flags (−1 entries take F0, the amplitude or the index).
- **Flow** (`F1659`, 0 idle / 1 wait / 2 done): the window is prev `8D00`, current `8D02`, next `8D04`.
  `next_segment` `F27DB` looks past the commands to the next segment and inserts silence (`F2520`: a 15-frame
  segment for a C command, or look-ahead + room + 10 frames, then a 4-frame end pause) at a C command, an `x` or a
  stopping index, a hold, or when nothing follows. `ring` `F1559` (also called by the main loop and the frame
  builder): 1 room check, 2 rebase all positions by 0x400, 3 frames ready, 4 advance. An end pause with nothing
  after it ends the utterance: `STOP_AT` `96E0` tells the frame builder where to stop. `advance` `F2709` moves the
  write positions on and marks the boundary in the bitset `96E6`.
- **Firmware quirk** [verified: code]: `setup` clears the voice-onset delay `A176` and then tests it, so it never
  lengthens F0's segment.
- **Capturing it** (scratch harness, 2026-09-25): the other stages were captured with interrupts held off during
  each call, so that only the stage changed RAM. The generator's `segment` takes over 10 ms, and holding the frame
  interrupt that long makes v1.1 raise a fatal error and restart. Its captures therefore run with interrupts on
  (`NO_CLI`), and record, per interrupted call, the RAM bytes the interrupts changed (`CAPTURE.bin.isr`), which
  `v1_replay` leaves out. The one remaining difference is an interrupt that moved the frame builder's position
  past 0x400 during a call, so the firmware rebased the ring there and the replay (from the pre-interrupt state)
  cannot.

**Frame builder `EE564` and the frame interrupt** (`src/v1/v1_frame.c`) [verified: decompiled, byte-exact on 115k
captured calls (7 inputs, with interrupts running as for the generator); 94 % of the instructions ran. The rest are
the spurious-interrupt count, the lengths of runs of missed frames (the generator never fell behind), the handshake's
busy and fatal branches, and two floors the sums never reach]:
- **`dsp_irq_service` `EC1E0`** (IR0): latch bit 2 up; if the DSP status shows USF0 it runs `dsp_frame_tick`, else it
  counts the interrupt in `8CA3` (20 in a row: fatal error 0x23).
- **`dsp_frame_tick` `EDA11`**: unless the output is held (`95C2`, ESC[H) or the builder has stopped (`96DC`), it sends
  the ready frame (`dsp_write_frame` `EC236`: 37 words from `DS:95C8`, low byte first; fatal error 0x24 if the DSP
  still wants a frame after them) and sets `96DC` when the ring position reaches `STOP_AT` `96E0`. A request with no
  frame ready is a miss (`A41E`, the run length in `959A`, counted by length in `A526-A53A` when the run ends). Then,
  with interrupts back on, it builds the next frame.
- **`frame_handshake` `EE49E`**: 3 may a build start (no frame waiting in `95C4`, no build in `95C6`), 4 built,
  5 is one ready, 6 sent. Same as v3.4.1's `D8949`.
- **`dsp_build_frame` `EE564`** runs when `ring(3)` says the generator is past the look-ahead. At ring position
  `pos = [96DE] & 0x7F` it takes the frame's bits out of the mark set `96E6` (counting `96F6`) and the 10 Hz set
  `A20E`, copies p1-p16 to `DS:9612 + 2i`, then (w = frame word, `DS:95C8 + 2w`):

  | Word | Value |
  |---|---|
  | w2 | `0x1800 \| F0 << 1` |
  | w36 | −dB[AV + 0x8C] |
  | w21 | dB[AH + 0x69] |
  | w11, w28 | F4: cos(2·F4)·0x1E11 >> 12; x = 0x3C40 − (product >> 13), w28 = x + x/8 (fixed bandwidth) |
  | w13, w14, w30 | F3/B3: r·cos(2·F3) >> 12, r² << 2, 0x2000 − hi + r² (r = exp(−πBT) at B3/2) |
  | w15, w16, w32 | F2/B2, cos index F2 + 0x3F |
  | w19, w20, w24 | F1/B1, cos index F1/2; w24 is the third term << 3 |
  | w3, w34 | nasal zero: cos(FN/4)·(−0x1EFB) >> 12, gain[FN/8] << 1 |
  | w31, w29, w27, w25, w23, w33 | parallel A2-A6 and AB when AF > 0 (else 0): dB of A + AF + the spacing corrections + 0x1E/0x16/0x11/0x10/0xF, times the F2, F3, F4 terms, 0x63E0 and 0x6520 (>> 11, w29 and w25 negated); AB: dB[AB + AF + 0x25] << 2 |

  In a 10 Hz frame F1-F3 index the cosine table at F·10/8. Tables: cos `1A51`, r `186D`, r² `1945`, nasal gain
  `1A17`, dB `1E4F`, spacing `1FF9`/`20EB`. The helpers are `FB283` (product >> 12, and >> 13 into `9638`) and
  `FB26D` (>> 11). The words not listed (0, 1, 4-10, 12, 17, 18, 22, 26, 35) keep their silent defaults from
  `DS:1823`.

**The whole of v1.1 in C: `v1_pipeline_play`** (`src/tests/v1_pipeline_play.c`, 2026-09-25) [verified: the frames
sent to the DSP match the emulator's word for word]:
- **Main loop** (`v1_loop.c`): `v1_loop_check_stop` (the stop request: drain the output, `loop_reset`, reply `ESC[S` or
  `ESC[n x`, XON), `v1_loop_pass` (one pass of `F7204`: playback, then the generator, or the first of prosody,
  lexical, text rules and input that has work and more free nodes than 0x14 / 0x25 / 0x25 / 0x0F) and
  `v1_idle_has_work` (one spin of `loop_idle` `F73F0`). The generator's result goes through the table at `DS:48CA`:
  0 clears its flag, 1 sets `TRACKS_FULL` `A564`, and playback runs after it every time.
- **Run:** from the C power-up; between frame requests the loop runs until it would idle, and each request runs
  `dsp_irq_service`. `-p N` sends the host text at N characters per frame (a serial line) instead of all at once.
- **Check** (scratch: `v1params` with the stub's frames, `FRAMES_OUT`): 11 inputs, from one sentence to 25 s of text
  (XOFF flow control and a ring rebase), with rate, pitch 0, volume, the `s`/`g`/`t` holds, phoneme input, fast mode
  and `ESC[x` (reply `ESC[0x`). **All frames are identical**, except 3 silent frames before the first word in the
  holds input: there the C, which runs every stage between two frames, writes the next segment's backward F2 blend
  into frames that the 8086, still busy with the text, had already sent. The XON/XOFF pattern also depends on host
  timing.
- **Frame mapping** (`v1_map.c`): v1.1's DSP program is lost, so each 37-word frame is turned into a 40-word v3.12
  frame and played by `prose_synth`. **Word-level, not parameter-level:** the two builders share their arithmetic
  and their tables (cosine, r, r², dB and spacing tables agree to within one in a few entries; v1.1's nasal-zero gain
  table is v3.4.1's, indexed by the same frequency: v1.1 index FN/8 = 16·(FN/8) Hz, v3.4.1 index p16/4 =
  16·(p16/4) + 192 Hz). So these words are copied: F4 w11 → 6, F3 w13/w14/w30 → 8/9/25, F2 w15/w16/w32 →
  10/11/27, F1 w19/w20 → 14/15, nasal zero w3/w34 → 32/29, AH w21 → 16, parallel w31/w29/w27/w23/w33 →
  26/24/22/18/28. Three need rescaling to v3.12's convention: F1's gain (v1.1 w24 = g·8, v3.12 w36 = g·16), F4's
  gain (v1.1 w28 = g·9/8; recomputed from w11), and the parallel F5 amplitude (v1.1 scales it by a fixed 0x63E0,
  v3.12 by voice 0's F5 gain 0x6C2B). **The source is v3.12's:** F0 comes from w2 and AV from w36 (the smallest dB
  index giving −w36), and v3.4.1's `frame_build` makes the periods, the voicing gain and amplitudes, the silence flag
  and the fixed voice words with p18-p21 = 17, 8, 0, 0 (voice 0, no jitter or shimmer). v1.1's AV 0 still sends a
  small amplitude (dB[0x8C] = 8); here it is an unvoiced frame. v1.1's constant words (0, 1, 4-10, 12, 17, 18, 22,
  26, 35) are dropped: what its DSP did with them is unknown. The levels match the earlier parameter-level render,
  whose peaks were twice as high.

**`ESC[x` acts when it is parsed** [verified in emulation]: the parser sets the end request `A56A` at once, so an `ESC[x`
queued behind text ends the utterance early. For example, "Hello, my friend. … ESC[x" sent in one go is cut off after
about 2 s and replies `ESC[7x`, taking the index of the last marker. As with v3.4.1, wait for the reply before sending
more.

**Consequence for `src/`** [inferred]: v1.1 is an ancestor, not a variant. A build switch cannot swap in its
data, because nearly every routine differs in code and in data layout. Its front end would need its own
decompilation, and its audio cannot be produced faithfully without its DSP ROM: the 37-word frame would have to be
mapped onto the v3.12 DSP model, which is an approximation. **Decision (user, 2026-09-25):** decompile v1.1 as its
own tree, `src/v1/`, built with `cmake -DPROSE_VERSION=1`. Its audio is approximated by mapping its frames onto
the v3.12 DSP model; the v3.12 DSP ROMs are the only Prose DSP ROMs known to survive. Its data comes from
`tools/rom_extract.py --v1 prose_v1` (`src/data/prose_v1_data.c`, 35,232 bytes: `DS:0000-899F`, the data to `DS:8712`, then the FF padding and the reset jump, which a lexicon scan can reach).

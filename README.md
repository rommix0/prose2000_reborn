# Prose 2000 decompilation

A C decompilation of the firmware of the Speech Plus / Telesensory Systems **Prose 2000** text-to-speech
synthesizer (8086 host CPU plus an NEC µPD7720 formant-synthesis DSP). It turns text into speech entirely in C,
with no emulator and no ROM files.

Two firmware versions are covered:

- **v3.4.1** (the default build): the whole pipeline, from the serial input, escape commands, text rules, lexicon,
  letter-to-sound rules and prosody to the parameter generator, the frame builder and the DSP program.
- **v1.1** (1983, `-DPROSE_VERSION=1`): the same pipeline for the older firmware. Its DSP program was never dumped,
  so its frames are played through the v3.12 DSP model, which makes its audio an approximation.

Every stage was checked against an emulator of the original board: byte-exact on captured calls, and frame- or
sample-exact end to end.

## Building

```
cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Add `-DPROSE_VERSION=1` for v1.1. The ROM data the C needs is built in (`src/data/`).

## Running

```
build/pipeline_play -s "Hello, my friend. How are you today?"
build/pipeline_play -s "\e[150pHigher pitch." -w out.wav -q
```

`-s` speaks the text, `\e` stands for ESC in the Prose's `ESC[` commands, `-w` saves a 10 kHz WAV file and `-q` skips
playback (Windows). With `-DPROSE_VERSION=1` the program is `v1_pipeline_play`.

## Layout

| Path | What |
|---|---|
| `src/` | The decompilation, one directory per firmware stage; `src/v1/` is v1.1, `src/tests/` the players and replay tests |
| `src/data/` | Data extracted from the ROMs (lexicon, rule tables, targets, DSP tables) |
| `REFERENCE.md` | Hardware, firmware and verification notes |
| `API.md` | Draft API of the planned DLL |
| `docs/` | The patents describing the Prose 2000 (text extracts and scans) |

## Notes

The original firmware was proprietary to Telesensory Systems and Speech Plus, both long defunct; `src/data/` holds
data extracted from it. The board memory map comes from MAME's `tsispch.cpp` driver by Jonathan Gevaryahu.

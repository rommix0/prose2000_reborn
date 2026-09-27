# Prose 2000 Reborn

A C decompilation of the firmware of the Speech Plus / Telesensory Systems **Prose 2000** text-to-speech
synthesizer (8086 host CPU plus an NEC µPD7720 formant-synthesis DSP). It turns text into speech entirely in C,
with no emulator and no ROM files.

Two firmware versions are covered:

- **v3.4.1** (1988, the default build): the whole pipeline, from the serial input, escape commands, text rules, lexicon,
  letter-to-sound rules and prosody to the parameter generator, the frame builder and the DSP program.
- **v1.1** (1983, `-DPROSE_VERSION=1`): the same pipeline for the older firmware. Its DSP program was never dumped,
  so its frames are played through the v3.12 DSP model, which makes its audio an approximation.

Every stage was checked against an emulator of the original board: byte-exact on captured calls, and frame- or
sample-exact end to end.

## Building

Everything is built with CMake (3.20 or later) and a C99 compiler: gcc or clang on Linux, MinGW-w64 gcc on Windows.
The ROM data the C needs is built in (`src/data/`), and no audio packages are needed.

Windows (MinGW-w64, e.g. winlibs, which includes Ninja):

```
cmake -S src -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

`-G Ninja` and `-DCMAKE_C_COMPILER=gcc` matter when Visual Studio is installed too: without them CMake picks Visual
Studio and MSVC, which the project is not built or tested with. With `mingw32-make` instead of Ninja, use
`-G "MinGW Makefiles"`.

Linux:

```
cmake -S src -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Either builds the speech library, `prose_say`, the samples and the v3.4.1 tests into `build/`. Options:

- `-DPROSE_VERSION=1` builds the v1.1 tests instead of v3.4.1's (the library always has both versions).
- `-DPROSE_SAMPLES=OFF` leaves out the samples.

## The speech library

Both versions are built into one library, `prose.dll` on Windows and `libprose.so` on Linux, with the API in
[API.md](API.md) (header `src/include/prose.h`). Audio is 10 kHz, 16-bit mono.

```
build/prose_say "Hello, my friend."
build/prose_say -w out.wav -r 180 "Saved to a file."
build/prose_say -1 -t "Hello."          (v1.1; print the phonemes)
```

The samples (`samples/`) are the examples from API.md: events and markers, audio buffers, raw frame synthesis,
parameter export and import as CSV, and a custom glottal pulse. On Linux the speaker output uses PulseAudio or ALSA,
loaded at run time.

`samples/python/` has the same samples in Python, through `ctypes` (no packages needed; `prose.py` declares the API):

```
python samples/python/speak_events.py
```

They find the library in `build/` or `build64/`, or wherever `PROSE_LIB` points. Python and the library must have the
same bitness: the usual 64-bit Python on Windows needs a `prose.dll` built with a 64-bit MinGW-w64 gcc
(`x86_64-w64-mingw32`), for example with that gcc first on `PATH`:
`cmake -S src -B build64 -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release`.

## Running the tests

```
build/pipeline_play -s "Hello, my friend. How are you today?"
build/pipeline_play -s "\e[150pHigher pitch." -w out.wav -q
```

`-s` speaks the text, `\e` stands for ESC in the Prose's `ESC[` commands, `-w` saves a 10 kHz WAV file and `-q` skips
playback (Windows). With `-DPROSE_VERSION=1` the program is `v1_pipeline_play`. The replay tests are described in
REFERENCE.md §14.

## Layout

| Path                                   | What                                                                                                              |
| -------------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| `src/`                                 | The decompilation, one directory per firmware stage; `src/v1/` is v1.1, `src/tests/` the players and replay tests |
| `src/dll/`, `src/include/`, `src/cli/` | The speech library, its header and `prose_say`                                                                    |
| `samples/`                             | Programs using the library (the API.md examples)                                                                  |
| `src/data/`                            | Data extracted from the ROMs (lexicon, rule tables, targets, DSP tables)                                          |
| `REFERENCE.md`                         | Hardware, firmware and verification notes                                                                         |
| `API.md`                               | The library's API                                                                                                 |
| `docs/`                                | The patents describing the Prose 2000 (text extracts and scans)                                                   |

## Notes

The original firmware was proprietary to Telesensory Systems and Speech Plus, both long defunct; `src/data/` holds
data extracted from it. The board memory map comes from MAME's `tsispch.cpp` driver by Jonathan Gevaryahu.

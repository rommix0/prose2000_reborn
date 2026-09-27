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

Windows (a 64-bit MinGW-w64 gcc, `x86_64-w64-mingw32`, e.g. winlibs, which includes Ninja; its `bin` folder first on
`PATH`):

```
cmake -S src -B build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

`-G Ninja` and `-DCMAKE_C_COMPILER=gcc` matter when Visual Studio is installed too: without them CMake picks Visual
Studio and MSVC, which the project is not built or tested with. With `mingw32-make` instead of Ninja, use
`-G "MinGW Makefiles"`.

The Windows build is **64-bit**, which is what 64-bit programs, the usual Python among them, can load. For a 32-bit
program, build a 32-bit library with a 32-bit MinGW-w64 gcc (`i686-w64-mingw32`) first on `PATH` instead:

```
cmake -S src -B build32 -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release -DPROSE_32BIT=ON
cmake --build build32
```

Configuring with a 32-bit gcc without `-DPROSE_32BIT=ON` stops with an error, so a 32-bit library is not built by
mistake. Both give the same output.

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
parameter export and import as CSV, a custom glottal pulse, and lip sync (a mouth that follows the phonemes). On
Linux the speaker output uses PulseAudio or ALSA, loaded at run time. The programs find the library next to themselves
(on Linux through their `$ORIGIN` run path) or in the build folder they were built in.

`samples/python/` has the same samples in Python, through `ctypes` (no packages needed; `prose.py` declares the API):

```
python samples/python/speak_events.py
```

They find the library (`prose.dll`, or `libprose.so` under that name) next to the script being run or to `prose.py`,
in the repository's build folders (`build/`, then any other `build*`), on the system path, or wherever
`PROSE_LIB` points; so the folder can be copied anywhere with the library in it. Python and the library must have the
same bitness: the default Windows build is 64-bit, as the usual Python is, and a 32-bit Python needs the 32-bit build
(`build32` above). A library of the other bitness is skipped, and if none loads the error says so.

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
| `VOICE_CONTEXTS.md`                    | The phonetic contexts the parameter generator distinguishes: what a new voice's data must cover                   |
| `PITCH_SYSTEM.md`                      | How the pitch (F0) contour is built: phrase line, contours, accents, phrase endings, the F0 track                 |
| `pitch_graphs/`                        | Annotated graphs of pitch contours, made by `make_graphs.py` from `pitch_trace` (`src/cli/`)                      |
| `voice/`                               | The contexts and phoneme classes as TSV, an ARPABET to Prose mapping, a corpus coverage checker (VOICE_CONTEXTS.md §8) |
| `docs/`                                | The patents describing the Prose 2000 (text extracts and scans)                                                   |

## Notes

The original firmware was proprietary to Telesensory Systems and Speech Plus, both long defunct; `src/data/` holds
data extracted from it. The board memory map comes from MAME's `tsispch.cpp` driver by Jonathan Gevaryahu.

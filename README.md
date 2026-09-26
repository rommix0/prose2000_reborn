# Prose 2000 for NVDA

Purpose-built emulation of the Telesensory Systems/Speech Plus Prose 2000/2020
speech synthesizer for modern NVDA.

The project does not embed or launch MAME. Its intended architecture is a small
engine containing only the hardware needed by Prose 2000:

- Intel 8086 firmware execution.
- Minimal 8251 UART and 8259 interrupt-controller behavior.
- NEC uPD7720 speech-DSP execution.
- Serial DSP output capture as PCM audio.
- A small isolated host that streams 10 kHz PCM to an NVDA synth driver.

The source build expects a directory containing the standard `prose2k` ROM
set. The original firmware remains the property of its respective rights
holders and is not covered by the emulator source's BSD license.

## Features

The native engine boots the original 8086 and uPD7720 firmware, accepts serial
text and reconstructs the board's 12-bit DAC output. `ProseHost.exe` runs
outside NVDA and communicates through a bounded binary protocol. NVDA remains
responsive while speech is generated, and can cancel or replace the host if it
stops responding.

The add-on currently provides speech, pause, cancellation, Say All completion
and NVDA audio-device routing. Rate, Pitch and Volume use the original Prose
firmware controls and are exposed through NVDA. Its DSP scheduler enters the
firmware audio routine at the synthesis loop's safe boundary, avoiding the
timer race that corrupts sibilants.

The supplied firmware provides one English male voice. It does not expose
selectable voices or variants; its supported user settings are Rate, Pitch and
Volume.

The packaged add-on checks releases at
https://github.com/OnjLouis/prose2000 once a day. A manual check is available
from NVDA's Tools menu; network checks run outside NVDA's main thread.

SoundWave 1.2.1 and later can render Prose 2000 directly through an independent
host process without altering NVDA's active synthesizer or global audio path.

## Building

Configure `native` with CMake and build the `ProseHost` target. The diagnostic
`prose_cli` and `prose_dsp` targets are intentionally retained for firmware and
audio regression testing, but are not packaged in the NVDA add-on. See
[`native/README.md`](native/README.md) for build and command-line usage.

## Research basis

MAME's `tsispch.cpp` documents the board memory map and ROM layout under the
BSD-3-Clause license. MAME currently marks Prose 2000 as not working and without
sound because its uPD7725 core does not expose the serial output and clock used
by the board's DAC. This project implements that missing path directly instead
of distributing MAME.

## Credits and attribution

- The original Prose 2000/2020 hardware and firmware were developed by
  Telesensory Systems Inc. and Speech Plus. The firmware remains proprietary
  and is not covered by this project's BSD license.
- [MAME's Prose 2000 driver](https://github.com/mamedev/mame/blob/master/src/mame/skeleton/tsispch.cpp),
  by Jonathan Gevaryahu with thanks to Kevin Horton, established the board
  memory map, ROM layout and hardware clocks used by this implementation.
- The standalone MAME compatibility shim is adapted from
  [David Sexton's DoubleTalk PC project](https://github.com/daiverd/doubletalk-pc).
- The vendored 8086 core and endian helpers come from
  [MAME](https://github.com/mamedev/mame) and retain their original
  BSD-3-Clause copyright notices.
- Prose-specific emulation, DSP serial-output capture and NVDA integration were
  developed by Andre Louis with OpenAI Codex assistance.

## License

The emulator and NVDA integration source are available under the BSD 3-Clause
License. See `LICENSE` for the full terms and retained attribution.

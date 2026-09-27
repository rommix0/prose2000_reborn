"""Grabbing audio with prose_speak_to_buffer (API.md): 100 ms chunks through a callback, with an index marker as a
bookmark, plus each phoneme as it starts. The chunks are collected and saved as buffer.wav with Python's wave module.

    python speak_buffer.py [v11] [TEXT]"""
import ctypes
import sys
import wave
from array import array

from prose import *

h = open_handle()
text = sys.argv[1] if len(sys.argv) > 1 else "Hello \x1b[1i world."
pcm = array("h")  # stands in for wherever the audio goes: a SAPI site, a file, the network
chunks = 0


def on_audio(h, buf, count, pos, user):
    global chunks
    pcm.extend(buf[:count])  # copy it out: the buffer is reused for the next chunk
    chunks += 1
    return 0  # nonzero stops synthesis


def on_index(h, n, pos, user):
    print(f"bookmark {n} at byte offset {pos * 2}")  # SAPI: 2 bytes per sample


def on_phoneme(h, ph, ms, pos, user):
    print(f"  {ph.decode()!r:5} {ms:4d} ms at {pos / PROSE_SAMPLE_RATE:.2f} s")


cb = prose_callbacks(on_index=ON_INDEX(on_index), on_phoneme=ON_PHONEME(on_phoneme))
lib.prose_set_callbacks(h, ctypes.byref(cb), None)
buf = (ctypes.c_int16 * 1000)()  # 100 ms chunks
audio_cb = AUDIO_CB(on_audio)  # kept in a variable: it must outlive the call
total = lib.prose_speak_to_buffer(h, text.encode(), buf, len(buf), audio_cb, None)
if total < 0:
    sys.exit(f"prose_speak_to_buffer: {error_string(total)}")
print(f"{total} samples ({total / PROSE_SAMPLE_RATE:.2f} s) in {chunks} chunks")

with wave.open("buffer.wav", "wb") as w:  # prose_save_wave would do as well
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(PROSE_SAMPLE_RATE)
    w.writeframes(pcm.tobytes())  # little-endian on x86
lib.prose_close(h)

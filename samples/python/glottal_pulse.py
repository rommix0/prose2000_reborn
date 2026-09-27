"""A custom glottal pulse (API.md): makes one Rosenberg period as pulse.wav, loads it, and speaks a sentence with it
(custom.wav) and with the Prose's own pulse (original.wav).

    python glottal_pulse.py [v11]"""
import ctypes
import math
import sys

from prose import *

# a period for testing: a Rosenberg pulse, 40 % rising, 16 % falling, then closed
N = 100  # one 10 ms period at 10 kHz
OPEN, CLOSE = 0.40, 0.16
period = (ctypes.c_int16 * N)()
for i in range(N):
    t = i / N
    if t < OPEN:
        g = 0.5 * (1 - math.cos(math.pi * t / OPEN))
    elif t < OPEN + CLOSE:
        g = math.cos(math.pi / 2 * (t - OPEN) / CLOSE)
    else:
        g = 0
    period[i] = int(g * 32000)
err = lib.prose_save_wave(b"pulse.wav", period, N)
if err:
    sys.exit(f"pulse.wav: {error_string(err)}")

h = open_handle()
err = lib.prose_load_glottal_wave(h, b"pulse.wav", PROSE_GLOTTAL_FLOW)
print(f"load pulse.wav: {error_string(err)}")
if err == 0:
    lib.prose_use_custom_glottal(h, 1)
err = lib.prose_speak_to_wave(h, b"custom.wav", b"This voice uses a custom glottal pulse.")
print(f"custom.wav: {error_string(err)}")

lib.prose_use_custom_glottal(h, 0)  # back to the Prose's own pulse
err = lib.prose_speak_to_wave(h, b"original.wav", b"This one uses the original.")
print(f"original.wav: {error_string(err)}")
lib.prose_close(h)

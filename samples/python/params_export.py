"""Extracting parameters to a CSV file (API.md): one row per 10 ms frame, in Hz and dB, written to hello.csv.

    python params_export.py [v11] [TEXT]"""
import csv
import ctypes
import sys

from prose import *

h = open_handle()
text = sys.argv[1] if len(sys.argv) > 1 else "Hello there."
count = lib.prose_param_count(h)

with open("hello.csv", "w", newline="") as f:
    out = csv.writer(f, lineterminator="\n")
    out.writerow(["time_ms"] + [lib.prose_param_name(h, i).decode() for i in range(count)])  # AV,AF,...,F0

    def on_params(h, p, pos, user):
        # time in ms (10 samples per ms), then each parameter in Hz or dB; p is only valid during the call
        out.writerow([pos // 10] + [f"{lib.prose_param_value(h, i, p[i]):g}" for i in range(count)])

    cb = prose_callbacks(on_params=ON_PARAMS(on_params))
    lib.prose_set_callbacks(h, ctypes.byref(cb), None)
    discard_audio = AUDIO_CB(lambda h, pcm, count, pos, user: 0)  # no audio is needed
    total = lib.prose_speak_to_buffer(h, text.encode(), None, 0, discard_audio, None)

if total < 0:
    print(f"hello.csv: {error_string(total)}")
else:
    print(f"hello.csv: written ({total / PROSE_SAMPLE_RATE:.2f} s of speech)")
lib.prose_close(h)

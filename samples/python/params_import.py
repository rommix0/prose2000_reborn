"""Synthesizing from a CSV file (API.md): hello.csv (from params_export.py) read back one row per frame through the
raw path, saved as hello_csv.wav. The header decides which column sets which parameter, so the file may hold any
subset of the columns, in any order.

    python params_import.py [v11] [FILE.csv]"""
import csv
import ctypes
import sys

from prose import *

h = open_handle()
file = sys.argv[1] if len(sys.argv) > 1 else "hello.csv"
try:
    with open(file, newline="") as f:
        rows = list(csv.reader(f))
except OSError:
    sys.exit(f"cannot open {file} (run params_export.py first)")
header, rows = rows[0], rows[1:]
cols = [lib.prose_param_index(h, name.encode()) for name in header[1:]]  # -1 skips an unknown column
pcm = (ctypes.c_int16 * (len(rows) * 100))()


def load_row(k):
    """loads row k into the frame parameters; False at the end of the file"""
    if k >= len(rows):
        return False
    for p, value in zip(cols, rows[k][1:]):  # rows[k][0] is time_ms
        if p >= 0:
            lib.prose_set_frame_param(h, p, lib.prose_param_raw(h, p, float(value)))
    return True


def on_frame(h, frame, buf, count, user):
    pcm[frame * 100:frame * 100 + count] = buf[:count]
    return not load_row(frame + 1)  # nonzero stops at the end of the file


frames = 0
if load_row(0):  # row 0 is the first frame
    frames = lib.prose_render_frames(h, len(rows), FRAME_CB(on_frame), None)
err = frames if frames < 0 else lib.prose_save_wave(b"hello_csv.wav", pcm, frames * 100)
print(f"{frames} frames from {file}; hello_csv.wav: {error_string(err)}")
lib.prose_close(h)

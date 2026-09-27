"""Raw synthesis (API.md): an "ah" gliding to "ee", set frame by frame, saved as glide.wav.

    python raw_glide.py [v11]"""
import ctypes

from prose import *

FRAMES = 30  # 30 x 10 ms = 0.3 s

h = open_handle()
pcm = (ctypes.c_int16 * (FRAMES * 100))()  # 100 samples per frame


def lerp(a, b, k):
    return a + int((b - a) * k / FRAMES)  # truncates toward zero, as C does


def on_frame(h, frame, buf, count, user):
    """after each frame: keep its audio, then set up the next frame"""
    pcm[frame * 100:frame * 100 + count] = buf[:count]
    k = frame + 1
    lib.prose_set_frame_param(h, PROSE_F1, lerp(175, 75, k))   # 700 -> 300 Hz
    lib.prose_set_frame_param(h, PROSE_F2, lerp(87, 212, k))   # 1200 -> 2200 Hz
    lib.prose_set_frame_param(h, PROSE_F3, lerp(156, 181, k))  # 2500 -> 2900 Hz
    lib.prose_set_frame_param(h, PROSE_F0, lerp(130, 100, k))  # pitch 130 -> 100 Hz
    return 0


lib.prose_set_frame_param(h, PROSE_AV, 60)  # frame 0: voiced "ah"
lib.prose_set_frame_param(h, PROSE_F1, 175)
lib.prose_set_frame_param(h, PROSE_F2, 87)
lib.prose_set_frame_param(h, PROSE_F3, 156)
lib.prose_set_frame_param(h, PROSE_F0, 130)
n = lib.prose_render_frames(h, FRAMES, FRAME_CB(on_frame), None)
err = n if n < 0 else lib.prose_save_wave(b"glide.wav", pcm, FRAMES * 100)
print(f"{n} frames rendered; glide.wav: {error_string(err)}")
lib.prose_close(h)

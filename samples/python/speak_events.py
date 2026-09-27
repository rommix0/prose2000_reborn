"""Speaking with events (API.md): prose_speak with an index marker, then a WAV file and the phonemes.

    python speak_events.py [v11]

Writes out.wav. The two sentences play on the sound device; without one they are skipped."""
import ctypes
import threading

from prose import *

h = open_handle()
done = threading.Semaphore(0)  # prose_speak callbacks run on the library's audio thread


def my_index(h, n, pos, user):
    print(f"  marker {n} at {pos / PROSE_SAMPLE_RATE:.2f} s")


def my_done(h, last_index, total, user):
    print(f"  done: last marker {last_index}, {total / PROSE_SAMPLE_RATE:.2f} s of audio")
    done.release()


# keep the structure (and the callback objects in it) alive while callbacks can arrive
cb = prose_callbacks(on_index=ON_INDEX(my_index), on_done=ON_DONE(my_done))
lib.prose_set_callbacks(h, ctypes.byref(cb), None)

lib.prose_set_rate(h, 180)
lib.prose_set_volume(h, 12)
print("speaking two sentences")
err = lib.prose_speak(h, b"Hello there.")
if not err:
    lib.prose_index(h, 5)
    lib.prose_speak(h, b"Second sentence.")
    done.acquire()  # prose_speak returns at once; wait for both
    done.acquire()
else:
    print(f"  prose_speak: {error_string(err)}")

print("writing out.wav")
err = lib.prose_speak_to_wave(h, b"out.wav", b"Saved to a file.")
if err:
    print(f"  prose_speak_to_wave: {error_string(err)}")

ph = ctypes.create_string_buffer(256)
need = lib.prose_text_to_phoneme(h, b"Hello.", ph, len(ph))
print(f'phonemes of "Hello.": "{ph.value.decode() if need >= 0 else ""}" ({need} characters)')

lib.prose_close(h)

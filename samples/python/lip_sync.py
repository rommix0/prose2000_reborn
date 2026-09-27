"""Lip sync with on_phoneme (API.md): a mouth drawn in the terminal changes shape with each phoneme as it is spoken.

    python lip_sync.py [v11] [TEXT]

on_phoneme fires as each phoneme starts playing on the sound device. The callback maps the Prose phoneme to one of
ten mouth shapes (the Preston Blair visemes used by lip-sync tools such as Papagayo) and redraws the mouth. With no
sound device it prints the timeline instead: when each mouth shape starts, as an animation program would take it."""
import ctypes
import os
import sys
import threading

from prose import *

# Each shape is 5 lines of 13 characters.
MOUTHS = {
    "rest": ["             ", "             ", "  (-------)  ", "             ", "             "],
    "MBP": ["             ", "             ", " (=========) ", "             ", "             "],  # lips pressed
    "FV": ["             ", "  .-------.  ", " ( VVVVVVV ) ", "  '~~~~~~~'  ", "             "],  # lip under teeth
    "L": ["             ", "  .-------.  ", " (  ##^##  ) ", "  '-------'  ", "             "],  # tongue up
    "WQ": ["             ", "      .      ", "     (o)     ", "      '      ", "             "],  # tight round
    "U": ["             ", "     .-.     ", "    (   )    ", "     '-'     ", "             "],  # pursed
    "O": ["    .---.    ", "   /     \\   ", "  |       |  ", "   \\     /   ", "    '---'    "],  # round
    "E": ["             ", " .---------. ", "( ######### )", " '---------' ", "             "],  # wide
    "AI": ["   .-----.   ", "  /       \\  ", " |         | ", "  \\       /  ", "   '-----'   "],  # open
    "etc": ["             ", "  .-------.  ", " ( ####### ) ", "  '-------'  ", "             "],  # teeth
}

# The Prose's phoneme codes (API.md, Phonemes) as mouth shapes, after Papagayo's table for the ARPABET phonemes.
# Anything not listed (T D t q K G C s J z X x S Z H d N n ~ R Y Q, and the release vocoid p) is "etc".
VISEMES = {}
for shape, codes in {"MBP": "PBMm", "FV": "FV", "L": "Ljl", "WQ": "Wh", "U": "ubUc", "O": "Owgyf",
                     "E": "EAek3", "AI": "aov@i|Ir4", "rest": " "}.items():
    VISEMES.update(dict.fromkeys(codes, shape))


def viseme_of(ph):
    return VISEMES.get(ph, "etc")


drawn = False


def show_mouth(shape, ph):
    """Redraw the mouth in place: back up over the last drawing with ANSI cursor movement."""
    global drawn
    out = "\x1b[6A" if drawn else ""
    out += "".join(f"\r\x1b[2K        {line}\n" for line in MOUTHS[shape])
    out += f"\r\x1b[2K        {shape:<4}  {'_' if ph == ' ' else ph}\n"
    sys.stdout.write(out)
    sys.stdout.flush()
    drawn = True


done = threading.Event()


# prose_speak's callbacks run on the library's audio thread. A GUI program would post the shape to its UI thread
# here instead of drawing.
def on_phoneme(h, ph, ms, pos, user):
    ph = ph.decode()
    show_mouth(viseme_of(ph), ph)


def on_done(h, last_index, total, user):
    show_mouth("rest", " ")
    done.set()


last_shape = None


def print_timeline(h, ph, ms, pos, user):
    """Without a sound device: the timeline, one line per change of shape."""
    global last_shape
    ph = ph.decode()
    shape = viseme_of(ph)
    if shape != last_shape:
        print(f"  {pos / PROSE_SAMPLE_RATE:6.3f} s  {shape:<4}  ({'_' if ph == ' ' else ph}, {ms} ms)")
    last_shape = shape


os.system("")  # lets the Windows console take ANSI cursor movement
h = open_handle()
text = sys.argv[1] if len(sys.argv) > 1 else "Hello, my friend. How are you today?"

# keep the structure (and the callback objects in it) alive while callbacks can arrive
cb = prose_callbacks(on_phoneme=ON_PHONEME(on_phoneme), on_done=ON_DONE(on_done))
lib.prose_set_callbacks(h, ctypes.byref(cb), None)

print(f'"{text}"\n')
err = lib.prose_speak(h, text.encode())
if not err:
    done.wait()  # prose_speak returns at once
else:
    print(f"prose_speak: {error_string(err)}\nthe mouth shapes instead:")
    timeline = prose_callbacks(on_phoneme=ON_PHONEME(print_timeline))
    lib.prose_set_callbacks(h, ctypes.byref(timeline), None)
    lib.prose_speak_to_buffer(h, text.encode(), None, 0, AUDIO_CB(lambda h, pcm, count, pos, user: 0), None)
lib.prose_close(h)

"""prose.py: ctypes declarations for the Prose 2000 speech library (API.md), shared by the Python samples.

The library (prose.dll; libprose.so on Linux, under that name) is looked for in $PROSE_LIB (a file), then next to the
program being run, next to this file, in the build folders at the top of the repository (build/, then any other
build*, and their Release/Debug folders), then on the system path. The first that loads is used, so a library of the
wrong bitness is skipped: 64-bit Python needs the default (64-bit) Windows build, 32-bit Python the 32-bit one
(README).

Everything is plain ctypes, so the calls read as in API.md: lib.prose_open(byref(h), PROSE_V341) and so on. The
callbacks' `user` pointer is not needed in Python (closures do its job), so the samples pass None."""
import ctypes
import ctypes.util
import os
import sys
from ctypes import (CFUNCTYPE, POINTER, Structure, c_char, c_char_p, c_double, c_int, c_int16, c_int32, c_size_t,
                    c_uint8, c_uint32, c_void_p)

PROSE_SAMPLE_RATE = 10000
PROSE_V341, PROSE_V11 = 341, 11

(PROSE_AV, PROSE_AF, PROSE_AH, PROSE_A2, PROSE_A3, PROSE_A4, PROSE_A5, PROSE_A6, PROSE_AB,
 PROSE_F1, PROSE_F2, PROSE_F3, PROSE_F4, PROSE_B1, PROSE_B2, PROSE_B3, PROSE_FN, PROSE_F0,
 PROSE_SOURCE, PROSE_SOURCE_GAIN, PROSE_JITTER, PROSE_SHIMMER, PROSE_PARAM_MAX) = range(23)

PROSE_GLOTTAL_FLOW, PROSE_GLOTTAL_DERIVATIVE = 0, 1

prose_h = c_void_p

# every export and callback is __cdecl, hence CDLL and CFUNCTYPE
ON_INDEX = CFUNCTYPE(None, prose_h, c_int, c_uint32, c_void_p)
ON_DONE = CFUNCTYPE(None, prose_h, c_int, c_uint32, c_void_p)
ON_PHONEME = CFUNCTYPE(None, prose_h, c_char, c_int, c_uint32, c_void_p)
ON_PARAMS = CFUNCTYPE(None, prose_h, POINTER(c_uint8), c_uint32, c_void_p)
AUDIO_CB = CFUNCTYPE(c_int, prose_h, POINTER(c_int16), c_size_t, c_uint32, c_void_p)
FRAME_CB = CFUNCTYPE(c_int, prose_h, c_int, POINTER(c_int16), c_int, c_void_p)


class prose_callbacks(Structure):
    _fields_ = [("on_index", ON_INDEX), ("on_done", ON_DONE), ("on_phoneme", ON_PHONEME), ("on_params", ON_PARAMS)]


_SIGNATURES = {
    "prose_open": (c_int, [POINTER(prose_h), c_int]),
    "prose_close": (None, [prose_h]),
    "prose_get_version": (c_int, [prose_h]),
    "prose_set_callbacks": (None, [prose_h, POINTER(prose_callbacks), c_void_p]),
    "prose_error_string": (c_char_p, [c_int]),
    "prose_last_firmware_error": (c_int, [prose_h]),
    "prose_set_voice": (None, [prose_h, c_int]),
    "prose_set_rate": (None, [prose_h, c_int]),
    "prose_set_pitch": (None, [prose_h, c_int]),
    "prose_set_volume": (None, [prose_h, c_int]),
    "prose_set_word_mode": (None, [prose_h, c_int]),
    "prose_set_fast_read": (None, [prose_h, c_int]),
    "prose_set_speak_punctuation": (None, [prose_h, c_int]),
    "prose_speak": (c_int, [prose_h, c_char_p]),
    "prose_speak_to_wave": (c_int, [prose_h, c_char_p, c_char_p]),
    "prose_speak_to_buffer": (c_int32, [prose_h, c_char_p, POINTER(c_int16), c_size_t, AUDIO_CB, c_void_p]),
    "prose_text_to_phoneme": (c_int, [prose_h, c_char_p, c_char_p, c_size_t]),
    "prose_index": (None, [prose_h, c_int]),
    "prose_stop": (None, [prose_h]),
    "prose_pause": (None, [prose_h]),
    "prose_resume": (None, [prose_h]),
    "prose_reset": (None, [prose_h]),
    "prose_set_frame_param": (None, [prose_h, c_int, c_int]),
    "prose_render_frames": (c_int, [prose_h, c_int, FRAME_CB, c_void_p]),
    "prose_save_wave": (c_int, [c_char_p, POINTER(c_int16), c_size_t]),
    "prose_param_count": (c_int, [prose_h]),
    "prose_param_name": (c_char_p, [prose_h, c_int]),
    "prose_param_index": (c_int, [prose_h, c_char_p]),
    "prose_param_value": (c_double, [prose_h, c_int, c_int]),
    "prose_param_raw": (c_int, [prose_h, c_int, c_double]),
    "prose_load_glottal_wave": (c_int, [prose_h, c_char_p, c_int]),
    "prose_use_custom_glottal": (None, [prose_h, c_int]),
}


# Not prose.so: next to this file, Python would take it for the `prose` module and fail to import it.
_NAMES = {"win32": ["prose.dll"], "darwin": ["libprose.dylib"]}.get(sys.platform, ["libprose.so"])


def _script_dir():
    """The folder of the program being run, if it is a file."""
    path = getattr(sys.modules.get("__main__"), "__file__", None) or (sys.argv[0] if sys.argv else "")
    return os.path.dirname(os.path.abspath(path)) if path else None


def _search_dirs():
    here = os.path.dirname(os.path.abspath(__file__))
    top = os.path.normpath(os.path.join(here, "..", ".."))
    dirs = [_script_dir(), here]  # next to the sample being run, or to prose.py
    try:  # build/ (README) first, then any other build* folder
        builds = sorted((d for d in os.listdir(top) if d.startswith("build") and os.path.isdir(os.path.join(top, d))),
                        key=lambda d: (d != "build", d))
    except OSError:
        builds = []
    for b in builds:  # multi-config generators (Visual Studio, Ninja Multi-Config) add a folder per configuration
        dirs += [os.path.join(top, b)] + [os.path.join(top, b, c) for c in ("Release", "RelWithDebInfo", "Debug")]
    return [d for i, d in enumerate(dirs) if d and d not in dirs[:i]]


def _candidates():
    """The library files found, or the bare name for the system path when there are none."""
    if os.environ.get("PROSE_LIB"):
        return [os.environ["PROSE_LIB"]]
    paths = [os.path.join(d, n) for d in _search_dirs() for n in _NAMES if os.path.isfile(os.path.join(d, n))]
    return paths or [ctypes.util.find_library("prose") or _NAMES[0]]


def _file_bits(path):
    """32 or 64 from a library file's header (Windows PE or ELF), or None."""
    try:
        with open(path, "rb") as f:
            head = f.read(4096)
    except OSError:
        return None
    if head[:4] == b"\x7fELF":
        return {1: 32, 2: 64}.get(head[4])
    if head[:2] == b"MZ" and len(head) >= 0x40:
        pe = int.from_bytes(head[0x3C:0x40], "little")
        if head[pe:pe + 4] == b"PE\0\0":
            return {0x14C: 32, 0x8664: 64, 0xAA64: 64}.get(int.from_bytes(head[pe + 4:pe + 6], "little"))
    return None


def _load():
    bits = ctypes.sizeof(c_void_p) * 8
    errors, mismatch = [], False
    for path in _candidates():  # the first that loads
        try:
            lib = ctypes.CDLL(path)
            break
        except OSError as e:
            other = _file_bits(path)
            if other and other != bits:  # a 64-bit process cannot load a 32-bit library, nor the reverse
                errors.append(f"  {path}: a {other}-bit library, and this Python is {bits}-bit")
                mismatch = True
            else:
                errors.append(f"  {path}: {e}")
    else:
        lines = ["cannot load the Prose library:"] + errors
        if mismatch:
            made = "the default Windows build" if bits == 64 else "build32, configured with -DPROSE_32BIT=ON"
            lines.append(f"Python and the library must have the same bitness: use a {bits}-bit library (README: "
                         f"{made}) or a {96 - bits}-bit Python.")
        lines.append(f"(looked for {' or '.join(_NAMES)} in {', '.join(_search_dirs())}, then on the system path; "
                     "PROSE_LIB can name the file)")
        sys.exit("\n".join(lines))
    for name, (restype, argtypes) in _SIGNATURES.items():
        f = getattr(lib, name)
        f.restype, f.argtypes = restype, argtypes
    return lib


lib = _load()


def error_string(code):
    return lib.prose_error_string(code).decode()


def open_handle(argv=sys.argv):
    """Opens a handle, v3.4.1 or `v11` if it is the first argument, as the C samples do; removes that argument."""
    version = PROSE_V341
    if len(argv) > 1 and argv[1] == "v11":
        version = PROSE_V11
        del argv[1]
    h = prose_h()
    err = lib.prose_open(ctypes.byref(h), version)
    if err:
        sys.exit(f"prose_open: {error_string(err)}")
    return h

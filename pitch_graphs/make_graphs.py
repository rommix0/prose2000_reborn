"""make_graphs.py: plot the Prose 2000's pitch contours (PITCH_SYSTEM.md) as annotated PNGs in this folder.

    python pitch_graphs/make_graphs.py [--tool PATH] [--only NAME ...]

Each example is spoken by pitch_trace (built with the C project: build/pitch_trace), which runs the firmware's
v3.4.1 pipeline and reports every phoneme's F0 target, the parts it was built from, and the 10 ms F0 track. Needs
matplotlib.

Top panel: the F0 track (what the synthesizer plays), each phoneme's target, the phrase line (the contour before any
accent), and markers for the events of f0_target and the parameter generator. Bottom panel: how many Hz each rule
added to or took from the phrase line for each phoneme.
"""
import argparse
import os
import shutil
import subprocess
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
from matplotlib.lines import Line2D  # noqa: E402
from matplotlib.patches import Patch  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# name, title, text, pitch_trace options. Most start "warm" (-W): see PITCH_SYSTEM.md §4.6 for the cold case.
EXAMPLES = [
    ("01_statement", "Statement: declination, hat, accents, nucleus and final fall",
     "The old man walked slowly to the store.", ["-W"]),
    ("02_yes_no_question", "Yes/no question: the ending jumps to 2·pitch − pitch/8",
     "Are you ready?", ["-W"]),
    ("03_wh_question", "Wh-question: kind 1, falls like a statement",
     "How are you today?", ["-W"]),
    ("04_comma_list_question", "Commas: kind 3 phrases end with a slight rise; the last phrase is a question",
     "Is it red, green, or blue?", ["-W"]),
    ("05_comma_statement", "Comma then statement: one declination line across both phrases",
     "Hello, my friend. I am here.", ["-W"]),
    ("06_emphasis", "Emphatic stress (~old): 3x accent before it, other accents halved, hat ends there",
     "The ~old man walked slowly to the store.", ["-W"]),
    ("07_power_up_quirk", "Cold start: DECL_LOW is set by the RAM test, so the question gets no rise",
     "Are you ready?", []),
    ("08_contour_6", "Contour 6 (mark \\ before 'walked'): rise to the marked word, then fall",
     "\\e[6AThe old man [\\]walked slowly to the store.", ["-W"]),
    ("09_contour_7", "Contour 7 (mark / before 'walked'): fall to the marked word, then rise",
     "\\e[6AThe old man [/]walked slowly to the store.", ["-W"]),
    ("10_contour_8", "Contour 8 (mark /\\ before 'walked'): as 6, but no nucleus and no final fall",
     "\\e[6AThe old man [/\\]walked slowly to the store.", ["-W"]),
    ("11_contour_16", "Contour 16 (\\ on 'old', / on 'store'): rise, fall, rise; comma ending",
     "\\e[6AThe [\\]old man walked slowly to the [/]store.", ["-W"]),
    ("12_contour_15", "Contour 15 (/ on 'old', \\ on 'store'): fall, rise, fall; no ending",
     "\\e[6AThe [/]old man walked slowly to the [\\]store.", ["-W"]),
    ("13_type12_register", "Type 12 (mark { before 'walked'): the rest of the sentence 8 % higher",
     "\\e[6AThe old man [{]walked slowly to the store.", ["-W"]),
    ("14_type17_carryover", "Type 17 (mark _ on the last word): DECL_LOW stays set, the next question is flat",
     "\\e[6AThe old man walked slowly to the [_]store. Are you ready?", ["-W"]),
    ("15_word_mode", "Word mode (ESC[0P): every word is its own accent domain",
     "\\e[0PThe old man walked slowly to the store.", ["-W"]),
    ("16_long_sentence", "Long sentence: inserted phrase breaks, one line for the whole sentence",
     "When the old man walked slowly down the long road to the little store near the river, "
     "he saw that the door was closed and the lights were out.", ["-W"]),
]
# overlays of the F0 track: name, title, text, [(label, options)]
OVERLAYS = [
    ("17_voices", "Voices 0-2 at their default pitch (85, 75, 110 Hz); excursion factor 1.0, 0.8, 0.9",
     "The old man walked slowly to the store.",
     [("voice 0", ["-W", "-v", "0"]), ("voice 1", ["-W", "-v", "1"]), ("voice 2", ["-W", "-v", "2"])]),
    ("18_pitch_settings", "Pitch setting 60, 85 and 150 (voice 0): the whole contour scales, clamped at 240 Hz",
     "The old man walked slowly to the store.",
     [("pitch 60", ["-W", "-p", "60"]), ("pitch 85", ["-W", "-p", "85"]), ("pitch 150", ["-W", "-p", "150"])]),
]

# the parts of a target, in stacking order, with their colours and legend names
PARTS = [
    ("register", "#8c564b", "type-12 raise"),
    ("accent", "#2ca02c", "accent (stressed vowel)"),
    ("prenucleus", "#ff7f0e", "rise before the nucleus"),
    ("hat", "#f5c242", "hat"),
    ("high", "#17becf", "high segment +3"),
    ("dip", "#7f7f7f", "voiced dip"),
    ("end", "#d62728", "phrase ending"),
    ("clamp", "#000000", "clamp 50-240"),
]
PART_COLOUR = {k: c for k, c, _ in PARTS}
KIND_NAME = {0: "word mode", 1: "statement", 2: "no ending", 3: "comma", 4: "question", 5: "question", 6: "no ending"}
STRESS_MARK = {1: "ˌ", 2: "ˈ", 3: '"'}


def find_tool(path):
    if path:
        return path
    for d in ("build", "build64", "build32"):
        for exe in ("pitch_trace.exe", "pitch_trace"):
            p = os.path.join(ROOT, d, exe)
            if os.path.exists(p):
                return p
    p = shutil.which("pitch_trace")
    if p:
        return p
    sys.exit("pitch_trace not found: build the C project (README) or pass --tool")


def run(tool, text, opts):
    out = subprocess.run([tool] + opts + [text], capture_output=True, text=True, encoding="utf-8", check=True).stdout
    frames, phones = [], []
    for line in out.splitlines():
        f = line.split("\t")
        if f[0] == "F":
            frames.append((int(f[1]), int(f[2]), int(f[3])))
        elif f[0] == "P":
            p = {"t": int(f[1]), "len": int(f[2]), "ph": f[3], "stress": int(f[4]), "flags": int(f[5]),
                 "feat": int(f[6]), "target": int(f[7]), "traced": f[9] != "-", "events": {}}
            if p["traced"]:
                p.update(line=None if f[8] == "-" else int(f[8]), contour=int(f[9]), kind=int(f[10]),
                         first=int(f[11]), pos=int(f[12]), plen=int(f[13]), word=f[14])
                for ev in filter(None, f[15].split(",")):
                    k, v = ev.split("=")
                    p["events"][k] = p["events"].get(k, 0) + int(v)
            phones.append(p)
    return frames, phones


def voiced(p):
    return bool(p["feat"] & 4)


def sounding(f, av):
    return f > 0 and av > 0


def track_line(frames):
    """F0 track with gaps where nothing is voiced (F0 0, or AV 0 as in pauses, which keep an F0 byte)."""
    return [t for t, _, _ in frames], [f if sounding(f, av) else float("nan") for _, f, av in frames]


def shade_runs(ax, spans, colour, alpha, z=0):
    for a, b in spans:
        ax.axvspan(a, b, color=colour, alpha=alpha, lw=0, zorder=z)


def plot_example(tool, name, title, text, opts):
    frames, phones = run(tool, text, opts)
    # up to shortly after the last phoneme (the final pause pair only fills the ring)
    end = max([p["t"] + p["len"] for p in phones if p["ph"] != "_"] or [t for t, _, _ in frames]) + 150
    width = max(11.0, min(40.0, end / 1000 * 5.5))
    fig, (ax, bx) = plt.subplots(2, 1, figsize=(width, 7.6), sharex=True, gridspec_kw={"height_ratios": [3, 1.3]})

    # unvoiced frames and the hat
    spans, start = [], None
    for t, f, av in [fr for fr in frames if fr[0] < end] + [(end, 1, 1)]:
        if not sounding(f, av) and start is None:
            start = t
        elif sounding(f, av) and start is not None:
            spans.append((start, t))
            start = None
    shade_runs(ax, spans, "#bbbbbb", 0.25)
    shade_runs(ax, [(p["t"], p["t"] + p["len"]) for p in phones if "hat" in p["events"]], "#f5c242", 0.18)

    # the phrase line, the targets and the track
    lx, ly = [], []
    for p in phones:
        if p["traced"] and p.get("line") is not None:
            lx += [p["t"], p["t"] + p["len"]]
            ly += [p["line"], p["line"]]
        else:
            lx.append(p["t"])
            ly.append(float("nan"))
    ax.plot(lx, ly, color="#555555", ls=":", lw=1.4, zorder=2)
    for p in phones:
        if not p["traced"] or p["target"] <= 0:
            continue
        ax.hlines(p["target"], p["t"], p["t"] + p["len"], color="#1f77b4" if voiced(p) else "#aaaaaa",
                  lw=2.2, alpha=0.55 if voiced(p) else 0.5, ls="-" if voiced(p) else "--", zorder=3)
    tx, ty = track_line(frames)
    ax.plot(tx, ty, color="#d62728", lw=2.0, zorder=5)

    ys = [f for t, f, av in frames if sounding(f, av) and t < end] + [p["target"] for p in phones if p["traced"] and p["target"] > 0]
    ylo, yhi = (min(ys) if ys else 50) - 35, (max(ys) if ys else 150) + 30
    ax.set_ylim(ylo, yhi)

    # events
    notes = []
    for i, p in enumerate(phones):
        mid, ev, y = p["t"] + p["len"] / 2, p["events"], p["target"]
        nxt = phones[i + 1] if i + 1 < len(phones) else None
        prev = phones[i - 1] if i else None
        if "nucleus" in ev:
            ax.plot(mid, y, marker="*", ms=17, color="#d62728", mec="k", mew=0.6, zorder=7)
        if "accent" in ev:
            ax.plot(mid, y, marker="^", ms=9, color=PART_COLOUR["accent"], mec="k", mew=0.5, zorder=7)
        if "prenucleus" in ev:
            ax.plot(mid, y, marker="D", ms=7, color=PART_COLOUR["prenucleus"], mec="k", mew=0.5, zorder=7)
        if "dip" in ev:
            ax.plot(mid, y - 4, marker="v", ms=5, color=PART_COLOUR["dip"], zorder=6)
        if "end" in ev:
            k = p.get("kind")
            label = {1: "fall", 3: "comma rise", 4: "question", 5: "question"}.get(k, "ending")
            notes.append((mid, y, f"{label} {ev['end']:+d} Hz → {y}", "#d62728"))
        if "end_skipped" in ev and not (prev and "end_skipped" in prev["events"]):
            ax.plot(mid, y, marker="X", ms=11, color="#9467bd", mec="k", mew=0.5, zorder=7)
            notes.append((mid, y, "ending skipped:\nDECL_LOW set", "#9467bd"))
        if "type12" in ev:
            notes.append((p["t"], y, "type 12: +8 % from here", PART_COLOUR["register"]))
        if "type17" in ev:
            notes.append((p["t"], y, "type 17: DECL_LOW set", "#9467bd"))
        if "decl_cleared" in ev:
            notes.append((p["t"], y, "DECL_LOW cleared", "#9467bd"))
        if "given" in ev:
            notes.append((mid, y, f"given F0 {y}", "#1f77b4"))
        # the parameter generator's events
        if p["flags"] & 0x40 and voiced(p):
            ax.plot(p["t"] + 15, max(ylo + 8, 42), marker="$g$", ms=10, color="#9467bd", zorder=7)
        if p["ph"] == "q" and prev and prev["feat"] & 2:
            ax.plot(p["t"] + 15, max(ylo + 8, 42), marker="$q$", ms=10, color="#9467bd", zorder=7)
        if voiced(p) and p["ph"] not in "_Z" and nxt and nxt["ph"] == "_" and p["target"] > 0:
            ax.annotate("", xy=(p["t"] + p["len"], 58), xytext=(p["t"] + p["len"] * 0.33, y),
                        arrowprops=dict(arrowstyle="->", color="#8c564b", lw=1.2, ls="--"), zorder=6)
    for k, (x, y, s, c) in enumerate(notes):
        ax.annotate(s, xy=(x, y), xytext=(x, yhi - 6 - 12 * (k % 2)), fontsize=8, color=c, ha="center",
                    va="top", arrowprops=dict(arrowstyle="-", color=c, lw=0.6, alpha=0.6), zorder=8)

    # phoneme labels, word starts and phrase kinds
    for p in phones:
        if p["ph"] == "_":
            continue
        s = p["ph"] + STRESS_MARK.get(p["stress"], "")
        ax.text(p["t"] + p["len"] / 2, ylo + 4, s, ha="center", va="bottom", fontsize=10,
                fontweight="bold" if p["stress"] >= 2 else "normal", family="monospace", zorder=8)
        if p.get("word") in ("&", "%"):
            for a in (ax, bx):
                a.axvline(p["t"], color="#999999", lw=0.6, ls="-", alpha=0.6, zorder=1)
    last = None
    for p in phones:
        if not p["traced"] or p["ph"] == "_":
            continue
        key = (p["kind"], p["first"], p["contour"])
        if key != last:
            ax.text(p["t"], ylo + 18, f"kind {p['kind']} ({KIND_NAME.get(p['kind'], '?')}), contour {p['contour']}",
                    fontsize=7.5, color="#444444", ha="left", va="bottom", zorder=8)
            last = key

    # bottom: the parts of each target
    for p in phones:
        if not p["traced"]:
            continue
        up = down = 0
        for key, colour, _ in PARTS:
            v = p["events"].get(key, 0)
            if v > 0:
                bx.bar(p["t"], v, width=p["len"], bottom=up, align="edge", color=colour, ec="white", lw=0.4)
                up += v
            elif v < 0:
                bx.bar(p["t"], v, width=p["len"], bottom=down, align="edge", color=colour, ec="white", lw=0.4)
                down += v
    bx.axhline(0, color="k", lw=0.6)
    bx.set_ylabel("Hz added\nto the line")
    bx.set_xlabel("time (ms)")
    bx.set_xlim(0, end)
    ax.set_ylabel("F0 (Hz)")
    settings = " ".join(opts) or "cold start"
    ax.set_title(f"{title}\n\"{text.replace(chr(92) + 'e', 'ESC')}\"   [{settings}]", fontsize=11)

    handles = [
        Line2D([], [], color="#d62728", lw=2, label="F0 track (10 ms frames)"),
        Line2D([], [], color="#1f77b4", lw=2.2, alpha=0.55, label="phoneme target"),
        Line2D([], [], color="#aaaaaa", lw=2.2, ls="--", label="target of a voiceless phoneme (not played)"),
        Line2D([], [], color="#555555", lw=1.4, ls=":", label="phrase line (contour)"),
        Patch(color="#f5c242", alpha=0.3, label="hat up"),
        Patch(color="#bbbbbb", alpha=0.4, label="unvoiced or pause"),
        Line2D([], [], marker="^", ls="", color=PART_COLOUR["accent"], mec="k", label="accent"),
        Line2D([], [], marker="D", ls="", color=PART_COLOUR["prenucleus"], mec="k", label="rise before nucleus"),
        Line2D([], [], marker="*", ms=12, ls="", color="#d62728", mec="k", label="nucleus"),
        Line2D([], [], marker="v", ls="", color=PART_COLOUR["dip"], label="voiced dip"),
        Line2D([], [], marker="X", ls="", color="#9467bd", mec="k", label="ending skipped (DECL_LOW)"),
        Line2D([], [], marker="$g$", ls="", color="#9467bd", ms=9, label="glottal onset (dip to 40 Hz)"),
        Line2D([], [], color="#8c564b", ls="--", label="pre-pausal fall toward 58 Hz"),
    ]
    ax.legend(handles=handles, loc="upper left", bbox_to_anchor=(1.005, 1.0), fontsize=8, frameon=False)
    bx.legend(handles=[Patch(color=c, label=n) for _, c, n in PARTS], loc="upper left", bbox_to_anchor=(1.005, 1.0),
              fontsize=8, frameon=False)
    fig.tight_layout()
    fig.savefig(os.path.join(HERE, name + ".png"), dpi=110)
    plt.close(fig)


def plot_overlay(tool, name, title, text, runs):
    fig, ax = plt.subplots(figsize=(12, 5))
    end = 0
    for (label, opts), colour in zip(runs, ("#1f77b4", "#d62728", "#2ca02c", "#9467bd")):
        frames, phones = run(tool, text, opts)
        tx, ty = track_line(frames)
        ax.plot(tx, ty, color=colour, lw=1.8, label=label)
        end = max([end] + [t for t, y in zip(tx, ty) if y == y])  # the last voiced frame
    ax.set_xlim(0, end + 100)
    ax.set_xlabel("time (ms)")
    ax.set_ylabel("F0 (Hz)")
    ax.set_title(f"{title}\n\"{text}\"", fontsize=11)
    ax.legend(frameon=False)
    ax.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(os.path.join(HERE, name + ".png"), dpi=110)
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--tool", help="path of pitch_trace (default: build/, build64/ or PATH)")
    ap.add_argument("--only", nargs="*", help="names (or prefixes) of the graphs to make")
    a = ap.parse_args()
    tool = find_tool(a.tool)
    for name, title, text, opts in EXAMPLES:
        if not a.only or any(name.startswith(o) for o in a.only):
            plot_example(tool, name, title, text, opts)
            print(name + ".png")
    for name, title, text, runs in OVERLAYS:
        if not a.only or any(name.startswith(o) for o in a.only):
            plot_overlay(tool, name, title, text, runs)
            print(name + ".png")


if __name__ == "__main__":
    main()

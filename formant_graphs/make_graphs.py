"""make_graphs.py: plot the Prose 2000's formant movements and coarticulation (FORMANT_SYSTEM.md) as annotated PNGs
in this folder.

    python formant_graphs/make_graphs.py [--tool PATH] [--only NAME ...]

Each example is spoken by formant_trace (built with the C project: build/formant_trace), which runs the firmware's
v3.4.1 pipeline and reports, for every segment, the rule and routines the parameter generator applied, the events of
those routines (closure, burst, voice onset, aspiration, diphthong onglide, vowel reduction), each parameter's
target, onset and locus, and the 10 ms tracks as played. Needs matplotlib.
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

# parameters (REFERENCE §11.4): index, name, track byte -> natural units
P = {"AV": 0, "AF": 1, "AH": 2, "A2": 3, "A3": 4, "A4": 5, "A5": 6, "A6": 7, "AB": 8, "F1": 9, "F2": 10, "F3": 11,
     "F4": 12, "B1": 13, "B2": 14, "B3": 15, "FN": 16, "F0": 17}
CODING = {"F1": lambda b: b * 4, "F2": lambda b: b * 8 + 500, "F3": lambda b: b * 16, "F4": lambda b: b * 16,
          "B1": lambda b: b * 2, "B2": lambda b: b * 2, "B3": lambda b: b * 2, "FN": lambda b: b * 4 + 192}

# the reference categorical palette (dataviz skill), slots 1-4 in fixed order per panel
C1, C2, C3, C4 = "#2a78d6", "#eb6834", "#1baf7a", "#eda100"
COLOUR = {"F1": C1, "F2": C2, "F3": C3, "F4": C4, "FN": C4, "B1": C1,
          "AV": C1, "AF": C2, "AH": C3, "AB": C4}
INK, INK2, MUTED = "#0b0b0b", "#52514e", "#8a8984"

IPA = {"E": "i", "i": "ɪ", "A": "eɪ", "e": "ɛ", "a": "æ", "o": "ɑ", "w": "ɔ", "O": "oʊ", "u": "ʊ", "b": "u",
       "v": "ʌ", "@": "ə", "|": "ɨ", "I": "aɪ", "f": "aʊ", "y": "ɔɪ", "U": "ju", "3": "ɝ", "r": "ɑr", "g": "ɔr",
       "k": "ɛr", "4": "ɪr", "c": "ʊr", "P": "p", "B": "b", "T": "t", "D": "d", "t": "ɾ", "q": "ʔt", "K": "k",
       "G": "g", "C": "tʃ", "J": "dʒ", "F": "f", "V": "v", "X": "θ", "x": "ð", "S": "s", "Z": "z", "s": "ʃ",
       "z": "ʒ", "H": "h", "d": "ɦ", "h": "ʍ", "Q": "ʔ", "M": "m", "m": "m̩", "N": "n", "n": "n̩", "~": "ŋ",
       "L": "l", "j": "ɫ", "l": "l̩", "W": "w", "Y": "j", "R": "r", "p": "ᵊ", "_": ""}
ROUTINE = {"pg_voiceless_onset": "vl. onset", "pg_after_closure": "after cl.", "pg_sonorant_onset": "son. onset",
           "pg_vowel": "vowel", "pg_sonorant_consonant": "glide/liq.", "pg_obstruent_voicing": "obs. voicing",
           "pg_fricative_amps": "fricative", "pg_closure_types": "closure", "pg_stop_burst": "burst",
           "pg_shift_aspiration": "asp. shift", "pg_finalize": None}

# name, title, [texts], options. Options: formants (list), amps (list), extra ("FN" etc.), warm
EXAMPLES = [
    ("01_stop_anatomy", "A voiceless stop: closure, burst, voice onset, and the transitions into the vowel",
     ["a tea."], {}),
    ("02_voicing_b_p", "Voiced /b/ and voiceless /p/ before the same vowel: the voice onset delay and aspiration",
     ["a bee.", "a pea."], {}),
    ("03_place_b_d_g", "Place of articulation: /b d g/ before /ɑ/ start F2 and F3 from different loci",
     ["\\e[1I@Bo1 .", "\\e[1I@Do1 .", "\\e[1I@Go1 ."], {}),
    ("05_velar_front_back", "/k/ before a front and a back vowel: burst, aspiration and loci follow the vowel",
     ["a key.", "a coo."], {}),
    ("06_voiceless_stops", "The three voiceless stops before /ɑ/: burst length and aspiration by place",
     ["\\e[1I@Po1 .", "\\e[1I@To1 .", "\\e[1I@Ko1 ."], {}),
    ("07_fricatives", "Fricatives /s ʃ f θ/: frication, and F2 onsets pulled only a little toward the vowel",
     ["a sea.", "a she.", "a fee.", "a thigh."], {}),
    ("09_nasals", "Nasals /m n ŋ/: the nasal resonator FN, a wide B1, and the jump of F1 at the release",
     ["a me.", "a knee.", "a singer."], {"formants": ["F1", "F2", "F3", "FN"], "amps": ["AV", "AH"],
                                          "extra": ["B1"]}),
    ("10_glides_liquids", "Glides and liquids /w r l j/: 7-frame transitions, and /r l/ targets pulled toward the next vowel",
     ["a way.", "a ray.", "a lay.", "a yea."], {}),
    ("11_diphthongs", "Diphthongs /aɪ ɔɪ aʊ oʊ/: the onglide is held, then moves to the offglide",
     ["\\e[1I@BI1 .", "\\e[1I@By1 .", "\\e[1I@Bf1 .", "\\e[1I@BO1 ."], {}),
    ("12_clusters", "Clusters: /p/ is aspirated in 'pie', unaspirated after /s/, and 'tr' is an affricate",
     ["a pie.", "a spy.", "a try."], {}),
    ("13_flap_glottal", "Allophones of /t/: the flap's formants are the mean of its neighbours'; the glottalized /t/ of 'button' "
     "is a voiced closure",
     ["water.", "button."], {"amps": ["AV", "AF", "AH"]}),
    ("14_anticipation", "Vowel targets shift with the next and previous consonant: 'bet', 'bell' (F2 -300), "
     "'red' (F3 down)",
     ["a bet.", "a bell.", "a red."], {}),
]
LOCUS_STOPS = [("B", "/b/"), ("D", "/d/"), ("G", "/g/")]
LOCUS_VOWELS = ["E", "i", "A", "e", "a", "o", "w", "O", "u", "b", "v", "@"]


def find_tool(path):
    if path:
        return path
    for d in ("build", "build64", "build32"):
        for exe in ("formant_trace.exe", "formant_trace"):
            p = os.path.join(ROOT, d, exe)
            if os.path.exists(p):
                return p
    p = shutil.which("formant_trace")
    if p:
        return p
    sys.exit("formant_trace not found: build the C project (README) or pass --tool")


def run(tool, args):
    return subprocess.run([tool] + args, capture_output=True, text=True, encoding="utf-8", check=True).stdout


def trace(tool, text, warm=True):
    """frames: list of (pos, bytes[22]); segs: list of dicts with the segment's rule, routines, events and structs"""
    out = run(tool, (["-W"] if warm else []) + [text])
    frames, segs = [], []
    for line in out.splitlines():
        f = line.split("\t")
        if f[0] == "F":
            # frames played after the ring is reset at the end of the utterance start again at a low position
            if not frames or int(f[1]) > frames[-1][0]:
                frames.append((int(f[1]), [int(x) for x in f[2:24]]))
        elif f[0] == "S":
            segs.append({"pos": int(f[1]), "ch": f[2], "prev": f[3], "next": f[4], "dur": int(f[5]),
                         "stress": int(f[6]), "flags": int(f[7]), "rule": None, "routines": [], "events": [],
                         "q": {}})
        elif f[0] == "R":
            segs[-1]["rule"] = (int(f[1]), int(f[2]))
        elif f[0] == "C":
            segs[-1]["routines"].append(f[1])
        elif f[0] == "E":
            segs[-1]["events"].append((f[2], int(f[1]), int(f[3]), int(f[4])))
        elif f[0] == "Q":
            v = [int(x) for x in f[1:]]
            segs[-1]["q"][v[0]] = dict(zip(("type", "durB", "durF", "len", "locB", "onset", "target", "weight",
                                            "locus", "pos"), v[1:]))
    return frames, segs


def value(name, b):
    return CODING.get(name, lambda x: x)(b)


def plot_example(ax, bx, rx, tool, text, opts, show_ylabels):
    frames, segs = trace(tool, text, opts.get("warm", True))
    pos0 = frames[0][0]
    ms = lambda pos: (pos - pos0) * 10  # noqa: E731
    for i, s in enumerate(segs):
        s["end"] = segs[i + 1]["pos"] if i + 1 < len(segs) else s["pos"] + s["dur"]
    speech = [s for s in segs if s["ch"] != "_"]
    t0 = ms(speech[0]["pos"]) - 40
    t1 = ms(speech[-1]["end"]) + 80
    fr = [(ms(p), b) for p, b in frames if t0 <= ms(p) <= t1]
    t = [x for x, _ in fr]
    formants = opts.get("formants", ["F1", "F2", "F3"])
    amps = opts.get("amps", ["AV", "AF", "AH"])

    # the formant tracks: solid where the cascade is excited (AV or AH > 0), faint elsewhere
    for name in formants + opts.get("extra", []):
        ys = [value(name, b[P[name]]) for _, b in fr]
        on = [b[P["AV"]] > 0 or b[P["AH"]] > 0 for _, b in fr]
        style = {"FN": "--", "B1": ":"}.get(name, "-")
        ax.plot(t, ys, color=COLOUR[name], lw=0.9, alpha=0.3, ls=style)
        ys_on = [y if o else None for y, o in zip(ys, on)]
        ax.plot(t, [float("nan") if y is None else y for y in ys_on], color=COLOUR[name], lw=2, ls=style,
                label=name if name != "B1" else "B1 (bandwidth)")
    for name in amps:
        bx.plot(t, [b[P[name]] for _, b in fr], color=COLOUR[name], lw=2, label=name)

    ymax = 3300
    for s in speech:
        a, b = ms(s["pos"]), ms(s["end"])
        for axis in (ax, bx, rx):
            axis.axvline(a, color=MUTED, lw=0.6, ls=":", zorder=0)
        label = s["ch"] + (" /" + IPA.get(s["ch"], "") + "/" if IPA.get(s["ch"]) else "")
        if s["stress"] >= 2:
            label = "ˈ" + label
        ax.text((a + b) / 2, ymax - 60, label, ha="center", va="top", fontsize=9, color=INK)
        # targets (dashed), onsets (open circles) and fixed loci (x) of the formants
        for name in formants:
            q = s["q"].get(P[name])
            if not q:
                continue
            c = COLOUR[name]
            qa = ms(q["pos"])
            ax.plot([qa, b], [q["target"], q["target"]], color=c, lw=1, ls=(0, (3, 2)), alpha=0.9)
            if q["type"] in (2, 3, 6, 7) and name != "FN":
                ax.plot([qa], [q["onset"]], marker="o", ms=7, mfc="white", mec=c, mew=1.6, zorder=6)
                prev = [bb for pp, bb in frames if pp == q["pos"] - 1]
                if prev and abs(value(name, prev[0][P[name]]) - q["locus"]) > 60 and name in ("F2", "F3"):
                    ax.plot([qa], [q["locus"]], marker="x", ms=8, color=c, mew=2, zorder=6)
        # events of the routines
        closures = 0
        for ev, epos, x1, x2 in s["events"]:
            e = ms(epos)
            # P and K run pg_stop_burst twice: the second call splits the burst again (still burst frames)
            if ev == "closure" and closures == 0:
                closures += 1
                band(bx, e, e + 10 * x1, "#bdbcb6", "closure", "", 78)
                band(bx, e + 10 * x1, e + 10 * (x1 + x2), "#f3c9b4", "burst", "", 71)
            elif ev == "release":
                band(bx, e, e + 10 * x1, "#c9c3ee", "voice\nonset", "", 64)
            elif ev == "aspiration" and s["ch"] != "_":
                band(bx, e, e + 10 * x1, "white", "early AH", "///", 57)
            elif ev == "onglide" and x1 > 0:
                band(ax, e, e + 10 * min(x1, s["end"] - s["pos"]), "#fbe3a6", "onglide", "", 3050)
        tags = [ROUTINE[r] for r in s["routines"] if ROUTINE.get(r)]
        tags += ["reduce %d%%" % round(100 * x1 / 32768) for ev, _, x1, _ in s["events"] if ev == "reduction"]
        rule = "g%d/%d" % s["rule"] if s["rule"] else ""
        rx.text((a + b) / 2, 0.95, "\n".join([rule] + tags), ha="center", va="top", fontsize=6.5, color=INK2,
                linespacing=1.1)

    ax.set_ylim(0, ymax)
    bx.set_ylim(0, 80)
    for axis in (ax, bx, rx):
        axis.set_xlim(t0, t1)
        for side in ("top", "right"):
            axis.spines[side].set_visible(False)
        axis.tick_params(colors=INK2, labelsize=8)
    ax.grid(axis="y", color="#e6e5e0", lw=0.6)
    bx.grid(axis="y", color="#e6e5e0", lw=0.6)
    rx.set_ylim(0, 1)
    rx.set_yticks([])
    rx.spines["left"].set_visible(False)
    rx.set_xlabel("ms", color=INK2, fontsize=8)
    if show_ylabels:
        ax.set_ylabel("Hz", color=INK2)
        bx.set_ylabel("dB", color=INK2)
        rx.set_ylabel("rule and\nroutines", color=INK2, fontsize=8)
    ax.set_title('"%s"' % text.replace("\\e", "ESC"), fontsize=10, color=INK)


def band(axis, a, b, colour, label, hatch, ytext=78):
    if b <= a:
        return
    axis.axvspan(a, b, color=colour, alpha=1 if hatch else 0.6, hatch=hatch, ec="#9a9994" if hatch else None,
                 lw=0, zorder=0)
    axis.text((a + b) / 2, ytext, label, ha="center", va="top", fontsize=7, color=INK2, zorder=7)


def legend(fig, opts):
    formants = opts.get("formants", ["F1", "F2", "F3"])
    amps = opts.get("amps", ["AV", "AF", "AH"])
    h = [Line2D([], [], color=COLOUR[n], lw=2, ls={"FN": "--"}.get(n, "-"), label=n) for n in formants]
    h += [Line2D([], [], color=COLOUR[n], lw=2, ls=":", label="B1 (bandwidth)") for n in opts.get("extra", [])]
    h += [Line2D([], [], color=INK2, lw=0.9, alpha=0.4, label="not excited (AV = AH = 0)"),
          Line2D([], [], color=INK2, lw=1, ls=(0, (3, 2)), label="segment target"),
          Line2D([], [], color=INK2, lw=0, marker="o", mfc="white", mec=INK2, ms=7,
                 label="onset = W·target + (1−W)·L"),
          Line2D([], [], color=INK2, lw=0, marker="x", ms=8, mew=2,
                 label="locus L, where it is not the value\nthe track shows (a fixed locus, or the\n"
                       "track was blended back later)")]
    h2 = [Line2D([], [], color=COLOUR[n], lw=2, label=n) for n in amps]
    h2 += [Patch(fc="#bdbcb6", alpha=0.6, label="closure"), Patch(fc="#f3c9b4", alpha=0.6, label="burst"),
           Patch(fc="#c9c3ee", alpha=0.6, label="voice onset delay (AV 0)"),
           Patch(fc="white", ec="#9a9994", hatch="///", label="aspiration moved early"),
           Patch(fc="#fbe3a6", alpha=0.6, label="vowel onglide stretch ([EBBC] frames)")]
    return h, h2


def make_example(tool, name, title, texts, opts):
    n = len(texts)
    col = 7.0 if n == 1 else 5.0 if n == 2 else 4.2
    width = col * n + 3.2
    fig, axes = plt.subplots(3, n, figsize=(width, 8.2), squeeze=False,
                             gridspec_kw={"height_ratios": [3.2, 1.8, 0.8], "hspace": 0.12})
    for j, text in enumerate(texts):
        plot_example(axes[0][j], axes[1][j], axes[2][j], tool, text, opts, j == 0)
        axes[0][j].tick_params(labelbottom=False)
        axes[1][j].tick_params(labelbottom=False)
    h, h2 = legend(fig, opts)
    axes[0][-1].legend(handles=h, loc="upper left", bbox_to_anchor=(1.02, 1.0), fontsize=8, frameon=False)
    axes[1][-1].legend(handles=h2, loc="upper left", bbox_to_anchor=(1.02, 1.05), fontsize=8, frameon=False)
    fig.suptitle(title, fontsize=12, color=INK, x=0.02, ha="left")
    fig.subplots_adjust(left=0.7 / width, right=1 - 3.0 / width, top=0.9, bottom=0.07,
                        wspace=0.12)
    fig.savefig(os.path.join(HERE, name + ".png"), dpi=110, facecolor="white")
    plt.close(fig)


def tables(tool):
    t = {"T": {}, "N": None, "D": [], "W": {}}
    for line in run(tool, ["-T"]).splitlines():
        f = line.split("\t")
        if f[0] == "T":
            v = [int(x) for x in f[2:]]
            t["T"][f[1]] = v
        elif f[0] == "N":
            t["N"] = [int(x) for x in f[1:]]
        elif f[0] == "D":
            t["D"].append(int(f[2]))
        elif f[0] == "W":
            t["W"][(int(f[1]), int(f[2]))] = int(f[3])
    return t


VOWELS = ["E", "i", "A", "e", "a", "o", "w", "O", "u", "b", "v", "@", "|", "I", "f", "y", "U", "3", "4", "c", "g",
          "k", "r"]
RCOLOURED = "3 4 c g k r".split()


def make_vowel_space(tool):
    tb = tables(tool)
    fig, (ax, bx) = plt.subplots(1, 2, figsize=(16, 7.4), gridspec_kw={"width_ratios": [1.35, 1]})
    # T fields: 1-3 F1-F3 targets; 12-14 the offglide's F1-F3 (vowels 0-17, which pg_vowel always moves to it)
    for ch in VOWELS:
        v = tb["T"][ch]
        glide = len(v) > 13
        c = C2 if glide else C1
        if ch in RCOLOURED:
            x, y, ox, oy, axis = v[2], v[3], (v[13] if glide else None), (v[14] if glide else None), bx
        else:
            x, y, ox, oy, axis = v[2], v[1], (v[13] if glide else None), (v[12] if glide else None), ax
        axis.plot(x, y, "o", color=c, ms=8, mec="white", mew=1.5, zorder=5)
        axis.annotate("%s /%s/" % (ch, IPA[ch]), (x, y), xytext=(6, -12), textcoords="offset points", fontsize=9,
                      color=INK)
        if glide:
            axis.annotate("", xy=(ox, oy), xytext=(x, y),
                          arrowprops=dict(arrowstyle="-|>", color=C2, lw=1.6, shrinkA=5, shrinkB=2))
    n1, n2, n3 = tb["N"]
    ax.plot(n2, n1, marker="*", color=C3, ms=16, mec="white", zorder=6)
    ax.annotate("neutral vowel\n(reduction target)", (n2, n1), xytext=(10, 8), textcoords="offset points",
                fontsize=9, color=INK2)
    bx.plot(n2, n3, marker="*", color=C3, ms=16, mec="white", zorder=6)
    ax.set_xlim(2400, 500)
    ax.set_ylim(800, 220)
    ax.set_xlabel("F2 (Hz): front vowels on the left", color=INK2)
    ax.set_ylabel("F1 (Hz): high vowels at the top", color=INK2)
    ax.set_title("F1 against F2", fontsize=10, color=INK2, loc="left")
    bx.set_xlim(2400, 500)
    bx.set_ylim(1200, 3000)
    bx.set_xlabel("F2 (Hz)", color=INK2)
    bx.set_ylabel("F3 (Hz)", color=INK2)
    bx.set_title("r-coloured vowels, F3 against F2: the offglide lowers F3 to about 1500-1650 Hz", fontsize=10,
                 color=INK2, loc="left")
    for a in (ax, bx):
        a.grid(color="#e6e5e0", lw=0.6)
        for side in ("top", "right"):
            a.spines[side].set_visible(False)
        a.tick_params(colors=INK2, labelsize=8)
    ax.legend(handles=[Line2D([], [], marker="o", color=C1, lw=0, ms=8, label="no offglide (vowel index ≥ 18)"),
                       Line2D([], [], marker="o", color=C2, lw=1.6, ms=8, label="target → offglide (index < 18)"),
                       Line2D([], [], marker="*", color=C3, lw=0, ms=14, label="neutral vowel 490 / 1450 / 2500 Hz")],
              loc="lower left", fontsize=9, frameon=False)
    fig.suptitle("The vowel targets (DS:944C / 9486 / 94C0) and their offglides (DS:9690 / 96A2 / 96B4), voice 0: "
                 "before reduction and the context rules", fontsize=12, color=INK, x=0.02, ha="left")
    fig.tight_layout()
    fig.savefig(os.path.join(HERE, "15_vowel_space.png"), dpi=110, facecolor="white")
    plt.close(fig)


def make_reduction(tool):
    tb = tables(tool)
    n1, n2 = tb["N"][0], tb["N"][1]
    fig, (ax, bx) = plt.subplots(1, 2, figsize=(13, 5.6), gridspec_kw={"width_ratios": [1, 1.2]})
    # pull k = DS:9884[dur*10/16] for durations 1-30 frames
    durs = list(range(1, 31))
    pull = [tb["D"][min(d * 10, 255) // 16] / 32768 for d in durs]
    ax.plot([d * 10 for d in durs], [100 * k for k in pull], color=C1, lw=2, marker="o", ms=4)
    ax.set_xlabel("vowel duration (ms)", color=INK2)
    ax.set_ylabel("pull toward the neutral vowel (%)", color=INK2)
    ax.set_title("DS:9884[duration·10/16]: the pull by duration", fontsize=10, loc="left", color=INK)
    ax.grid(color="#e6e5e0", lw=0.6)
    for d in (4, 8, 16):
        k = pull[d - 1]
        ax.annotate("%d ms: %.0f %%" % (d * 10, 100 * k), (d * 10, 100 * k), xytext=(12, 8),
                    textcoords="offset points", fontsize=8, color=INK2)
    # where the targets end up for 40, 80, 160 ms
    for i, ch in enumerate(["E", "a", "o", "b", "e"]):
        v = tb["T"][ch]
        c = [C1, C2, C3, C4, "#e87ba4"][i]
        pts = []
        for d in (30, 16, 8, 6, 4, 3, 2, 1):
            k = tb["D"][min(d * 10, 255) // 16] / 32768
            pts.append((v[2] + k * (n2 - v[2]), v[1] + k * (n1 - v[1]), d))
        bx.plot([p[0] for p in pts], [p[1] for p in pts], color=c, lw=2, marker="o", ms=5,
                label="%s /%s/" % (ch, IPA[ch]))
        for x, y, d in pts:
            if d in (30, 8, 4, 1):
                bx.annotate("%d" % (d * 10), (x, y), xytext=(4, 4), textcoords="offset points", fontsize=7,
                            color=INK2)
    bx.plot(n2, n1, marker="*", color=INK, ms=14)
    bx.annotate("neutral", (n2, n1), xytext=(-60, 10), textcoords="offset points", fontsize=9, color=INK2)
    bx.set_xlim(2400, 800)
    bx.set_ylim(780, 240)
    bx.set_xlabel("F2 (Hz)", color=INK2)
    bx.set_ylabel("F1 (Hz)", color=INK2)
    bx.set_title("Where the targets go for 300 → 10 ms vowels (labels: ms)", fontsize=10, loc="left", color=INK)
    bx.grid(color="#e6e5e0", lw=0.6)
    bx.legend(fontsize=8, frameon=False, loc="lower left")
    for a in (ax, bx):
        for side in ("top", "right"):
            a.spines[side].set_visible(False)
        a.tick_params(colors=INK2, labelsize=8)
    fig.suptitle("Vowel reduction: short vowels move toward 490 / 1450 / 2500 Hz (pg_vowel)", fontsize=12,
                 x=0.02, ha="left", color=INK)
    fig.tight_layout()
    fig.savefig(os.path.join(HERE, "16_vowel_reduction.png"), dpi=110, facecolor="white")
    plt.close(fig)


def make_locus_equations(tool):
    fig, axes = plt.subplots(1, 2, figsize=(14, 6.4))
    rows = []
    for (stop, label), c in zip(LOCUS_STOPS, (C1, C2, C3)):
        for fi, (ax, pname) in enumerate(zip(axes, ("F2", "F3"))):
            pts = []
            for v in LOCUS_VOWELS:
                _, segs = trace(tool, "\\e[1I@%s%s1 ." % (stop, v))
                seg = next(s for s in segs if s["ch"] == v and s["prev"] == stop)
                q = seg["q"][P[pname]]
                pts.append((q["target"], q["onset"], v, q["weight"], q["locus"]))
                if fi == 0:
                    rows.append((stop, v, q["target"], q["onset"], q["weight"], q["locus"]))
            ax.plot([p[0] for p in pts], [p[1] for p in pts], "o", color=c, ms=8, mec="white", mew=1.2,
                    label="%s  (W = %s)" % (label, ", ".join(sorted({"%.2f" % (p[3] / 32768) for p in pts}))),
                    zorder=5)
            for x, y, v, _, _ in pts:
                ax.annotate(IPA[v], (x, y), xytext=(4, 4), textcoords="offset points", fontsize=8, color=c)
    for ax, pname in zip(axes, ("F2", "F3")):
        lo = min(ax.get_xlim()[0], ax.get_ylim()[0])
        hi = max(ax.get_xlim()[1], ax.get_ylim()[1])
        ax.plot([lo, hi], [lo, hi], color=MUTED, lw=0.8, ls=":")
        ax.text(hi, hi, "onset = target ", fontsize=8, color=MUTED, va="top", ha="right")
        ax.set_xlim(lo, hi)
        ax.set_ylim(lo, hi)
        ax.set_xlabel("vowel %s target (Hz)" % pname, color=INK2)
        ax.set_ylabel("vowel %s onset (Hz)" % pname, color=INK2)
        ax.grid(color="#e6e5e0", lw=0.6)
        ax.legend(fontsize=9, frameon=False, loc="upper left")
        for side in ("top", "right"):
            ax.spines[side].set_visible(False)
        ax.tick_params(colors=INK2, labelsize=8)
    axes[0].set_title("F2: /d/ always from 1600 Hz (1924 before /i/); /g/ from a locus chosen by the vowel's "
                      "class;\n/b/ 10-20 % of the way from its own locus to the target", fontsize=9, loc="left",
                      color=INK2)
    axes[1].set_title("F3: /d/ from 2620 Hz; /b/ and /g/ from their own F3, which the rules also set by vowel "
                      "class", fontsize=9, loc="left", color=INK2)
    fig.suptitle("Locus equations: the onset of each vowel after /b d g/, for 12 stressed vowels "
                 "(traced; phoneme input @CV1)", fontsize=12, x=0.02, ha="left", color=INK)
    fig.tight_layout()
    fig.savefig(os.path.join(HERE, "04_locus_equations.png"), dpi=110, facecolor="white")
    plt.close(fig)
    return rows


def make_fricative_spectra(tool):
    chans = ["A2", "A3", "A4", "A5", "A6", "AB"]
    words = [("a sea.", "S", "/s/"), ("a she.", "s", "/ʃ/"), ("a fee.", "F", "/f/"), ("a thigh.", "X", "/θ/")]
    fig, ax = plt.subplots(figsize=(11, 5.4))
    width = 0.2
    for i, ((text, ch, label), c) in enumerate(zip(words, (C1, C2, C3, C4))):
        _, segs = trace(tool, text)
        seg = next(s for s in segs if s["ch"] == ch)
        vals = [seg["q"][P[n]]["target"] for n in chans]
        xs = [k + (i - 1.5) * width for k in range(len(chans))]
        ax.bar(xs, vals, width=width - 0.03, color=c, label="%s %s (AF %d dB)" % (label, text,
                                                                               seg["q"][P["AF"]]["target"]))
    ax.set_xticks(range(len(chans)))
    ax.set_xticklabels(["A2\n(at F2)", "A3\n(at F3)", "A4\n(at F4)", "A5\n(F5)", "A6\n(F6)", "AB\n(bypass)"])
    ax.set_ylabel("parallel amplitude target (dB)", color=INK2)
    ax.grid(axis="y", color="#e6e5e0", lw=0.6)
    ax.set_axisbelow(True)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    ax.legend(fontsize=9, frameon=False, loc="upper left")
    ax.set_title("Fricative spectra: the parallel branch's amplitudes for /s ʃ f θ/ before /i/ or /aɪ/ "
                 "(targets after the rules)", fontsize=11, color=INK, loc="left")
    fig.tight_layout()
    fig.savefig(os.path.join(HERE, "08_fricative_spectra.png"), dpi=110, facecolor="white")
    plt.close(fig)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tool")
    ap.add_argument("--only", nargs="*")
    a = ap.parse_args()
    tool = find_tool(a.tool)
    want = lambda n: not a.only or any(n.startswith(o) for o in a.only)  # noqa: E731
    for name, title, texts, opts in EXAMPLES:
        if want(name):
            make_example(tool, name, title, texts, opts)
            print(name)
    if want("04_locus_equations"):
        for row in make_locus_equations(tool):
            print("\t".join(str(x) for x in row))
        print("04_locus_equations")
    if want("08_fricative_spectra"):
        make_fricative_spectra(tool)
        print("08_fricative_spectra")
    if want("15_vowel_space"):
        make_vowel_space(tool)
        print("15_vowel_space")
    if want("16_vowel_reduction"):
        make_reduction(tool)
        print("16_vowel_reduction")


if __name__ == "__main__":
    main()

"""gen_contexts.py: writes contexts.tsv, the phoneme contexts the v3.4.1 parameter generator distinguishes
(VOICE_CONTEXTS.md). It reads the ROM tables from src/data/prose_data.c, so it needs no ROM files.

    python voice/gen_contexts.py

Three kinds of rows:
- rule:    the rule table (DS:8CFE, 88 groups, 621 rules). Every (prev, cur, next) triple is run through the rule
           engine as paramgen_apply_rules does, together with the stress of next and, for the affricate groups, the
           sound after next; the triples that select each rule are then written as a few rows of sets.
- release: the stop release table (burst and voice onset time by the class of the next sound, 9 x 8 cells).
- code:    contexts tested with fixed numbers in the context routines (src/paramgen/pg_context.c, pg_loci.c).
"""
import collections
import functools
import os
import re
import struct

HERE = os.path.dirname(os.path.abspath(__file__))


def load_ds():
    src = open(os.path.join(HERE, "..", "src", "data", "prose_data.c"), encoding="utf-8").read()
    body = src.split("const uint8_t prose_ds_data[")[1].split("};")[0].split("{", 1)[1]
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    data = bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", body))
    return data + b"\xFF" * (0x10000 - len(data))


DS = load_ds()
def rb(a): return DS[a & 0xFFFF]
def rsb(a): v = rb(a); return v - 256 if v > 127 else v
def rw(a): return struct.unpack_from("<h", DS, a & 0xFFFF)[0]
def ruw(a): return struct.unpack_from("<H", DS, a & 0xFFFF)[0]

# the 58 phoneme codes in index order (DS:93CA); the pause is written _ in the TSV
CODES = "4AEIOUabcefgikruwy3@ov|jlLWYRpdhHBPDTtJCGKQqVFxXZSzsMmNn~ "
PMAP = rw(0x944A)
assert [rsb(PMAP + ord(c)) for c in CODES] == list(range(58))
def idx(c): return rsb(PMAP + ord(c))
def feat(c, plane, mask): return rb(0xA8 + (plane | ord(c))) & mask
def name(c): return "_" if c == " " else c
def fmt(cs): return " ".join(name(c) for c in CODES if c in cs)

PNAMES = "AV AF AH A2 A3 A4 A5 A6 AB F1 F2 F3 F4 B1 B2 B3 FN F0 p18 p19 p20 p21".split()
VARS = {0xEBB2: "aspShift", 0xEBB4: "VOT", 0xEBB6: "burstLen", 0xEBC4: "aspAH", 0xEBC6: "burstAV",
        0xEBC8: "burstAF", 0xEBCA: "burstAH"}
FIELDS = {0: ".type", 4: ".durF", 12: ""}


def target_name(a):
    if 0xDE22 <= a < 0xDE22 + 14 * 22:
        p, f = divmod(a - 0xDE22, 14)
        return PNAMES[p] + FIELDS.get(f, ".%d" % f)
    if 0xEB52 <= a < 0xEB52 + 44:
        return "W[%s]" % PNAMES[(a - 0xEB52) // 2]
    return VARS.get(a)  # None: a variable nothing reads (DS:EBB0)


def action_sets(lst):
    names = []
    while lst:
        e = ruw(lst)
        lst += 2
        if e == 0:
            break
        while True:
            op, a = rb(e) & 0x7F, ruw(e + 4)
            n = target_name(a) if op in (0, 1, 2, 7) else None
            if n and n not in names:
                names.append(n)
            e += 6
            if not rb(e - 6) & 0x80:
                break
    return names


# ---- the rule engine (paramgen_segment's context flags + rule_condition), on a synthetic window ----
def cls(c):  # CLASS_CUR/CLASS_PREV: 0 vowel, 3 closure, 1 sonorant, 2 other
    return 0 if feat(c, 0x100, 2) else 3 if feat(c, 0x100, 1) else 1 if feat(c, 0, 2) else 2


def flags(prev, cur, nxt, nstress):
    f = [0] * 11
    f[10] = nstress
    f[0] = 1 if feat(cur, 0, 4) else 0
    f[{0: 1, 3: 2, 1: 3, 2: 4}[cls(prev)]] = 1
    if (f[2] and feat(prev, 0, 0x10)) or f[1]:
        f[3] = 1
    if cls(cur) != 0:
        if feat(cur, 0, 0x20) and (feat(nxt, 0x100, 1) or nxt == " "):  # a glottal onset (bit 6) also counts
            f[5] = 1
        k = rsb(0x98C2 + idx(nxt))
        if k in (0, 1, 2, 3):
            f[{0: 6, 1: 7, 3: 8, 2: 9}[k]] = 1
    return f


def condition(c, win, f):
    uses_nn = False
    while True:
        head = rsb(c)
        want, kind, node = head & 1, head >> 3, (head & 6) >> 1
        if node == 2:
            uses_nn = True
        ch = win[node]
        if kind == 0:
            ok = f[rsb(c + 1)] == 1
            c += 2
        elif kind == 1:
            m = ruw(0x48 + 2 * (rsb(c + 1) & 0x7F))
            ok = ch is not None and (m & 0xFF & rb(0xA8 + (((m & 0xFF00) >> 1) | ord(ch)))) != 0
            c += 2
        else:
            chars = ""
            while rsb(c + 1) > 0x18:
                c += 1
                chars += chr(rb(c))
            c += 1
            ok = ch is not None and ch in chars
        if ok != bool(want):
            return False, uses_nn
        if rb(c) == 0x18:
            return True, uses_nn


def group_of(cur, prev): return rb(rw(0x9376 + 2 * idx(cur)) + idx(prev))


@functools.lru_cache(None)
def rules(g):
    r = ruw(0x8CFE + 2 * g)
    return [(ruw(r + 8 * i), action_sets(ruw(r + 8 * i + 2)) + action_sets(ruw(r + 8 * i + 6)))
            for i in range(rsb(0x8DAE + g))]


@functools.lru_cache(None)
def uses_next2(g):
    for cond, _ in rules(g):
        c = cond
        while cond:
            head = rsb(c)
            if (head & 6) >> 1 == 2:
                return True
            if head >> 3 == 2:
                while rsb(c + 1) > 0x18:
                    c += 1
                c += 1
            else:
                c += 2
            if rb(c) == 0x18:
                break
    return False


def fire(prev, cur, nxt, nstress, nn):
    g = group_of(cur, prev)
    f = flags(prev, cur, nxt, nstress)
    for i, (cond, _) in enumerate(rules(g)):
        if cond == 0 or condition(cond, (cur, nxt, nn, None), f)[0]:
            return g, i
    raise ValueError("no rule")


def factor(tuples):
    """Rows of sets (prev, cur, next, stress, next2) covering exactly the given (p, c, n, s, nn) tuples."""
    step = collections.defaultdict(set)
    for p, c, n, s, nn in tuples:
        step[(p, c, n, s)].add(nn)
    step2 = collections.defaultdict(set)
    for (p, c, n, s), nns in step.items():
        step2[(p, c, n, frozenset(nns))].add(s)
    step3 = collections.defaultdict(set)
    for (p, c, n, nns), ss in step2.items():
        step3[(c, n, frozenset(ss), nns)].add(p)
    step4 = collections.defaultdict(set)
    for (c, n, ss, nns), ps in step3.items():
        step4[(c, ss, nns, frozenset(ps))].add(n)
    step5 = collections.defaultdict(set)
    for (c, ss, nns, ps), ns in step4.items():
        step5[(ss, nns, ps, frozenset(ns))].add(c)
    return [(ps, frozenset(cs), ns, ss, nns) for (ss, nns, ps, ns), cs in step5.items()]


def rule_rows():
    hits = collections.defaultdict(list)
    for cur in CODES:
        for prev in CODES:
            g = group_of(cur, prev)
            nn_list = list(CODES) if uses_next2(g) else [None]
            for nxt in CODES:
                for s in (0, 1):
                    for nn in nn_list:
                        hits[fire(prev, cur, nxt, s, nn)].append((prev, cur, nxt, s, nn))
    return hits


CTX = ["cur voiced", "prev vowel", "prev closure", "prev voiced", "prev other", "cur released",
       "next V0", "next V1", "next V3", "next V2", "next stressed"]
FEATS = {(0, 1): "syllabic", (0, 2): "sonorant", (0, 0x20): "stop", (0, 0x40): "fricative", (2, 1): "closure",
         (2, 2): "vowel", (2, 0x20): "front vowel", (3, 0x20): "back/rounded", (4, 1): "high"}


def cond_text(c):
    if c == 0:
        return "otherwise"
    out = []
    while True:
        head = rsb(c)
        neg = "" if head & 1 else "not "
        node = ["cur", "next", "next2", "prev2"][(head & 6) >> 1]
        if head >> 3 == 0:
            out.append(neg + CTX[rsb(c + 1)])
            c += 2
        elif head >> 3 == 1:
            m = ruw(0x48 + 2 * (rsb(c + 1) & 0x7F))
            out.append("%s%s %s" % (neg, node, FEATS.get((m >> 8, m & 0xFF), "feature %X" % m)))
            c += 2
        else:
            chars = ""
            while rsb(c + 1) > 0x18:
                c += 1
                chars += chr(rb(c))
            c += 1
            out.append("%s%s in {%s}" % (neg, node, " ".join(name(ch) for ch in chars)))
        if rb(c) == 0x18:
            return " and ".join(out)


# ---- stop release rows (paramgen_load_targets: DS:98DB[stop] + class of next, tables DS:99DD ... 9A6D) ----
def next_class_set(k): return {c for c in CODES if rsb(0x98C2 + idx(c)) == k}


CLASS_NAMES = ["V0 (high front vowel)", "V1 (other front vowel)", "V2 (back rounded vowel)",
               "V3 (central/low vowel)", "glide or liquid", "release vocoid p", "fricative", "h-like sound"]


def release_rows():
    rows = []
    for stop in "BPDTtJCGK":
        base = rsb(0x98DB + idx(stop))
        for k in range(8):
            vals = [rsb(t + base + k) for t in (0x99DD, 0x9905, 0x994D, 0x9A25, 0x9A6D, 0x9995)]
            sets = [n for n, v in zip(("burstAV", "burstAF", "burstAH", "burstLen", "VOT", "AB"), vals) if v != 0x7F]
            rows.append(("REL.%s.%d" % (stop, k), "release", None, ANY, {stop}, next_class_set(k), None, "*",
                         " ".join(sets) or "-", "stop release into a " + CLASS_NAMES[k]))
    return rows


# ---- named sets for the contexts tested in the routines ----
def F(plane, mask): return {c for c in CODES if feat(c, plane, mask)}


ANY = set(CODES)
VOWEL = F(0x100, 2)            # the 23 vowels and p
FRONT = F(0x100, 0x20)
BACK = F(0x180, 0x20)          # back/rounded
HIGH = F(0x200, 1)
STOP = F(0, 0x20)              # B P D T J C G K q
FRIC = F(0, 0x40)
NASAL = F(0, 0x10)
SON = F(0, 2)
SYLL = F(0, 1)
VOICED = F(0, 4)
VOICELESS = ANY - VOICED       # includes the pause
CLOSURE = F(0x100, 1)
ALV = F(0x100, 4)
LAB = F(0x100, 0x10)
VELAR = F(0x100, 0x80)
LAT = F(0x100, 0x40)
RHOT = F(0x100, 8)             # 3 R
GLIDE = F(0x180, 1)
PAL = F(0x180, 4)
FULLV = F(0x180, 0x10)
YOFF = F(0x180, 0x40)          # A E I y
WOFF = F(0x200, 4)             # O U b f
RCOL = F(0x200, 2)             # 4 c g k r
V = [next_class_set(k) for k in range(4)]
P = {" "}
def L(s): return set(s)


# Contexts tested with fixed values in the routines (src/paramgen/pg_context.c, pg_loci.c; VOICE_CONTEXTS.md §5.4).
# (id, prev2, prev, cur, next, next2, stress, sets, note). None = any; stress: cur+/cur-/prev+/prev-/next+/next-.
CODE = [
    # pg_after_closure (pg_context.c:26-73)
    ("after_closure.lab", None, L("BPMm"), ANY - CLOSURE, None, None, "*", "F1-FN.durF", "out of a labial closure: 6 frames"),
    ("after_closure.alv", None, L("DTtNnQq"), ANY - CLOSURE, None, None, "*", "F1-FN.durF", "out of an alveolar closure: 8 frames"),
    ("after_closure.vel", None, L("JCGK~"), ANY - CLOSURE, None, None, "*", "F1-FN.durF", "out of a palatal/velar closure: 10 frames"),
    ("after_closure.stop_V", None, STOP, VOWEL, None, None, "*", "W[F2] W[F3]", "F2/F3 start from the burst locus"),
    ("after_closure.T3", None, L("T"), L("3"), None, None, "*", "W[F4]", ""),
    ("after_closure.Kp", None, L("K"), L("p"), None, None, "*", "A2-A6.durF", ""),
    ("after_closure.s@", None, None, L("s"), L("@"), None, "*", "AF.durF A3-A6.durF", "also P s @"),
    # pg_sonorant_onset (pg_context.c:78-130)
    ("sonorant_onset.st", L("Sj"), L("T"), VOWEL - L("p"), None, None, "cur-", "VOT", "st, lt clusters: no aspiration"),
    ("sonorant_onset.T_n3", None, L("T"), L("n3"), None, None, "*", "VOT", "VOT 2"),
    ("sonorant_onset.vfric_glide", None, L("VxZz"), GLIDE, None, None, "*", "VOT aspAH", "release after a voiced fricative"),
    ("sonorant_onset.asp_stressed", None, L("PTCK"), VOWEL, None, None, "prev+", "len", "vowel lengthened by VOT/2"),
    ("sonorant_onset.after_glide", None, GLIDE, ANY, None, None, "*", "F1-FN.durF", "7 frames"),
    ("sonorant_onset.Y_back", None, L("Y"), BACK, None, None, "*", "F1-FN.durF", "11 frames"),
    ("sonorant_onset.after_R", None, L("R"), ANY, None, None, "*", "F3.durF", ""),
    ("sonorant_onset.offglide", None, WOFF | YOFF, ANY - LAB, None, None, "*", "W[F1] W[F2] W[F3]", "after a w- or y-offglide vowel"),
    ("sonorant_onset.pal_back", None, PAL, BACK, None, None, "*", "F2.durF", ""),
    ("sonorant_onset.keep_TV", None, L("T"), VOWEL, None, None, "*", "A2-AB", "parallel amplitudes carried over"),
    ("sonorant_onset.keep_K", None, L("K"), V[2] | V[3] | L("alWLYp"), None, None, "*", "A2-AB", ""),
    ("sonorant_onset.keep_Pi", None, L("P"), L("i"), None, None, "*", "A2-AB", ""),
    ("sonorant_onset.keep_X", None, L("X"), VOWEL | L("R"), None, None, "*", "A2-AB", ""),
    ("sonorant_onset.PR", None, L("P"), L("R"), VOWEL, None, "next+", "A2-AB", ""),
    ("sonorant_onset.Tn", None, L("T"), L("n"), None, None, "*", "A2-AB", ""),
    # pg_release_onset (pg_context.c:750-889)
    ("release.fric_nonsyl", None, FRIC & VOICELESS, ANY - SYLL, None, None, "*", "AH.len", ""),
    ("release.T_rhotic", None, L("T"), RHOT, None, None, "*", "aspAH", ""),
    ("release.T_V3", None, L("T"), V[3] - L("3"), None, None, "*", "aspAH AF", ""),
    ("release.T_V", None, L("T"), VOWEL | L("Wn"), None, None, "*", "aspAH AF F0", ""),
    ("release.K_V02", None, L("K"), V[0] | V[2], None, None, "*", "aspAH AF F0", ""),
    ("release.K_a", None, L("K"), L("a"), None, None, "*", "aspAH AF F0", ""),
    ("release.K_V1", None, L("K"), V[1] - L("a"), None, None, "*", "aspAH AF F0", ""),
    ("release.K_V3", None, L("K"), V[3], None, None, "*", "aspAH AF F0", ""),
    ("release.K_W", None, L("K"), L("W"), None, None, "*", "VOT AF F0", ""),
    ("release.K_Y", None, L("K"), L("Y"), None, None, "*", "VOT AF F0", ""),
    ("release.K_L", None, L("K"), L("L"), None, None, "*", "AF F0", ""),
    ("release.P_L", None, L("P"), L("L") | V[1], None, None, "*", "AF F0", ""),
    ("release.p", None, L("TKP"), L("p"), None, None, "*", "AF", "release vocoid: 3 frames of frication"),
    ("release.unstressed", None, L("PTCK"), ANY, None, None, "prev-", "aspAH VOT", "less aspiration, VOT 2"),
    # pg_vowel (pg_context.c:196-293)
    ("vowel.voiced_p", None, L("BDJGq"), L("p"), None, None, "*", "AV", ""),
    ("vowel.Xp", None, L("X"), L("p"), None, None, "*", "AV", ""),
    ("vowel.lax_velar", None, None, L("aei|"), VELAR, None, "*", "F2 offglide", "no F2 offglide before a velar"),
    ("vowel.uj", None, None, L("u"), L("j"), None, "*", "F2", "no diphthong"),
    ("vowel.Rb", None, L("R"), L("b"), None, None, "*", "F2", "F2 1164"),
    ("vowel.bV", None, None, L("b"), VOWEL, None, "*", "F2", "F2 1164"),
    ("vowel.after_rhotic", None, RHOT, VOWEL, ANY - LAT, None, "*", "F2 F3 offglide", "F3 (front vowels also F2) lowered"),
    ("vowel.lateral", None, None, FRONT | YOFF, LAT, None, "*", "F2 offglide", "F2 -300 before a lateral"),
    ("vowel.U_alv", None, None, L("U"), L("DTq"), None, "*", "F2 offglide", ""),
    ("vowel.velar_E", None, L("GK"), L("E"), None, None, "*", "F3 F4 A5.type", ""),
    ("vowel.Po", None, L("P"), L("o"), None, None, "*", "A2.type A3.type", ""),
    ("vowel.EUY_back", None, L("EUY"), BACK, None, None, "*", "F2.durF", ""),
    ("vowel.yoff_alv", None, None, YOFF, ALV, None, "*", "F2 offglide", ""),
    ("vowel.Kp", None, L("K"), L("p"), None, None, "*", "F2 F3", ""),
    ("vowel.stressed", None, None, VOWEL, None, None, "cur+", "AV onglide", "AV +2"),
    ("vowel.unstressed", None, None, VOWEL, None, None, "cur-", "AV onglide", "AV -3; r-coloured onglide closer to the offglide"),
    ("vowel.EU_nonlabial", None, ANY - LAB, L("EU"), None, None, "*", "F2.durF", ""),
    ("vowel.velar_V", None, L("GK"), VOWEL, None, None, "*", "F4.type W[F4]", ""),
    ("vowel.G_V23", None, L("G"), V[2] | V[3], None, None, "*", "F1.type", ""),
    # pg_sonorant_consonant (pg_context.c:300-375)
    ("sonorant.pause_voiced", None, P, SON - VOWEL, None, None, "*", "AV", "AV 56 after a pause"),
    ("sonorant.velar_R", None, L("G~"), L("R"), None, None, "*", "AV F2 F3", ""),
    ("sonorant.R_V2", None, ANY - L("K"), L("R"), V[2], None, "*", "AV F2 F3", ""),
    ("sonorant.R_V0", None, ANY - L("K"), L("R"), V[0], None, "*", "AV F2 F3", "F2 a quarter of the way to the next"),
    ("sonorant.PR_unstressed", None, L("P"), L("R"), VOWEL, None, "next-", "F2 F3", ""),
    ("sonorant.tr", L("C"), L("s"), L("R"), None, None, "*", "F2 F3", "tr = C s R"),
    ("sonorant.velar_L", None, VELAR, L("L"), None, None, "*", "AV F2", ""),
    ("sonorant.KW", None, L("K"), L("W"), None, None, "*", "F2", "F2 800"),
    ("sonorant.GY", None, L("G"), L("Y"), None, None, "*", "F2 F3", ""),
    ("sonorant.son_lateral", None, SON - NASAL, LAT, None, None, "*", "W[F1] W[F2] W[F3]", ""),
    ("sonorant.glide_glide", None, GLIDE, GLIDE, None, None, "*", "F1-FN.durF", "5 frames"),
    ("sonorant.pause_WE", None, P, L("W"), L("E"), None, "*", "B3", ""),
    ("sonorant.n__", None, None, L("n"), P, P, "*", "F2-F4.durF", ""),
    ("sonorant.Dn", None, L("D"), L("n"), None, None, "*", "F2.type", ""),
    # pg_obstruent_voicing (pg_context.c:379-451)
    ("voicing.voice_bar", None, None, STOP & VOICED, ANY - VOWEL, None, "*", "AV", "voiced stop not before a vowel"),
    ("voicing.after_voiceless", None, VOICELESS, STOP & VOICED, None, None, "*", "AV", "voice bar off"),
    ("voicing.palatal", None, None, PAL - SON, None, None, "*", "AF.durF", ""),
    ("voicing.affr_voiceless", None, VOICELESS, L("JC"), None, None, "*", "AV", ""),
    ("voicing.before_son", None, None, FRIC, SON, None, "*", "AF.len", "frication overlaps the sonorant"),
    ("voicing.DV", None, ANY - P - L("S"), L("D"), VOWEL, None, "next+", "AV", "AV 43"),
    ("voicing.D_flap", None, SYLL, L("D"), VOWEL - L("p"), None, "next-", "A3 A4", "flap-like d between vowels"),
    ("voicing.BV", None, None, L("B"), VOWEL, None, "*", "AV", ""),
    ("voicing.s@", None, None, L("s"), L("@"), None, "*", "AF", ""),
    ("voicing.S_BG", None, L("S"), L("BG"), None, None, "*", "AV", "unaspirated p k after s"),
    ("voicing.GV", None, None, L("G"), VOWEL, None, "*", "p18", ""),
    ("voicing.Jz", None, None, L("J"), L("z"), V[1], "*", "burstAF burstAH AV F1 F2 F3 F4", ""),
    ("voicing.CsR", None, None, L("C"), L("s"), L("R"), "cur+", "burstAF burstAH A3 A6", ""),
    # pg_fricative_amps (pg_context.c:455-566)
    ("fricative.after_pause", None, P, FRIC, None, None, "*", "A2-A6.type", ""),
    ("fricative.before_pause", None, None, FRIC & VOICELESS, P, None, "*", "AV", ""),
    ("fricative.Jz", None, L("J"), L("z"), None, None, "*", "AV AF", ""),
    ("fricative.voiceless_nonV", None, None, FRIC & VOICELESS, ANY - VOWEL, None, "*", "AH", ""),
    ("fricative.S_unstressed", None, None, L("S"), VOWEL, None, "next-", "AF AH A2-A6 AB", ""),
    ("fricative.voiced_S", None, VOICED, L("S"), None, None, "*", "AV", ""),
    ("fricative.Z_stop", None, None, L("Z"), STOP, None, "*", "A4-A6 AV", ""),
    ("fricative.pause_Z_son", None, P, L("Z"), SON, None, "*", "A4 A6", ""),
    ("fricative.V_Z_son", None, VOWEL, L("Z"), SON, None, "*", "AF A2-A6", ""),
    ("fricative.Z_son", None, ANY - VOWEL - P, L("Z"), SON, None, "*", "A4-A6 p18", ""),
    ("fricative.Jz_V1", None, L("J"), L("z"), V[1], None, "*", "AV AF AH A3-A5 F1-F4 p18", ""),
    ("fricative.CsR", None, L("C"), L("s"), L("R"), None, "*", "AF AH A6 F2 F3", "stressed: AF A3"),
    ("fricative.Cs_", None, L("C"), L("s"), P, None, "*", "AF F2 F3 F4", ""),
    # pg_closure_types (pg_context.c:570-627)
    ("closure.after_aspirated", None, L("PTC"), CLOSURE, None, None, "*", "F1-FN.durF", "5 frames"),
    ("closure.after_lateral", None, LAT, CLOSURE, None, None, "*", "F2.type F3.type", ""),
    ("closure.EC", None, L("E"), L("C"), None, None, "*", "F1.type", ""),
    ("closure.PT", None, None, L("P"), L("T"), None, "*", "burstAV burstAF burstAH burstLen A2", ""),
    ("closure.P_fric", None, None, L("P"), FRIC, None, "*", "burstLen", ""),
    ("closure.K_closure", None, None, L("K"), CLOSURE - STOP, None, "*", "burstAF burstLen VOT AB", "K before a nasal, t or Q"),
    ("closure.VK", None, FULLV, L("K"), ANY - CLOSURE, None, "*", "F2.type", ""),
    ("closure.GY", None, None, L("G"), L("Y"), None, "*", "AV.type", ""),
    # pg_stop_burst (pg_context.c:635-738)
    ("burst.voiced_Z", None, None, STOP & VOICED, L("Z"), None, "*", "burstAV", ""),
    ("burst.SB", None, L("S"), L("B"), None, None, "*", "burstAF burstLen", ""),
    ("burst.GZ", None, None, L("G"), L("Z"), None, "*", "burstAF burstAH AB burstLen", ""),
    ("burst.SGR", None, L("S"), L("G"), L("R"), None, "*", "burstAF AV", ""),
    ("burst.SD", None, L("S"), L("D"), VOWEL, None, "next+", "burstAF burstAH burstLen", ""),
    ("burst.G_unstressed", None, None, L("G"), VOWEL, None, "next-", "burstAV burstLen", ""),
    ("burst.decay", None, None, STOP, None, None, "cur-", "burst", "per-frame decay: K 1, G, others 3"),
    ("burst.decay_nasal", None, NASAL, STOP, None, None, "cur-", "burst", "6 per frame after a nasal"),
    ("burst.back_K", None, VOWEL & BACK, L("K"), None, None, "*", "burstLen", "K next to a back vowel"),
    ("burst.K_back", None, None, L("K"), VOWEL & BACK, None, "*", "burstLen", ""),
    ("burst.prevoice", None, P, L("BD"), None, None, "*", "AV", "prevoicing after a pause"),
    ("burst.VTV", None, VOWEL, L("T"), VOWEL, None, "*", "AV", "intervocalic t"),
    ("burst.voiced_BD", None, VOICED - P, L("BD"), None, None, "*", "AV", ""),
    ("burst.Dp", None, None, L("D"), L("p"), None, "*", "AF", ""),
    # pg_locus_weights (pg_loci.c:9-285)
    ("locus.rhotic_into", None, RHOT, ANY - VOWEL - P, None, None, "*", "W[F2] W[F3]", ""),
    ("locus.palatal_obstruent", None, ANY - P, PAL - SON, None, None, "*", "W[F2] F2.durF", ""),
    ("locus.G", None, ANY - P, L("G"), None, None, "*", "W[F1] F1.durB", ""),
    ("locus.Vq", None, VOWEL, L("q"), None, None, "*", "W[F1] F1.durB", ""),
    ("locus.T3", None, None, L("T"), L("3"), None, "*", "W[F2] W[F3] W[F4]", ""),
    ("locus.Cs_", None, None, L("C"), L("s"), P, "*", "W[F2] W[F3]", ""),
    ("locus.alveolar", None, ANY - LAT - RHOT, CLOSURE & ALV, None, None, "*", "L[F2] L[F3]", "F2 1600, F3 2620"),
    ("locus.lateral_alv", None, LAT, CLOSURE & ALV, None, None, "*", "L[F2]", "F2 1050"),
    ("locus.rhotic_alv", None, RHOT, CLOSURE & ALV, None, None, "*", "L[F3]", "F3 2300"),
    ("locus.labial_velar", None, LAB, L("K~"), None, None, "*", "L[F3]", ""),
    ("locus.ng_obstruent", None, None, L("~"), ANY - SON, None, "*", "L[F2] L[F3]", "after a front vowel F2 1200, F3 2500"),
    ("locus.ng_sonorant", None, None, L("~"), SON, None, "*", "L[F2] L[F3]", "F2 900, F3 2176"),
    ("locus.VK", None, FULLV, L("K"), None, None, "*", "W[F2]", ""),
    ("locus.pause_nasal", None, P, NASAL, None, None, "*", "W[AV] AV.durF", ""),
    ("locus.son_nasal", None, SON, NASAL, None, None, "*", "W[FN] W[B1] L[B1] W[F3]", ""),
    ("locus.high_Nn", None, L("bcuWYh"), L("Nn"), None, None, "*", "L[F2]", "F2 1670"),
    ("locus.nonfront_Mm", None, ANY - FRONT, L("Mm"), None, None, "*", "W[F2] L[F2]", "F2 700"),
    ("locus.s_", None, ANY - FRONT, L("s"), P, None, "*", "F3.type", ""),
    ("locus.rhotic_cur", None, None, RHOT, None, None, "*", "W[F2] W[F3]", ""),
    ("locus.after_palatal", None, PAL - SON, ANY, None, None, "*", "W[F2] F2.durF", ""),
    ("locus.fric_V", None, FRIC, VOWEL, None, None, "*", "W[F2] W[F3]", ""),
    ("locus.S_V3", None, L("S"), V[3], None, None, "*", "W[F2] W[F3]", "0.1 into a central vowel"),
    ("locus.nasal_obstruent", None, NASAL, ANY - SON, None, None, "*", "F2-F4.durF B1-FN.durF", ""),
    ("locus.labial_into", None, LAB & CLOSURE, ANY - CLOSURE, None, None, "*", "W[F2] W[F3]", ""),
    ("locus.labial_front", None, LAB & CLOSURE, FRONT, None, None, "*", "W[F2] W[F3]", ""),
    ("locus.labial_rhotic", None, LAB & CLOSURE, RHOT, None, None, "*", "W[F2] L[F3]", "F3 1750"),
    ("locus.alveolar_into", None, ALV & CLOSURE, ANY - CLOSURE, None, None, "*", "L[F2] L[F3]", ""),
    ("locus.velar_into", None, VELAR, ANY - CLOSURE, None, None, "*", "W[F1] L[F2] L[F3]", ""),
    ("locus.nasal_son", None, NASAL, SON, None, None, "*", "W[F1] W[F4] L[F2] L[F3] L[F4]", ""),
    ("locus.ng_p_", None, L("~"), L("p"), P, None, "*", "F2-FN", ""),
    ("locus.F0", None, VOICED, SYLL, None, None, "cur+ prev+", "W[F0]", "F0 between stressed syllables"),
    # pg_consonant_loci (pg_loci.c:288-336)
    ("loci.alvstop_W", None, L("DTq"), L("W"), None, None, "*", "L[F2] L[F3] L[F4]", "1200/2050/2500 Hz"),
    ("loci.rhotic_K", None, RHOT | RCOL, L("K~"), ANY - VOWEL, None, "*", "F2 F3", "F2 1700, F3 1900"),
    ("loci.rhotic_alv", None, RHOT | RCOL, L("DtqNn"), None, None, "*", "F2 F3 F4", ""),
    ("loci.rhotic_V", None, RHOT | RCOL, VOWEL, None, None, "*", "F3.durF", ""),
    ("loci.RE", None, L("R"), L("E"), None, None, "*", "AV.durF AV", ""),
    ("loci.Nn_EUY", None, L("Nn"), L("EUY"), None, None, "*", "L[F2]", "F2 1850"),
    ("loci.yoff_alv", None, YOFF, ALV - L("T"), None, None, "*", "F2", "F2 1900"),
    ("loci.alv_rhotic", None, ALV - L("D"), RHOT, None, None, "*", "L[F3]", "F3 2100"),
    ("loci.lab_rhotic", None, LAB, RHOT, None, None, "*", "L[F3]", "F3 1700"),
    # pg_apply_loci (pg_loci.c:339-563)
    ("apply.flap", None, None, L("t"), None, None, "*", "F1 F2 F3", "mean of target, last and next"),
    ("apply.EUY", None, L("EUY"), ANY - NASAL - LAB - VELAR - L("xX"), None, None, "*", "F2.durF", ""),
    ("apply.voiceless_J", None, VOICELESS, L("J"), None, None, "*", "AV.durF", ""),
    ("apply.Gp", None, L("G"), L("p"), None, None, "*", "F2.durB", ""),
    ("apply.pause_F", None, P, L("F"), None, None, "*", "AF AB AF.durF AB.durF", ""),
    ("apply.ZS_V", None, L("ZS"), VOWEL, None, None, "*", "F2/F3.durB/durF A2-A6.durF", ""),
    ("apply.VFxX_V", None, L("VFxX"), VOWEL, None, None, "*", "F2/F3.durB AV.durF", ""),
    ("apply.s_V", None, L("s"), VOWEL, None, None, "*", "AF.durF", ""),
    ("apply.V_ZS", None, VOWEL, L("ZS"), None, None, "*", "AV.durB F2/F3.durB/durF", ""),
    ("apply.V_VFxX", None, VOWEL, L("VFxX"), None, None, "*", "AV.durB F2/F3.durB/durF", ""),
    ("apply.D_front", None, L("D"), FRONT, None, None, "*", "F2/F3.durB/durF", ""),
    ("apply.D_back", None, L("D"), L("oub"), None, None, "*", "AV.durF F2/F3.durF", ""),
    ("apply.D_V0", None, L("D"), V[0], None, None, "*", "F2 onset", "F2 onset 1924"),
    ("apply.T_V01", None, L("T"), V[0] | V[1], None, None, "*", "F3 onset", "F3 2700"),
    ("apply.BP_V", None, L("BP"), VOWEL, None, None, "*", "F1-F3.durF AV.durF", ""),
    ("apply.velar", None, L("GK"), ANY, None, None, "*", "F2/F3.durB AV.durF", ""),
    ("apply.K_bu", None, L("K"), L("bu"), None, None, "*", "F3.durF", ""),
    ("apply.KE", None, L("K"), L("E"), None, None, "*", "F2/F3.durF", ""),
    ("apply.G_V23", None, L("G"), V[2] | V[3], None, None, "*", "F1-F4.durF", ""),
    ("apply.Go", None, L("G"), L("o"), None, None, "*", "AV.durF", ""),
    ("apply.GE", None, L("G"), L("E"), None, None, "*", "AV.durF F3.durF", ""),
    ("apply.lateral", None, LAT, ANY, None, None, "*", "F1/F2 onset locB", ""),
    ("apply.A", None, L("A"), ANY, None, None, "*", "F2.durB", ""),
    ("apply.G_mid", None, ANY - P, L("G"), ANY - L("Y"), None, "*", "F2/F3.durB", ""),
    ("apply.ZV", None, None, L("Z"), VOWEL, None, "*", "F1-F4.durF", ""),
    ("apply.XR", None, L("X"), L("R"), None, None, "*", "F3.durB AF", ""),
    # pg_amplitude_boundaries (pg_loci.c:567-828)
    ("amp.voiced_stop_V", None, STOP & VOICED, VOWEL, None, None, "*", "AV onset", "AV onset 60"),
    ("amp.G_V", None, L("G"), (VOWEL - HIGH - FRONT - L("po")) | L("E"), None, None, "*", "AV onset", ""),
    ("amp.B_back", None, L("B"), L("bcu"), None, None, "*", "AV B1 B2 onset", ""),
    ("amp.B_Eo", None, L("B"), L("Eo"), None, None, "*", "AV F2 onset", ""),
    ("amp.DE", None, L("D"), L("E"), None, None, "*", "AV B1 B2 onset", ""),
    ("amp.vfric_son", None, FRIC & VOICED, SON, None, None, "*", "AV onset", ""),
    ("amp.front_ng", None, FRONT, L("~"), None, None, "*", "F2 locB", "2100"),
    ("amp.EC", None, L("E"), L("C"), None, None, "*", "F1 B1 onset", ""),
    ("amp.G_front", None, None, L("G"), (FRONT | (ANY - HIGH)) - L("o"), None, "*", "AV onset locB", ""),
    ("amp.Do", None, None, L("D"), L("o"), None, "*", "AV onset", ""),
    ("amp.WE", None, L("W"), L("E"), None, None, "*", "F2 F3 B3", ""),
    ("amp.pause_WE", None, P, L("W"), L("E"), None, "*", "F3 B3 onset", ""),
    ("amp.H_front", None, None, L("H"), V[0] | V[1], None, "*", "AH onset AH.durF", ""),
    ("amp.RE", None, L("R"), L("E"), None, None, "*", "AV onset", ""),
    ("amp.Kp", None, L("K"), L("p"), None, None, "*", "AF AF.durF", ""),
    ("amp.pause_F", None, P, L("F"), None, None, "*", "AF AB onset", ""),
    ("amp.XR", None, L("X"), L("R"), None, None, "*", "AF onset", ""),
    ("amp.CsV", None, L("C"), L("s"), VOWEL, None, "*", "A3-A6", ""),
    ("amp.VTV", None, VOWEL, L("T"), VOWEL, None, "*", "AV", "AV 45"),
    ("amp.D_unstressed", None, SON, L("D"), SON, None, "cur-", "AB", ""),
    ("amp.voiced_voiceless", None, VOICED, VOICELESS - P, None, None, "*", "AV.durB AV locB", ""),
    ("amp.before_pause", None, None, VOICED - STOP, P, None, "*", "AV.durF", "cur dur/4 before a pause"),
    ("amp.voiced_K", None, VOICED, L("K"), None, None, "*", "AV.type AV.durF", ""),
    ("amp.B_voiced", None, L("B"), VOICED, None, None, "*", "AV.type AV.durF", ""),
    ("amp.B_V023", None, None, L("B"), V[0] | V[2] | V[3], None, "*", "p18 p19", ""),
    ("amp.Dp", None, L("D"), L("p"), None, None, "*", "p18 p19", ""),
    ("amp.after_Z", None, L("Z"), ANY, None, None, "*", "p18 onset", ""),
    ("amp.voiced_SZ", None, VOICED, L("SZ"), None, None, "*", "AV.type AV.durF AV.durB", ""),
    ("amp.voiced_SZ_", None, VOICED, L("SZ"), P, None, "*", "AV", "before a pause"),
    ("amp.Z_nonV", None, ANY - VOWEL, L("Z"), ANY - VOWEL - P, None, "*", "F0.durB F0.durF", ""),
    ("amp.prepausal", None, VOICED - L("Z"), P, None, None, "*", "F0 locB, AV locB", "pre-pausal fall"),
    ("amp.Z_pause", None, L("Z"), P, None, None, "*", "AF AF.durB", ""),
    ("amp.n__", None, L("n"), P, P, None, "*", "F2 locB, AV", ""),
    ("amp.son_q", None, SON, L("q"), None, None, "*", "AV F0", ""),
    # pg_finalize (pg_loci.c:831-853)
    ("finalize.T3", None, None, L("T"), L("3"), None, "*", "F1-F4", "the F(n+1) >= F(n)+200 rule is skipped"),
]


# ---- named classes (classes.tsv): a set column is written with the fewest of these and codes ----
VOWELS = set(CODES[:23])
CLASSES = [
    ("any", ANY, "every code, the pause included"),
    ("phoneme", ANY - P, "every code but the pause"),
    ("vowel", VOWELS, "the 23 vowels"),
    ("consonant", set(CODES[23:57]) - L("p"), "the 33 consonants (not the release vocoid p)"),
    ("two-target-vowel", set(CODES[:18]), "vowels with an onglide and an offglide target"),
    ("high-front-vowel", V[0], "next-vowel class V0"),
    ("nonhigh-front-vowel", V[1], "next-vowel class V1"),
    ("back-rounded-vowel", V[2], "next-vowel class V2"),
    ("central-low-vowel", V[3], "next-vowel class V3"),
    ("front-vowel", FRONT, "feature front vowel (100.20)"),
    ("r-colored-vowel", RCOL | L("3"), "r-coloured vowels"),
    ("y-offglide-vowel", YOFF, "diphthongs ending in a front glide"),
    ("w-offglide-vowel", WOFF, "diphthongs ending in a back glide"),
    ("reduced-vowel", L("@|"), "schwa and barred i"),
    # the previous-sound groups of the rule table (VOICE_CONTEXTS.md 4.2)
    ("front-ending", L("AEIaeiy|Y"), "sounds ending front: front vowels, y-offglide diphthongs, Y (not 4 U k)"),
    ("tense-front-vowel", L("AE"), "ey and i"),
    ("other-front-ending", L("Iaeiy|"), "front-ending but ey, i and Y"),
    ("central-back-vowel", L("uw@ov"), "the unrounded or lax back and central vowels"),
    ("w-ending", WOFF | L("W"), "w-offglide diphthongs and W"),
    ("rounded", BACK, "feature back/rounded (180.20): back rounded vowels and W h"),
    ("plosive", L("BPDTGK"), "plosives"),
    ("voiced-plosive", L("BDG"), "voiced plosives"),
    ("voiceless-plosive", L("PTK"), "voiceless plosives"),
    ("affricate", L("JC"), "affricate closures (their release is s or z)"),
    ("stop", STOP, "feature stop (0.20): plosives, affricates and q"),
    ("closure", CLOSURE, "feature closure (100.01): stops, t, Q, q and nasals"),
    ("nasal", NASAL, "nasals, syllabic ones included"),
    ("fricative", FRIC, "fricatives"),
    ("voiced-fricative", FRIC & VOICED, "voiced fricatives"),
    ("voiceless-fricative", FRIC - VOICED, "voiceless fricatives"),
    ("obstruent", (STOP | FRIC | L("tQ")), "stops, fricatives, t and Q"),
    ("liquid-glide", GLIDE - L("h"), "laterals, R, W and Y"),
    ("lateral", LAT, "l sounds"),
    ("h-like", L("dhH"), "h, voiced h and wh"),
    ("syllabic", SYLL, "feature syllabic (0.01): vowels and l m n"),
    ("sonorant", SON, "feature sonorant (0.02): vowels, p, nasals, liquids, glides and h sounds"),
    ("sonorant-consonant", SON - VOWELS - L("p"), "nasals, liquids, glides and h sounds"),
    ("voiced", VOICED, "feature voiced (0.04)"),
    ("voiceless", VOICELESS - P, "voiceless codes (not the pause)"),
    ("labial", LAB, "feature labial (100.10)"),
    ("alveolar", ALV, "feature alveolar (100.04)"),
    ("velar", VELAR, "feature velar (100.80)"),
    ("palatal", PAL, "feature palatal (180.04)"),
]


def condense(s):
    """The shortest way to write the set s: a class, a class minus codes (!x), or classes and codes."""
    s = frozenset(s)
    for n, c, _ in CLASSES:
        if c == s:
            return n

    def cover(t, allowed):  # t as classes (largest first) that stay inside `allowed`, and the codes left over
        left, toks = set(t), []
        while True:
            best = max(((len(c & left), n, c) for n, c, _ in CLASSES if c <= allowed and len(c & left) >= 2),
                       default=None, key=lambda x: x[0])
            if not best:
                return toks + [name(c) for c in CODES if c in left]
            toks.append(best[1])
            left -= best[2]

    cands = [cover(s, s)]
    # or a superset minus what it has too much (the removed classes only have to miss s): a class, or s plus a class
    for t in {frozenset(c) for _, c, _ in CLASSES if c > s} | {s | c for _, c, _ in CLASSES if not c <= s}:
        cands.append(cover(t, t) + ["!" + x for x in cover(t - s, ANY - s)])
    return " ".join(min(cands, key=lambda t: (len(t), sum(x.startswith("!") for x in t), sum(len(x) == 1 for x in t))))


def expand(col):
    """The set a column denotes (the inverse of condense)."""
    names = dict((n, c) for n, c, _ in CLASSES)
    plus, minus = set(), set()
    for t in col.split():
        target = minus if t.startswith("!") else plus
        t = t.lstrip("!")
        target |= names[t] if t in names else {" " if t == "_" else t}
    return plus - minus


def write(path):
    out = ["\t".join(("id", "kind", "prev2", "prev", "cur", "next", "next2", "stress", "sets", "note"))]

    def col(s):
        text = condense(ANY if s is None else s)
        assert expand(text) == (ANY if s is None else set(s)), text
        return text

    hits = rule_rows()
    for g in range(88):
        rs = rules(g)
        for i, (cond, sets) in enumerate(rs):
            rows = factor(hits.get((g, i), []))
            if not rows:
                print("unreachable: group %d rule %d (%s)" % (g, i, cond_text(cond)))
            for k, (ps, cs, ns, ss, nns) in enumerate(sorted(rows, key=lambda r: (fmt(r[1]), fmt(r[0]), fmt(r[2])))):
                rid = "R%d.%d" % (g, i) + ("" if len(rows) == 1 else chr(ord("a") + k))
                stress = "*" if len(ss) == 2 else "next+" if 1 in ss else "next-"
                nn = None if None in nns else nns
                out.append("\t".join((rid, "rule", col(None), col(ps), col(cs), col(ns), col(nn), stress,
                                      " ".join(sets) or "-", "rule %d of %d: %s" % (i + 1, len(rs), cond_text(cond)))))
    for r in release_rows():
        rid, kind, p2, p, c, n, n2, st, sets, note = r
        out.append("\t".join((rid, kind, col(p2), col(p), col(c), col(n), col(n2), st, sets, note)))
    for rid, p2, p, c, n, n2, st, sets, note in CODE:
        out.append("\t".join(("C." + rid, "code", col(p2), col(p), col(c), col(n), col(n2), st, sets, note)))
    with open(path, "w", encoding="ascii", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print("%s: %d rows" % (path, len(out) - 1))
    cpath = os.path.join(os.path.dirname(path), "classes.tsv")
    with open(cpath, "w", encoding="ascii", newline="\n") as f:
        f.write("class\tmembers\tdescription\n")
        for n, c, desc in CLASSES:
            f.write("%s\t%s\t%s\n" % (n, fmt(c), desc))
    print("%s: %d classes" % (cpath, len(CLASSES)))


if __name__ == "__main__":
    write(os.path.join(HERE, "contexts.tsv"))

"""check_coverage.py: which contexts of contexts.tsv a phone-aligned corpus covers (VOICE_CONTEXTS.md section 8).

    python voice/check_coverage.py PHONES.txt [...] [-o coverage.tsv] [--show-prose]

Input: one utterance per line, ARPABET phones separated by spaces, vowels with stress digits (CMUdict style); sil,
sp, spn or SIL mark pauses. The phones are mapped to Prose codes with arpabet_to_prose.tsv, then every context row is
counted. Writes contexts.tsv plus a count column (default coverage.tsv) and prints the rows with no match.
"""
import argparse
import collections
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PAUSES = {"sil", "sp", "spn", ""}
REDUCED = {"AX", "IX", "AXR", "AX-H", "EL", "EM", "EN", "ENG"}
VOWELS = {"AA", "AE", "AH", "AO", "AW", "AY", "EH", "ER", "EY", "IH", "IY", "OW", "OY", "UH", "UW", "UX"} | REDUCED
VOICELESS = {"P", "T", "K", "CH", "F", "TH", "S", "SH", "HH", "WH", "Q"}
OBSTRUENT = {"P", "B", "T", "D", "K", "G", "CH", "JH", "F", "V", "TH", "DH", "S", "Z", "SH", "ZH", "DX", "Q"}
PROSE_VOWELS = set("4AEIOUabcefgikruwy3@ov|p")


def base(ph): return ph.rstrip("012")
def is_pause(ph): return ph is None or ph.lower() in PAUSES
def is_vowel(ph): return not is_pause(ph) and base(ph) in VOWELS
def stressed(ph): return is_vowel(ph) and base(ph) not in REDUCED and not ph.endswith("0")


def is_class(ph, cls, nxt=None):
    """ph is the phone next to the matched sequence (None = utterance edge); nxt the one after it."""
    if cls.startswith("!"):
        return not is_class(ph, cls[1:], nxt)
    if cls == "pause":
        return is_pause(ph)
    if cls == "vowel":
        return is_vowel(ph)
    if cls == "nonvowel":
        return not is_vowel(ph)
    if cls == "stressed":
        return stressed(ph)
    if cls == "unstressed":
        return is_vowel(ph) and not stressed(ph)
    if cls == "stressed-syl":
        return stressed(ph) or (not is_pause(ph) and base(ph) in {"L", "R", "W", "Y"} and stressed(nxt))
    if is_pause(ph):
        return False
    b = base(ph)
    if cls == "consonant":
        return b not in VOWELS
    if cls == "obstruent":
        return b in OBSTRUENT
    if cls == "sonorant":
        return b not in OBSTRUENT
    if cls == "voiceless":
        return b in VOICELESS
    if cls == "voiced":
        return b not in VOICELESS
    return b == cls


def load_map():
    rows = []
    with open(os.path.join(HERE, "arpabet_to_prose.tsv"), encoding="utf-8") as f:
        next(f)
        for line in f:
            arpa, prose, ctx, _ = (line.rstrip("\n").split("\t") + ["", ""])[:4]
            conds = [c.split("=", 1) for c in ctx.split()]
            rows.append((arpa.split(), [] if prose == "-" else prose.split(), conds))
    return rows


def phone_matches(pattern, ph):
    return not is_pause(ph) and (ph == pattern or (pattern.rstrip("012") == pattern and base(ph) == pattern))


def to_prose(phones, table):
    """ARPABET phones -> [(Prose code, in a stressed syllable)]; pauses become ' '."""
    out, i = [], 0
    while i < len(phones):
        ph = phones[i]
        if is_pause(ph):
            out.append([" ", False])
            i += 1
            continue
        for arpa, prose, conds in table:
            n = len(arpa)
            if not all(i + k < len(phones) and phone_matches(a, phones[i + k]) for k, a in enumerate(arpa)):
                continue
            before = phones[i + n] if i + n < len(phones) else None
            after2 = phones[i + n + 1] if i + n + 1 < len(phones) else None
            after = phones[i - 1] if i > 0 else None
            ok = all(any(is_class(before if key == "before" else after, alt, after2 if key == "before" else None)
                         for alt in val.split("|")) for key, val in conds)
            if ok:
                stress = any(stressed(p) for p in phones[i:i + n])
                out.extend([c.replace("_", " "), stress] for c in prose)
                i += n
                break
        else:
            sys.exit("no mapping for %r" % ph)
    # a consonant takes the stress of the vowel after it (the Prose marks the onset of a stressed syllable, up to 3
    # consonants: an approximation of mark_stressed_syllable)
    for k in range(len(out) - 1, -1, -1):
        if out[k][0] not in PROSE_VOWELS and out[k][0] != " ":
            j = k + 1
            while j < len(out) and j - k <= 3 and out[j][0] not in PROSE_VOWELS and out[j][0] != " ":
                j += 1
            out[k][1] = j < len(out) and j - k <= 3 and out[j][0] in PROSE_VOWELS and out[j][1]
    return out


def load_contexts():
    with open(os.path.join(HERE, "contexts.tsv"), encoding="ascii") as f:
        header = next(f).rstrip("\n").split("\t")
        rows = [line.rstrip("\n").split("\t") for line in f]

    classes = {}
    with open(os.path.join(HERE, "classes.tsv"), encoding="ascii") as f:
        next(f)
        for line in f:
            n, members, _ = line.rstrip("\n").split("\t")
            classes[n] = {" " if c == "_" else c for c in members.split()}

    def s(col):
        """A set column: class names and codes, ! removes; any = no condition."""
        if col == "any":
            return None
        plus, minus = set(), set()
        for t in col.split():
            (minus if t.startswith("!") else plus).update(classes.get(t.lstrip("!"), {" " if t[-1] == "_" else t[-1]}))
        return plus - minus
    parsed = [(r, [s(r[2]), s(r[3]), s(r[4]), s(r[5]), s(r[6])], r[7].split()) for r in rows]
    by_cur = collections.defaultdict(list)
    for k, (r, sets, stress) in enumerate(parsed):
        for c in (sets[2] if sets[2] is not None else set("4AEIOUabcefgikruwy3@ov|jlLWYRpdhHBPDTtJCGKQqVFxXZSzsMmNn~ ")):
            by_cur[c].append((k, sets, stress))
    return header, rows, by_cur


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+")
    ap.add_argument("-o", default="coverage.tsv")
    ap.add_argument("--show-prose", action="store_true", help="print each utterance in Prose codes")
    a = ap.parse_args()
    table = load_map()
    header, rows, by_cur = load_contexts()
    counts = [0] * len(rows)
    for path in a.files:
        with open(path, encoding="utf-8") as f:
            for line in f:
                phones = line.split()
                if not phones:
                    continue
                seq = to_prose(phones, table)
                # the Prose starts an utterance after a pause and ends it with a pause pair
                if seq[0][0] != " ":
                    seq.insert(0, [" ", False])
                while len(seq) < 3 or seq[-1][0] != " " or seq[-2][0] != " ":
                    seq.append([" ", False])
                if a.show_prose:
                    print("".join(c + ("'" if st and c in PROSE_VOWELS else "") for c, st in seq).replace(" ", "_"))
                for i in range(1, len(seq) - 2):
                    win = [seq[i - 2] if i >= 2 else None, seq[i - 1], seq[i], seq[i + 1], seq[i + 2]]
                    for k, sets, stress in by_cur.get(seq[i][0], ()):
                        if any(st is not None and (w is None or w[0] not in st) for st, w in zip(sets, win)):
                            continue
                        ok = True
                        for t in stress:
                            if t != "*":
                                node = {"prev": win[1], "cur": win[2], "next": win[3]}[t[:-1]]
                                ok = ok and node[1] == (t[-1] == "+")
                        if ok:
                            counts[k] += 1
    with open(a.o, "w", encoding="ascii", newline="\n") as f:
        f.write("\t".join(header + ["count"]) + "\n")
        for r, n in zip(rows, counts):
            f.write("\t".join(r + [str(n)]) + "\n")
    missing = [r for r, n in zip(rows, counts) if n == 0]
    kinds = collections.Counter(r[1] for r in rows)
    miss = collections.Counter(r[1] for r in missing)
    print("covered: " + ", ".join("%s %d/%d" % (k, kinds[k] - miss[k], kinds[k]) for k in kinds))
    for r in missing:
        print("\t".join((r[0], "prev2=" + r[2], "prev=" + r[3], "cur=" + r[4], "next=" + r[5], "next2=" + r[6],
                         r[7], r[9])))
    print("counts written to " + a.o)


if __name__ == "__main__":
    main()

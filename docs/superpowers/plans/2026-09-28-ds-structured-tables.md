# Structured Data-Segment Tables Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the raw dump `prose_ds_data` (v3.4.1 DS:0000-AEA9) with named, typed C tables that rebuild the
same bytes, so the ROM data can be read in the source.

**Architecture:** A Python generator (`tools/ds_gen.py`, driven by the table list `tools/ds_spec.py`, called from
`tools/rom_extract.py`) reads DS:0000-AEA9 from the ROM dumps and writes one typed `const` array per table into
`src/data/ds_<subsystem>.c`, a header `src/data/prose_ds_tables.h` (struct types, offset macros, a hash of the
original bytes) and a layout list `src/data/ds_layout.c`. `prose_rom_builtin` copies each table to its offset in
the ROM image. No stage code changes. A new test, `ds_image_check`, proves that the image is unchanged.

**Tech Stack:** C99 (MinGW-w64 gcc 64-bit, Ninja, CMake ≥ 3.20), Python 3.13, WSL Ubuntu gcc 13 for the Linux check.

**Spec:** `docs/superpowers/specs/2026-09-28-ds-structured-tables-design.md`

## Global Constraints

- The image the C runs on must stay **byte-identical**: every table sits at its original DS offset with its original bytes.
- Every byte of DS:0000-AEA9 belongs to exactly one table; no gaps, no overlaps.
- Scope is v3.4.1 DS:0000-AEA9 only. Do not touch the lexicon (`prose_lexicon`), the DSP data ROM, the lane sums, v1.1 (`prose_v1_data.c`), or any stage code under `src/{input,textrules,lexical,prosody,paramgen,frame,dsp,dll,v1}`.
- The build needs no Python and no ROM files: the generated C is committed.
- `tools/` is git-ignored (`.gitignore: /tools/`). `ds_gen.py`, `ds_spec.py` and `rom_extract.py` stay local and are **never** `git add`ed. Only `src/`, `docs/` and the docs are committed.
- C99: no `static_assert`. Compile-time checks use `PROSE_DS_ASSERT(name, cond)`, a typedef of a negative-size array.
- Little-endian hosts only: `prose_ds_tables.h` has an `#error` for a big-endian target.
- Names: array `prose_ds_<name>`, offset macro `PROSE_DS_<NAME>`, struct types `prose_<thing>`. Table names must not be `byte`, `word`, `table` or `layout`, because `prose_rom.h` already has functions called `prose_ds_byte/word/table` and the layout list is `prose_ds_layout`.
- Generated text files are LF. The C style is tabs, `-Wall -Wextra`, no new warnings.
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Work on branch `restructure`.
- Environment: Windows, Git Bash. The repo is `/c/Users/abart/Desktop/prose2000_reborn`. `python` is 3.13, and there is no `python3`. The ROM dumps are in `prose_v3/`.

## File structure

| File | Status | Responsibility |
|---|---|---|
| `tools/ds_gen.py` | create (local only) | table-list machinery: checks, pointer rendering, C emitters |
| `tools/ds_spec.py` | create (local only) | `TABLES`: every DS table with offset, type, file, doc, status |
| `tools/rom_extract.py` | modify (local only) | drop `prose_ds_data` from `REGIONS`; call `ds_gen` |
| `src/data/prose_ds_tables.h` | generated | struct types, `PROSE_DS_*` offsets, `extern`s, `PROSE_DS_FNV1A64`, `prose_ds_span` |
| `src/data/ds_layout.c` | generated | `prose_ds_layout[]`, `prose_ds_layout_count` |
| `src/data/ds_{boot,textrules,lexical,frame,prosody,paramgen}.c` | generated | the tables |
| `src/data/prose_data.c/.h` | regenerated | lexicon, lane sums, DSP data ROM (no more `prose_ds_data`) |
| `src/common/prose_rom.c`, `prose_rom.h` | modify | assemble DS from the layout list |
| `src/tests/ds_image_check.c` | create | layout contiguity plus the FNV-1a hash of DS:0000-AEA9; optional dump |
| `src/CMakeLists.txt` | modify | new data sources; `ds_image_check` in both versions |
| `REFERENCE.md`, `AGENTS.md` | modify | §14 data files; stale §9.3 rows |

Build directory for this plan: `build-ds/` (ignored by `/build*/`). It is separate from the user's `build/` and `build64/`.

---

### Task 1: Baseline build and reference outputs

**Files:**
- Create: `build-ds/regress.sh` (ignored, not committed)
- Create: `build-ds/ds_old.bin` (ignored)

**Interfaces:**
- Produces: `bash build-ds/regress.sh base` (write the baseline) and `bash build-ds/regress.sh now` (write and compare; exit 1 on any difference). Also `build-ds/ds_old.bin`, the 0xAEAA bytes of the current `prose_ds_data`.

- [ ] **Step 1: Configure and build the current tree**

```bash
cd /c/Users/abart/Desktop/prose2000_reborn
cmake -S src -B build-ds -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release
cmake --build build-ds 2>&1 | tail -3
```
Expected: `ninja: no work to do` or a completed link, with no errors.

- [ ] **Step 2: Write the regression script**

Create `build-ds/regress.sh`:
```bash
#!/bin/bash
# regress.sh base|now: speaks fixed texts with pipeline_play (C power-up, whole pipeline) and prose_say (the DLL);
# "base" stores the output, "now" stores it again and compares byte for byte with the base.
set -e
B=/c/Users/abart/Desktop/prose2000_reborn/build-ds
mode=${1:?usage: regress.sh base|now}
mkdir -p "$B/$mode"
texts=(
	'Hello, my friend. The quick brown fox jumps over the lazy dog.'
	$'\e[1V\e[16NDr. Smith paid $1,234.56 on 3/14/1999; call 555-1212.'
	$'\e[2V\e[120pAntidisestablishmentarianism, photosynthesis, and unbelievably preprocessed affixes?'
	$'\e[0V\e[5vNumbers: 1st, 22nd, 1,000,000. Symbols & abbreviations: St. Louis, ft., Mr. Jones.'
)
for i in "${!texts[@]}"; do
	"$B/pipeline_play.exe" -q -s "${texts[$i]}" -w "$B/$mode/p$i.wav" > "$B/$mode/p$i.txt"
	"$B/prose_say.exe" -w "$B/$mode/s$i.wav" "${texts[$i]}" > /dev/null
done
if [ "$mode" = now ]; then
	bad=0
	for f in "$B/base"/*; do cmp -s "$f" "$B/now/$(basename "$f")" || { echo "DIFFERS: $(basename "$f")"; bad=1; }; done
	[ $bad = 0 ] && echo "regress: all $(ls "$B/base" | wc -l) outputs identical"
	exit $bad
fi
echo "regress: baseline written ($(ls "$B/base" | wc -l) files)"
```

- [ ] **Step 3: Record the baseline and check it is deterministic**

```bash
bash build-ds/regress.sh base && bash build-ds/regress.sh now
```
Expected: `regress: baseline written (16 files)`, then `regress: all 16 outputs identical`. If `now` differs from `base` with no code change, the outputs are not deterministic. Stop and report it, because this plan's regression check depends on them being deterministic.

- [ ] **Step 4: Save the current DS bytes**

```bash
python - <<'EOF'
import re
src = open('src/data/prose_data.c').read()
body = re.search(r'prose_ds_data\[0x[0-9A-F]+\] = \{(.*?)\};', src, re.S).group(1)
body = re.sub(r'/\*.*?\*/', '', body)
ds = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-F]{2})', body))
assert len(ds) == 0xAEAA, hex(len(ds))
open('build-ds/ds_old.bin', 'wb').write(ds)
h = 0xCBF29CE484222325
for b in ds: h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
print('ds_old.bin written, FNV-1a 64 = 0x%016X' % h)
EOF
```
Expected: `ds_old.bin written, FNV-1a 64 = 0x…`. Note the value down, because Task 2's header must hold the same one.

No commit (nothing tracked changed).

---

### Task 2: Generator, coarse tables, image assembly and `ds_image_check`

After this task the DS is built from 7 coarse byte tables (one per subsystem region) through the layout list, and
`prose_ds_data` is gone. Tasks 3-6 then refine the regions.

**Files:**
- Create: `src/tests/ds_image_check.c`
- Create: `tools/ds_gen.py`, `tools/ds_spec.py` (local only)
- Modify: `tools/rom_extract.py` (local only)
- Modify: `src/common/prose_rom.c`, `src/common/prose_rom.h:12-13`
- Modify: `src/CMakeLists.txt:39` and the block after the samples
- Generated: `src/data/prose_ds_tables.h`, `src/data/ds_layout.c`, `src/data/ds_*.c`, `src/data/prose_data.c/.h`

**Interfaces:**
- Produces, for Tasks 3-7:
  - `ds_gen.T(name, off, kind, size=None, file=None, doc='', status='full', cols=None, ptr=None, to=None, render='bytes', starts=(), split_zero=False, fmt=None)`
  - `ds_gen.S(name, fields, doc, note=None)` and `ds_gen.F(name, kind='u16', n=1, ptr=None, to=None, fmt=None)`
  - `ds_spec.TABLES` (a list of `T`) and `ds_spec.ROUTINES` (a dict)
  - C: `prose_ds_span {uint16_t off, size; const void *data; const char *name;}`, `prose_ds_layout[]`, `prose_ds_layout_count`, `PROSE_DS_DATA_BASE`, `PROSE_DS_SIZE`, `PROSE_DS_FNV1A64`, `PROSE_DS_ASSERT`, `PROSE_W`
  - `build-ds/ds_image_check.exe [OUT.bin]`

- [ ] **Step 1: Write the test**

Create `src/tests/ds_image_check.c`:
```c
/* ds_image_check [OUT.bin]
 *
 * The data segment DS:0000-AEA9 is built from the tables in src/data/ds_*.c (prose_ds_tables.h), which may be edited
 * by hand. This test checks that the layout list covers DS:0000-AEA9 in order without gaps or overlaps, and that the
 * image prose_rom_builtin builds from it is the ROM's, byte for byte (FNV-1a hash of the dump, PROSE_DS_FNV1A64).
 * OUT.bin receives the built DS:0000-AEA9. Returns 0 if both checks pass.
 */
#include "prose_ds_tables.h"
#include "prose_rom.h"

#include <stdio.h>

static prose_rom rom;

int main(int argc, char **argv)
{
	unsigned i, at = 0, bad = 0;
	uint64_t h = 0xCBF29CE484222325ull;
	for (i = 0; i < prose_ds_layout_count; i++) {
		const prose_ds_span *s = &prose_ds_layout[i];
		if (s->off != at) {
			printf("%s starts at DS:%04X, expected DS:%04X\n", s->name, s->off, at);
			bad++;
		}
		at = s->off + s->size;
	}
	if (at != PROSE_DS_SIZE) {
		printf("the tables end at DS:%04X, not DS:%04X\n", at, PROSE_DS_SIZE);
		bad++;
	}
	prose_rom_builtin(&rom);
	for (i = 0; i < PROSE_DS_SIZE; i++) {
		h ^= prose_ds_byte(&rom, (uint16_t)i);
		h *= 0x100000001B3ull;
	}
	if (h != PROSE_DS_FNV1A64) {
		printf("DS:0000-%04X differs from the ROM (FNV-1a %016llX, expected %016llX)\n", PROSE_DS_SIZE - 1,
		       (unsigned long long)h, (unsigned long long)PROSE_DS_FNV1A64);
		bad++;
	}
	if (argc > 1) {
		FILE *f = fopen(argv[1], "wb");
		if (!f || fwrite(rom.image + (PROSE_DS_DATA_BASE - 0xC0000u), 1, PROSE_DS_SIZE, f) != PROSE_DS_SIZE) {
			printf("cannot write %s\n", argv[1]);
			bad++;
		}
		if (f)
			fclose(f);
	}
	printf("%u tables, DS:0000-%04X: %s\n", prose_ds_layout_count, PROSE_DS_SIZE - 1,
	       bad ? "DIFFERENT" : "equal to the ROM");
	return bad != 0;
}
```

Add this to `src/CMakeLists.txt`, directly above the line `# Which firmware to build: 3 = v3.4.1 (default), 1 = the older v1.1 (1983, REFERENCE §16).`:
```cmake
# The data-segment tables (src/data/ds_*.c) must still build the ROM's DS:0000-AEA9; in both versions, since the
# library is always built
add_executable(ds_image_check tests/ds_image_check.c)
target_link_libraries(ds_image_check PRIVATE prose_common)

```

- [ ] **Step 2: Build it and watch it fail**

```bash
cmake --build build-ds --target ds_image_check 2>&1 | grep -m2 -i 'error'
```
Expected: `fatal error: prose_ds_tables.h: No such file or directory`.

- [ ] **Step 3: Write the generator machinery**

Create `tools/ds_gen.py`:
```python
"""Structured tables for the v3.4.1 data segment, DS:0000-AEA9.

Design: docs/superpowers/specs/2026-09-28-ds-structured-tables-design.md. ds_spec.TABLES lists every table in the
data segment; Gen checks the list against the dump (full coverage, no overlap, whole records) and writes
src/data/prose_ds_tables.h, ds_layout.c and ds_<file>.c for each subsystem file. Pointers are written as
PROSE_DS_<TABLE> + k expressions, so the C shows what they point to while the bytes stay the same.
"""
import bisect
import os

DS_SIZE = 0xAEAA
WIDTH = {'u8': 1, 'u16': 2, 'i16': 2}
CTYPE = {'u8': 'uint8_t', 'u16': 'uint16_t', 'i16': 'int16_t'}
FILES = ['boot', 'textrules', 'lexical', 'frame', 'prosody', 'paramgen']
RESERVED = {'byte', 'word', 'table', 'layout'}  # prose_ds_byte/word/table (prose_rom.h), prose_ds_layout
NOTE = ('/* Generated by tools/ds_gen.py from the Prose 2000 v3.4.1 ROM dumps, and maintained by hand from then on: the\n'
        ' * tables may be edited (names, types, splits), and ds_image_check checks that they still build the ROM\'s\n'
        ' * DS:0000-AEA9 byte for byte. Design: docs/superpowers/specs/2026-09-28-ds-structured-tables-design.md */')


def default_fmt(kind):
    return '%d' if kind == 'i16' else '0x%04X' if kind == 'u16' else '0x%02X'


class F:
    """A struct field: n values of kind. ptr: None, or the bias of a DS pointer (it points to value + ptr).
    to: how the target is shown when it lies in a pool ('text', 'bytes' or 'words')."""

    def __init__(self, name, kind='u16', n=1, ptr=None, to=None, fmt=None):
        self.name, self.kind, self.n, self.ptr, self.to = name, kind, n, ptr, to
        self.fmt = fmt or default_fmt(kind)
        self.size = WIDTH[kind] * n


class S:
    """A record type (typedef struct). note(values) returns an extra comment for a record, values by field name."""

    def __init__(self, name, fields, doc, note=None):
        self.name, self.fields, self.doc, self.note = name, fields, doc, note
        self.size = sum(f.size for f in fields)


class T:
    """A table at DS:off.
    kind: 'u8' / 'u16' / 'i16' (flat values), an S (records), or 'pool' (bytes split into items, one per line).
    size: bytes; None = up to the next table. cols: a 2-D table of rows of cols values. ptr: the values are DS
    pointers with this bias (one per line, commented). to: how pointer targets inside pools are shown.
    Pools: render = how items are shown by default ('text' or 'bytes'); items start at the table, at every pointer
    target inside it, at each of starts, and (split_zero) after every 0 byte."""

    def __init__(self, name, off, kind, size=None, file=None, doc='', status='full', cols=None, ptr=None, to=None,
                 render='bytes', starts=(), split_zero=False, fmt=None):
        self.name, self.off, self.kind, self.size, self.file = name, off, kind, size, file
        self.doc, self.status, self.cols, self.ptr, self.to = doc, status, cols, ptr, to
        self.render, self.starts, self.split_zero = render, tuple(starts), split_zero
        self.fmt = fmt or (default_fmt(kind) if kind in WIDTH else None)

    def unit(self):
        if isinstance(self.kind, S):
            return self.kind.size
        if self.kind == 'pool':
            return 1
        return WIDTH[self.kind] * (self.cols or 1)


def values(ds, a, kind, n):
    w, out = WIDTH[kind], []
    for i in range(n):
        v = ds[a + i] if w == 1 else ds[a + 2 * i] | ds[a + 2 * i + 1] << 8
        out.append(v - 0x10000 if kind == 'i16' and v >= 0x8000 else v)
    return out


def fnv1a64(data):
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


class Gen:
    def __init__(self, ds, tables):
        assert len(ds) == DS_SIZE
        self.ds, self.t = bytes(ds), sorted(tables, key=lambda t: t.off)
        self.check()
        self.offs = [t.off for t in self.t]
        self.refs = {}  # pointer target -> how it is shown in a pool (or None)
        for v, bias, to in self.pointers():
            if v and self.refs.get((v + bias) & 0xFFFF) is None:
                self.refs[(v + bias) & 0xFFFF] = to

    # --- checks
    def check(self):
        if not self.t or self.t[0].off != 0:
            raise SystemExit('ds_spec: the first table must start at DS:0000')
        names = set()
        for i, t in enumerate(self.t):
            end = self.t[i + 1].off if i + 1 < len(self.t) else DS_SIZE
            if t.size is None:
                t.size = end - t.off
            if t.off + t.size != end:
                raise SystemExit('ds_spec: %s covers DS:%04X-%04X, but the next table starts at DS:%04X'
                                 % (t.name, t.off, t.off + t.size - 1, end))
            if t.size <= 0 or t.size % t.unit():
                raise SystemExit('ds_spec: %s: %d bytes is not a whole number of %d-byte entries'
                                 % (t.name, t.size, t.unit()))
            t.count = t.size // t.unit()
            if t.name in names or t.name in RESERVED:
                raise SystemExit('ds_spec: duplicate or reserved table name %s' % t.name)
            if t.file not in FILES:
                raise SystemExit('ds_spec: %s: file must be one of %s' % (t.name, FILES))
            names.add(t.name)
        img = bytearray()
        for t in self.t:  # read every value and write it back: the entry sizes must rebuild the segment
            fields = ([(f.kind, f.n) for f in t.kind.fields] * t.count if isinstance(t.kind, S)
                      else [('u8', t.size)] if t.kind == 'pool' else [(t.kind, t.size // WIDTH[t.kind])])
            a = t.off
            for kind, n in fields:
                for v in values(self.ds, a, kind, n):
                    img += (v & 0xFFFF).to_bytes(WIDTH[kind], 'little')
                a += WIDTH[kind] * n
        if bytes(img) != self.ds:
            raise SystemExit('ds_spec: the tables do not rebuild DS:0000-AEA9')

    def pointers(self):
        """(value, bias, to) of every DS pointer in a pointer table or a pointer field"""
        for t in self.t:
            if isinstance(t.kind, S):
                for r in range(t.count):
                    a = t.off + r * t.kind.size
                    for f in t.kind.fields:
                        if f.ptr is not None:
                            for v in values(self.ds, a, f.kind, f.n):
                                yield v, f.ptr, f.to
                        a += f.size
            elif t.ptr is not None:
                for v in values(self.ds, t.off, t.kind, t.size // WIDTH[t.kind]):
                    yield v, t.ptr, t.to

    # --- rendering
    def find(self, a):
        return self.t[bisect.bisect_right(self.offs, a) - 1] if 0 <= a < DS_SIZE else None

    def sym(self, v, bias=0):
        """a pointer as a C expression: PROSE_DS_<TABLE> [+ unit * index] [+ k] [- bias]; 0 = null"""
        if v == 0 and bias == 0:
            return '0'
        a = (v + bias) & 0xFFFF
        t = self.find(a)
        if t is None:
            return '0x%04X' % v
        k, e = a - t.off, 'PROSE_DS_' + t.name.upper()
        unit = t.unit() if isinstance(t.kind, S) or t.cols else 0
        if unit:
            i, r = divmod(k, unit)
            e += (' + %d * %d' % (unit, i) if i else '') + (' + %d' % r if r else '')
        elif k:
            e += ' + 0x%X' % k
        return e + (' - %d' % bias if bias > 0 else ' + %d' % -bias if bias < 0 else '')

    def text(self, a):
        """the string at DS:a for a comment (no '*' or '/', so no comment can end early)"""
        s = []
        while a < DS_SIZE and self.ds[a] and len(s) < 24:
            b = self.ds[a]
            s.append(chr(b) if 0x20 <= b < 0x7F and b not in (0x2A, 0x2F, 0x5C) else '\\x%02X' % b)
            a += 1
        return '"%s"' % ''.join(s)

    def target_note(self, v, bias):
        a = (v + bias) & 0xFFFF
        t = self.find(a) if v else None
        return ' ' + self.text(a) if t is not None and t.kind == 'pool' and t.render == 'text' else ''

    @staticmethod
    def byte(b, render):
        if render == 'text' and 0x20 <= b < 0x7F:
            return "'\\''" if b == 0x27 else "'\\\\'" if b == 0x5C else "'%c'" % b
        return '0' if b == 0 else '0x%02X' % b

    def c_table(self, t):
        ds, arr = self.ds, 'prose_ds_' + t.name
        out = ['/* DS:%04X-%04X %s [%s] */' % (t.off, t.off + t.size - 1, t.doc, t.status)]
        if isinstance(t.kind, S):
            out.append('const %s %s[%d] = {' % (t.kind.name, arr, t.count))
            for r in range(t.count):
                a0 = a = t.off + r * t.kind.size
                vals, notes, named = [], [], {}
                for f in t.kind.fields:
                    vs = values(ds, a, f.kind, f.n)
                    named[f.name] = vs[0]
                    if f.ptr is not None:
                        rs = [self.sym(v, f.ptr) for v in vs]
                        notes += ['%s%s' % (f.name, self.target_note(v, f.ptr)) for v in vs
                                  if self.target_note(v, f.ptr)]
                    else:
                        rs = [f.fmt % v for v in vs]
                    vals.append(rs[0] if f.n == 1 else '{' + ', '.join(rs) + '}')
                    a += f.size
                if t.kind.note and t.kind.note(named):
                    notes.append(t.kind.note(named))
                out.append('\t{%s}, /* %d DS:%04X%s */' % (', '.join(vals), r, a0, ''.join(' ' + n for n in notes)))
        elif t.kind == 'pool':
            out.append('const uint8_t %s[%d] = {' % (arr, t.size))
            out += self.c_pool(t)
        else:
            w, n = WIDTH[t.kind], t.size // WIDTH[t.kind]
            vs, per = values(ds, t.off, t.kind, n), 16 if WIDTH[t.kind] == 1 else 8
            if t.ptr is not None:
                out.append('const %s %s[%d] = {' % (CTYPE[t.kind], arr, n))
                for i, v in enumerate(vs):
                    out.append('\t%s, /* %d DS:%04X%s */' % (self.sym(v, t.ptr), i, t.off + w * i,
                                                              self.target_note(v, t.ptr)))
            elif t.cols:
                out.append('const %s %s[%d][%d] = {' % (CTYPE[t.kind], arr, t.count, t.cols))
                for r in range(t.count):
                    row = vs[r * t.cols:(r + 1) * t.cols]
                    lines = [', '.join(t.fmt % v for v in row[i:i + per]) for i in range(0, len(row), per)]
                    out.append('\t{%s}, /* %d DS:%04X */' % (',\n\t '.join(lines), r, t.off + r * t.cols * w))
            else:
                out.append('const %s %s[%d] = {' % (CTYPE[t.kind], arr, n))
                for i in range(0, n, per):
                    out.append('\t%s, /* DS:%04X */' % (', '.join(t.fmt % v for v in vs[i:i + per]), t.off + w * i))
        out.append('};')
        out.append('PROSE_DS_ASSERT(%s, sizeof %s == %d);' % (arr, arr, t.size))
        return out

    def c_pool(self, t):
        ds, end = self.ds, t.off + t.size
        starts = {t.off} | {a for a in self.refs if t.off <= a < end} | set(t.starts)
        if t.split_zero:
            starts |= {a + 1 for a in range(t.off, end - 1) if ds[a] == 0}
        starts, out = sorted(starts), []
        for i, a in enumerate(starts):
            b = starts[i + 1] if i + 1 < len(starts) else end
            render = self.refs.get(a) or t.render
            if render == 'words':  # a 0-terminated list of pointers, then any bytes up to the next item
                toks, p = [], a
                while p + 1 < b:
                    v = ds[p] | ds[p + 1] << 8
                    toks.append('PROSE_W(%s)' % self.sym(v))
                    p += 2
                    if v == 0:
                        break
                toks += [self.byte(ds[q], 'bytes') for q in range(p, b)]
                per = 4
            else:
                toks, per = [self.byte(ds[q], render) for q in range(a, b)], 16
            for j in range(0, len(toks), per):
                out.append('\t%s,%s' % (', '.join(toks[j:j + per]), ' /* DS:%04X */' % a if j == 0 else ''))
        return out

    # --- files
    def header(self):
        h = [NOTE, '#ifndef PROSE_DS_TABLES_H', '#define PROSE_DS_TABLES_H', '', '#include <stdint.h>', '',
             '#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__',
             '#error "the data-segment tables are copied into the ROM image as they lie in memory: little-endian only"',
             '#endif', '',
             '#define PROSE_DS_DATA_BASE 0xF4100u /* linear address of DS:0000 */',
             '#define PROSE_DS_SIZE 0x%04Xu       /* DS:0000-%04X is ROM data */' % (DS_SIZE, DS_SIZE - 1),
             '#define PROSE_DS_FNV1A64 0x%016Xull /* FNV-1a (64-bit) of the ROM\'s DS:0000-%04X */'
             % (fnv1a64(self.ds), DS_SIZE - 1),
             '', '/* compile-time check (C99) */',
             '#define PROSE_DS_ASSERT(name, cond) typedef char prose_ds_assert_##name[(cond) ? 1 : -1]',
             '/* a 16-bit value as two bytes, little-endian, inside a byte table */',
             '#define PROSE_W(x) (uint8_t)((x) & 0xFF), (uint8_t)(((x) >> 8) & 0xFF)', '',
             '/* where each table lies; prose_rom_builtin copies them into the ROM image (ds_layout.c) */',
             'typedef struct {', '\tuint16_t off, size;', '\tconst void *data;', '\tconst char *name;',
             '} prose_ds_span;', 'extern const prose_ds_span prose_ds_layout[];',
             'extern const unsigned prose_ds_layout_count;', '']
        structs = []
        for t in self.t:
            if isinstance(t.kind, S) and t.kind not in structs:
                structs.append(t.kind)
        for s in structs:
            h += ['/* %s */' % s.doc, 'typedef struct {']
            h += ['\t%s %s%s;' % (CTYPE[f.kind], f.name, '[%d]' % f.n if f.n > 1 else '') for f in s.fields]
            h += ['} %s;' % s.name, 'PROSE_DS_ASSERT(%s, sizeof(%s) == %d);' % (s.name, s.name, s.size), '']
        h.append('/* the tables, in DS order: offset, then the array (src/data/ds_<file>.c) */')
        for t in self.t:
            ctype = t.kind.name if isinstance(t.kind, S) else 'uint8_t' if t.kind == 'pool' else CTYPE[t.kind]
            dims = '[%d][%d]' % (t.count, t.cols) if t.cols else '[%d]' % t.count
            h.append('#define PROSE_DS_%s 0x%04Xu' % (t.name.upper(), t.off))
            h.append('extern const %s prose_ds_%s%s;' % (ctype, t.name, dims))
        return h + ['', '#endif', '']

    def layout_c(self):
        c = [NOTE, '#include "prose_ds_tables.h"', '', 'const prose_ds_span prose_ds_layout[] = {']
        c += ['\t{0x%04X, sizeof prose_ds_%s, prose_ds_%s, "%s"},' % (t.off, t.name, t.name, t.name) for t in self.t]
        return c + ['};', 'const unsigned prose_ds_layout_count = sizeof prose_ds_layout / sizeof prose_ds_layout[0];', '']

    def write(self, out):
        files = {'prose_ds_tables.h': self.header(), 'ds_layout.c': self.layout_c()}
        for name in FILES:
            c = [NOTE, '#include "prose_ds_tables.h"', '']
            for t in self.t:
                if t.file == name:
                    c += self.c_table(t) + ['']
            files['ds_%s.c' % name] = c
        for name, lines in files.items():
            with open(os.path.join(out, name), 'w', newline='\n') as f:
                f.write('\n'.join(lines))
        return len(self.t)
```

- [ ] **Step 4: Write the coarse table list**

Create `tools/ds_spec.py`:
```python
"""The tables of the Prose 2000 v3.4.1 data segment DS:0000-AEA9, for ds_gen.py (REFERENCE §9-15).

Each table: T(name, DS offset, kind, size=..., file=..., doc=..., status='full'|'partial'|'opaque', ...). A table
without size runs to the next one. Tasks 3-6 of docs/superpowers/plans/2026-09-28-ds-structured-tables.md replace
the coarse raw_* regions with the real tables.
"""
from ds_gen import F, S, T

# far pointers of the generator's routine lists -> the C routine (src/paramgen/pg_segment.c routines[])
ROUTINES = {
    0xD3FFB: 'pg_shift_aspiration', 0xDEBFA: 'pg_voiceless_onset', 0xDEC87: 'pg_after_closure',
    0xDEE85: 'pg_sonorant_onset', 0xDF245: 'pg_vowel', 0xDFB5B: 'pg_sonorant_consonant',
    0xDFE87: 'pg_obstruent_voicing', 0xE01C0: 'pg_fricative_amps', 0xE067F: 'pg_closure_types',
    0xE08EA: 'pg_stop_burst', 0xE2FB8: 'pg_finalize',
}

RAW = dict(status='opaque', doc='not yet split into tables')
TABLES = [
    T('raw_0000', 0x0000, 'u8', file='boot', **RAW),
    T('raw_03d0', 0x03D0, 'u8', file='textrules', **RAW),
    T('raw_212e', 0x212E, 'u8', file='lexical', **RAW),
    T('raw_52f8', 0x52F8, 'u8', file='frame', **RAW),
    T('raw_6052', 0x6052, 'u8', file='prosody', **RAW),
    T('raw_626c', 0x626C, 'u8', file='paramgen', **RAW),
    T('raw_9be8', 0x9BE8, 'u8', file='lexical', **RAW),
]
```

- [ ] **Step 5: Hook the generator into `rom_extract.py`**

In `tools/rom_extract.py`:

1. Replace the `REGIONS` list with the lexicon only, and add the DS range:
```python
# (C name, linear start, linear end (exclusive), description)
REGIONS = [
    ('prose_lexicon', 0xE9000, 0xF268E,
     'segment E900 (linear E9000-F268D): the pronunciation lexicon, index at E900:0200 (REFERENCE 9.3-9.6)'),
]
# the data segment F410, DS:0000-AEA9 (linear F4100-FEFA9): written as structured tables by ds_gen.py
DS_START, DS_END = 0xF4100, 0xFEFAA
```
2. In `check_layout`, change the `keep` line to
```python
    keep = [(0xD3000, 0xE582E), (0xFFFF0, 0xFFFF5), (DS_START, DS_END)] + [(a, b) for _, a, b, _ in REGIONS]
```
3. At the end of `main()`, replace the last two statements (the `total = …` line and the `print`) with
```python
    import ds_gen
    import ds_spec
    n = ds_gen.Gen(img[DS_START - BASE:DS_END - BASE], ds_spec.TABLES).write(args.out)
    total = sum(b - a for _, a, b, _ in REGIONS) + 12 + 1024
    print('wrote %s: %d bytes of data; %s: %d tables (DS:0000-AEA9)'
          % (os.path.join(args.out, 'prose_data.c'), total, os.path.join(args.out, 'ds_*.c'), n))
```
4. In the module docstring, change `The output, prose_data.c and prose_data.h, lets` to `The output (prose_data.c/.h, and the data-segment tables prose_ds_tables.h and ds_*.c from ds_gen.py) lets`.

- [ ] **Step 6: Generate**

```bash
python tools/rom_extract.py prose_v3
```
Expected: `wrote …prose_data.c: 39578 bytes of data; …ds_*.c: 7 tables (DS:0000-AEA9)`. Check that `grep -c prose_ds_data src/data/prose_data.[ch]` prints `0` twice, and that `PROSE_DS_FNV1A64` in `src/data/prose_ds_tables.h` matches the value from Task 1 Step 4.

- [ ] **Step 7: Assemble the image from the layout list**

Replace `src/common/prose_rom.c` with:
```c
#include "prose_rom.h"

#include "prose_data.h"
#include "prose_ds_tables.h"

#include <string.h>

void prose_rom_builtin(prose_rom *rom)
{
	unsigned i;
	memset(rom->image, 0xFF, sizeof rom->image);
	memcpy(rom->image + (PROSE_LEXICON_BASE - 0xC0000u), prose_lexicon, sizeof prose_lexicon);
	for (i = 0; i < prose_ds_layout_count; i++)
		memcpy(rom->image + (PROSE_DS_DATA_BASE - 0xC0000u) + prose_ds_layout[i].off, prose_ds_layout[i].data,
		       prose_ds_layout[i].size);
	memcpy(rom->lane_sum, prose_rom_lane_sums, sizeof rom->lane_sum);
}
```
In `src/common/prose_rom.h`, replace the comment above `void prose_rom_builtin(prose_rom *rom);` with:
```c
/* The image from the data extracted into src/data (tools/rom_extract.py): the lexicon segment, the data-segment
   tables (prose_ds_tables.h, placed by the layout list) and the lane sums of the full ROM. The code areas read as
   FF; the C never reads them. */
```
In `src/CMakeLists.txt`, replace `add_library(prose_data STATIC data/prose_data.c)` with:
```cmake
add_library(prose_data STATIC data/prose_data.c data/ds_layout.c data/ds_boot.c data/ds_textrules.c
	data/ds_lexical.c data/ds_frame.c data/ds_prosody.c data/ds_paramgen.c)
```

- [ ] **Step 8: Run the test and the changeover comparison**

```bash
cmake --build build-ds 2>&1 | grep -i -E 'warning|error' ; build-ds/ds_image_check.exe build-ds/ds_new.bin && cmp build-ds/ds_old.bin build-ds/ds_new.bin && echo SAME
```
Expected: no warnings or errors, then `7 tables, DS:0000-AEA9: equal to the ROM`, then `SAME`.

- [ ] **Step 9: Check that the test catches a change (mutation)**

```bash
sed -i '0,/0x/s/0x\([0-9A-F][0-9A-F]\),/0xEE,/' src/data/ds_frame.c && cmake --build build-ds --target ds_image_check >/dev/null && build-ds/ds_image_check.exe; echo "exit $?"; git checkout -- src/data/ds_frame.c 2>/dev/null || python tools/rom_extract.py prose_v3
```
Expected: `… DIFFERENT` and `exit 1`. The file is untracked at this point, so the command regenerates it. Rebuild afterwards and check that `ds_image_check` passes again.

- [ ] **Step 10: Regression**

```bash
cmake --build build-ds && bash build-ds/regress.sh now
```
Expected: `regress: all 16 outputs identical`.

- [ ] **Step 11: Commit**

```bash
git add src/tests/ds_image_check.c src/common/prose_rom.c src/common/prose_rom.h src/CMakeLists.txt src/data/
git commit -m "Build the data segment from a layout of tables, with a check against the ROM

prose_ds_data is replaced by tables placed by a layout list (src/data/prose_ds_tables.h,
ds_layout.c, ds_*.c, generated by tools/ds_gen.py); for now seven coarse regions.
ds_image_check verifies the layout and the FNV-1a hash of DS:0000-AEA9.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Flat tables: boot, class masks and features, frame, prosody

**Files:**
- Modify: `tools/ds_spec.py` (local only)
- Generated: `src/data/ds_boot.c`, `ds_lexical.c`, `ds_frame.c`, `ds_prosody.c`, `prose_ds_tables.h`, `ds_layout.c`

**Interfaces:**
- Consumes: `T`, `S`, `F` from Task 2.
- Produces offset macros that later docs refer to, among them `PROSE_DS_FEATURES` (00A8), `PROSE_DS_T_COS` (58CA), `PROSE_DS_LOW_DEFAULT` (61FE) and `PROSE_DS_DEMO_TEXT` (6224).

- [ ] **Step 1: Replace `raw_0000`, `raw_52f8` and `raw_6052` in `TABLES`**

Delete the three lines `T('raw_0000', …)`, `T('raw_52f8', …)` and `T('raw_6052', …)`, and insert these entries (the list is sorted by offset when generated, but keep it in DS order for reading):
```python
    # --- boot and host: ds_boot.c; class masks and features: ds_lexical.c
    T('ram_ptrs', 0x0000, 'u16', 12, 'boot', 'six RAM addresses (EBF0 C202 DD80 DBCC EE7A EE5C); no reader found',
      'opaque'),
    T('lane_sums', 0x000C, 'u16', 16, 'boot', 'rom_checksum: byte sums of ROM lanes 0-7, lanes 0-1 unused (REFERENCE §14)'),
    T('ds_001c', 0x001C, 'u8', 4, 'boot', 'no reader found', 'opaque'),
    T('stage_accept', 0x0020, 'u16', 16, 'boot', 'the bit each node kind sets in a stage window (REFERENCE §15.1)'),
    T('ds_0030', 0x0030, 'u16', 24, 'boot', 'a symmetric curve 0252 ... 7ED8 ... 0252; no v3 reader', 'opaque'),
    T('class_masks', 0x0048, 'u16', 96, 'lexical',
      'CLASS_MASKS: low byte = feature bit, high byte = plane << 1 (REFERENCE §9.4, §12.5)'),
    T('features', 0x00A8, 'u8', 0x280, 'lexical',
      'FEATURES: 5 planes of 128 bytes indexed by character; bit use verified, bit names inferred (REFERENCE §9.3, '
      '§12.6)', 'partial', cols=0x80),
    T('tr_attr_ptr', 0x03A8, 'u16', 2, 'boot', 'TR_ATTR_PTR: the text-rule attribute record in RAM (DS:DB04)'),
    T('int8_stub', 0x03AA, 'u8', 15, 'boot', 'INT8_STUB: 8086 code copied to 0000:0024 for ESC[nL (REFERENCE §2, §8.2)'),
    T('ds_03b9', 0x03B9, 'u8', 1, 'boot', 'padding', 'opaque'),
    T('speed_cap', 0x03BA, 'u16', 20, 'boot', 'SPEED_CAP: the highest speed for each fast-read level (REFERENCE §8.2)',
      fmt='%d'),
    T('identity', 0x03CE, 'u8', 1, 'boot', 'IDENTITY: the ESC[E answer, 34', fmt='%d'),
    T('ds_03cf', 0x03CF, 'u8', 1, 'boot', 'padding', 'opaque'),

    # --- frame builder and voices: ds_frame.c (REFERENCE §11.3, §12.1, §13)
    T('voice_scale', 0x52F8, 'u16', 16, 'frame', 'VOICE_SCALE: formant scale per voice, q15'),
    T('voice_src', 0x5308, 'u8', 32, 'frame', 'VOICE_SRC: source bytes p18-p21 per voice'),
    T('voice_pitch', 0x5328, 'u16', 16, 'frame', 'default pitch per voice (85, 75, 110; ESC[V, REFERENCE §8.2)',
      fmt='%d'),
    T('ds_5338', 0x5338, 'u16', 16, 'frame', 'per voice (000D 000D 000F ...); no reader found', 'opaque'),
    T('voice_f4', 0x5348, 'u16', 16, 'frame', 'VOICE_F4: F4 offset per voice'),
    T('voice_f4_max', 0x5358, 'u16', 16, 'frame', 'VOICE_F4_MAX: F4 cap per voice (REFERENCE §12.5a)'),
    T('voice_frame', 0x5368, 'u16', 128, 'frame',
      'T_VOICE_*: 8 rows of 8 per-voice frame words: w17, F0 scale (F0_SCALE), w38, w39, w37, w4, w5, w21', cols=8),
    T('t_jitter_depth', 0x53E8, 'u16', 32, 'frame', 'T_JITTER_DEPTH: by the low nibble of p20'),
    T('t_shimmer_depth', 0x5408, 'u16', 32, 'frame', 'T_SHIMMER_DEPTH: by p21'),
    T('t_jitter', 0x5428, 'i16', 128, 'frame', 'T_JITTER: values picked by the LFSR'),
    T('t_vgain_scale', 0x54A8, 'u16', 64, 'frame', 'T_VGAIN_SCALE: by p18', 'partial'),
    T('t_vgain', 0x54E8, 'u16', None, 'frame', 'T_VGAIN: voicing gain by pitch period (frame word 1)', 'partial'),
    T('t_av_by_p18', 0x55E4, 'u16', 64, 'frame', 'T_AV_BY_P18: AV scaling by p18'),
    T('t_av_by_p19', 0x5624, 'u16', 32, 'frame', 'T_AV_BY_P19: AV scaling by p19'),
    T('dsp_boot', 0x5644, 'u16', 18, 'frame', 'the DSP boot block: 1, then 8 set-up words (dsp_boot, REFERENCE §11.4)'),
    T('t_template', 0x5656, 'u16', 80, 'frame', 'T_TEMPLATE: the 40-word frame template'),
    T('t_nz_gain', 0x56A6, 'u16', 36, 'frame', 'T_NZ_GAIN: nasal-zero gain by FN >> 2'),
    T('t_exp1', 0x56CA, 'u16', 256, 'frame', 'T_EXP1: 8192 exp(-pi B T), B in 4 Hz steps', fmt='%d'),
    T('t_exp2', 0x57CA, 'u16', 256, 'frame', 'T_EXP2: exp(-2 pi B T), B in 4 Hz steps', fmt='%d'),
    T('t_cos', 0x58CA, 'i16', 1024, 'frame', 'T_COS: 16384 cos(2 pi F T), F in 8 Hz steps'),
    T('t_db', 0x5CCA, 'u16', None, 'frame', 'T_DB: dB to linear (also read by v1_map.c)', fmt='%d'),
    T('t_par_corr1', 0x5E74, 'u16', None, 'frame', 'T_PAR_CORR1: parallel-amplitude correction for formant spacing',
      'partial'),
    T('t_par_corr2', 0x5F66, 'u16', None, 'frame', 'T_PAR_CORR2: parallel-amplitude correction; the tail is zeros',
      'partial'),

    # --- prosody, host settings and test mode: ds_prosody.c
    T('pause_scale', 0x6052, 'u16', 52, 'prosody', 'PAUSE_SCALE: per speed 0-25 (REFERENCE §15.2)', fmt='%d'),
    T('speed_pct', 0x6086, 'u16', 52, 'prosody', 'SPEED_PCT: duration percentage per speed 0-25', fmt='%d'),
    T('break_mask', 0x60BA, 'u8', 30, 'prosody', 'BREAK_MASK: phrase-break masks per speed'),
    T('cluster_pct', 0x60D8, 'u16', 8, 'prosody', 'CLUSTER_PCT: by NEXT_PLACE', fmt='%d'),
    T('low_max', 0x60E0, 'u16', 44, 'prosody', 'LOW_MAX: ESC[l maxima per parameter (REFERENCE §8.2)', fmt='%d'),
    T('track_default', 0x610C, 'u8', 22, 'prosody', 'TRACK_DEFAULT: rest value of each of the 22 tracks (REFERENCE §12.1)'),
    T('test_rows', 0x6122, 'u8', 220, 'prosody',
      'ESC[t test-mode rows of 22 track bytes, FF = computed (REFERENCE §8.2); p2/p3 inferred', 'partial', cols=22),
    T('low_default', 0x61FE, 'u8', 22, 'prosody', 'LOW_DEFAULT: ESC[l defaults per parameter'),
    T('av_by_f0', 0x6214, 'u8', 16, 'prosody', 'test mode: AV correction by F0 >> 4', 'partial'),
    T('demo_text', 0x6224, 'pool', None, 'prosody', 'the self-test text (DIP S4-7)', render='text'),
    T('demo_text_ptr', 0x626A, 'u16', 2, 'prosody', 'DEMO_TEXT_PTR', ptr=0),
```
The positional arguments of `T` are `(name, off, kind, size, file, doc, status)`.

- [ ] **Step 2: Generate and check**

```bash
python tools/rom_extract.py prose_v3 && cmake --build build-ds 2>&1 | grep -i -E 'warning|error'; build-ds/ds_image_check.exe
```
Expected: the generator prints `… N tables`. If it prints `ds_spec: X covers …, but the next table starts at …` or `not a whole number`, the size or offset of X (or of its neighbour) is wrong: compare with REFERENCE §11.3 / §12 / §15.2 and fix it; never change a size to make a gap disappear without finding what fills the gap. Then there should be no compiler output and `ds_image_check` should print `… equal to the ROM`.

- [ ] **Step 3: Read the output**

Open `src/data/ds_frame.c` and check three tables by eye:
- `prose_ds_t_cos` starts at 16384 and falls.
- `prose_ds_voice_pitch` holds 85, 75, 110.
- `prose_ds_demo_text` shows readable characters.

In `ds_prosody.c`, `prose_ds_demo_text_ptr` should read `PROSE_DS_DEMO_TEXT,`.

- [ ] **Step 4: Regression**

```bash
bash build-ds/regress.sh now
```
Expected: `regress: all 16 outputs identical`.

- [ ] **Step 5: Commit**

```bash
git add src/data/
git commit -m "Split the boot, frame and prosody tables of the data segment

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Text-rule tables

**Files:**
- Modify: `tools/ds_spec.py` (local only)
- Generated: `src/data/ds_textrules.c`, header, layout

**Interfaces:**
- Consumes: `T` from Task 2.
- Produces: `PROSE_DS_TR_PATTERNS` (03D0), `PROSE_DS_TR_LIST_ITEMS` (11A8), `PROSE_DS_TR_LISTS` (1434), `PROSE_DS_TR_WORD_CHARS` (145E), `PROSE_DS_TR_PROGRAM` (146E), `PROSE_DS_TR_STRINGS` (2053).

Background (verified 2026-09-28 from `match_list`, `src/textrules/tr_stage.c:396`): a list is a 0-terminated array of **pattern** pointers, and each replacement is stored directly after its pattern's 0 byte (there is no pair record). The last pattern, "wed" at 11A4, has its replacement read from DS:11A8, the start of the lists. That is the firmware's data; leave it as it is.

- [ ] **Step 1: Replace `raw_03d0`**

Delete `T('raw_03d0', …)` and insert:
```python
    # --- text rules: ds_textrules.c (REFERENCE §15.4)
    T('tr_patterns', 0x03D0, 'pool', None, 'textrules',
      'match_list patterns, each followed by its replacement, both 0-terminated', render='text'),
    T('tr_list_items', 0x11A8, 'u16', None, 'textrules', 'the lists: 0-terminated arrays of pattern pointers',
      ptr=0),
    T('tr_lists', 0x1434, 'u16', 42, 'textrules', 'TR_LISTS: the 21 lists', ptr=0),
    T('tr_word_chars', 0x145E, 'u8', 16, 'textrules', 'TR_WORD_CHARS: characters that end a word besides letters and 1-4'),
    T('tr_program', 0x146E, 'u8', None, 'textrules',
      'TR_PROGRAM: the text-rule byte code; goto / call targets are relative to DS:146E (not yet disassembled)'),
    T('tr_strings', 0x2053, 'pool', None, 'textrules', 'TR_STRINGS: 0-terminated character sets and inserted text',
      render='text', split_zero=True),
```

- [ ] **Step 2: Generate and check**

```bash
python tools/rom_extract.py prose_v3 && cmake --build build-ds 2>&1 | grep -i -E 'warning|error'; build-ds/ds_image_check.exe
```
Expected: no compiler output, then `equal to the ROM`. Check that `grep -c 'PROSE_DS_TR_PATTERNS' src/data/ds_textrules.c` is about 305 (one per pattern pointer), and that the comment on the first entry of `prose_ds_tr_list_items` shows a readable pattern.

- [ ] **Step 3: Regression**

```bash
bash build-ds/regress.sh now
```
Expected: `regress: all 16 outputs identical`.

- [ ] **Step 4: Commit**

```bash
git add src/data/
git commit -m "Split the text-rule tables of the data segment

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Lexical tables: lexicon states, LTS rules, affixes, durations

**Files:**
- Modify: `tools/ds_spec.py` (local only)
- Generated: `src/data/ds_lexical.c`, header, layout

**Interfaces:**
- Consumes: `T`, `S`, `F` from Task 2.
- Produces: struct types `prose_lts_pair`, `prose_lts_rule` and `prose_affix`, with offsets `PROSE_DS_LTS_RULES` (37FE), `PROSE_DS_LTS_LETTER_RULES` (52C0), `PROSE_DS_SUFFIX_RECORDS` (A20C), `PROSE_DS_PREFIX_RECORDS` (AA5E) and `PROSE_DS_INH` / `PROSE_DS_MIN` / `PROSE_DS_PLACE`.

Background (verified 2026-09-28 by walking the pointers in the dump):
- The LTS pool 21A6-37FD divides into:
  - 21A6: a shared pair, an empty pattern and an empty letter string
  - 21AC-291F: output strings
  - 2920-2E30: letter strings, stored reversed
  - 2E31-33E1: context patterns
  - 33E2-37FD: 263 word pairs
- Rules 37FE-52BF: 685 records of 10 bytes. Field +6 (condition) and field +8 (pass-on) point to pairs, not to inline words (`lts_class_match`, `src/lexical/lx_lts.c:165`).
- `LTS_RULES` (52C0) has **28** entries, one per character 0x40-0x5B. The 28th, at 52F6, is not a gap.
- Affixes: 94 suffix records A20C-A5B7 and 60 prefix records AA5E-ACB5, 10 bytes each (`affix_match`, `lx_lexicon.c:253`). Each record set has a pool before it holding strings, patterns and next-lists, and pointer lists after it.

- [ ] **Step 1: Add the record types above `TABLES`**

```python
LTS_PAIR = S('prose_lts_pair', [F('state'), F('cls')],
             'LTS condition / pass-on pair: a rule\'s +6 tests LTS_STATE and LTS_CLASS (bit 15 of a word: none / any '
             'present); +8 gives the flags and the class passed on (lts_class_match, lts_rules)')
LTS_RULE = S('prose_lts_rule', [F('letters', ptr=0, to='text'), F('output', ptr=0, to='text'),
                                F('pattern', ptr=0, to='text'), F('cond', ptr=0), F('pass_on', ptr=0)],
             'letter-to-sound rule: letters (reversed), phoneme output, right context pattern, condition pair, '
             'pass-on pair (lts_rules D78AE, REFERENCE §15.3)')
AFFIX = S('prose_affix', [F('string', ptr=0, to='text'), F('pattern', ptr=0, to='text'), F('lookup', 'u8', fmt='%d'),
                          F('word_class', 'u8', fmt='%d'), F('code', 'u8', fmt='%d'), F('pad', 'u8'),
                          F('next', ptr=0, to='words')],
          'affix record: string (suffixes reversed), context pattern, 1 = look the stem up, word class, stress or '
          'AFFIX_CODE, 0-terminated list of records to try next (affix_match E5152)')
```

- [ ] **Step 2: Replace `raw_212e` and `raw_9be8`**

Delete `T('raw_212e', …)` and `T('raw_9be8', …)`, and insert:
```python
    # --- lexicon unpacking and letter-to-sound: ds_lexical.c (REFERENCE §9.6, §15.3)
    T('letter_states', 0x212E, 'u8', 36, 'lexical', 'LETTER_STATES: lexicon key letter-packing states', cols=12),
    T('phoneme_states', 0x2152, 'u8', 32, 'lexical', 'PHONEME_STATES: 6-bit phoneme unpacking states', cols=8),
    T('entry_adjust', 0x2172, 'u8', 51, 'lexical', 'ENTRY_ADJUST: entry size by state * 17 + k', cols=17),
    T('ds_21a5', 0x21A5, 'u8', 1, 'lexical', 'padding', 'opaque'),
    T('lts_head', 0x21A6, 'pool', 6, 'lexical', 'a condition pair (0, 0), an empty pattern and an empty letter string'),
    T('lts_outputs', 0x21AC, 'pool', None, 'lexical', 'LTS phoneme outputs, 0-terminated', render='text'),
    T('lts_letters', 0x2920, 'pool', None, 'lexical', 'LTS letter strings, stored reversed', render='text'),
    T('lts_patterns', 0x2E31, 'pool', None, 'lexical',
      'LTS context patterns: strings ending in an empty one (lx_lts.c reads DS:2E3E directly)', render='text',
      starts=(0x2E3E,)),
    T('lts_pairs', 0x33E2, LTS_PAIR, None, 'lexical', 'the rules\' condition and pass-on pairs'),
    T('lts_rules', 0x37FE, LTS_RULE, None, 'lexical', 'the rules; each letter\'s rules run on until one matches'),
    T('lts_letter_rules', 0x52C0, 'u16', 56, 'lexical', 'LTS_RULES: the first rule for each character 0x40-0x5B',
      ptr=0),

    # --- affixes and durations: ds_lexical.c (REFERENCE §9.3, §15.2, §15.3)
    T('suffix_pool', 0x9BE8, 'pool', None, 'lexical',
      'suffix strings (reversed), context patterns and next-lists; lx_lexicon.c reads the patterns at 9C65, 9C97, '
      '9CA0 and 9D0B directly', render='text', starts=(0x9C65, 0x9C97, 0x9CA0, 0x9D0B)),
    T('suffix_records', 0xA20C, AFFIX, None, 'lexical',
      'suffixes; lx_lexicon.c compares the record addresses A25C, A310, A450, A4B4, A4F0, A54A and A55E'),
    T('suffix_lists', 0xA5B8, 'u16', None, 'lexical', '0-terminated lists of suffix records', ptr=0),
    T('suffixes', 0xA696, 'u16', 52, 'lexical', 'SUFFIXES: the list for each last letter A-Z', ptr=0),
    T('prefix_pool', 0xA6CA, 'pool', None, 'lexical', 'prefix strings, context patterns and next-lists',
      render='text'),
    T('prefix_records', 0xAA5E, AFFIX, None, 'lexical', 'prefixes'),
    T('prefix_lists', 0xACB6, 'u16', None, 'lexical', '0-terminated lists of prefix records', ptr=0),
    T('prefixes', 0xAD50, 'u16', 52, 'lexical', 'PREFIXES: the list for each first letter A-Z', ptr=0),
    T('inh', 0xAD84, 'u8', 96, 'lexical', 'inherent duration by character 0x20-0x7F', fmt='%d'),
    T('inh_ptr', 0xADE4, 'u16', 2, 'lexical', 'INH_TABLE: DS:AD84 - 0x20, indexed by character', ptr=0x20),
    T('min', 0xADE6, 'u8', 96, 'lexical', 'minimum duration by character 0x20-0x7F', fmt='%d'),
    T('min_ptr', 0xAE46, 'u16', 2, 'lexical', 'MIN_TABLE: DS:ADE6 - 0x20', ptr=0x20),
    T('place', 0xAE48, 'u8', 96, 'lexical', 'place of articulation by character 0x20-0x7F'),
    T('place_ptr', 0xAEA8, 'u16', 2, 'lexical', 'PLACE_TABLE: DS:AE48 - 0x20', ptr=0x20),
```

- [ ] **Step 3: Generate and check**

```bash
python tools/rom_extract.py prose_v3 && cmake --build build-ds 2>&1 | grep -i -E 'warning|error'; build-ds/ds_image_check.exe
```
Expected: no compiler output, then `equal to the ROM`. Check the header for `PROSE_DS_ASSERT(prose_lts_rule, sizeof(prose_lts_rule) == 10);`, `prose_ds_lts_rules[685]`, `prose_ds_lts_pairs[263]`, `prose_ds_suffix_records[94]` and `prose_ds_prefix_records[60]`. Record 8 of `prose_ds_suffix_records` must be at DS:A25C (its comment says `8 DS:A25C`). `prose_ds_inh_ptr` must read `PROSE_DS_INH - 32,`.

- [ ] **Step 4: Regression**

```bash
bash build-ds/regress.sh now
```
Expected: `regress: all 16 outputs identical`. (Text 3 goes through affix stripping and letter-to-sound.)

- [ ] **Step 5: Commit**

```bash
git add src/data/
git commit -m "Split the lexical tables of the data segment: LTS rules, affixes, durations

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Parameter-generator tables

**Files:**
- Modify: `tools/ds_spec.py` (local only)
- Generated: `src/data/ds_paramgen.c`, header, layout

**Interfaces:**
- Consumes: `T`, `S`, `F` and `ROUTINES` from Task 2.
- Produces: struct types `prose_pg_action`, `prose_far_ptr`, `prose_pg_rule` and `prose_cons_block`, with offsets `PROSE_DS_PG_RULES` (7996), `PROSE_DS_TARGETS` (944C) and `PROSE_DS_PHONEME_MAP` (93EA).

Background (verified 2026-09-28 by walking the pointers in the dump):
- Rules 7996-8CFD: 621 records of `{condition, action list, routine list, second action list (always 0)}` (`src/paramgen/pg_segment.c:337-353`).
- Conditions 626C-6375 are byte code, and each ends with 0x18.
- Actions 6376-7245: 632 six-byte slots, 225 of them referenced.
- Action lists 725E-783D: 0-terminated word lists.
- Routine lists 783E-7995: 0-terminated lists of far pointers (segment DEB3, among others).
- The consonant blocks at 9756 are 8 blocks of 32 bytes: 30 target bytes for phoneme indices 29-58, then a base pointer equal to the block minus 29 (`pg_segment.c:97`).
- The phoneme map pointer 944A = 93CA points 0x20 before the map at 93EA, which is indexed by character.
- The nasal-FN pointer 98C0 = 9886 points 52 before the 6 bytes at 98BA.

- [ ] **Step 1: Add the record types above `TABLES`**

```python
PG_ACTION = S('prose_pg_action', [F('op', 'u8'), F('voices', 'u8'), F('value', 'i16'), F('addr')],
              'generator action: op, voice mask, value, RAM address (DS:DE2E-EBCA) it acts on '
              '(paramgen_rule_action, REFERENCE §12.5)')
FAR_PTR = S('prose_far_ptr', [F('off'), F('seg')], 'far pointer (offset, segment) to a generator routine; 0:0 ends a list',
            note=lambda v: ROUTINES.get(v['seg'] * 16 + v['off'], ''))
PG_RULE = S('prose_pg_rule', [F('cond', ptr=0), F('actions', ptr=0), F('routines', ptr=0), F('actions_b', ptr=0)],
            'generator rule: condition, action list, routine list, second action list (paramgen_apply_rules, '
            'REFERENCE §12.5)')
CONS_BLOCK = S('prose_cons_block', [F('target', 'u8', 30), F('base', ptr=29)],
               'consonant targets of one parameter (AF ... AB): bytes for phoneme indices 29-58, then the block\'s '
               'address minus 29, which the generator indexes by phoneme')
```

- [ ] **Step 2: Replace `raw_626c`**

Delete `T('raw_626c', …)` and insert:
```python
    # --- parameter generator: ds_paramgen.c (REFERENCE §12, VOICE_CONTEXTS.md)
    T('pg_conditions', 0x626C, 'pool', None, 'paramgen', 'rule conditions: byte code, each ending with 0x18'),
    T('pg_actions', 0x6376, PG_ACTION, None, 'paramgen', 'actions; 225 of the 632 are referenced'),
    T('ds_7246', 0x7246, 'u8', 24, 'paramgen', 'no reference found', 'opaque'),
    T('pg_action_lists', 0x725E, 'u16', None, 'paramgen', '0-terminated lists of action pointers', ptr=0),
    T('pg_routines', 0x783E, FAR_PTR, None, 'paramgen', '0-terminated lists of routine far pointers'),
    T('pg_rules', 0x7996, PG_RULE, None, 'paramgen', 'the 621 rules, in 88 groups'),
    T('pg_groups', 0x8CFE, 'u16', 176, 'paramgen', 'the first rule of each group', ptr=0),
    T('pg_group_counts', 0x8DAE, 'u8', 88, 'paramgen', 'rules per group (sum 621)', fmt='%d'),
    T('pg_pair_rows', 0x8E06, 'u8', 0x570, 'paramgen', 'group index by (current, previous) phoneme: 24 rows of 58',
      cols=58, fmt='%d'),
    T('pg_pair_row_ptrs', 0x9376, 'u16', 116, 'paramgen', 'the row for each current phoneme', ptr=0),
    T('phoneme_map', 0x93EA, 'u8', 96, 'paramgen', 'phoneme character 0x20-0x7F -> phoneme index 0-57 (REFERENCE §12.3)',
      fmt='%d'),
    T('phoneme_map_ptr', 0x944A, 'u16', 2, 'paramgen', 'PHONEME_MAP_PTR: DS:93EA - 0x20, indexed by character',
      ptr=0x20),
    T('targets', 0x944C, 'u8', 464, 'paramgen',
      'T_F1 ... T_AV: rows F1 F2 F3 F4 B1 B2 B3 AV of 58 phoneme targets (REFERENCE §12.3)', cols=58, fmt='%d'),
    T('ds_961c', 0x961C, 'u16', 116, 'paramgen', 'per phoneme, copied to DS:EBB0; role not named', 'partial'),
    T('offglides', 0x9690, 'u8', 108, 'paramgen', 'diphthong offglides: rows F1 F2 F3 B1 B2 B3 of 18', cols=18,
      fmt='%d'),
    T('ds_96fc', 0x96FC, 'u16', 36, 'paramgen', 'per previous index, copied to DS:EBAE; role not named', 'partial'),
    T('onglide_hold', 0x9720, 'u16', 36, 'paramgen', 'onglide hold fraction per diphthong (REFERENCE §12.6)'),
    T('ds_9744', 0x9744, 'u8', 18, 'paramgen', 'per diphthong, copied to DS:EBBA; role not named', 'partial'),
    T('cons_targets', 0x9756, CONS_BLOCK, 256, 'paramgen', 'consonant targets for AF ... AB'),
    T('formant_dur', 0x9856, 'u8', 8, 'paramgen', 'formant transition duration by place (6 / 8 / 10)', fmt='%d'),
    T('neutral_f1', 0x985E, 'u16', 6, 'paramgen', 'NEUTRAL_F1: the neutral vowel F1 F2 F3 (490, 1450, 2500)', fmt='%d'),
    T('locus_weights', 0x9864, 'u16', 32, 'paramgen', 'locus weight matrix 4 x 4 (REFERENCE §12.5a)', cols=4),
    T('reduction', 0x9884, 'u16', 32, 'paramgen', 'REDUCTION: vowel-reduction pull'),
    T('default_dur', 0x98A4, 'u8', 22, 'paramgen', 'default transition duration per parameter (REFERENCE §12.2)',
      fmt='%d'),
    T('nasal_fn', 0x98BA, 'u8', 6, 'paramgen', 'FN for phoneme indices 52-57', fmt='%d'),
    T('nasal_fn_ptr', 0x98C0, 'u16', 2, 'paramgen', 'DS:98BA - 52, indexed by phoneme', ptr=52),
    T('next_class', 0x98C2, 'u8', 58, 'paramgen', 'class of each phoneme as a following sound (REFERENCE §12.5)',
      fmt='%d'),
    T('stop_locus_rows', 0x98FC, 'u8', 9, 'paramgen', 'stop locus row offsets for phoneme indices 33-41'),
    T('stop_loci', 0x9905, 'u8', 432, 'paramgen',
      'stop and next-class burst and locus bytes, 6 rows of 72 (to DS:EBC6/C8/CA/B6/B4 and AB; 7F = keep)',
      'partial', cols=72),
    T('ramps', 0x9AB5, 'pool', None, 'paramgen', 'k-frame smoothing ramps: fractions / 256, 0-terminated'),
    T('ramp_ptrs', 0x9B9E, 'u16', 42, 'paramgen', 'RAMPS: the ramp for k = 0-20 frames', ptr=0),
    T('bit_masks', 0x9BC8, 'u16', 32, 'paramgen', 'BITMASK: 1 << i, i = 0-15'),
```

- [ ] **Step 3: Generate and check**

```bash
python tools/rom_extract.py prose_v3 && cmake --build build-ds 2>&1 | grep -i -E 'warning|error'; build-ds/ds_image_check.exe; grep -c raw_ src/data/prose_ds_tables.h
```
Expected: no compiler output, then `equal to the ROM`, then `0` (no coarse regions left). Check that `prose_ds_pg_rules[621]`, `prose_ds_pg_actions[632]` and `prose_ds_cons_targets[8]` are in the header, that the first `prose_ds_pg_routines` entries carry routine names in their comments, and that `prose_ds_cons_targets` ends each record with `PROSE_DS_CONS_TARGETS + 32 * i - 29` (`PROSE_DS_CONS_TARGETS - 29` for record 0).

- [ ] **Step 4: Regression**

```bash
bash build-ds/regress.sh now
```
Expected: `regress: all 16 outputs identical`.

- [ ] **Step 5: Commit**

```bash
git add src/data/
git commit -m "Split the parameter-generator tables of the data segment

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Final checks (v1 build, Linux build) and documentation

**Files:**
- Modify: `REFERENCE.md` (§9.3 rows, §14), `AGENTS.md` (the `src/` row)
- Create: `build-ds/wsl_check.sh` (ignored)

- [ ] **Step 1: v1.1 configuration builds and runs the test**

```bash
cmake -S src -B build-ds-v1 -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release -DPROSE_VERSION=1 && cmake --build build-ds-v1 2>&1 | grep -i -E 'warning|error'; build-ds-v1/ds_image_check.exe
```
Expected: no compiler output, then `… equal to the ROM`.

- [ ] **Step 2: Linux build in WSL**

Create `build-ds/wsl_check.sh`:
```bash
#!/bin/bash
set -e
rm -rf ~/prose_ds && mkdir -p ~/prose_ds && cp -r /mnt/c/Users/abart/Desktop/prose2000_reborn/src ~/prose_ds/
cd ~/prose_ds
cmake -S src -B b -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build b 2>&1 | grep -i -E 'warning|error' || true
./b/ds_image_check
./b/prose_say -w /tmp/ds_check.wav "The data segment is built from tables." && ls -l /tmp/ds_check.wav
```
Run:
```bash
MSYS_NO_PATHCONV=1 wsl bash /mnt/c/Users/abart/Desktop/prose2000_reborn/build-ds/wsl_check.sh
```
Expected: no warning or error lines, then `… equal to the ROM`, then a WAV file of a few hundred KB.

- [ ] **Step 3: Update REFERENCE.md §14**

In §14:
- In the paragraph that starts `**The ROM's data contents are built in** (2026-09-25)`, after the sentence ending `…which the boot self-test (\`rom_checksum\`) compares with \`DS:0010-001A\`.`, insert:
  `Since 2026-09-28 the data segment is not one byte dump but N named, typed tables (\`src/data/ds_boot.c\`, \`ds_textrules.c\`, \`ds_lexical.c\`, \`ds_frame.c\`, \`ds_prosody.c\`, \`ds_paramgen.c\`; types, \`PROSE_DS_<NAME>\` offsets and the layout in \`prose_ds_tables.h\` and \`ds_layout.c\`). Record tables are structs (LTS rules, affixes, generator rules and actions, consonant blocks), and pointers are written as \`PROSE_DS_<TABLE> + k\`. \`prose_rom_builtin\` copies each table to its offset. The tables were generated once by \`tools/ds_gen.py\` (table list \`tools/ds_spec.py\`, called by \`rom_extract.py\`) and may now be edited: \`ds_image_check\` checks the layout and the FNV-1a hash of DS:0000-AEA9.` Replace N with the table count that `build-ds/ds_image_check.exe` prints.
- In the file table, replace the `src/data/prose_data.c` row with two rows:
  `| \`src/data/prose_data.c\` | generated by \`tools/rom_extract.py\`: lexicon segment, DSP data ROM, ROM lane sums | equal to the dumps |`
  `| \`src/data/ds_*.c\`, \`prose_ds_tables.h\` | the data segment DS:0000-AEA9 as typed tables (generated once, then edited by hand) | **byte-exact** (\`ds_image_check\`) |`
- In the **Tests** list, add as the first bullet:
  `- \`ds_image_check [OUT.bin]\`: the layout list covers DS:0000-AEA9 without gaps or overlaps, and the image built from the tables has the dump's FNV-1a hash. Built in both versions.`
- Replace `**Next:** split the extracted data into named tables as their structures are documented; capture` with `**Next:** name the partial and opaque data-segment tables as they are understood, and move readers from \`rw(0x…)\` offsets to the typed tables; capture`.

- [ ] **Step 4: Fix the stale §9.3 rows**

Find the rows with `grep -n '544E\|612B\|94C1\|AE64' REFERENCE.md`. In §9.3:
- mark 544E as inside `T_JITTER` (DS:5428-54A7);
- mark 612B as inside the `ESC[t` test rows (DS:6122-61FD);
- change 94C1 from inferred to **verified**: the F3 target row (`T_F3`, DS:94C0 = 944C + 2 × 58, entry 1);
- change AE64 from inferred to **verified**: inside the place table (DS:AE48-AEA7).

Keep each row's existing wording otherwise.

- [ ] **Step 5: Update AGENTS.md**

In the `src/` row of the layout table, replace `the ROM's data is built in (\`src/data/prose_data.c\`, generated by \`tools/rom_extract.py\`, the only code that reads the dumps)` with `the ROM's data is built in (\`src/data/prose_data.c\` from \`tools/rom_extract.py\`, the only code that reads the dumps; the data segment as typed, editable tables in \`src/data/ds_*.c\`, guarded by \`ds_image_check\`, REFERENCE §14)`.

- [ ] **Step 6: Final regression**

```bash
cmake --build build-ds && build-ds/ds_image_check.exe && bash build-ds/regress.sh now
```
Expected: `equal to the ROM`, then `regress: all 16 outputs identical`.

- [ ] **Step 7: Commit**

```bash
git add REFERENCE.md AGENTS.md
git commit -m "Document the data-segment tables and fix stale table rows in §9.3

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

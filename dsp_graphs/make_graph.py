"""Draw the signal-flow topology of the Prose 2000's DSP program (uPD7720, v3.12) with Graphviz.

The structure is that of src/dsp/prose_synth.c (REFERENCE.md section 13); the frame word roles are those of
src/frame/frame_build.c (section 11.3). The fixed resonators (per-voice F5 and glottal filter, the frame template's
HF pole and nasal pole) are decoded from the data segment, read from the raw image the ds_image_check test writes
(src/data/ds_*.c built the ROM's DS:0000-AEA9 in memory; there is no more single prose_ds_data[] array to parse).

Build ds_image_check once (any CMake build directory; it also runs on every build, REFERENCE §14) and have it write
the image, then run this script:

    cmake --build <builddir> --target ds_image_check
    <builddir>/ds_image_check build-ds/ds.bin
    python dsp_graphs/make_graph.py [DS_IMAGE]      (needs Graphviz's dot on PATH; DS_IMAGE default build-ds/ds.bin)

writes synth_topology.dot, .svg and .png next to this script.
"""
import math
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DS_SIZE = 0xAEAA  # DS:0000-AEA9
DEFAULT_DS_IMAGE = os.path.join(HERE, '..', 'build-ds', 'ds.bin')
FS = 10000.0  # sample rate

# DS offsets of the frame builder's tables (src/frame/frame_build.c)
T_VOICE_W17, T_VOICE_W38, T_VOICE_W39, T_VOICE_W37 = 0x5368, 0x5388, 0x5398, 0x53A8
T_VOICE_F5A, T_VOICE_F5B = 0x53B8, 0x53C8
T_SETUP, T_TEMPLATE = 0x5646, 0x5656


def load_ds(path):
    if not os.path.isfile(path):
        raise SystemExit('%s not found: build ds_image_check and run it with this path to write the DS image, e.g.\n'
                          '  cmake --build <builddir> --target ds_image_check\n'
                          '  <builddir>/ds_image_check %s' % (path, path))
    data = open(path, 'rb').read()
    if len(data) != DS_SIZE:
        raise SystemExit('%s is %d bytes, expected %d (DS:0000-AEA9): re-run ds_image_check %s to rewrite it'
                          % (path, len(data), DS_SIZE, path))
    return data


DS = load_ds(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_DS_IMAGE)


def word(off, i=0):
    v = DS[off + 2 * i] | DS[off + 2 * i + 1] << 8
    return v - 0x10000 if v & 0x8000 else v


def resonator(a, b):
    """Poles of y = 2*a*y1 - b*y2 (Q15): a = r cos(theta), b = r^2. Returns (Hz, bandwidth Hz)."""
    r = math.sqrt(b / 32768.0)
    c = max(-1.0, min(1.0, a / 32768.0 / r))
    return math.acos(c) * FS / (2 * math.pi), -math.log(r) * FS / math.pi


def fmt_res(a, b):
    if b == 0:
        return 'flat (no poles)'
    f, bw = resonator(a, b)
    return f'{f:.0f} Hz / {bw:.0f} Hz'


tmpl = [word(T_TEMPLATE, i) for i in range(40)]
setup = [word(T_SETUP, i) for i in range(8)]
VOICES = range(3)
f5 = {v: fmt_res(word(T_VOICE_F5A, v), word(T_VOICE_F5B, v)) for v in VOICES}
glot = {v: fmt_res(word(T_VOICE_W38, v), word(T_VOICE_W39, v)) for v in VOICES}
w17 = {v: word(T_VOICE_W17, v) for v in VOICES}
hf = fmt_res(tmpl[2], tmpl[3])
np_ = fmt_res(tmpl[12], tmpl[13])
nz_bw = resonator(0, tmpl[33])[1]
f4_bw = resonator(0, tmpl[7])[1]

# ---- colours by update rate ----------------------------------------------------------------------------------------
FRAME, PITCH, VOICE, FIXED, OP = '#cfe3fa', '#fcd9b0', '#cdeccb', '#e2e2e2', '#ffffff'
EDGE_SIG, EDGE_CTL = '#333333', '#9a3fb8'


def esc(s):
    return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')


def block(name, title, lines, fill, addr=None):
    """A box: bold title (with the program address), then small detail lines."""
    head = f'<B>{esc(title)}</B>'
    if addr:
        head += f'  <FONT POINT-SIZE="9" COLOR="#666666">[{addr}]</FONT>'
    rows = [f'<TR><TD>{head}</TD></TR>']
    rows += [f'<TR><TD><FONT POINT-SIZE="9">{esc(l)}</FONT></TD></TR>' for l in lines]
    label = '<<TABLE BORDER="0" CELLBORDER="0" CELLSPACING="0" CELLPADDING="1">' + ''.join(rows) + '</TABLE>>'
    return f'  {name} [label={label}, fillcolor="{fill}"];'


def op(name, text):
    return f'  {name} [label="{text}", shape=circle, fixedsize=true, width=0.42, fontsize=15, fillcolor="{OP}"];'


def gain(name, text, fill=FRAME):
    return f'  {name} [label=<{text}>, shape=box, style="rounded,filled", fontsize=10, fillcolor="{fill}", margin="0.06,0.03"];'


def edge(a, b, label=None, ctl=False, **kw):
    attrs = []
    if label:
        attrs.append(f'label=<{label}>')
    if ctl:
        attrs += [f'color="{EDGE_CTL}"', f'fontcolor="{EDGE_CTL}"', 'style=dashed', 'arrowhead=vee']
    attrs += [f'{k}={v}' for k, v in kw.items()]
    return f'  {a} -> {b}' + (f' [{", ".join(attrs)}]' if attrs else '') + ';'



L = []
L.append('digraph prose_dsp {')
L.append('  graph [rankdir=TB, newrank=true, nodesep=0.3, ranksep=0.38, fontname="Helvetica", fontsize=12, '
         'labelloc=t, pad=0.3, label=<<B>Prose 2000 DSP (µPD7720, program v3.12): synthesizer topology, one '
         'sample every 100 µs (10 kHz)</B><BR/><FONT POINT-SIZE="10">Decompiled program: src/dsp/prose_synth.c '
         '(REFERENCE §13). [xxx] = program address. wN = word N of the 40-word frame the 8086 sends every 10 ms '
         '(§11.3). Resonator values are pole frequency / bandwidth.</FONT>>];')
L.append('  node [shape=box, style="rounded,filled", fontname="Helvetica", fontsize=11, color="#555555", margin="0.08,0.04"];')
L.append(f'  edge [fontname="Helvetica", fontsize=9, color="{EDGE_SIG}", arrowsize=0.7];')

# ---- control: frame fetch and pitch clock ---------------------------------------------------------------------------
L.append('  subgraph cluster_ctl { label=<<B>Control</B>>; style="rounded,dashed"; color="#9a3fb8"; fontcolor="#9a3fb8";')
L.append(block('fetch', 'Frame fetch', [
    'every 100 samples (RAM[3C] runs out)',
    'raise USF0, pulse P0 → 8086 IRQ (8259 IR0)',
    'w0: closing type (bits 0-4), silence (bit 7),',
    '     output offset ROM[0x145+n] (bits 8-12)',
    'w1: phase step = w1·0x517D >> 15',
    'w2-w39 → RAM[ROM[0x145−i] >> 3]',
    'no word: keep the frame once, then silence'], FRAME, '1A2'))
L.append(block('pitch', 'Pitch clock', [
    'counts the period down (RAM[3E])',
    'and the half period (RAM[3F])',
    'periods w30 / w31 alternate (jitter)',
    'unvoiced (period 0): restarts every sample'], PITCH, '15C'))
L.append('  }')

# ---- source ----------------------------------------------------------------------------------------------------------
L.append('  subgraph cluster_src { label=<<B>Sources</B>>; style=rounded; color="#888888";')
L.append(block('pulse', 'Glottal pulse', [
    'phase accumulator RAM[2E] += step',
    'shape: ROM[2 + phase_hi] − frac·slope',
    'opening: AV/2 − (AV/2)·shape',
    'closing (after wrap): AV·(1 − rate·(1 − shape)),',
    '     rate = ROM[0x103 + type], fine ramp if type ≥ 15',
    'then 0 until the next period',
    'AV = w34 / w35, alternating (shimmer)'], FRAME, '147'))
L.append(block('gdiff', 'First difference, ×8', ['saturating (radiation / flow derivative)'], OP, '064'))
L.append(gain('xw17', f'× w17<BR/><FONT POINT-SIZE="8">voice gain<BR/>{", ".join(f"V{v} {w17[v]}" for v in VOICES)}</FONT>', VOICE))
L.append(block('noise', 'Noise generator', [
    'k = RAM[2A] ^ RAM[3A]',
    'new = ((k >> 7) & 0x1F8) | (k << 6)',
    '0 → 0xAAAA (seed, setup word 2)'], OP, '07A'))
L.append(gain('xah', '× AH<BR/><FONT POINT-SIZE="8">w16</FONT>'))
L.append(op('ssrc', 'Σ'))
L.append('  }')

L.append(block('glot', 'Glottal filter', [
    'resonator, gain w37, coefficients w38 / w39',
    *[f'V{v} {glot[v]}' for v in VOICES]], VOICE, '128'))

# ---- cascade ---------------------------------------------------------------------------------------------------------
L.append('  subgraph cluster_casc { label=<<B>Cascade branch</B> (voicing + aspiration)>; style=rounded; color="#2f6db5"; fontcolor="#2f6db5";')
L.append(block('c5', 'R5 (F5)', ['w4 / w5, fixed per voice', f'V0 {f5[0]}', f'V1 {f5[1]}', f'V2 {f5[2]}'], VOICE, '11C'))
L.append(block('c4', 'R4 (F4)', ['w6 = F4 (p12)', f'w7 fixed: bandwidth {f4_bw:.0f} Hz'], FRAME, '11C'))
L.append(block('c3', 'R3 (F3)', ['w8 / w9 = F3, B3 (p11, p15)'], PITCH, '11C'))
L.append(block('c2', 'R2 (F2)', ['w10 / w11 = F2, B2 (p10, p14)'], PITCH, '11C'))
L.append(block('cnp', 'Nasal pole', ['w12 / w13, fixed', np_], FIXED, '11C'))
L.append(block('cnz', 'Nasal zero (FN)', ['antiresonator on the pole\'s output', 'w32 = FN (p16)', f'w33 fixed: bandwidth {nz_bw:.0f} Hz', 'no saturation'], PITCH, '136'))
L.append(block('c1', 'R1 (F1)', ['w14 / w15 = F1, B1 (p9, p13)'], PITCH, '11C'))
L.append(gain('xw36', '× 2·w36<BR/><FONT POINT-SIZE="8">R1 gain (×16)</FONT>', PITCH))
L.append('  }')

# ---- parallel --------------------------------------------------------------------------------------------------------
L.append('  subgraph cluster_par { label=<<B>Parallel branch</B> (noise only; all amplitudes 0 when AF = 0)>; style=rounded; color="#b5652f"; fontcolor="#b5652f";')
L.append(block('vmod', 'Voicing modulation', ['noise ÷ 2 in the second half', 'of each pitch period', '(RAM[3E] ^ RAM[3F] < 0)'], OP, '09F'))
L.append(block('phf', 'HF resonator', ['w2 / w3, fixed', hf, 'amp w18 ← A6 (p7)'], FIXED, '13C'))
L.append(block('p5', 'F5', ['coefficients of R5', 'amp w20 ← A5 (p6)'], VOICE, '13C'))
L.append(block('p4', 'F4', ['coefficients of R4', 'amp w22 ← A4 (p5)'], FRAME, '13C'))
L.append(block('p3', 'F3', ['coefficients of R3', 'amp w24 ← A3 (p4)'], PITCH, '13C'))
L.append(block('p2', 'F2', ['coefficients of R2', 'amp w26 ← A2 (p3)'], PITCH, '13C'))
L.append(block('pab', 'Bypass', ['amp w28 ← AB (p8)'], FRAME))
L.append(op('spar', 'Σ'))
L.append(gain('xpar', f'× 0x{setup[0] & 0xFFFF:04X}<BR/><FONT POINT-SIZE="8">setup word 0 (×{setup[0] / 32768:.3f})</FONT>', FIXED))
L.append(block('pdiff', 'First difference', [], OP))
L.append('  }')

# ---- output ----------------------------------------------------------------------------------------------------------
L.append('  subgraph cluster_out { label=<<B>Output</B>>; style=rounded; color="#888888";')
L.append(op('sout', 'Σ'))
L.append(block('deemph', 'One-pole filter', ['y = x − 0.23·y[n−1]  (0x1D71)'], OP, '0B1'))
L.append(block('bias', 'Bias and clip', ['+ w0 offset (one sample)', '+ 0x1001, clip to 0-0x1FFF', 'silence (w0 bit 7): 0x0FE0'], OP))
L.append(block('fifo', 'Output FIFO', ['RAM[00-0D], 14 slots'], OP))
L.append(block('sio', 'Sample interrupt', ['SOL: RAM[22] bit-reversed', 'onto the serial output'], OP, '100'))
L.append(block('dac', '12-bit DAC', ['10 kHz'], FIXED))
L.append('  }')

# ---- legend ----------------------------------------------------------------------------------------------------------
def swatch(fill, text):
    return f'<TR><TD BGCOLOR="{fill}" BORDER="1" COLOR="#555555" WIDTH="22"> </TD><TD ALIGN="LEFT">{esc(text)}</TD></TR>'


L.append('  key [shape=plaintext, style="", fontsize=9, label=<<TABLE BORDER="1" COLOR="#aaaaaa" CELLBORDER="0" '
         'CELLSPACING="3" CELLPADDING="2" STYLE="rounded"><TR><TD COLSPAN="2"><B>When a value changes</B></TD></TR>'
         + swatch(FRAME, 'every frame (10 ms)')
         + swatch(PITCH, 'pitch-synchronous: latched at each period start')
         + swatch(VOICE, 'per voice (ESC[nV), sent in every frame')
         + swatch(FIXED, 'fixed: frame template / setup words')
         + f'<TR><TD>——</TD><TD ALIGN="LEFT">signal</TD></TR>'
         + f'<TR><TD><FONT COLOR="{EDGE_CTL}">- - -</FONT></TD><TD ALIGN="LEFT"><FONT COLOR="{EDGE_CTL}">control</FONT></TD></TR>'
         + '</TABLE>>];')

# ---- signal edges ----------------------------------------------------------------------------------------------------
L += [
    edge('pulse', 'gdiff'), edge('gdiff', 'xw17'), edge('xw17', 'ssrc', 'voice'),
    edge('noise', 'xah'), edge('xah', 'ssrc', 'aspiration'),
    edge('ssrc', 'glot'),
    edge('glot', 'c5', '× w19<BR/><FONT POINT-SIZE="8">(0x2000)</FONT>'),
    edge('c5', 'c4', '× w21'), edge('c4', 'c3', '× w23'), edge('c3', 'c2', '× w25'),
    edge('c2', 'cnp', '× w27'), edge('cnp', 'cnz', '+ old y2'), edge('cnz', 'c1', '× w29'),
    edge('c1', 'xw36'), edge('xw36', 'sout', 'cascade'),
    edge('noise', 'vmod', 'noise'),
]
# the frame builder alternates the amplitudes' signs (frame_build.c): F3 and F5 negative
for p, sign in (('phf', '+'), ('p5', '−'), ('p4', '+'), ('p3', '−'), ('p2', '+'), ('pab', '+')):
    L.append(edge('vmod', p))
    L.append(edge(p, 'spar', sign))
L += [edge('spar', 'xpar'), edge('xpar', 'pdiff'), edge('pdiff', 'sout', 'parallel'),
      edge('sout', 'deemph'), edge('deemph', 'bias'), edge('bias', 'fifo'), edge('fifo', 'sio'), edge('sio', 'dac')]

# ---- control edges ---------------------------------------------------------------------------------------------------
L += [
    edge('fetch', 'pitch', 'w30, w31', ctl=True),
    edge('fetch', 'pulse', 'w0, w1, w34, w35', ctl=True),
    edge('pitch', 'pulse', 'period start: phase 0,<BR/>next AV, step, type', ctl=True),
    edge('pitch', 'vmod', 'half period', ctl=True),
]

# layout hints: keep the three chains in rows
L.append('  { rank=same; glot; vmod; }')
L.append('  { rank=same; sout; deemph; bias; fifo; sio; dac; }')
L.append('  { rank=same; fetch; pitch; key; }')
L.append('  { rank=same; pulse; noise; }')
L.append('}')

dot_src = '\n'.join(L) + '\n'
base = os.path.join(HERE, 'synth_topology')
with open(base + '.dot', 'w', encoding='utf-8', newline='\n') as f:
    f.write(dot_src)
dot = shutil.which('dot')
if not dot:
    sys.exit('dot not found: install Graphviz (the .dot file was written)')
subprocess.run([dot, '-Tsvg', base + '.dot', '-o', base + '.svg'], check=True)
subprocess.run([dot, '-Tpng', '-Gdpi=110', base + '.dot', '-o', base + '.png'], check=True)
print('wrote', base + '.{dot,svg,png}')

#!/usr/bin/env python3
"""Builds ARCO's built-in JAZZ KIT from Virtuosity Drums (Versilian Studios / Karoryfer, CC0-1.0):
for every piece, two velocity layers x two round robins; the close mic, the overheads and the room mixed
to one stereo sample, trimmed, faded, 48 kHz 16-bit FLAC. Usage: make_jazz_kit.py <virtuosity_drums checkout> <out dir>"""
import sys, os, json
import numpy as np, soundfile as sf
from scipy.signal import resample_poly

src, out = sys.argv[1], sys.argv[2]
plan = json.load(open(os.path.join(os.path.dirname(sys.argv[0]), 'jazz_plan.json')))
GAIN = {'kickmic': 1.0, 'snaremic': 0.9, 'oh': 0.75, 'room': 0.45, 'mid': 0.6}
MAXLEN = {'kick': 1.2, 'snare': 1.3, 'rim': 1.2, 'xstick': 0.7, 'hhc': 0.7, 'hhp': 0.7, 'hho': 2.2,
          'ride': 3.2, 'bell': 2.8, 'crash': 4.0, 'htom': 1.8, 'ltom': 2.2}
os.makedirs(out, exist_ok=True)

def load(path):
    x, sr = sf.read(path, always_2d=True, dtype='float64')
    if x.shape[1] == 1: x = np.repeat(x, 2, axis=1)
    if sr != 48000:
        x = resample_poly(x, 48000, sr, axis=0)
    return x

summary = []
for piece, p in plan.items():
    rendered = []
    for li, layer in enumerate(p['layers']):
        for ri, (v, r) in enumerate(layer):
            mix = None
            for mc in p['mics']:
                f = os.path.join(src, 'Samples', mc, p['fold'], '%s_%s_vl%d%s.flac' % (mc, p['art'], v, '_rr%d' % r if r else ''))
                x = load(f) * GAIN.get(mc, 0.6)
                if mix is None: mix = x
                else:
                    n = max(len(mix), len(x)); a = np.zeros((n, 2)); a[:len(mix)] += mix; a[:len(x)] += x; mix = a
            rendered.append((li, ri, mix))
    peak = max(np.abs(m).max() for _, _, m in rendered)
    for li, ri, m in rendered:
        m = m / peak * 0.89                       # the loudest hit of the piece at -1 dBFS; the soft layer keeps its level
        # start: the first sample above -60 dB (minus 1 ms)
        thr = 10 ** (-60 / 20) * np.abs(m).max()
        idx = np.argmax(np.abs(m).max(axis=1) > thr)
        m = m[max(0, idx - 48):]
        n = min(len(m), int(MAXLEN[piece] * 48000))
        # the end: where it falls under -66 dB, faded
        env = np.abs(m[:n]).max(axis=1)
        above = np.nonzero(env > 10 ** (-66 / 20))[0]
        n = min(n, int(above[-1]) + 2400 if len(above) else n)
        m = m[:n].copy()
        fade = min(n // 3, int(0.12 * 48000))
        m[n - fade:] *= np.linspace(1, 0, fade)[:, None] ** 2
        name = '%s_%d_%d.flac' % (piece, li, ri)
        sf.write(os.path.join(out, name), m.astype(np.float32), 48000, subtype='PCM_16', format='FLAC')
        summary.append((name, n / 48000))
tot = sum(os.path.getsize(os.path.join(out, f)) for f in os.listdir(out))
for s in summary: print('%-16s %.2f s' % s)
print('total %.1f MB' % (tot / 1e6))

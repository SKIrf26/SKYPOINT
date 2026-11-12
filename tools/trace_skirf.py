#!/usr/bin/env python3
"""Trace the SKIrF brand lockup (wordmark + broadcast tower) out of an existing SKIrF logo image
into clean vector paths, so the SkyPoint logo uses the exact same brand mark, recoloured.

Usage: trace_skirf.py banner.png out.json [x0 y0 x1 y1]
Writes {"word": d, "tower": d, "w": width, "h": height} in the crop's pixel units."""
import json, sys
import numpy as np
from PIL import Image, ImageFilter
import potrace

src, out = sys.argv[1:3]
im = Image.open(src).convert('RGB')
box = tuple(int(v) for v in sys.argv[3:7]) if len(sys.argv) > 6 else (330, 565, 700, 645)   # "SKIrF ((·))" on the Wardriver banner
crop = im.crop(box)
S = 8                                                # trace at 6x for smooth curves
big = crop.resize((crop.width * S, crop.height * S), Image.LANCZOS)
a = np.asarray(big).astype(np.float32)
r, g, b = a[..., 0], a[..., 1], a[..., 2]
orange = (r > 150) & (r - b > 90) & (g < 170)       # the wordmark
white = (r > 175) & (g > 175) & (b > 175)            # the tower icon

BLUR = 1.1   # smooths out the JPEG ripples along the letter edges
def trace(mask):
    m = Image.fromarray((mask * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(S * BLUR))
    bm = potrace.Bitmap(np.asarray(m) <= 127)   # potracer traces the False pixels
    path = bm.trace(turdsize=S * S * 4, alphamax=0.75, opticurve=True, opttolerance=0.6)
    d = []
    for c in path:
        sx, sy = c.start_point.x / S, c.start_point.y / S
        d.append(f'M{sx:.2f} {sy:.2f}')
        for seg in c.segments:
            if seg.is_corner:
                d.append(f'L{seg.c.x / S:.2f} {seg.c.y / S:.2f}L{seg.end_point.x / S:.2f} {seg.end_point.y / S:.2f}')
            else:
                d.append(f'C{seg.c1.x / S:.2f} {seg.c1.y / S:.2f} {seg.c2.x / S:.2f} {seg.c2.y / S:.2f} {seg.end_point.x / S:.2f} {seg.end_point.y / S:.2f}')
        d.append('Z')
    return ''.join(d)

# keep only the tower region for "white" (the "by" above is cropped out already)
json.dump({'word': trace(orange), 'tower': trace(white), 'w': crop.width, 'h': crop.height}, open(out, 'w'))
print('traced', crop.size)

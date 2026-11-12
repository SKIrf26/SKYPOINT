#!/usr/bin/env python3
"""SkyPoint logo, in the sticker style of the Wardriver / HUNTR-SEEKR logos, in a space palette.

Usage: make_logo.py orbitron-latin-900.woff2 saira-latin-700.woff2 skirf.json out.svg [mark.svg]
skirf.json is the SKIrF lockup traced from the other logos by trace_skirf.py.
Text is converted to outlines so the SVG needs no fonts. Render to PNG with any browser."""
import sys
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

import json
F900, F700, SKJ, OUT = sys.argv[1:5]
MARK = sys.argv[5] if len(sys.argv) > 5 else None
fonts = {9: TTFont(F900), 7: TTFont(F700)}

def text(s, x, y, size, w=9, track=0.0):
    """outline path for s with its baseline at y; returns (d, advance)"""
    f = fonts[w]; gs = f.getGlyphSet(); cmap = f.getBestCmap(); k = size / f['head'].unitsPerEm
    d, cx = [], x
    for ch in s:
        g = cmap[ord(ch)]
        pen = SVGPathPen(gs)
        gs[g].draw(TransformPen(pen, (k, 0, 0, -k, cx, y)))
        d.append(pen.getCommands())
        cx += gs[g].width * k + track * size
    return ' '.join(d), cx - x - track * size

def adv(s, size, w=9, track=0.0):
    return text(s, 0, 0, size, w, track)[1]

# palette: deep space, cyan, violet, white
INK, BODY, BODY2, BODY3 = '#05070d', '#0b1124', '#070b18', '#121a33'
CY, VI, WH, STEEL = '#5cf2ff', '#9d7bff', '#ffffff', '#33405e'

def sparkle(x, y, r, col, thin=0.22):
    t = r * thin
    return (f'<path d="M{x} {y-r} Q{x+t} {y-t} {x+r} {y} Q{x+t} {y+t} {x} {y+r} Q{x-t} {y+t} {x-r} {y} Q{x-t} {y-t} {x} {y-r}Z" fill="{col}"/>')

# ---------------------------------------------------------------- the device cube (isometric-ish)
Ft, Fb = (470, 300), (470, 520)          # front vertical edge
Lt, Lb = (255, 210), (255, 430)          # left edge (screen face is the big one)
Rt, Rb = (620, 225), (620, 445)          # right edge
Bt = (405, 135)                          # back top corner
P = lambda *ps: ' '.join(f'{x},{y}' for x, y in ps)
mL = 'matrix(1,0.4186,0,1,255,210)'      # local 215 x 220 -> screen face
mR = 'matrix(1,-0.5,0,1,470,300)'        # local 150 x 220 -> side face
mT = 'matrix(1,0.4186,1,-0.5,255,210)'   # local 215 x 150 -> top face

screen = f'''
<g transform="{mL}">
  <rect x="16" y="16" width="183" height="170" rx="12" fill="#040817" stroke="{WH}" stroke-width="5"/>
  <g fill="none" stroke="{WH}" stroke-width="5"><circle cx="107" cy="101" r="62"/></g>
  <g fill="none" stroke="{CY}" stroke-width="3"><circle cx="107" cy="101" r="40" opacity=".8"/><circle cx="107" cy="101" r="18" opacity=".6"/></g>
  <path d="M107 30v142M36 101h142" stroke="{CY}" stroke-width="2.5" opacity=".55"/>
  <path d="M107 33v20M107 149v20M39 101h20M155 101h20" stroke="{WH}" stroke-width="6" stroke-linecap="square"/>
  <g fill="{WH}"><circle cx="76" cy="74" r="4"/><circle cx="141" cy="128" r="3.5"/><circle cx="84" cy="136" r="3"/><circle cx="148" cy="66" r="3"/></g>
  <circle cx="128" cy="82" r="7" fill="{CY}"/>
  <path d="M117 71h-5v5M139 71h5v5M117 93h-5v-5M139 93h5v-5" fill="none" stroke="{CY}" stroke-width="3" stroke-linecap="square"/>
  <rect x="30" y="198" width="60" height="8" rx="2" fill="{STEEL}"/><rect x="98" y="198" width="22" height="8" rx="2" fill="{CY}"/><rect x="126" y="198" width="12" height="8" rx="2" fill="{STEEL}"/>
</g>'''
side = f'''
<g transform="{mR}">
  <g fill="none" stroke="{WH}" stroke-width="4.5" stroke-linecap="round" stroke-linejoin="round">
    <rect x="32" y="38" width="12" height="12" rx="1.5"/><path d="M18 44h10M48 44h10M38 38v-6"/>
    <path d="M38 98 C28 86 28 76 38 72 C48 76 48 86 38 98Z"/>
    <circle cx="38" cy="132" r="13"/><path d="M38 121l5 11-5 11-5-11z" fill="{WH}" stroke-width="1.5"/>
  </g>
  <circle cx="38" cy="82" r="3.5" fill="{WH}"/>
  <g fill="{WH}"><rect x="26" y="184" width="5" height="8" rx="1"/><rect x="34" y="178" width="5" height="14" rx="1"/><rect x="42" y="172" width="5" height="20" rx="1"/></g>
  <rect x="50" y="166" width="5" height="26" rx="1" fill="{CY}"/>
  <g fill="{STEEL}">{''.join(f'<rect x="78" y="{24+i*20}" width="48" height="10" rx="2"/>' for i in range(9))}</g>
  <rect x="78" y="24" width="48" height="10" rx="2" fill="{CY}"/><rect x="78" y="44" width="30" height="10" rx="2" fill="{CY}" opacity=".6"/>
</g>'''
top = f'''
<g transform="{mT}">
  <path d="M60 40h-16v70h16M155 40h16v70h-16" fill="none" stroke="{CY}" stroke-width="7" stroke-linecap="square"/>
  <ellipse cx="107" cy="75" rx="26" ry="26" fill="#040817" stroke="{WH}" stroke-width="5"/>
  <ellipse cx="107" cy="75" rx="11" ry="11" fill="{CY}"/>
</g>'''
cube = f'''
<polygon points="{P(Lt, Bt, Rt, Ft)}" fill="{BODY3}"/>
<polygon points="{P(Lt, Ft, Fb, Lb)}" fill="{BODY}"/>
<polygon points="{P(Ft, Rt, Rb, Fb)}" fill="{BODY2}"/>
{top}{screen}{side}
<g fill="none" stroke="{WH}" stroke-width="8" stroke-linejoin="round" stroke-linecap="round">
  <polygon points="{P(Lt, Bt, Rt, Rb, Fb, Lb)}"/>
  <polyline points="{P(Lt, Ft, Rt)}"/><polyline points="{P(Ft, Fb)}"/>
</g>'''

# ---------------------------------------------------------------- beam, star, orbit above the cube
lens = (255 + 107 + 75, 210 + 0.4186 * 107 - 0.5 * 75)   # top-face lens centre
SX, SY = lens[0] + 6, 52
MX, MY = lens[0], lens[1]
TOPY = MY - 82
def arc(r, w):
    import math
    a0, a1 = math.radians(-145), math.radians(-35)
    x0_, y0_ = MX + r * math.cos(a0), TOPY + r * math.sin(a0)
    x1_, y1_ = MX + r * math.cos(a1), TOPY + r * math.sin(a1)
    return f'<path d="M{x0_:.1f} {y0_:.1f} A{r} {r} 0 0 1 {x1_:.1f} {y1_:.1f}" fill="none" stroke="{CY}" stroke-width="{w}" stroke-linecap="square"/>'
sky = (f'<path d="M{MX-7:.1f} {MY-2:.1f} L{MX-4:.1f} {TOPY:.1f} H{MX+4:.1f} L{MX+7:.1f} {MY-2:.1f}Z" fill="{WH}"/>'
       f'<rect x="{MX-11:.1f}" y="{TOPY-6:.1f}" width="22" height="9" rx="2" fill="{WH}"/>'
       f'<circle cx="{MX:.1f}" cy="{TOPY-14:.1f}" r="6" fill="{CY}"/>'
       + arc(34, 10) + arc(60, 10) + arc(86, 10) +
       # locked target above the scan: a star in corner brackets
       f'<g transform="translate({MX:.1f} {TOPY-118:.1f})">'
       f'<path d="M0 -15 L4 -4 L15 0 L4 4 L0 15 L-4 4 L-15 0 L-4 -4Z" fill="{WH}"/>'
       f'<path d="M-24 -14v-10h10M24 -14v-10h-10M-24 14v10h10M24 14v10h-10" fill="none" stroke="{VI}" stroke-width="5" stroke-linecap="square"/></g>')
SX, SY = MX, TOPY - 118

# ---------------------------------------------------------------- wordmark
size, track = 124, 0.03
y0 = 668
wSKY = adv('SKY', size, 9, track); wPOINT = adv('POINT', size, 9, track)
gap = 0.06 * size
total = wSKY + gap + wPOINT
x0 = 500 - total / 2
dSKY, _ = text('SKY', x0, y0, size, 9, track)
dPOINT, _ = text('POINT', x0 + wSKY + gap, y0, size, 9, track)
# a little extra weight: Orbitron Black is lighter than the hand-drawn lettering on the other logos
word = (f'<path d="{dSKY}" fill="{WH}" stroke="{WH}" stroke-width="5" stroke-linejoin="miter"/>'
        f'<path d="{dPOINT}" fill="url(#word)" stroke="url(#word)" stroke-width="5" stroke-linejoin="miter"/>')

# "by" between two bars, then the SKIrF lockup (traced from the other logos), tight like theirs
cap = 0.72 * size
barY = y0 + 22
dBy, wBy = text('by', 0, 0, 42, 7)
dBy, _ = text('by', 500 - wBy / 2, barY + 18, 42, 7)
bars = (f'<rect x="{x0+4:.0f}" y="{barY:.0f}" width="{500-wBy/2-20-x0:.0f}" height="10" rx="2" fill="url(#barL)"/>'
        f'<rect x="{500+wBy/2+16:.0f}" y="{barY:.0f}" width="{x0+total-(500+wBy/2+16)-4:.0f}" height="10" rx="2" fill="url(#barR)"/>')
sk = json.load(open(SKJ))
SKW = total * 0.40                       # lockup width, about the same share of the wordmark as on HUNTR/SEEKR
k = SKW / sk['w']; SKH = sk['h'] * k
xs, ys = 500 - SKW / 2, barY + 30
brand = (f'<path d="{dBy}" fill="{WH}"/>{bars}'
         f'<g transform="translate({xs:.1f} {ys:.1f}) scale({k:.4f})"><path d="{sk["word"]}" fill="url(#skw)"/><path d="{sk["tower"]}" fill="{WH}"/></g>')
BOTTOM = ys + SKH * 0.86        # the traced crop has a little empty margin at the bottom

defs = f'''<defs>
  <linearGradient id="word" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="{CY}"/><stop offset="1" stop-color="{VI}"/></linearGradient>
  <linearGradient id="skw" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="{CY}"/><stop offset="1" stop-color="{VI}"/></linearGradient>
  <linearGradient id="barL" x1="0" x2="1"><stop offset="0" stop-color="{VI}"/><stop offset="1" stop-color="{CY}"/></linearGradient>
  <linearGradient id="barR" x1="0" x2="1"><stop offset="0" stop-color="{CY}"/><stop offset="1" stop-color="{VI}"/></linearGradient>
  <linearGradient id="beam" x1="0" y1="1" x2="0" y2="0"><stop offset="0" stop-color="{CY}"/><stop offset="1" stop-color="{CY}" stop-opacity=".25"/></linearGradient>
  <!-- die-cut sticker: round dilation of the artwork's silhouette, black then a thin white rim -->
  <filter id="sticker" x="-15%" y="-15%" width="130%" height="130%" color-interpolation-filters="sRGB">
    <feMorphology in="SourceAlpha" operator="dilate" radius="22" result="d"/>
    <feGaussianBlur in="d" stdDeviation="9" result="b"/>
    <feComponentTransfer in="b" result="black"><feFuncA type="linear" slope="20" intercept="-9.5"/></feComponentTransfer>
    <feMorphology in="black" operator="dilate" radius="5" result="white"/>
    <feFlood flood-color="{INK}"/><feComposite in2="black" operator="in" result="blackFill"/>
    <feFlood flood-color="{WH}"/><feComposite in2="white" operator="in" result="whiteFill"/>
    <feMerge><feMergeNode in="whiteFill"/><feMergeNode in="blackFill"/><feMergeNode in="SourceGraphic"/></feMerge>
  </filter>
</defs>'''
# solid backing in the sticker colour, so the die-cut outline wraps everything as one piece
backing = (f'<g fill="{INK}">'
           f'<rect x="{x0+6:.0f}" y="{y0-cap:.0f}" width="{total-12:.0f}" height="{cap+40:.0f}" rx="4"/>'
           f'<rect x="{xs+4:.0f}" y="{barY:.0f}" width="{SKW-8:.0f}" height="{BOTTOM-barY:.0f}" rx="4"/>'
           + '</g>')
dev_backing = (f'<g fill="{INK}">'
               
               f'<polygon points="{P((SX-26, SY-26), (SX+26, SY-26), (SX+26, SY+26), (lens[0]+10, lens[1]), (lens[0]-10, lens[1]), (SX-26, SY+26))}"/>'
               + '</g>')
# the device sits small on top of the wordmark, about a third of its width, like the other SKIrF logos
K = 0.62
DCX, DBY = (Lt[0] + Rt[0]) / 2, Fb[1] + 6
top_of_word = y0 - 0.72 * size
device = f'<g transform="translate(500 {top_of_word - 16:.1f}) scale({K}) translate({-DCX:.1f} {-DBY:.1f})">{dev_backing}{sky}{cube}</g>'
dev_top = top_of_word - 16 + (SY - 30 - DBY) * K
VB_Y = dev_top - 60
svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="-20 {VB_Y:.0f} 1040 {BOTTOM + 60 - VB_Y:.0f}" width="1040" height="{BOTTOM + 60 - VB_Y:.0f}">{defs}'
       f'<g filter="url(#sticker)">{backing}{device}{word}{brand}</g></svg>')
open(OUT, 'w').write(svg)
if MARK:   # square mark: just the cube + sky, for icons and avatars
    m = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="130 -70 640 690" width="640" height="690">{defs}'
         f'<g filter="url(#sticker)"><polygon fill="{INK}" points="{P((SX-26, SY-26), (SX+26, SY-26), (SX+26, SY+26), (lens[0]+10, lens[1]), (lens[0]-10, lens[1]), (SX-26, SY+26))}"/>{sky}{cube}</g></svg>')
    open(MARK, 'w').write(m)
print('ok', OUT)

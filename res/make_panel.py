#!/usr/bin/env python3
# Draws res/JustFiends.svg. VCV Rack can't draw SVG <text>, so every label is
# turned into outline paths with fontTools. Positions are in mm and must match
# src/JustFiends.cpp.
#   pip install fonttools
#   python3 res/make_panel.py
import math
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")

W, H = 142.24, 128.5  # 28 HP

BG = "#2B2230"
BG_DARK = "#221B27"
INK = "#EFE9DC"
SOFT = "#A79FB0"
ACID = "#B8E04A"
OUTBOX = "#17121B"
CH = ["#F2ECDE", "#F26B5B", "#F2B13A", "#B8E04A", "#4CD6C8", "#9B8CF5"]

fonts = {}


def text(s, x, y, size, font=FONT_MED, fill=INK, anchor="middle", spacing=0.04):
    """Return an SVG path for string s; (x, y) is the baseline point in mm."""
    if font not in fonts:
        f = TTFont(font)
        fonts[font] = (f, f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm)
    f, gs, cmap, upm = fonts[font]
    scale = size / upm
    track = spacing * upm
    names = [cmap[ord(c)] for c in s]
    width = sum(gs[n].width + track for n in names) - track
    if anchor == "middle":
        x0 = x - width * scale / 2
    elif anchor == "end":
        x0 = x - width * scale
    else:
        x0 = x
    pen = SVGPathPen(gs)
    cx = 0.0
    for n in names:
        tp = TransformPen(pen, (scale, 0, 0, -scale, x0 + cx * scale, y))
        gs[n].draw(tp)
        cx += gs[n].width + track
    return f'<path fill="{fill}" d="{pen.getCommands()}"/>'


parts = []
add = parts.append


def line(x1, y1, x2, y2, col=SOFT, w=0.25, op=0.5):
    add(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{col}" stroke-width="{w}" opacity="{op}"/>')


def tick(cx, cy, value, label, r0=7.0, r1=8.2, fill=INK, size=1.6):
    """A mark at a knob position (value 0..1 over Rack's -150..+150 degree sweep)."""
    a = math.radians(-150 + 300 * value)
    sx, sy = math.sin(a), -math.cos(a)
    add(f'<line x1="{cx + r0 * sx:.3f}" y1="{cy + r0 * sy:.3f}" x2="{cx + r1 * sx:.3f}" '
        f'y2="{cy + r1 * sy:.3f}" stroke="{fill}" stroke-width="0.35"/>')
    if label:
        rl = r1 + 1.8
        add(text(label, cx + rl * sx, cy + rl * sy + size * 0.36, size, FONT_BOLD, fill))


add(f'<rect width="{W}" height="{H}" fill="{BG}"/>')
add(f'<rect x="0" y="0" width="{W}" height="9.6" fill="{BG_DARK}"/>')

# title
add(text("JUST FIENDS", 18.0, 7.4, 4.0, FONT_BOLD, INK, anchor="start", spacing=0.12))
add(text("six slopes  /  one time", W - 18.0, 7.0, 2.2, FONT_MED, SOFT, anchor="end"))

# display window
add(f'<rect x="4.4" y="12.0" width="133.4" height="38.6" rx="1.6" fill="#0B090D"/>')

rowA, rowB, rowC = 64.0, 78.0, 89.5
KX = {"time": 16.0, "intone": 42.0, "ramp": 64.0, "curve": 87.0, "range": 105.5, "mode": 122.0}
TY = 54.8
# big knobs
for x, name, lo, hi in [(KX["time"], "TIME", "slow", "fast"), (KX["intone"], "INTONE", "under", "over")]:
    add(text(name, x, TY, 2.7, FONT_BOLD, INK, spacing=0.08))
    add(text(lo, x - 9.6, rowA + 9.2, 1.7, FONT_MED, SOFT))
    add(text(hi, x + 9.6, rowA + 9.2, 1.7, FONT_MED, SOFT))
add(text("noon: 1 Hz / middle C", KX["time"], rowA + 11.8, 1.45, FONT_MED, SOFT))
# ramp / curve
for x, name, lo, hi in [(KX["ramp"], "RAMP", "pluck", "swell"), (KX["curve"], "CURVE", "rect", "sine")]:
    add(text(name, x, TY, 2.7, FONT_BOLD, INK, spacing=0.08))
    add(text(lo, x - 7.6, rowA + 8.0, 1.7, FONT_MED, SOFT))
    add(text(hi, x + 7.6, rowA + 8.0, 1.7, FONT_MED, SOFT))
    tick(x, rowA, 0.5, "", r0=7.0, r1=7.9, fill=ACID)
add(text("log", KX["curve"] - 8.4, rowA - 3.6, 1.5, FONT_MED, SOFT))
add(text("exp", KX["curve"] + 8.4, rowA - 3.6, 1.5, FONT_MED, SOFT))

# switches
x = KX["range"]
add(text("RANGE", x, TY, 2.3, FONT_BOLD, INK, spacing=0.06))
add(text("SOUND", x + 3.2, rowA - 2.6, 1.7, FONT_BOLD, CH[4], anchor="start"))
add(text("SHAPE", x + 3.2, rowA + 3.4, 1.7, FONT_BOLD, CH[2], anchor="start"))
x = KX["mode"]
add(text("MODE", x, TY, 2.3, FONT_BOLD, INK, spacing=0.06))
add(text("CYCLE", x + 3.0, rowA - 3.0, 1.6, FONT_BOLD, INK, anchor="start"))
add(text("SUSTAIN", x + 3.0, rowA + 0.6, 1.6, FONT_BOLD, INK, anchor="start"))
add(text("TRANSIENT", x + 3.0, rowA + 4.2, 1.6, FONT_BOLD, INK, anchor="start"))

# row B
for x in (KX["intone"], KX["ramp"], KX["curve"]):
    add(text("amt", x - 6.2, rowB + 0.6, 1.6, FONT_MED, SOFT))
add(text("FM", KX["range"], rowB - 5.0, 2.1, FONT_BOLD, INK))
add(text("RUN", KX["mode"], rowB - 5.0, 2.1, FONT_BOLD, ACID))
add(text("ON", 134.0, rowB - 4.4, 1.7, FONT_BOLD, ACID))
add(text("-", KX["range"] - 5.4, rowB + 0.6, 1.8, FONT_BOLD, SOFT))
add(text("+", KX["range"] + 5.4, rowB + 0.6, 1.8, FONT_BOLD, SOFT))

# row C labels
for x, lab in [(KX["time"], "V/OCT"), (KX["intone"], "CV"), (KX["ramp"], "CV"), (KX["curve"], "CV"), (KX["range"], "FM IN"), (KX["mode"], "RUN IN")]:
    add(text(lab, x, rowC + 6.4, 1.7, FONT_BOLD, SOFT))
add(text("RUN turns on:   shape  SHIFT / STRATA / VOLLEY      sound  SPILL / PLUME / FLOOM", W / 2, 98.4, 1.55, FONT_MED, ACID))

line(3.0, 100.2, W - 3.0, 100.2)

# trigger / output section
trigY, outY = 107.5, 120.5
add(text("TRIGGERS IN: each jack also feeds every unpatched jack to its right.  Buttons fire them by hand.", W / 2, 103.0, 1.45, FONT_MED, SOFT))
add(f'<rect x="3.2" y="115.6" width="{W - 6.4}" height="12.2" rx="1.2" fill="{OUTBOX}"/>')
labels = ["IDENTITY", "2N", "3N", "4N", "5N", "6N"]
for i, lab in enumerate(labels):
    x = 15.5 + 16.5 * i
    add(text(lab, x, 114.4, 2.0 if i else 1.8, FONT_BOLD, CH[i], spacing=0.05))
add(text("POLY TRIG", 114.5, 114.4, 1.7, FONT_BOLD, INK))
add(text("MIX", 114.5, 127.2, 1.8, FONT_BOLD, CH[0]))
add(text("POLY", 127.0, 127.2, 1.7, FONT_BOLD, INK))
add(text("OUT", 15.5, 127.2, 1.5, FONT_MED, SOFT))
add(text("shape 0 to 8 V  /  sound +-5 V", 57.0, 127.2, 1.45, FONT_MED, SOFT))

svg = (
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" '
    f'viewBox="0 0 {W} {H}">\n' + "\n".join(parts) + "\n</svg>\n"
)
with open(os.path.join(HERE, "JustFiends.svg"), "w") as fh:
    fh.write(svg)
print("wrote JustFiends.svg")

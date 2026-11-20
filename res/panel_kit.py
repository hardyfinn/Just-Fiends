# Shared drawing helpers for the Weird Sisters and Mangrave panel scripts.
# VCV Rack can't draw SVG <text>, so labels become outline paths via fontTools.
import math
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")
CTRL, JACK, HINT = 3.2, 2.9, 2.4  # label sizes in mm (Haer's standard)
_fonts = {}


def text(s, x, y, size, font=FONT_MED, fill="#EEE", anchor="middle", spacing=0.04):
    """SVG path for string s; (x, y) is the baseline point in mm."""
    if font not in _fonts:
        f = TTFont(font)
        _fonts[font] = (f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm)
    gs, cmap, upm = _fonts[font]
    scale = size / upm
    track = spacing * upm
    names = [cmap[ord(c)] for c in s]
    width = sum(gs[n].width + track for n in names) - track
    x0 = x - width * scale / 2 if anchor == "middle" else x - width * scale if anchor == "end" else x
    pen = SVGPathPen(gs)
    cx = 0.0
    for n in names:
        gs[n].draw(TransformPen(pen, (scale, 0, 0, -scale, x0 + cx * scale, y)))
        cx += gs[n].width + track
    return f'<path fill="{fill}" d="{pen.getCommands()}"/>'


def tick(cx, cy, value, r0, r1, fill):
    a = math.radians(-150 + 300 * value)
    sx, sy = math.sin(a), -math.cos(a)
    return (f'<line x1="{cx + r0 * sx:.3f}" y1="{cy + r0 * sy:.3f}" x2="{cx + r1 * sx:.3f}" '
            f'y2="{cy + r1 * sy:.3f}" stroke="{fill}" stroke-width="0.4"/>')


def write(path, W, H, parts, logo_x, logo_y, logo_bg):
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" '
           f'viewBox="0 0 {W} {H}">\n' + "\n".join(parts) + "\n</svg>\n")
    with open(path, "w") as fh:
        fh.write(svg)
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import haer_logo
    haer_logo.add_to_file(path, x=logo_x, y=logo_y, cap=3.0, bg=logo_bg)
    print("wrote", os.path.basename(path))

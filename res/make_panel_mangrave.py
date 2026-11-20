#!/usr/bin/env python3
# Draws res/Mangrave.svg. Positions in mm must match src/Mangrave.cpp.
#   pip install fonttools && python3 res/make_panel_mangrave.py
import os
from panel_kit import text, tick, write, FONT_BOLD, FONT_MED, CTRL, JACK, HINT

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 121.92, 128.5  # 24 HP
BG, BG_DARK, INK, SOFT, OUTBOX = "#1D2620", "#151C17", "#EEE8DC", "#98A69A", "#111712"
LEAF, MUD = "#7CE08A", "#C89B5A"
P = []
add = P.append
add(f'<rect width="{W}" height="{H}" fill="{BG}"/>')
add(f'<rect x="0" y="0" width="{W}" height="9.6" fill="{BG_DARK}"/>')
add(text("MANGRAVE", 16.0, 7.4, 4.0, FONT_BOLD, INK, anchor="start", spacing=0.12))
add(f'<rect x="4.4" y="12.0" width="{W - 8.8:.2f}" height="33.2" rx="1.6" fill="#090C0A"/>')

A, B, C = 59.0, 76.0, 88.5
TY = 50.4
for x, name in [(19.0, "PITCH"), (48.0, "FORMANT"), (76.0, "BARREL"), (102.0, "AIR")]:
    add(text(name, x, TY, CTRL, FONT_BOLD, LEAF if name == "AIR" else INK, spacing=0.05))
add(text("low", 48.0 - 10.0, A + 8.8, HINT, FONT_MED, SOFT))
add(text("high", 48.0 + 10.0, A + 8.8, HINT, FONT_MED, SOFT))
add(text("rise", 76.0 - 8.6, A + 8.4, HINT, FONT_MED, SOFT))
add(text("fall", 76.0 + 8.6, A + 8.4, HINT, FONT_MED, SOFT))
add(tick(76.0, A, 0.5, 7.0, 7.9, LEAF))
add(text("still", 102.0 - 8.6, A + 8.4, HINT, FONT_MED, SOFT))
add(text("gale", 102.0 + 8.6, A + 8.4, HINT, FONT_MED, SOFT))
add(text("FINE", 11.0, B - 5.0, 2.6, FONT_BOLD, INK))
add(text("FM", 27.0, B - 5.0, 2.6, FONT_BOLD, INK))
for x in (48.0, 76.0, 102.0):
    add(text("amt", x - 7.4, B + 0.9, HINT, FONT_MED, SOFT))
for x, lab in [(11.0, "V/OCT"), (27.0, "FM"), (48.0, "CV"), (62.0, "STRIKE"), (76.0, "CV"), (102.0, "CV")]:
    add(text(lab, x, C + 7.1, JACK, FONT_BOLD, MUD if lab == "STRIKE" else SOFT))
add(f'<line x1="3" y1="98.6" x2="{W - 3}" y2="98.6" stroke="{SOFT}" stroke-width="0.25" opacity="0.5"/>')
add(text("CONSTANT", 16.0, 106.0, JACK, FONT_BOLD, INK))
add(text("FORMANT", 19.4, 113.4, HINT, FONT_BOLD, LEAF, anchor="start"))
add(text("WAVE", 19.4, 120.8, HINT, FONT_BOLD, MUD, anchor="start"))
add(text("STRIKE", 50.0, 109.8, JACK, FONT_BOLD, MUD))
add(f'<rect x="62.0" y="102.6" width="{W - 3.2 - 62.0:.2f}" height="25.3" rx="1.2" fill="{OUTBOX}"/>')
add(text("SQUARE", 76.0, 110.6, JACK, FONT_BOLD, INK))
add(text("FORMANT", 100.0, 110.6, JACK, FONT_BOLD, LEAF))
write(os.path.join(HERE, "Mangrave.svg"), W, H, P, logo_x=W - 24.0, logo_y=5.6, logo_bg=BG_DARK)

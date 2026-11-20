#!/usr/bin/env python3
# Draws res/WeirdSisters.svg. Positions in mm must match src/WeirdSisters.cpp.
#   pip install fonttools && python3 res/make_panel_sisters.py
import os
from panel_kit import text, tick, write, FONT_BOLD, FONT_MED, CTRL, JACK, HINT

HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 121.92, 128.5  # 24 HP
BG, BG_DARK, INK, SOFT, OUTBOX = "#1F2427", "#171B1E", "#EEE8DC", "#9AA3A8", "#121518"
LOW, CEN, HIGH = "#F28C3A", "#B8E04A", "#5CC8F0"
P = []
add = P.append
add(f'<rect width="{W}" height="{H}" fill="{BG}"/>')
add(f'<rect x="0" y="0" width="{W}" height="9.6" fill="{BG_DARK}"/>')
add(text("WEIRD SISTERS", 16.0, 7.4, 4.0, FONT_BOLD, INK, anchor="start", spacing=0.1))
add(f'<rect x="4.4" y="12.0" width="{W - 8.8:.2f}" height="33.2" rx="1.6" fill="#0A0C0D"/>')

A, B, C = 59.0, 76.0, 88.5
TY = 50.4
for x, name in [(20.0, "FREQ"), (52.0, "SPAN"), (80.0, "QUALITY"), (100.0, "MODE")]:
    add(text(name, x, TY, CTRL, FONT_BOLD, INK, spacing=0.05))
add(text("20 Hz", 20.0 - 10.6, A + 8.8, HINT, FONT_MED, SOFT))
add(text("20k", 20.0 + 10.6, A + 8.8, HINT, FONT_MED, SOFT))
add(text("none", 52.0 - 8.6, A + 8.4, HINT, FONT_MED, SOFT))
add(text("wide", 52.0 + 8.6, A + 8.4, HINT, FONT_MED, SOFT))
add(text("notch", 80.0 - 8.6, A + 8.4, HINT, FONT_MED, SOFT))
add(text("sing", 80.0 + 8.6, A + 8.4, HINT, FONT_MED, SOFT))
add(tick(80.0, A, 0.5, 7.0, 7.9, CEN))
add(text("FORMANT", 103.2, A - 2.0, 2.4, FONT_BOLD, HIGH, anchor="start"))
add(text("CROSSOVER", 103.2, A + 4.0, 2.4, FONT_BOLD, LOW, anchor="start"))
add(text("FM", 33.4, B + 1.0, JACK, FONT_BOLD, INK, anchor="start"))
for x in (52.0, 80.0):
    add(text("amt", x - 7.4, B + 0.9, HINT, FONT_MED, SOFT))
for x, lab in [(12.0, "V/OCT"), (30.0, "FM"), (52.0, "CV"), (80.0, "CV")]:
    add(text(lab, x, C + 7.1, JACK, FONT_BOLD, SOFT))
add(f'<line x1="3" y1="98.6" x2="{W - 3}" y2="98.6" stroke="{SOFT}" stroke-width="0.25" opacity="0.5"/>')
add(text("ALL in feeds every sister with nothing patched", 58.0, 101.9, 2.1, FONT_MED, SOFT))
add(text("IN", 7.6, 107.0, HINT, FONT_BOLD, SOFT))
add(f'<rect x="3.2" y="115.6" width="{W - 6.4:.2f}" height="12.3" rx="1.2" fill="{OUTBOX}"/>')
for x, lab, col in [(20.0, "LOW", LOW), (44.0, "CENTRE", CEN), (68.0, "HIGH", HIGH), (96.0, "ALL", INK)]:
    add(text(lab, x, 114.4, JACK, FONT_BOLD, col, spacing=0.04))
write(os.path.join(HERE, "WeirdSisters.svg"), W, H, P, logo_x=W - 24.0, logo_y=5.6, logo_bg=BG_DARK)

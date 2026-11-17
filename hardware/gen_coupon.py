#!/usr/bin/env python3
"""Generate the LED window test coupon (hardware/coupon/coupon.kicad_pcb).

Eight ways to let an LED shine through the dial, side by side, to settle the design doc's open
question before ordering the full dial. Row 1 puts each in a black field (like the rim LEDs);
row 2 inside a gold pad (like the star points). Order it at 0.8 mm (the dial's thickness) and,
for comparison, 1.6 mm. Two M2 holes take an LED test board behind it at the same 9 mm pitch.

Run:  .venv/bin/python gen_coupon.py
"""
import pathlib

from shapely import affinity
from shapely.geometry import box
from shapely.ops import unary_union

from kicadart import Board, Font, disc, polar

OUT = pathlib.Path(__file__).resolve().parent / "coupon" / "coupon.kicad_pcb"

PITCH = 9.0
COLS = 8
WIDTH = PITCH * COLS + 6
HEIGHT = 34.0
ROW_Y = (-6.0, 6.0)
PAD = 6.0                # gold pad around each row-2 spot
WINDOW_CLEAR = 0.2

serif = Font("CormorantGaramond[wght].ttf", weight=700)
symbols = Font("NotoSansSymbols[wght].ttf", weight=500)

STYLES = [
    ("A", "FR4 1.6"),
    ("B", "FR4 2.4"),
    ("C", "HOLE 0.6"),
    ("D", "HOLE 1.0"),
    ("E", "PTH 0.8"),
    ("F", "FENCE"),
    ("G", "GLYPH"),
    ("H", "MASK"),
]


def main():
    board = Board(center=(148.5, 105.0))
    gold, copper_extra, windows, copper_clear, silk = [], [], [], [], []
    x0 = -PITCH * (COLS - 1) / 2

    for col, (letter, label) in enumerate(STYLES):
        x = x0 + col * PITCH
        silk.append(affinity.translate(serif.text(letter, 2.6), x, -13.0))
        silk.append(affinity.translate(serif.text(label, 1.5, 0.15), x, 13.2))
        for row, y in enumerate(ROW_Y):
            at = (x, y)
            if row == 1:
                gold.append(box(x - PAD / 2, y - PAD / 2, x + PAD / 2, y + PAD / 2))
            if letter in ("A", "B"):
                windows.append(disc(0.8 if letter == "A" else 1.2, at))
            elif letter in ("C", "D"):
                board.hole(0.6 if letter == "C" else 1.0, at)
                copper_clear.append(disc(0.6 if letter == "C" else 0.8, at))
            elif letter == "E":
                board.hole(0.8, at, plated=True, pad=1.4)
                copper_clear.append(disc(0.7 + 0.3, at))  # a black gap between the ring and the gold
            elif letter == "F":
                windows.append(disc(1.0, at))
                for k in range(8):  # plated vias around the window block light in the fibreglass
                    vx, vy = polar(1.9, k * 45)
                    board.hole(0.3, (x + vx, y + vy), plated=True, pad=0.6)
                    copper_clear.append(disc(0.3 + 0.3, (x + vx, y + vy)))
            elif letter == "G":
                windows.append(affinity.translate(symbols.text("♀", 4.2), x, y))
            elif letter == "H":
                copper_clear.append(disc(1.2, at))  # no copper, but the mask stays

    windows = unary_union(windows)
    clear = unary_union([windows.buffer(WINDOW_CLEAR), unary_union(copper_clear)])
    gold = unary_union(gold).difference(clear)
    copper = gold.buffer(0.1).intersection(unary_union([box(x0 - PAD, -HEIGHT / 2 + 0.6, -x0 + PAD, HEIGHT / 2 - 0.6)])).difference(clear)
    mask_open = unary_union([gold, windows])
    silk = unary_union(silk).buffer(0.03).difference(mask_open.buffer(0.1))

    board.fill(copper, "F.Cu")
    board.fill(mask_open, "F.Mask")
    board.fill(silk, "F.SilkS")
    board.fill(windows, "B.Mask")
    board.rect(-WIDTH / 2, -HEIGHT / 2, WIDTH / 2, HEIGHT / 2, "Edge.Cuts")
    for hx in (-WIDTH / 2 + 2.5, WIDTH / 2 - 2.5):
        board.hole(2.2, (hx, 0))
    OUT.parent.mkdir(exist_ok=True)
    board.write(OUT)
    print(f"wrote {OUT} ({len(board.items)} items)")


if __name__ == "__main__":
    main()

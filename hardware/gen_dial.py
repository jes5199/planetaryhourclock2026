#!/usr/bin/env python3
"""Generate the front dial PCB (hardware/dial/dial.kicad_pcb) from the "N · 84-LED day ring"
mockup of the Planetary Hours Dial canvas.

The mockup is drawn in SVG units with the rim at r = 324 and the 46 mm screen at r = 102, so
one unit is 23/102 mm and the dial is 146 mm across. Every radius and angle below is the
mockup's, converted with U().

PCB inks: gold = bare ENIG copper (a solder-mask opening over copper), black = solder mask,
white = silkscreen, and LED windows = bare fibreglass (copper and mask cleared on both sides).
Mapping: daytime ruler bands, star, rose, ticks and ring lines gold; night halves black with
white ruler symbols; day names, planet names and hour numerals white; the day rulers' symbols
knocked out of the gold in black.

Run:  .venv/bin/python gen_dial.py
"""
import pathlib

from shapely import affinity
from shapely.geometry import Point
from shapely.ops import unary_union

from kicadart import EMPTY, Board, Font, arc, disc, place, polar, radial, ring, sector, segment

OUT = pathlib.Path(__file__).resolve().parent / "dial" / "dial.kicad_pcb"

MM_PER_UNIT = 23 / 102
PX = (660 / 680) * MM_PER_UNIT  # the mockup's CSS px (its SVG is drawn 680 px for 660 units)
W = 360 / 7


def U(units):
    return units * MM_PER_UNIT


# ---- options -------------------------------------------------------------------------------

BOARD_R = U(324)            # 73.06 mm
# Centre: K&S telescoping brass (1/32" steps, 0.014" wall). A fixed 5/32" post carries the
# screen and its wires; the gold hand turns on a 3/16" tube over it, the hour hand on a 7/32"
# tube over that. The hole clears the 7/32" (5.56 mm) outer tube.
CENTER_HOLE_D = 6.5         # mm
RIM_LINE_R = 72.20          # the brass rim, kept clear of the board edge
EDGE_CLEAR = 0.55           # copper keep-back from board edges (KiCad default rule: 0.5)
WINDOW_D = {"rim": 2.0, "star": 1.6, "corner": 2.2, "compass": 2.2}  # LED windows, bare FR4
WINDOW_CLEAR = 0.2          # copper keep-back around a window
SCREEN_R = U(102)           # 23 mm: the AMOLED floats in front and hides everything inside this
MOUNT_D = 2.7               # standoff holes (M2.5 clearance), hidden behind the screen
MOUNT_R = 17.0              # their radius: clear of the centre hole, inside the screen with room for a nut
MOUNT_ANGLES = (45, 135, 225, 315)
MOUNT_CLEAR = 0.4           # copper keep-back around a mounting hole
PINHOLE_D = 0.5             # non-plated hole drilled through the middle of each window (fab minimum)
GOLD_UNDER_MASK = 0.10      # copper extends this far under the mask past each gold edge
SILK_THICKEN = 0.035        # grow silk glyphs (Cormorant's hairlines are fine for silkscreen)
SILK_CLEAR = 0.10           # keep silk this far from mask openings (fabs clip it there)

PLANETS = ["Saturn", "Jupiter", "Mars", "Sun", "Venus", "Mercury", "Moon"]  # Chaldean order
SYMBOLS = ["♄", "♃", "♂", "☉", "♀", "☿", "☽"]
WEEK = ["Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"]
ROMAN = ["I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII"]


def mod(x, m):
    return ((x % m) + m) % m


def pos(k):            # weekday k (Sunday = 0) sits in slice 3k mod 7
    return mod(3 * k, 7)


def day_ruler(k):      # Chaldean index of weekday k's ruler
    return mod(3 * k + 3, 7)


def night_ruler(k):
    return mod(day_ruler(k) + 5, 7)


# ---- fonts -----------------------------------------------------------------------------------

serif_700 = Font("CormorantGaramond[wght].ttf", weight=700)
serif_600 = Font("CormorantGaramond[wght].ttf", weight=600)
symbols = Font("NotoSansSymbols[wght].ttf", weight=500)


def symbol(chaldean, size):
    """A planet symbol, ink centred on the origin. The Sun (☉, missing from Noto Sans Symbols)
    is drawn: a ring with a dot."""
    if SYMBOLS[chaldean] == "☉":
        r = size * 0.36
        return ring(r, size * 0.085).union(disc(size * 0.09))
    return symbols.text(SYMBOLS[chaldean], size)


# ---- the dial ----------------------------------------------------------------------------------

def build():
    gold, under_mask, silk, windows = [], [], [], []

    # Ring lines (mockup circles at 324, 311, 279, 244, 141).
    gold += [ring(RIM_LINE_R, 0.40), ring(U(311), U(1.5)), ring(U(279), U(1.5)),
             ring(U(244), U(1.5)), ring(U(141), U(1.5))]

    # Ruler band (245..277): daytime middle half of each slice gold, night quarters black.
    # The day ruler's symbol is knocked out of the gold; the two night rulers are white.
    knockouts = []
    for k in range(7):
        base = (pos(k) - 0.5) * W
        a_s, a_e = base + 0.25 * W, base + 0.75 * W
        gold.append(sector(U(245), U(277), a_s, a_e))
        gold += [radial(U(245), U(277), a, U(1)) for a in (a_s, a_e)]
        knockouts.append(place(symbol(day_ruler(k), 21 * PX), U(261), pos(k) * W))
        silk.append(place(symbol(night_ruler(mod(k - 1, 7)), 13 * PX), U(261), (base + a_s) / 2))
        silk.append(place(symbol(night_ruler(k), 13 * PX), U(261), (a_e + base + W) / 2))
        # Day name, outermost band.
        silk.append(arc(serif_700.text(WEEK[k].upper(), 17 * PX, 3 * PX), U(295), pos(k) * W))

    # Slice edges across the two outer bands, and the planet ring's twelfth-hour ticks.
    for p in range(7):
        gold.append(radial(U(244), U(311), (p + 0.5) * W, U(1.5)))
        for j in range(1, 12):
            quarter = j % 3 == 0
            a = (p - 0.5) * W + j * W / 12
            gold.append(radial(U(231 if quarter else 237), U(243), a, U(1.5 if quarter else 1.0)))
        # Planet name in the middle of its wedge.
        silk.append(place(serif_700.text(PLANETS[(p + 3) % 7].upper(), 15 * PX, 2.5 * PX), U(222), p * W))

    # Hour ring (106..141): the daytime half is copper under the mask (a faint sheen), then
    # 24 ticks (sunrise and sunset run the full depth) and the numerals I..XII.
    under_mask.append(sector(U(106), U(141), -90, 90))
    for q in range(12):
        for a0 in (-90, 90):
            full = q == 0
            gold.append(radial(U(106 if full else 134), U(141), a0 + q * 15, U(2 if full else 1.25)))
        silk.append(place(serif_600.text(ROMAN[q], 12 * PX), U(123), -90 + (q + 0.5) * 15, rotate=False))
        silk.append(place(serif_600.text(ROMAN[q], 12 * PX), U(123), 90 + (q + 0.5) * 15, rotate=False))

    # Compass rose: four short diagonals under four long cardinals. Each point is split down
    # its spine: the anticlockwise facet gold, the clockwise facet white. Painted in order, so
    # each facet hides whatever it covers.
    facets = []
    for long_, tip, base, half in ((False, 124, 40, 22.5), (True, 141, 52, 30)):
        for k in range(4):
            a = k * 90 + (0 if long_ else 45)
            facets.append(("gold", _tri((0, 0), polar(U(tip), a), polar(U(base), a - half))))
            facets.append(("white", _tri((0, 0), polar(U(tip), a), polar(U(base), a + half))))
    rose_gold, rose_white = [], []
    for i, (ink, f) in enumerate(facets):
        covered = unary_union([g for _, g in facets[i + 1:]]) if i + 1 < len(facets) else EMPTY
        visible = f.difference(covered)
        (rose_gold if ink == "gold" else rose_white).append(visible)
    rose = unary_union([f for _, f in facets])

    gold = unary_union(gold)
    gold = gold.difference(unary_union(knockouts).buffer(0.08))
    gold = gold.difference(rose).union(unary_union(rose_gold))  # the rose sits on top

    # The star {7/2}: each line joins the end of one day's slice (midnight) to the start of the
    # next day's, drawn over a black outline so later lines cross earlier ones like a weave.
    star_points = []
    for k in range(7):
        base = (pos(k) - 0.5) * W
        p1, p2 = polar(U(244), base + W), polar(U(244), (pos(k + 1) - 0.5) * W)
        line = segment(p1, p2, U(4.5))
        gold = gold.difference(segment(p1, p2, U(8.5))).union(line)
        star_points.append(p1)
    for p in star_points:  # dots at the star's points, black-rimmed
        gold = gold.difference(disc(U(7), p)).union(disc(U(6), p))

    # LED windows: 84 on the rim, 7 star points, 7 star inner corners, 4 compass tips.
    for rp in range(7):
        for rj in range(12):
            windows.append(disc(WINDOW_D["rim"] / 2, polar(U(315), (rp - 0.5) * W + (rj + 0.5) * W / 12)))
    for p in star_points:
        windows.append(disc(WINDOW_D["star"] / 2, p))
    for pl in range(7):
        windows.append(disc(WINDOW_D["corner"] / 2, polar(U(167), pl * W)))
    for a in (0, 90, 180, 270):
        windows.append(disc(WINDOW_D["compass"] / 2, polar(U(141), a)))
    pinholes = [w.centroid.coords[0] for w in windows]
    windows = unary_union(windows)

    # Keep-outs: the board edge, the centre hole, and around each window.
    inside = disc(BOARD_R - EDGE_CLEAR).difference(disc(CENTER_HOLE_D / 2 + EDGE_CLEAR))
    for a in MOUNT_ANGLES:
        inside = inside.difference(disc(MOUNT_D / 2 + MOUNT_CLEAR, polar(MOUNT_R, a)))
    keep = inside.difference(windows.buffer(WINDOW_CLEAR))
    gold = gold.intersection(keep)
    copper = unary_union([gold.buffer(GOLD_UNDER_MASK), unary_union(under_mask)]).intersection(keep)
    mask_open = unary_union([gold, windows])
    silk = unary_union(silk).buffer(SILK_THICKEN)
    # White rose facets: numerals that cross them are knocked out (black) rather than lost.
    rose_white = unary_union(rose_white).difference(silk.buffer(0.15))
    silk = unary_union([silk, rose_white]).difference(mask_open.buffer(SILK_CLEAR)).intersection(inside)
    return copper, mask_open, silk, windows, pinholes


def _tri(a, b, c):
    from shapely.geometry import Polygon
    return Polygon([a, b, c])


def main():
    copper, mask_open, silk, windows, pinholes = build()
    board = Board()
    board.fill(copper, "F.Cu")
    board.fill(mask_open, "F.Mask")
    board.fill(silk, "F.SilkS")
    board.fill(windows, "B.Mask")       # the LEDs shine in from the back
    board.circle(BOARD_R, "Edge.Cuts")
    board.circle(CENTER_HOLE_D / 2, "Edge.Cuts")
    for i, a in enumerate(MOUNT_ANGLES):  # standoffs to the LED board / frame behind
        board.hole(MOUNT_D, polar(MOUNT_R, a), ref=f"M{i + 1}")
    for i, at in enumerate(pinholes):  # a pinhole through the middle of every LED window
        board.hole(PINHOLE_D, at, ref=f"H{i + 1}")
    OUT.parent.mkdir(exist_ok=True)
    board.write(OUT)
    print(f"wrote {OUT} ({len(board.items)} items)")


if __name__ == "__main__":
    main()

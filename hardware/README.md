# Hardware

PCBs for the clock, generated as KiCad 9 source by Python scripts. Edit the scripts, not the
`.kicad_pcb` files: re-running a script rewrites its board.

| Board | Script | What |
| --- | --- | --- |
| `dial/dial.kicad_pcb` | `gen_dial.py` | The 146 mm front dial: all the artwork, the LED windows, the centre hole |
| `coupon/coupon.kicad_pcb` | `gen_coupon.py` | A 78 × 34 mm test coupon of eight LED-window styles |

The dial follows mockup **N · 84-LED day ring** in the "Planetary Hours Dial" design canvas: its
SVG units are converted at 23/102 mm per unit (the 46 mm screen is r = 102 there), which puts
the rim at r = 73 mm, as in the design doc.

## Inks

A PCB has three: **gold** (bare ENIG copper where the solder mask is opened), **black** (the
mask) and **white** (silkscreen). LED windows are a fourth: **bare fibreglass**, with copper
and mask cleared on both faces, lit from an LED board behind.

| Dial element | Ink |
| --- | --- |
| Rim, ring lines, slice edges, ticks, star | Gold |
| Compass rose | Each point split down its spine: anticlockwise facet gold, clockwise facet white; diagonals under cardinals, the rose over everything else |
| Ruler band, daytime half of each slice | Gold, with the day ruler's symbol knocked out in black |
| Ruler band, night quarters | Black, with the night ruler's symbol in white |
| Day names | White, bent along the arc of their band |
| Planet names, hour numerals | White (numerals crossing the rose's white facets are knocked out in black) |
| Daytime half of the hour ring | Copper under the black mask (a faint sheen) |
| 84 rim LEDs, 7 star points, 7 star corners, 4 compass tips | Fibreglass windows, each with a 0.5 mm non-plated pinhole drilled through its centre |

Gold edges are defined by the mask openings; the copper underneath extends 0.1 mm further so
small registration errors don't show bare edges.

## Fab notes

- **Thickness 0.8 mm** (in the board stackup): brighter windows than 1.6 mm.
- **Black solder mask, white silkscreen, ENIG finish**, both sides (also in the stackup).
- The centre hole (6.5 mm) clears the hands' tube stack, all K&S telescoping brass (0.014"
  wall): a fixed 5/32" post that holds the screen and carries its 5 V and GND wires, the gold
  hand's 3/16" tube on it, and the hour hand's 7/32" tube (5.56 mm) outside that. Nothing else
  can hold the screen up: the hands sweep the whole gap between the dial and the screen.
- Four M2.5 standoff holes (2.7 mm, non-plated) at 17 mm radius on the diagonals, inside the
  46 mm screen's shadow so they never show. The hands sweep between the dial and the screen, so
  standoffs go *behind* the dial (to the LED board and frame), not in front.
- 102 pinholes, 0.5 mm non-plated (the usual fab minimum for NPTH; `PINHOLE_D` in `gen_dial.py`).
- Silk glyphs are grown by 0.035 mm so Cormorant Garamond's hairlines survive silkscreen.
- Order the coupon at 0.8 mm and 1.6 mm, light it from behind, and pick a window style (and a
  board thickness) before ordering the dial.

## Build

```sh
cd hardware
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
.venv/bin/python gen_dial.py      # -> dial/dial.kicad_pcb
.venv/bin/python gen_coupon.py    # -> coupon/coupon.kicad_pcb
./render.sh dial/dial.kicad_pcb   # DRC + SVG + 3D render into out/ (uses KiCad 9's kicad-cli)
```

`template.kicad_pcb` is an empty KiCad 9 board with the stackup; scripts append their
`gr_poly`, `gr_circle` and footprint s-expressions to it (`kicadart.py`).

## Fonts

`fonts/` holds Cormorant Garamond (names and numerals) and Noto Sans Symbols (planet
symbols), both under the SIL Open Font License (texts alongside). Noto Sans Symbols has no ☉,
so the Sun is drawn as a ring and dot.

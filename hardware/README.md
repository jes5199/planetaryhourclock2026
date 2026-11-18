# Hardware

PCBs for the clock, generated as KiCad 9 source by Python scripts. Edit the scripts, not the
`.kicad_pcb` files: re-running a script rewrites its board.

| Board | Script | What |
| --- | --- | --- |
| `dial/dial.kicad_pcb` | `gen_dial.py` | The 146 mm front dial: all the artwork, the LED windows, the centre hole |
| `coupon/coupon.kicad_pcb` | `gen_coupon.py` | A 78 × 34 mm test coupon of eight LED-window styles |
| `leds/leds.kicad_pcb` | `gen_leds.py` | The LED board behind the dial: 102 addressable LEDs, and the clock's power hub |

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

## LED board

Same outline, centre hole and standoff holes as the dial, sitting a few millimetres behind it.
It takes the LED positions from `gen_dial.led_sites()`, so a window and its LED can't drift
apart. Assembled by JLCPCB on **both sides**; 1.6 mm, black mask, ENIG (gold) finish, so it can
carry the servo board.

**Front** (facing the dial): only the 102 LEDs and a 100 nF capacitor each, so it stays flat.
Copper: a 5 V pour plus the data tracks, and a small GND island under the regulator.

**Back**: the power hub and connectors, a GND pour, and four M2.5 holes (58 × 23 mm) for the
servo board, currently in the lower half until the hand mechanism is designed (`SERVO_CENTRE`).
No pour copper within 3 mm of any mounting hole on either face, so a standoff or screw head
scratched through the mask can't bridge 5 V to GND. The DC jack, the diode, TVS, bulk capacitor, J2 and the regulator sit together at the bottom
right, in the band between the star points and the rim LEDs; J3 and J4 are at the top left.

| Connector | Part | Pins | Goes to |
| --- | --- | --- | --- |
| J1 12V IN | SMD 5.5 × 2.1 mm DC jack, XKB DC-005-5A-2.0-SMT (C319134), rated 2.5 A (JLCPCB's data; the "5A" in the name is a series code) | centre +12 V, sleeve GND, switch GND | A regulated 12 V brick (2–3 A is plenty; a bigger one is fine, the jack limits what's drawn). At 6 o'clock, mouth flush with the board edge: the cord leaves the bottom of the clock |
| J2 12V SERVO | JST XH 2-pin | 1 +12 V, 2 GND | The servo board's barrel jack (a short lead with a 5.5 × 2.1 mm plug) |
| J3 5V SCREEN | JST XH 2-pin | 1 +5 V (≈4.7 V, through D105), 2 GND | The screen board's 5 V pin (2×14 header J1 pin 4, GND pin 6), wires up the centre post |
| J4 LED DATA | 1.25 mm 3-pin | 1 nc, 2 GND, 3 data | The servo board's RGB-LED header (5 V, GND, LED-OUT): a straight 1:1 cable; its 5 V pin lands on nc |

**Power.** 12 V in → SS54 (reverse-polarity protection) → a 13 V TVS (SMAJ13A) and 100 µF bulk
capacitor, so plugging in a live brick can't ring the rail past the regulator's 19 V limit → 12 V
to the servo board, and to a TPS563201 buck (3 × 10 µF in, 4.7 µH, 3 × 22 µF out, 56k/10k: 5.07 V,
up to 3 A) → 5 V for the LEDs and, through an SS14, the screen. EN is tied to VIN (both rated
19 V). The regulator's GND pin and input capacitors drop through vias to a GND island on the
front, right under the chip, so the input current loop is a few millimetres. The power path
(jack → diode → J2) runs on copper pours. Pad connections to the pours: thermal relief on the
front (the LEDs and their 0402 capacitors, so both ends of each heat evenly during reflow), solid
for every part on the back (the power parts need the copper for current and heat). U1's GND pin
also has two vias straight down to the front island.

Things the hardware doesn't protect against, so the firmware must:
- **Servo stall current.** Two STS3215s stalling draw ~2.7 A each, more than the jack's 2.5 A,
  and it all passes the SS54 (~1.5 W at 3 A). Set the servos' torque limit so a jammed hand can't
  pull that. Normal running (hands moving slowly, LEDs mostly dim) is well under 1 A.
- **Brightness.** The XL-2020 LEDs draw up to 5 mA per colour, so all 102 at full white is about
  1.5 A, inside the regulator's 3 A. A firmware cap is still sensible (the screen and heat share
  the budget), and the dial only ever lights a few LEDs brightly. Check the colour order (GRB for
  WS2812B) when the firmware first drives them.
- The SS14 on the screen feed exists because the screen board's 5 V header pin **is its USB
  VBUS**: without it, plugging the screen board into a computer while the clock is powered would
  connect this board's 5 V to the computer's USB port, and a computer alone would try to power
  the LEDs.

**Data chain** (written to `leds/led-chain.csv` with each LED's angle): the servo board's two
on-board LEDs come first, then here: rim LEDs 0–83 clockwise from the start of Sunday's slice
(so chain index = the firmware's `dayRingLed`), then the 7 star points, the 7 star corners and the
4 compass tips, each set clockwise. Jumps between sets go through the back.

**Parts not in KiCad's library** live in `footprints/jlc.pretty` (`kicadart.Part` looks there
first):
- The LEDs: XINGLIGHT XL-2020RGBC-WS2812B (C5349955), a WS2812B-protocol LED in the 2020 package,
  because Worldsemi's own WS2812B-2020 (C965555) was out of stock. Footprint drawn from its
  datasheet's recommended pattern and numbered like KiCad's WS2812B-2020 (1 DO, 2 GND, 3 DI,
  4 VDD), which is its datasheet top view turned 180°.
- The DC jack: imported from JLCPCB's part data with easyeda2kicad (in the venv:
  `.venv/bin/easyeda2kicad --footprint --3d --lcsc_id C319134 --output footprints/jlc`),
  converted with `kicad-cli fp upgrade`, pads set from XKB's drawing (rev A1, 2025-07-19). Its
  3D model doesn't show in KiCad's render.

**Placement file.** JLCPCB places its *own* library footprint for each part, and those often
differ from KiCad's: the LEDs and the regulator have pin 1 in the opposite corner (180°), and
connectors are centred where KiCad puts pin 1 at the origin. `jlc_ref.py` downloads JLCPCB's
footprint for every LCSC part (easyeda2kicad, cached in `out/jlc-ref/`), matches its pads to ours,
and records the turn and offset; `cpl.py` (KiCad's Python) writes the CPL from the board with those
corrections. One override: JLCPCB's SS54 footprint numbers its cathode end "2", so it is matched
by its cathode band instead of by pad number. A check that places JLCPCB's footprints by the
finished CPL lands every pad on ours (within 0.03 mm for the LEDs, 0.2 mm for the regulator and
diodes, where the two libraries' pads differ in size).

**Reviewed** (2026-11-18) four times by independent agents against the datasheets: pinouts,
the LED chain, the buck design, JLCPCB rules and stock, the CPL against JLCPCB's footprints, and
the fit with the dial. Their findings are fixed above.

Before ordering:
- Upload `out/leds-jlc/` to JLCPCB's quote page and check every part in the assembly preview.
  The CPL is corrected to JLCPCB's footprints, but the back side relies on the mirrored
  convention (180° − angle) that kicad-jlcpcb-tools and KiBot use; JLCPCB doesn't document it and
  has changed it before. Odd back-side angles (U1 at 330°, D103 at 348°) make a wrong
  convention obvious in the preview. Check especially: the LEDs (data out at the package's marked
  corner, which is the silk tick), U1's pin-1 dot, the SS54 and SMAJ13A cathode bands towards
  the +12 V side, C111's + at the J2 side, and J2/J3's latches.
- Tell JLCPCB the LEDs are MSL 5 (bake before use; rated 260 °C for 6 s) and let them choose
  the reflow order: the back carries the heavy parts (the 11 mm jack, C111, L1), which shouldn't
  hang upside down through a second pass.
- Check stock (2026-11-18): LEDs C5349955 ~80k (listed by LCSC as "XL-2020RGBC-2812B"; its
  datasheet says XL-2020RGBC-WS2812B), jack C319134 ~4.4k, inductor C408410 459 (LCSC's product
  page is broken; fallback Sunlord SWPA5040S4R7MT, C48496), 1.25 mm header C481433 ~0.9–1.5k,
  JST XH C158012 ~1.9k, TPS563201 ~4.3k. All extended parts.
- A round board with parts near the edge may need JLCPCB's break-off rails for assembly. The DC
  jack sits flush with the edge at 6 o'clock (its corners overhang by ~0.1 mm): no rail there.
- The through-hole connector pins poke ~1.5 mm out of the front: the gap to the dial (and any
  baffle) has to clear them.
- Measure the servo board's hole spacing (58 × 23 mm from Waveshare's docs) with calipers.
- KiCad's DRC reports 0 errors and 0 unconnected; the only warnings are `lib_footprint_mismatch`,
  because the generator strips the hidden silkscreen from the library footprints.

## Build

```sh
cd hardware
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
.venv/bin/python gen_dial.py      # -> dial/dial.kicad_pcb
.venv/bin/python gen_coupon.py    # -> coupon/coupon.kicad_pcb
./render.sh dial/dial.kicad_pcb   # DRC + SVG + 3D render into out/ (uses KiCad 9's kicad-cli)

.venv/bin/python gen_leds.py      # -> leds/leds.kicad_pcb, leds-bom.csv, led-chain.csv
./fill_zones.sh leds/leds.kicad_pcb   # fill the copper pours (KiCad's own filler, via its Python)
./render.sh leds/leds.kicad_pcb
./jlc.sh leds/leds.kicad_pcb      # -> out/leds-jlc/: gerbers.zip, bom.csv, cpl.csv for JLCPCB
                                  #    (CPL corrected to JLCPCB's footprints: jlc_ref.py + cpl.py)
```

Boards with parts (`gen_leds.py`) place KiCad's own library footprints (read from the KiCad app),
set their nets, and write tracks, vias and zone outlines; `kicadart.Part` computes pad positions
the way KiCad does, including mirroring for the back. KiCad fills the zones, since its clearance
and island rules decide connectivity; DRC then confirms every pad is connected.

`template.kicad_pcb` is an empty KiCad 9 board with the stackup; scripts append their
`gr_poly`, `gr_circle` and footprint s-expressions to it (`kicadart.py`).

## Fonts

`fonts/` holds Cormorant Garamond (names and numerals) and Noto Sans Symbols (planet
symbols), both under the SIL Open Font License (texts alongside). Noto Sans Symbols has no ☉,
so the Sun is drawn as a ring and dot.

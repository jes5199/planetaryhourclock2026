#!/usr/bin/env python3
"""Generate the LED board (hardware/leds/leds.kicad_pcb): 102 addressable LEDs behind the dial's
windows, and the clock's power hub. Assembled by JLCPCB on both sides.

Front (towards the dial): only the WS2812B-2020 LEDs and their 100 nF capacitors, so the board
can sit close behind the dial. Back: a 12 V -> 5 V buck (TPS563201), the connectors, and the
servo board's mounting holes.

  12 V IN  (J1, SMD 5.5 x 2.1 mm DC jack at the bottom edge) -> reverse-polarity P-FET
           -> 12 V to the servo board (J2) and the buck
  5 V      -> every LED (front copper pour) and the screen board (J3, wires up the centre post)
  LED DATA (J4) <- the servo board's RGB-LED header (5 V, GND, data out of its two on-board
                   LEDs); only GND and data are used here, its 5 V pin is left unconnected.

Copper: front = 5 V pour + data tracks; back = GND pour + the power section. Pours are outlined
here and filled by KiCad (fill_zones.py).

Run:  .venv/bin/python gen_leds.py && ../hardware/fill_zones.sh leds/leds.kicad_pcb
"""
import csv
import math
import pathlib

from shapely import affinity
from shapely.ops import unary_union

import gen_dial as dial
from kicadart import (Board, Font, Nets, Part, board_keepout, board_track, board_via, board_zone, disc,
                      polar, rot_kicad)

HERE = pathlib.Path(__file__).resolve().parent
OUT = HERE / "leds" / "leds.kicad_pcb"

BOARD_R = dial.BOARD_R
DATA_W = 0.2          # data tracks
POWER_W = 0.8         # 12 V / 5 V / switch-node tracks
VIA = dict(d=0.6, drill=0.3)

# The servo board (Waveshare Servo Driver with ESP32) mounts on the back on M2.5 standoffs:
# its holes are 58 x 23 mm apart. Placeholder spot in the lower half until the mechanism exists.
SERVO_CENTRE = (0.0, 30.0)
SERVO_HOLES = (58.0, 23.0)
SERVO_OUTLINE = (65.0, 30.0)   # its board, drawn on the back silkscreen as a placement guide
SERVO_HOLE_D = 2.7

# JLCPCB / LCSC parts.
# XINGLIGHT XL-2020RGBC-WS2812B: WS2812B protocol in a 2020 package (Worldsemi's own WS2812B-2020,
# C965555, was out of stock). Footprint from its datasheet, numbered like KiCad's WS2812B-2020.
LED = ("jlc", "LED_XL-2020RGBC-WS2812B_C5349955", "XL-2020RGBC-WS2812B", "C5349955")
LED_PX, LED_PY = 0.675, 0.625   # its pad centres: +-x across the LED (radial here), +-y along it
LED_VIA_Y = 1.6                 # vias beside its pads, clear of them
C100N = ("Capacitor_SMD", "C_0402_1005Metric", "100nF", "C1525")
C100N_50V = ("Capacitor_SMD", "C_0402_1005Metric", "100nF 50V", "C307331")
C100U = ("Capacitor_SMD", "CP_Elec_6.3x7.7", "100uF 35V", "C2836437")
C10U = ("Capacitor_SMD", "C_0805_2012Metric", "10uF 25V", "C15850")
C22U = ("Capacitor_SMD", "C_0805_2012Metric", "22uF 25V", "C45783")
R56K = ("Resistor_SMD", "R_0402_1005Metric", "56k", "C25796")
R10K = ("Resistor_SMD", "R_0402_1005Metric", "10k", "C25744")
R100K = ("Resistor_SMD", "R_0402_1005Metric", "100k", "C25741")
# Series resistor on the LED data input: 1k also limits the current into LED 0's input if the servo
# board is powered (by USB) while this board isn't.
R1K = ("Resistor_SMD", "R_0402_1005Metric", "1k", "C11702")
BUCK = ("Package_TO_SOT_SMD", "TSOT-23-6", "TPS563201DDCR", "C116592")
IND = ("Inductor_SMD", "L_Sunlord_MWSA0503S", "4.7uH MWSA0503S", "C408410")
# Reverse-polarity protection: a P-channel MOSFET as an "ideal diode" (13 mohm; a Schottky in
# its place would drop ~0.5 V and run hot at servo currents). AO4407A: -30 V, -12 A, VGS +-25 V,
# so the gate can sit at GND through a resistor with no zener. Pins 1-3 S, 4 G, 5-8 D.
PFET = ("Package_SO", "SOIC-8_3.9x4.9mm_P1.27mm", "AO4407A", "C16072")
DIODE_SCREEN = ("Diode_SMD", "D_SMA", "SS14", "C2480")
TVS = ("Diode_SMD", "D_SMA", "SMAJ13A", "C148214")
XH2 = ("Connector_JST", "JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical", "B2B-XH-A", "C158012")
# SMD 5.5 x 2.1 mm DC jack, footprint imported from JLCPCB with easyeda2kicad (footprints/jlc.pretty)
# and checked against XKB's drawing. Pin 1 centre (+), 2 sleeve (GND), 3 switch (unused).
JACK = ("jlc", "DC-005-5A-2.0-SMT_C319134", "DC-005-5A-2.0-SMT", "C319134")
JACK_ANGLE = 180.0    # the cord comes out of the bottom of the clock
JACK_MOUTH = 8.05     # footprint origin to the jack's mouth, which sits on the board edge
# LED data in: Molex PicoBlade (1.25 mm) 3-pin, surface mount, vertical; mates with the servo
# board's RGB-LED header cable. Pins 1-3 plus two mounting tabs (MP).
MX3 = ("Connector_Molex", "Molex_PicoBlade_53398-0371_1x03-1MP_P1.25mm_Vertical", "53398-0371", "C293633")

# LED pads: 1 DOUT, 2 GND, 3 DIN, 4 VDD.
CAP_X = 2.8           # each LED's capacitor sits this far inward of it (clear of its courtyard)
CAP_VIA = 1.4         # and its GND via this far beyond it: the hole clear of the 0402's pad
DOUT, GND_PAD, DIN, VDD = "1", "2", "3", "4"


class Layout:
    def __init__(self):
        self.board = Board()
        self.nets = Nets()
        self.parts = []
        self.counters = {}

    def ref(self, prefix):
        self.counters[prefix] = self.counters.get(prefix, 0) + 1
        return f"{prefix}{self.counters[prefix]}"

    def add(self, spec, prefix, at, rot=0.0, back=False, nets=None, ref=None, silk=True, solid=None):
        lib, name, value, lcsc = spec
        # Thermal relief on the front (LEDs and their 0402s) and for 0402s anywhere (both ends heat
        # evenly in reflow); the power parts on the back join the pours solidly.
        p = Part(lib, name, ref or self.ref(prefix), value, at, rot, back,
                 {k: self.nets[v] for k, v in (nets or {}).items()}, lcsc, silk=silk,
                 solid=(back and "0402" not in name) if solid is None else solid)
        self.parts.append(p)
        return p

    def track(self, pts, net, width=DATA_W, layer="F.Cu"):
        board_track(self.board, pts, width, layer, self.nets[net])

    def via(self, at, net):
        board_via(self.board, at, self.nets[net], **VIA)


# ---- LEDs ---------------------------------------------------------------------------------------

def led_frame(site):
    """Rotation for an LED at this site: its local +x points at the centre and local -y points
    clockwise, so DIN and VDD face inward, DOUT and GND outward, and the chain runs clockwise."""
    return -(site["angle"] + 90)


def local(site, lx, ly):
    """A point given in an LED's own frame, on the board."""
    x0, y0 = polar(site["r"], site["angle"])
    dx, dy = rot_kicad(lx, ly, led_frame(site))
    return (x0 + dx, y0 + dy)


def chain_order(sites):
    """The data chain: rim LEDs 0..83 clockwise, then the star points from the one in the rim's
    start gap, then the star corners, then the compass tips, each set clockwise from the end of
    the last."""
    rim = sorted((s for s in sites if s["kind"] == "rim"), key=lambda s: s["i"])
    order = list(rim)
    start = (rim[-1]["angle"] + rim[0]["angle"] + 360) / 2 % 360   # the gap after LED 83
    for kind in ("star", "corner", "compass"):
        group = [s for s in sites if s["kind"] == kind]
        if kind != "star":
            start = order[-1]["angle"] % 360
            # begin with the nearest one anticlockwise of where the last set ended
            first = min(group, key=lambda s: (start - s["angle"]) % 360)
            start = first["angle"]
        group.sort(key=lambda s: (s["angle"] - start + 0.01) % 360)
        order += group
    return order


def arc_short(r, a0, a1, step=2.0):
    """Points along radius r from angle a0 to a1, whichever way round is shorter."""
    span = (a1 - a0 + 180) % 360 - 180
    n = max(1, int(abs(span) / step))
    return [polar(r, a0 + span * i / n) for i in range(n + 1)]


def arc_points(r, a0, a1, step=2.0):
    """Points along radius r from angle a0 clockwise to a1 (degrees)."""
    span = (a1 - a0) % 360
    n = max(1, int(span / step))
    return [polar(r, a0 + span * i / n) for i in range(n + 1)]


def jack_placement():
    """Where the DC jack goes: on the back, mouth flush with the edge, pointing outward."""
    return polar(BOARD_R - JACK_MOUTH, JACK_ANGLE), -(JACK_ANGLE + 90)


def jack_keepout():
    """Copper the jack puts on the back (pads, peg holes), grown by clearance + a via's radius."""
    from shapely.geometry import box
    at, rot = jack_placement()
    j = Part(*JACK[:2], "J1", "", at, rot, back=True)
    shapes = []
    for n, pts in j.pads.items():
        for i in range(len(pts)):
            x, y = j.pad(n, i)
            r = 1.6 if n == "" else 1.9          # pads are 2.2 x 3.0; pegs up to 2.0 across
            shapes.append(disc(r, (x, y)))
    return unary_union(shapes).buffer(0.5)


def place_leds(L):
    keepout = jack_keepout()
    sites = dial.led_sites()
    chain = chain_order(sites)
    leds = []
    for n, s in enumerate(chain):
        rot = led_frame(s)
        din = "LED_DATA0" if n == 0 else f"LED_D{n}"
        dout = f"LED_D{n + 1}" if n + 1 < len(chain) else "LED_END"
        led = L.add(LED, "D", polar(s["r"], s["angle"]), rot,
                    nets={DOUT: dout, GND_PAD: "GND", DIN: din, VDD: "+5V"})   # keeps its pin-1 tick
        # Its capacitor: inward, beside VDD; its GND end goes through a via to the back pour.
        cap = L.add(C100N, "C", local(s, CAP_X, -LED_PY), rot, nets={"1": "+5V", "2": "GND"}, silk=False)
        from shapely.geometry import Point
        for dx in (CAP_VIA, CAP_VIA + 0.5, CAP_VIA + 1.0, CAP_VIA + 1.5):   # clear of the jack's pads
            gvia = local(s, CAP_X + dx, -LED_PY)
            if not keepout.contains(Point(gvia)):
                break
        L.via(gvia, "GND")
        L.track([cap.pad(2), gvia], "GND")
        # The LED's GND pad: a via just anticlockwise of it, outside the data path.
        lvia = local(s, -LED_PX, LED_VIA_Y)
        L.via(lvia, "GND")
        L.track([led.pad(GND_PAD), lvia], "GND")
        leds.append((s, led, din, dout))

    jumps = []
    for (s, led, din, dout), nxt in zip(leds, leds[1:]):
        s2, led2 = nxt[0], nxt[1]
        if s["kind"] == s2["kind"]:
            # Same ring: leave DOUT clockwise on the outer row, cross to the inner row, and come
            # into the next LED's DIN from its anticlockwise side.
            a = local(s, -LED_PX, -1.5)
            b = local(s, LED_PX, -2.4)
            c = local(s2, LED_PX, 2.0)
            pts = [led.pad(DOUT), a, b]
            rb = math.dist(b, (0, 0))
            ab = math.degrees(math.atan2(b[0], -b[1]))
            ac = math.degrees(math.atan2(c[0], -c[1]))
            if (ac - ab) % 360 > 3:
                pts += arc_points(rb, ab, ac)[1:]
            pts += [c, led2.pad(DIN)]
            L.track(pts, dout)
        else:
            # Between sets: through the back, via-to-via.
            v_out = local(s, -LED_PX, -LED_VIA_Y)
            v_in = local(s2, LED_PX, LED_VIA_Y)
            L.track([led.pad(DOUT), v_out], dout)
            L.via(v_out, dout)
            L.via(v_in, dout)
            L.track([v_in, led2.pad(DIN)], dout)
            # On the back: straight in from the source LED to a radius between the two rings
            # (inside the source's capacitor vias, outside the target's GND vias), round to
            # just anticlockwise of the target, and in to its DIN via.
            r_mid = ((s["r"] - CAP_X - CAP_VIA - 0.1) + (s2["r"] + 1.3)) / 2
            a_out = math.degrees(math.atan2(v_out[0], -v_out[1]))
            a_tgt = s2["angle"] - math.degrees(3.0 / r_mid)
            w2 = local(s2, LED_PX, 3.0)
            if s["r"] - CAP_X - CAP_VIA - 0.3 - r_mid < 0.6:
                # Rings too close to pass inside the source's capacitor via: go round the
                # outside of the source LED and come down on its anticlockwise side instead.
                r_out = s["r"] + 2.0
                a_side = s["angle"] - math.degrees(2.5 / s["r"])
                start = [v_out] + arc_short(r_out, a_out, a_side)[1:] + [polar(r_mid, a_side)]
                a_out = a_side
            else:
                start = [v_out]
            jumps.append((start + arc_short(r_mid, a_out, a_tgt) + [w2, v_in], dout))
    for pts, net in jumps:
        L.track(pts, net, layer="B.Cu")
    return leds, chain


# ---- power and connectors (back) ----------------------------------------------------------------

class Block:
    """A group of back-side parts laid out as if at the top of the board (x to the right = clockwise,
    y down = inward), then turned as one about the board centre to dial angle `alpha`."""

    def __init__(self, L, alpha):
        self.L, self.alpha = L, alpha

    def T(self, pt):
        return rot_kicad(pt[0], pt[1], -self.alpha)

    def inv(self, pt):
        return rot_kicad(pt[0], pt[1], self.alpha)

    def add(self, spec, prefix, at, rot=0.0, nets=None):
        return self.L.add(spec, prefix, self.T(at), rot - self.alpha, back=True, nets=nets)

    def p(self, part, n):
        """A pad's position in this block's own layout coordinates."""
        return self.inv(part.pad(n))

    def track(self, pts, net, width, layer="B.Cu"):
        self.L.track([self.T(q) for q in pts], net, width, layer)

    def via(self, at, net):
        self.L.via(self.T(at), net)


REG_R = 61.0        # radius of the regulator (between the star points and the rim LEDs' vias)
REG_ALPHA = 150.0   # its dial angle: lower right, near the DC jack at the bottom
IN_ALPHA = 168.0    # the 12 V input block (J2, Q1), between the regulator and the jack
J3_R = 10.0         # 5 V out to the screen: just above the centre hole, where its wires go up the post
# LED data in: beside the servo board's RGB-LED header H1. With its servo ports, OLED and jacks
# facing out (ESP32 side towards this board) and its DC jack towards J2, H1 is at its left end
# (seen from the front), a side-entry header whose cable leaves over that edge.
J4_AT = (SERVO_CENTRE[0] - SERVO_OUTLINE[0] / 2 - 4.0, SERVO_CENTRE[1])
DATA_R = 60.0       # the data line's run round to LED 0, between the star points and rim vias


def place_power(L, first_led_site):
    """The power hub, on the back in the band between the star points and the rim LEDs."""
    B = Block(L, REG_ALPHA)
    o = (0.0, -REG_R)                      # the regulator, U1, at the block's origin

    def at(x, y):
        return (o[0] + x, o[1] + y)

    # EN is tied straight to VIN (both rated 19 V), as the datasheet allows.
    u1 = B.add(BUCK, "U", at(0, 0), 0, nets={"1": "GND", "2": "SW", "3": "+12V", "4": "FB", "5": "+12V", "6": "VBST"})
    # Three input capacitors (10 uF 0805s keep only ~half their value at 12 V).
    cin1 = B.add(C10U, "C", at(-1.2, -3.9), 90, nets={"1": "+12V", "2": "GND"})
    cin2 = B.add(C10U, "C", at(1.3, -3.9), 90, nets={"1": "+12V", "2": "GND"})
    cin3 = B.add(C10U, "C", at(3.8, -3.9), 90, nets={"1": "+12V", "2": "GND"})
    cbyp = B.add(C100N_50V, "C", at(5.4, -3.43), 90, nets={"1": "+12V", "2": "GND"})
    cbst = B.add(C100N, "C", at(0.2, 2.5), 0, nets={"1": "SW", "2": "VBST"})
    l1 = B.add(IND, "L", at(-5.3, 0), 180, nets={"1": "SW", "2": "+5V"})
    # Three output capacitors (22 uF 0805s keep ~half at 5 V; TI asks for 20-68 uF).
    co1 = B.add(C22U, "C", at(-10.5, -2.6), 180, nets={"1": "+5V", "2": "GND"})
    co2 = B.add(C22U, "C", at(-10.5, 0.0), 180, nets={"1": "+5V", "2": "GND"})
    co3 = B.add(C22U, "C", at(-10.5, 2.6), 180, nets={"1": "+5V", "2": "GND"})
    rtop = B.add(R56K, "R", at(3.0, -0.95), 0, nets={"1": "FB", "2": "+5V"})
    rbot = B.add(R10K, "R", at(3.0, 0.1), 0, nets={"1": "FB", "2": "GND"})

    P = B.p
    bus_y = P(cin1, 1)[1]
    bus_end = at(6.5, 0)[0], bus_y
    # 12 V bus across the input capacitors, VIN up to it, EN under the chip to VIN.
    B.track([P(cin1, 1), bus_end], "+12V", 0.8)
    B.track([P(u1, 3), (P(u1, 3)[0], bus_y)], "+12V", 0.6)
    x5, y5 = P(u1, 5)
    x3, y3 = P(u1, 3)
    B.track([P(u1, 5), (x5 - 0.6625, y5), (x3 + 0.6625, y3), P(u1, 3)], "+12V", 0.25)
    # Switch node to the inductor; bootstrap capacitor fed under the chip.
    x2, y2 = P(u1, 2)
    B.track([P(u1, 2), P(l1, 1)], "SW", POWER_W)
    xc = (x2 + P(u1, 5)[0]) / 2
    B.track([P(u1, 2), (xc - 0.3, y2), (xc, y2 + 0.45), (xc, P(cbst, 1)[1] - 0.5), P(cbst, 1)], "SW", 0.25)
    x6, y6 = P(u1, 6)
    B.track([P(u1, 6), (x6, P(cbst, 2)[1]), P(cbst, 2)], "VBST", 0.3)
    # Ground: U1's GND pin and the input capacitors' GND ends drop through vias to a GND
    # island on the front, directly under the chip, closing the input loop in a few mm.
    x1, y1 = P(u1, 1)
    gv1, gv2 = (x1 - 0.46, y1 + 0.95), (x1 - 0.46, y1 + 1.8)
    for v in (gv1, gv2):
        B.via(v, "GND")
    B.track([P(u1, 1), gv1, gv2], "GND", 0.6)
    for ca, cb in ((cin1, cin2), (cin2, cin3)):
        B.via(((P(ca, 2)[0] + P(cb, 2)[0]) / 2, P(ca, 2)[1]), "GND")
    island = [B.T(q) for q in (at(-2.4, -5.7), at(3.1, -5.7), at(3.1, 3.3), at(-2.4, 3.3))]
    # 5 V out, with vias up to the front pour.
    x5v = P(co1, 1)[0]
    B.track([P(l1, 2), P(co2, 1)], "+5V", POWER_W)
    B.track([P(co1, 1), P(co3, 1)], "+5V", POWER_W)
    yc1, yc2, yc3 = P(co1, 1)[1], P(co2, 1)[1], P(co3, 1)[1]
    for y in ((yc1 + yc2) / 2, (yc2 + yc3) / 2, yc1 - 1.35, yc3 + 1.35):
        B.via((x5v, y), "+5V")
    B.track([(x5v, yc1 - 1.35), (x5v, yc3 + 1.35)], "+5V", 0.6)
    # Feedback divider; its top end goes to the 5 V pour through a via.
    B.track([P(u1, 4), P(rtop, 1)], "FB", 0.25)
    B.track([P(rtop, 1), P(rbot, 1)], "FB", 0.25)
    vtop = (P(rtop, 2)[0] + 0.84, P(rtop, 2)[1])
    B.via(vtop, "+5V")
    B.track([P(rtop, 2), vtop], "+5V", 0.3)

    # The DC jack (J1) at the bottom edge, then J2 (12 V out to the servo board) and the
    # reverse-polarity FET just anticlockwise of it, on the side where its centre pin lands.
    jat, jrot = jack_placement()
    j1 = L.add(JACK, "J", jat, jrot, back=True, nets={"1": "+12V_IN", "2": "GND", "3": "GND"}, silk=False)
    L.track([j1.pad("1", 0), j1.pad("1", 1)], "+12V_IN", 1.0, "B.Cu")  # its two pin-1 pads
    bus_world = B.T(bus_end)
    r_bus = math.dist(bus_world, (0, 0))
    A = Block(L, IN_ALPHA)
    oa = (0.0, -r_bus)
    j2 = A.add(XH2, "J", (oa[0] - 9.8, oa[1]), 0, nets={"1": "+12V", "2": "GND"})
    # Drain to the jack, source to the 12 V rail, gate to GND through 100k: plugged in the right
    # way round the body diode lifts the source and the gate turns the FET fully on; reversed,
    # it stays off.
    q1 = A.add(PFET, "Q", (oa[0], oa[1] + 0.6), 0, nets={"1": "+12V", "2": "+12V", "3": "+12V", "4": "GATE",
                                        "5": "+12V_IN", "6": "+12V_IN", "7": "+12V_IN", "8": "+12V_IN"})
    rg = A.add(R100K, "R", (oa[0], oa[1] - 3.7), 90, nets={"1": "GATE", "2": "GND"})
    sx_, _ = A.p(q1, 1)
    dx_, _ = A.p(q1, 5)
    s_ys = sorted(A.p(q1, n)[1] for n in (1, 2, 3))
    d_ys = sorted(A.p(q1, n)[1] for n in (5, 6, 7, 8))
    A.track([(sx_, s_ys[0]), (sx_, s_ys[-1])], "+12V", 1.0)        # tie the source pins
    A.track([(dx_, d_ys[0]), (dx_, d_ys[-1])], "+12V_IN", 1.0)     # and the drain pins
    gx, gy = A.p(q1, 4)
    A.track([(gx, gy), (gx + 0.9, A.p(rg, 1)[1]), A.p(rg, 1)], "GATE", 0.3)
    q_in = max((A.p(q1, n) for n in (5, 6, 7, 8)), key=lambda q: q[1])   # the inward drain pin
    jack_pin1 = min((j1.pad("1", i) for i in range(2)), key=lambda q: math.dist(q, A.T(q_in)))
    L.track([A.T(q_in), jack_pin1], "+12V_IN", 2.0, "B.Cu")
    jx, jy = A.p(j2, 1)
    kx = sx_
    A.track([A.p(j2, 1), (jx, jy + 2.3), (kx, jy + 2.3), (kx, s_ys[-1])], "+12V", 1.5)
    # Behind the FET: a 13 V TVS and 100 uF of bulk capacitance, so plugging in a live brick
    # can't ring the 12 V rail up past U1's 19 V limit.
    tvs = A.add(TVS, "D", (oa[0] + 0.8, oa[1] + 5.6), 0, nets={"1": "+12V", "2": "GND"})
    cbulk = A.add(C100U, "C", (oa[0] - 8.0, oa[1] + 7.6), 0, nets={"1": "+12V", "2": "GND"})
    A.track([A.p(tvs, 1), (A.p(tvs, 1)[0], jy + 2.3), (kx, jy + 2.3)], "+12V", 1.0)
    A.track([(jx, jy + 2.3), (jx, A.p(cbulk, 1)[1]), A.p(cbulk, 1)], "+12V", 1.0)
    # Copper pours on the power path, for current and to spread any heat.
    from shapely.geometry import Polygon
    # They stay inward of the rim capacitors' GND vias (y > -1.4 here: some are nudged inward
    # near the jack) and clear of J2's GND pin,
    # which reach GND only through the back pour.
    zin = Polygon([A.T((oa[0] + x, oa[1] + y)) for x, y in ((0.9, -1.4), (9.5, -1.4), (9.5, 3.6), (0.9, 3.6))])
    z12 = Polygon([A.T((oa[0] + x, oa[1] + y)) for x, y in
                   ((-11.0, 1.3), (-3.8, 1.3), (-3.8, -1.1), (-1.2, -1.1), (-1.2, 3.9), (-11.0, 3.9))])
    # The bus follows the arc from the regulator to J2.
    a0 = math.degrees(math.atan2(bus_world[0], -bus_world[1]))
    j2w = j2.pad(1)
    a1 = math.degrees(math.atan2(j2w[0], -j2w[1]))
    L.track([bus_world] + arc_points(r_bus, a0, a1, step=1.0)[1:-1] + [j2w], "+12V", 0.6, "B.Cu")

    # 5 V to the screen board, through a Schottky: the screen board's 5 V pin is its USB VBUS,
    # so without it this board and a computer on that USB port would feed each other. Just above
    # the centre hole, so its wires go straight up the screen's post.
    C = Block(L, 0.0)
    j3 = C.add(XH2, "J", (-1.25, -J3_R), 0, nets={"1": "+5V_SCREEN", "2": "GND"})
    d2 = C.add(DIODE_SCREEN, "D", (-3.25, -J3_R - 6.0), 180, nets={"1": "+5V_SCREEN", "2": "+5V"})
    C.track([C.p(d2, 1), C.p(j3, 1)], "+5V_SCREEN", 0.8)
    v5 = (C.p(d2, 2)[0] - 2.2, C.p(d2, 2)[1])
    v5b = (v5[0], v5[1] - 1.0)
    for v in (v5, v5b):
        C.via(v, "+5V")
    C.track([C.p(d2, 2), v5, v5b], "+5V", 0.8)

    # LED data in, beside the servo board's H1: 1k, then round the back, up to the front
    # in the gap in the star ring (star 6 to star 0 has no track), and in to LED 0's DIN.
    D = Block(L, 0.0)
    j4 = D.add(MX3, "J", J4_AT, 90, nets={"2": "GND", "3": "LED_DATA_RAW", "MP": "GND"})
    p3 = D.p(j4, 3)
    r1 = D.add(R1K, "R", (p3[0], p3[1] - 5.75), 90, nets={"1": "LED_DATA_RAW", "2": "LED_DATA0"})
    D.track([p3, D.p(r1, 1)], "LED_DATA_RAW", 0.25)
    s0 = first_led_site
    a_led = s0["angle"] % 360
    r_star = [x for x in dial.led_sites() if x["kind"] == "star"][0]["r"]
    r_back = r_star - 1.0                          # between the star caps' vias and the star LEDs' vias
    a_r1 = math.degrees(math.atan2(r1.pad(2)[0], -r1.pad(2)[1])) % 360
    a_up = 276.0                                   # where it goes to the front, in the star-ring gap
    v_up = polar(DATA_R, a_up)
    L.track([r1.pad(2), polar(r_back, a_r1 + 5)] + arc_short(r_back, a_r1 + 5, a_up - 6)[1:]
            + [v_up], "LED_DATA0", DATA_W, "B.Cu")
    L.via(v_up, "LED_DATA0")
    b0 = local(s0, s0["r"] - DATA_R, 2.0)
    a_b0 = math.degrees(math.atan2(b0[0], -b0[1])) % 360
    led0 = L.parts[0]                               # LEDs are placed first: D1 is chain LED 0
    L.track([v_up] + arc_points(DATA_R, a_up, a_b0)[1:] + [b0, local(s0, 3.0, 2.0), led0.pad(DIN)],
            "LED_DATA0", DATA_W, "F.Cu")
    return dict(j1=j1, j2=j2, j3=j3, j4=j4, blocks=dict(j1=Block(L, JACK_ANGLE), j2=A, j3=C, j4=D),
                zones=dict(gnd_island=island, in12=zin, v12=z12))


# ---- back silkscreen ----------------------------------------------------------------------------

label_font = Font("CormorantGaramond[wght].ttf", weight=700)


def back_label(text, size, at, rot=0.0):
    """Text for the back silkscreen, centred on `at` and turned clockwise by `rot` degrees (as
    seen from the front), mirrored so it reads correctly from behind."""
    g = label_font.text(text, size, size * 0.08)
    g = affinity.scale(g, -1, 1, origin=(0, 0))
    g = affinity.rotate(g, rot, origin=(0, 0))
    return affinity.translate(g, *at).buffer(0.06)   # Cormorant's hairlines up to silkscreen width


def upright(alpha):
    """Rotation for a back label at dial angle alpha that keeps it reading upright from behind."""
    return alpha if math.cos(math.radians(alpha)) >= 0 else alpha - 180


def silk(L, conns):
    s = []
    blocks = conns["blocks"]
    jb = blocks["j1"]
    s.append(back_label("12V IN", 1.25, jb.T((0, -BOARD_R + 18.3)), upright(JACK_ANGLE)))
    s.append(back_label("12V  5.5×2.1  centre +", 1.1, jb.T((0, -BOARD_R + 16.5)), upright(JACK_ANGLE)))
    j4 = conns["j4"]
    x4 = j4.pad(2)[0]
    y4 = max(j4.pad("MP", i)[1] for i in range(2))
    s.append(back_label("LED DATA", 1.25, (x4 - 1.4, y4 + 3.2)))
    s.append(back_label("top to bottom: DATA GND nc", 0.9, (x4 - 1.4, y4 + 5.0)))
    for key, text in (("j2", "12V SERVO"), ("j3", "5V SCREEN")):
        j, B = conns[key], blocks[key]
        p1 = B.p(j, 1)
        p2 = B.p(j, 2)
        mid = ((p1[0] + p2[0]) / 2, p1[1] + {"j2": -6.4, "j3": -8.6}.get(key, 6.2))   # beside it
        s.append(back_label(text, 1.25, B.T(mid), upright(B.alpha)))
        plus = (p1[0], p1[1] + 4.4) if key == "j3" else (p1[0], p1[1] - 4.2)   # J3: inward, D2 is outward
        s.append(back_label("+", 1.8, B.T(plus), upright(B.alpha)))
    s.append(back_label("PLANETARY HOURS CLOCK", 2.2, (0, -44.0)))
    s.append(back_label("LED BOARD", 2.2, (0, -41.0)))
    sx, sy = SERVO_CENTRE
    w, h = SERVO_OUTLINE
    from shapely.geometry import box
    frame = box(sx - w / 2, sy - h / 2, sx + w / 2, sy + h / 2)
    s.append(frame.exterior.buffer(0.075))
    s.append(back_label("SERVO BOARD", 2.0, (sx, sy)))
    return unary_union(s)


# ---- outputs ------------------------------------------------------------------------------------

def write_bom(L, path):
    groups = {}
    for p in L.parts:
        if not p.lcsc:
            continue
        key = (p.value, f"{p.lib}:{p.name}", p.lcsc)
        groups.setdefault(key, []).append(p.ref)
    with open(path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
        for (value, fp, lcsc), refs in sorted(groups.items(), key=lambda kv: kv[1][0]):
            refs.sort(key=lambda r: (r.rstrip("0123456789"), int(r[len(r.rstrip("0123456789")):] or 0)))
            w.writerow([value, ",".join(refs), fp, lcsc])


def main():
    L = Layout()
    for n in ("GND", "+5V", "+12V", "+12V_IN"):
        L.nets[n]
    leds, chain = place_leds(L)
    conns = place_power(L, chain[0])

    b = L.board
    for p in L.parts:
        p.to_board(b)
    # Holes: centre (hand tubes), the dial's standoffs, the servo board's standoffs.
    b.circle(BOARD_R, "Edge.Cuts")
    b.circle(dial.CENTER_HOLE_D / 2, "Edge.Cuts")
    for i, a in enumerate(dial.MOUNT_ANGLES):
        b.hole(dial.MOUNT_D, polar(dial.MOUNT_R, a), ref=f"H{i + 1}")
    sx, sy = SERVO_CENTRE
    hx, hy = SERVO_HOLES
    holes = [polar(dial.MOUNT_R, a) for a in dial.MOUNT_ANGLES]
    for i, (dx, dy) in enumerate(((-1, -1), (1, -1), (-1, 1), (1, 1))):
        b.hole(SERVO_HOLE_D, (sx + dx * hx / 2, sy + dy * hy / 2), ref=f"H{5 + i}")
        holes.append((sx + dx * hx / 2, sy + dy * hy / 2))
    # No pour copper where standoffs, nuts and screw heads sit: a metal standoff scratched through
    # the mask must not bridge the 5 V pour on one face to the GND pour on the other.
    for h in holes:
        board_keepout(b, disc(3.0, h))
    b.fill(silk(L, conns), "B.SilkS")
    # Pours: 5 V on the front, GND on the back, over the whole board (filled by KiCad).
    outline = disc(BOARD_R - 0.3)
    board_zone(b, outline, "F.Cu", L.nets["+5V"])
    board_zone(b, outline, "B.Cu", L.nets["GND"])
    # Local pours that take priority: GND under the regulator (front), and the 12 V path (back).
    from shapely.geometry import Polygon
    z = conns["zones"]
    board_zone(b, Polygon(z["gnd_island"]), "F.Cu", L.nets["GND"], priority=1)
    board_zone(b, z["in12"], "B.Cu", L.nets["+12V_IN"], priority=1, thermal=False)
    board_zone(b, z["v12"], "B.Cu", L.nets["+12V"], priority=1, thermal=False)

    OUT.parent.mkdir(exist_ok=True)
    write_with_nets(b, L.nets, OUT, core_mm=1.51)
    write_bom(L, OUT.parent / "leds-bom.csv")
    with open(OUT.parent / "led-chain.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["chain index", "kind", "index", "angle_deg", "radius_mm"])
        for n, s in enumerate(chain):
            w.writerow([n, s["kind"], s["i"], f"{s['angle'] % 360:.2f}", f"{s['r']:.2f}"])
    print(f"wrote {OUT} ({len(L.parts)} parts, {len(L.nets.names) - 1} nets)")


def write_with_nets(board, nets, path, core_mm):
    """Board.write, plus the net table and a 1.6 mm stackup (the dial's template is 0.8 mm)."""
    board.write(path)
    text = pathlib.Path(path).read_text()
    text = text.replace('\t(net 0 "")\n', nets.sexprs(), 1)
    text = text.replace("(thickness 0.71)", f"(thickness {core_mm})", 1)
    text = text.replace("(general\n\t\t(thickness 0.8)", "(general\n\t\t(thickness 1.6)", 1)
    pathlib.Path(path).write_text(text)


if __name__ == "__main__":
    main()

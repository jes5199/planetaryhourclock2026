#!/usr/bin/env python3
"""Generate the LED board (hardware/leds/leds.kicad_pcb): 102 addressable LEDs behind the dial's
windows, and the clock's power hub. Assembled by JLCPCB on both sides.

Front (towards the dial): only the WS2812B-2020 LEDs and their 100 nF capacitors, so the board
can sit close behind the dial. Back: a 12 V -> 5 V buck (TPS563201), the connectors, and the
servo board's mounting holes.

  12 V IN  (J1) -> reverse-polarity diode -> 12 V to the servo board (J2) and the buck
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
from kicadart import (Board, Font, Nets, Part, board_track, board_via, board_zone, disc, polar,
                      rot_kicad)

HERE = pathlib.Path(__file__).resolve().parent
OUT = HERE / "leds" / "leds.kicad_pcb"

BOARD_R = dial.BOARD_R
DATA_W = 0.2          # data tracks
POWER_W = 0.8         # 12 V / 5 V / switch-node tracks
VIA = dict(d=0.6, drill=0.3)

# The servo board (Waveshare Servo Driver with ESP32) mounts on the back on M2.5 standoffs:
# its holes are 58 x 23 mm apart. Placeholder spot in the lower half until the mechanism exists.
SERVO_CENTRE = (0.0, 43.0)
SERVO_HOLES = (58.0, 23.0)
SERVO_OUTLINE = (65.0, 30.0)   # its board, drawn on the back silkscreen as a placement guide
SERVO_HOLE_D = 2.7

# JLCPCB / LCSC parts.
LED = ("LED_SMD", "LED_WS2812B-2020_PLCC4_2.0x2.0mm", "WS2812B-2020", "C965555")
C100N = ("Capacitor_SMD", "C_0402_1005Metric", "100nF", "C1525")
C10U = ("Capacitor_SMD", "C_0805_2012Metric", "10uF 25V", "C15850")
C22U = ("Capacitor_SMD", "C_0805_2012Metric", "22uF 25V", "C45783")
R56K = ("Resistor_SMD", "R_0402_1005Metric", "56k", "C25796")
R10K = ("Resistor_SMD", "R_0402_1005Metric", "10k", "C25744")
R100K = ("Resistor_SMD", "R_0402_1005Metric", "100k", "C25741")
R330 = ("Resistor_SMD", "R_0402_1005Metric", "330", "C25104")
BUCK = ("Package_TO_SOT_SMD", "TSOT-23-6", "TPS563201DDCR", "C116592")
IND = ("Inductor_SMD", "L_Sunlord_MWSA0503S", "4.7uH MWSA0503S", "C408410")
DIODE = ("Diode_SMD", "D_SMA", "SS54", "C22452")
XH2 = ("Connector_JST", "JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical", "B2B-XH-A", "C158012")
MX3 = ("Connector_Molex", "Molex_PicoBlade_53047-0310_1x03_P1.25mm_Vertical", "1.25mm 3P", "C481433")

# WS2812B-2020 pads: 1 DOUT, 2 GND, 3 DIN, 4 VDD.
CAP_X = 2.8           # each LED's capacitor sits this far inward of it (clear of its courtyard)
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

    def add(self, spec, prefix, at, rot=0.0, back=False, nets=None, ref=None, silk=True):
        lib, name, value, lcsc = spec
        p = Part(lib, name, ref or self.ref(prefix), value, at, rot, back,
                 {k: self.nets[v] for k, v in (nets or {}).items()}, lcsc, silk=silk)
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


def place_leds(L):
    sites = dial.led_sites()
    chain = chain_order(sites)
    leds = []
    for n, s in enumerate(chain):
        rot = led_frame(s)
        din = "LED_DATA0" if n == 0 else f"LED_D{n}"
        dout = f"LED_D{n + 1}" if n + 1 < len(chain) else "LED_END"
        led = L.add(LED, "D", polar(s["r"], s["angle"]), rot,
                    nets={DOUT: dout, GND_PAD: "GND", DIN: din, VDD: "+5V"}, silk=False)
        # Its capacitor: inward, beside VDD; its GND end goes through a via to the back pour.
        cap = L.add(C100N, "C", local(s, CAP_X, -0.55), rot, nets={"1": "+5V", "2": "GND"}, silk=False)
        gvia = local(s, CAP_X + 0.9, -0.55)
        L.via(gvia, "GND")
        L.track([cap.pad(2), gvia], "GND")
        # The LED's GND pad: a via just anticlockwise of it, outside the data path.
        lvia = local(s, -0.915, 1.45)
        L.via(lvia, "GND")
        L.track([led.pad(GND_PAD), lvia], "GND")
        if n == 0:  # data comes in from the back through a via beside DIN
            L.track([local(s, 0.915, 1.45), led.pad(DIN)], din)
        leds.append((s, led, din, dout))

    jumps = []
    for (s, led, din, dout), nxt in zip(leds, leds[1:]):
        s2, led2 = nxt[0], nxt[1]
        if s["kind"] == s2["kind"]:
            # Same ring: leave DOUT clockwise on the outer row, cross to the inner row, and come
            # into the next LED's DIN from its anticlockwise side.
            a = local(s, -0.915, -1.5)
            b = local(s, 0.915, -2.4)
            c = local(s2, 0.915, 2.0)
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
            v_out = local(s, -0.915, -1.45)
            v_in = local(s2, 0.915, 1.45)
            L.track([led.pad(DOUT), v_out], dout)
            L.via(v_out, dout)
            L.via(v_in, dout)
            L.track([v_in, led2.pad(DIN)], dout)
            # On the back: straight in from the source LED to a radius between the two rings
            # (inside the source's capacitor vias, outside the target's GND vias), round to
            # just anticlockwise of the target, and in to its DIN via.
            r_mid = ((s["r"] - CAP_X - 1.0) + (s2["r"] + 1.3)) / 2
            a_out = math.degrees(math.atan2(v_out[0], -v_out[1]))
            a_tgt = s2["angle"] - math.degrees(3.0 / r_mid)
            w2 = local(s2, 0.915, 3.0)
            if s["r"] - CAP_X - 1.2 - r_mid < 0.6:
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
REG_ALPHA = 8.0     # its dial angle
IN_ALPHA = 24.0     # the 12 V input block (J1, D1, J2)
J3_ALPHA = -8.5     # 5 V out to the screen
J4_ALPHA = -19.5    # LED data in from the servo board


def place_power(L, first_led_site):
    """The power hub, on the back in the band between the star points and the rim LEDs."""
    B = Block(L, REG_ALPHA)
    o = (0.0, -REG_R)                      # the regulator, U1, at the block's origin

    def at(x, y):
        return (o[0] + x, o[1] + y)

    # EN is tied straight to VIN (both rated 19 V), as the datasheet allows.
    u1 = B.add(BUCK, "U", at(0, 0), 0, nets={"1": "GND", "2": "SW", "3": "+12V", "4": "FB", "5": "+12V", "6": "VBST"})
    cin1 = B.add(C10U, "C", at(-1.2, -3.9), 90, nets={"1": "+12V", "2": "GND"})
    cin2 = B.add(C10U, "C", at(1.1, -3.9), 90, nets={"1": "+12V", "2": "GND"})
    cbyp = B.add(C100N, "C", at(2.9, -3.43), 90, nets={"1": "+12V", "2": "GND"})
    cbst = B.add(C100N, "C", at(0.2, 2.5), 0, nets={"1": "SW", "2": "VBST"})
    l1 = B.add(IND, "L", at(-5.3, 0), 180, nets={"1": "SW", "2": "+5V"})
    co1 = B.add(C22U, "C", at(-10.5, -1.4), 180, nets={"1": "+5V", "2": "GND"})
    co2 = B.add(C22U, "C", at(-10.5, 1.4), 180, nets={"1": "+5V", "2": "GND"})
    rtop = B.add(R56K, "R", at(3.0, -0.95), 0, nets={"1": "FB", "2": "+5V"})
    rbot = B.add(R10K, "R", at(3.0, -1.95), 0, nets={"1": "FB", "2": "GND"})

    P = B.p
    bus_y = P(cin1, 1)[1]
    bus_end = at(6.5, 0)[0], bus_y
    # 12 V bus across the input capacitors, VIN up to it, EN under the chip to VIN.
    B.track([P(cin1, 1), bus_end], "+12V", 0.6)
    B.track([P(u1, 3), (P(u1, 3)[0], bus_y)], "+12V", 0.5)
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
    # 5 V out, with vias up to the front pour.
    x5v = P(co1, 1)[0]
    B.track([P(l1, 2), (x5v, P(l1, 2)[1])], "+5V", POWER_W)
    B.track([P(co1, 1), P(co2, 1)], "+5V", POWER_W)
    for v in ((x5v, P(co1, 1)[1] - 1.35), (x5v, P(co2, 1)[1] + 1.35), (x5v + 0.65, P(l1, 2)[1])):
        B.via(v, "+5V")
    B.track([P(co1, 1), (x5v, P(co1, 1)[1] - 1.35)], "+5V", 0.6)
    B.track([P(co2, 1), (x5v, P(co2, 1)[1] + 1.35)], "+5V", 0.6)
    # Feedback divider; its top end goes to the 5 V pour through a via.
    xf = P(u1, 4)[0] + 0.86
    B.track([P(u1, 4), P(rtop, 1)], "FB", 0.25)
    B.track([(xf, P(rtop, 1)[1]), (xf, P(rbot, 1)[1]), P(rbot, 1)], "FB", 0.25)
    vtop = (P(rtop, 2)[0] + 0.84, P(rtop, 2)[1])
    B.via(vtop, "+5V")
    B.track([P(rtop, 2), vtop], "+5V", 0.3)

    # 12 V input block: J2 (12 V out to the servo board), the reverse-polarity diode, J1 (in).
    bus_world = B.T(bus_end)
    r_bus = math.dist(bus_world, (0, 0))
    A = Block(L, IN_ALPHA)
    oa = (0.0, -r_bus)
    j2 = A.add(XH2, "J", (oa[0] - 9.0, oa[1]), 0, nets={"1": "+12V", "2": "GND"})
    d1 = A.add(DIODE, "D", oa, 0, nets={"1": "+12V", "2": "+12V_IN"})
    j1 = A.add(XH2, "J", (oa[0] + 6.5, oa[1]), 0, nets={"1": "+12V_IN", "2": "GND"})
    A.track([A.p(d1, 2), A.p(j1, 1)], "+12V_IN", POWER_W)
    jx, jy = A.p(j2, 1)
    kx, ky = A.p(d1, 1)
    A.track([A.p(j2, 1), (jx, jy + 2.3), (kx, jy + 2.3), A.p(d1, 1)], "+12V", POWER_W)
    # The bus follows the arc from the regulator to J2.
    a0 = math.degrees(math.atan2(bus_world[0], -bus_world[1]))
    j2w = j2.pad(1)
    a1 = math.degrees(math.atan2(j2w[0], -j2w[1]))
    L.track([bus_world] + arc_points(r_bus, a0, a1, step=1.0)[1:-1] + [j2w], "+12V", 0.6, "B.Cu")

    # 5 V to the screen board: its pin 1 meets the front 5 V pour, pin 2 the back GND pour.
    C = Block(L, J3_ALPHA)
    j3 = C.add(XH2, "J", (-1.25, -REG_R), 0, nets={"1": "+5V", "2": "GND"})

    # LED data in from the servo board: 330 ohm, then up to LED 0 on the front.
    D = Block(L, J4_ALPHA)
    j4 = D.add(MX3, "J", (-1.25, -REG_R), 0, nets={"2": "GND", "3": "LED_DATA_RAW"})
    v_in = local(first_led_site, 0.915, 1.45)
    r1 = D.add(R330, "R", (-1.5, -REG_R - 3.6), 180, nets={"1": "LED_DATA_RAW", "2": "LED_DATA0"})
    D.track([D.p(j4, 3), (D.p(j4, 3)[0], D.p(r1, 1)[1] + 0.9), (D.p(r1, 1)[0], D.p(r1, 1)[1] + 0.9), D.p(r1, 1)],
            "LED_DATA_RAW", 0.25)
    a_in = math.degrees(math.atan2(v_in[0], -v_in[1]))
    r_r1 = math.dist(r1.pad(2), (0, 0))
    L.track([r1.pad(2), polar(r_r1, a_in), v_in], "LED_DATA0", DATA_W, "B.Cu")
    L.via(v_in, "LED_DATA0")
    return dict(j1=j1, j2=j2, j3=j3, j4=j4, blocks=dict(j1=A, j2=A, j3=C, j4=D))


# ---- back silkscreen ----------------------------------------------------------------------------

label_font = Font("CormorantGaramond[wght].ttf", weight=700)


def back_label(text, size, at, rot=0.0):
    """Text for the back silkscreen, centred on `at` and turned clockwise by `rot` degrees (as
    seen from the front), mirrored so it reads correctly from behind."""
    g = label_font.text(text, size, size * 0.08)
    g = affinity.scale(g, -1, 1, origin=(0, 0))
    g = affinity.rotate(g, rot, origin=(0, 0))
    return affinity.translate(g, *at).buffer(0.04)


def silk(L, conns):
    s = []
    blocks = conns["blocks"]
    for key, text in (("j1", "12V IN"), ("j2", "12V SERVO"), ("j3", "5V SCREEN"), ("j4", "LED DATA")):
        j, B = conns[key], blocks[key]
        p1 = B.p(j, 1)
        p2 = B.p(j, 2 if key != "j4" else 3)
        mid = ((p1[0] + p2[0]) / 2, p1[1] + 6.2)          # inward of the connector
        s.append(back_label(text, 1.25, B.T(mid), B.alpha))
        if key != "j4":
            s.append(back_label("+", 1.8, B.T((p1[0], p1[1] - 4.2)), B.alpha))
        else:
            s.append(back_label("nc  GND  DATA", 0.9, B.T(((p1[0] + p2[0]) / 2, p1[1] + 3.6)), B.alpha))
    s.append(back_label("PLANETARY HOURS CLOCK", 2.2, (0, -25.5)))
    s.append(back_label("LED BOARD", 2.2, (0, -22.5)))
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
    for i, (dx, dy) in enumerate(((-1, -1), (1, -1), (-1, 1), (1, 1))):
        b.hole(SERVO_HOLE_D, (sx + dx * hx / 2, sy + dy * hy / 2), ref=f"H{5 + i}")
    b.fill(silk(L, conns), "B.SilkS")
    # Pours: 5 V on the front, GND on the back, over the whole board (filled by KiCad).
    outline = disc(BOARD_R - 0.3)
    board_zone(b, outline, "F.Cu", L.nets["+5V"])
    board_zone(b, outline, "B.Cu", L.nets["GND"])

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
    pathlib.Path(path).write_text(text)


if __name__ == "__main__":
    main()

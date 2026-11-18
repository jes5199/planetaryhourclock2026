"""Shared helpers for generating PCB art as KiCad 9 source.

Geometry is built with Shapely in millimetres, centred on (0, 0) with y pointing down (the
same as KiCad and SVG). Angles are degrees clockwise from the top. `Board` collects shapes per
layer and writes a .kicad_pcb by appending s-expressions to hardware/template.kicad_pcb (an
empty KiCad 9 board).
"""
import math
import pathlib
import uuid

from fontTools.pens.basePen import BasePen
from fontTools.ttLib import TTFont
from fontTools.varLib import instancer
from shapely import affinity
from shapely.geometry import LineString, MultiPolygon, Point, Polygon
from shapely.ops import split, transform, unary_union
import shapely

HERE = pathlib.Path(__file__).resolve().parent
EMPTY = Polygon()


# ---- geometry --------------------------------------------------------------------------

def polar(r, ang):
    """Point at radius r, angle ang (degrees clockwise from the top)."""
    a = math.radians(ang)
    return (r * math.sin(a), -r * math.cos(a))


def disc(r, at=(0, 0)):
    return Point(at).buffer(r, quad_segs=64)


def ring(r, width):
    return disc(r + width / 2).difference(disc(max(r - width / 2, 0)))


def sector(r0, r1, a0, a1, step=0.5):
    """Annular sector between radii r0..r1 and angles a0..a1."""
    n = max(2, int(abs(a1 - a0) / step) + 1)
    outer = [polar(r1, a0 + (a1 - a0) * i / (n - 1)) for i in range(n)]
    inner = [polar(r0, a1 - (a1 - a0) * i / (n - 1)) for i in range(n)] if r0 > 0 else [(0, 0)]
    return Polygon(outer + inner)


def radial(r0, r1, ang, width, cap="flat"):
    """A line along the radius at angle ang, from r0 to r1."""
    return LineString([polar(r0, ang), polar(r1, ang)]).buffer(width / 2, cap_style=cap)


def segment(p0, p1, width, cap="round"):
    return LineString([p0, p1]).buffer(width / 2, cap_style=cap, quad_segs=16)


# ---- text ------------------------------------------------------------------------------

class _FlattenPen(BasePen):
    """Collects glyph contours as point lists, flattening curves."""

    def __init__(self, glyphset, steps=8):
        super().__init__(glyphset)
        self.contours, self.cur, self.steps = [], [], steps

    def _moveTo(self, p):
        self.cur = [p]

    def _lineTo(self, p):
        self.cur.append(p)

    def _curveToOne(self, p1, p2, p3):
        p0 = self.cur[-1]
        for i in range(1, self.steps + 1):
            t = i / self.steps
            u = 1 - t
            self.cur.append((u**3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t**3 * p3[0],
                             u**3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t**3 * p3[1]))

    def _qCurveToOne(self, p1, p2):
        p0 = self.cur[-1]
        for i in range(1, self.steps + 1):
            t = i / self.steps
            u = 1 - t
            self.cur.append((u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0],
                             u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1]))

    def _closePath(self):
        if len(self.cur) >= 3:
            self.contours.append(self.cur)
        self.cur = []

    _endPath = _closePath


def _signed_area(pts):
    return sum(x0 * y1 - x1 * y0 for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1])) / 2


class Font:
    """A font file (optionally a variable-font instance) that turns text into polygons."""

    def __init__(self, filename, weight=None):
        font = TTFont(HERE / "fonts" / filename)
        if weight is not None and "fvar" in font:
            font = instancer.instantiateVariableFont(font, {"wght": weight})
        self.font = font
        self.cmap = font.getBestCmap()
        self.glyphs = font.getGlyphSet()
        self.upm = font["head"].unitsPerEm
        self.hmtx = font["hmtx"]
        self._cache = {}

    def glyph(self, ch):
        """Polygon of one character in font units (y up), filled with the nonzero rule."""
        if ch not in self._cache:
            name = self.cmap[ord(ch)]
            pen = _FlattenPen(self.glyphs)
            self.glyphs[name].draw(pen)
            # Outer contours wind one way and holes the other: union each group, subtract.
            pos = [Polygon(c).buffer(0) for c in pen.contours if _signed_area(c) > 0]
            neg = [Polygon(c).buffer(0) for c in pen.contours if _signed_area(c) < 0]
            a, b = unary_union(pos), unary_union(neg)
            self._cache[ch] = b.difference(a) if b.area > a.area else a.difference(b)
        return self._cache[ch], self.hmtx[self.cmap[ord(ch)]][0]

    def text(self, s, size, spacing=0.0):
        """Text as polygons in mm, y down, ink centred on (0, 0). size = em size in mm;
        spacing = extra space after each letter in mm (CSS letter-spacing)."""
        k = size / self.upm
        parts, x = [], 0.0
        for ch in s:
            if ch != " ":
                g, adv = self.glyph(ch)
                parts.append(affinity.affine_transform(g, [k, 0, 0, -k, x, 0]))
            else:
                adv = self.hmtx[self.cmap[ord(" ")]][0]
            x += adv * k + spacing
        geom = unary_union(parts)
        minx, miny, maxx, maxy = geom.bounds
        return affinity.translate(geom, -(minx + maxx) / 2, -(miny + maxy) / 2)


def place(geom, r, ang, rotate=True):
    """Move a shape centred on the origin to radius r at angle ang, turned to face outward."""
    if rotate:
        geom = affinity.rotate(geom, ang, origin=(0, 0))  # clockwise on a y-down page
    x, y = polar(r, ang)
    return affinity.translate(geom, x, y)


def arc(geom, r, ang, inward=False, max_seg=0.05):
    """Bend a shape centred on the origin (like `Font.text`) around a circle: its centre line
    follows radius r, centred on angle ang. Tops point outward (reading clockwise), or inward
    with `inward` (reading anticlockwise, upright at the bottom of a dial)."""
    geom = shapely.segmentize(geom, max_seg)
    sign = -1 if inward else 1

    def warp(x, y, z=None):
        import numpy as np
        a = np.radians(ang) + sign * np.asarray(x) / r
        rr = r - sign * np.asarray(y)  # y down: up (negative y) is outward
        return rr * np.sin(a), -rr * np.cos(a)

    return transform(warp, geom)


# ---- KiCad output ----------------------------------------------------------------------

def _polygons(geom):
    if geom.is_empty:
        return []
    if isinstance(geom, Polygon):
        return [geom]
    if hasattr(geom, "geoms"):
        return [p for g in geom.geoms for p in _polygons(g)]
    return []


def _without_holes(poly):
    """gr_poly can't hold holes: cut a polygon vertically through each hole until none remain."""
    todo, done = [poly], []
    while todo:
        p = todo.pop()
        if not p.interiors:
            done.append(p)
            continue
        x = Polygon(p.interiors[0]).representative_point().x
        minx, miny, maxx, maxy = p.bounds
        pieces = _polygons(split(p, LineString([(x, miny - 1), (x, maxy + 1)])))
        if len(pieces) < 2:  # degenerate; nudge the cut
            pieces = _polygons(split(p, LineString([(x + 1e-3, miny - 1), (x + 1e-3, maxy + 1)])))
        todo.extend(pieces)
    return done


class Board:
    def __init__(self, center=(148.5, 105.0)):
        self.cx, self.cy = center
        self.items = []

    def _xy(self, x, y):
        return f"(xy {x + self.cx:.4f} {y + self.cy:.4f})"

    def fill(self, geom, layer):
        """Emit a filled shape on a layer (split into hole-free polygons)."""
        for poly in _polygons(geom):
            for piece in _without_holes(poly.buffer(0)):
                coords = list(piece.exterior.coords)[:-1]
                if len(coords) < 3 or piece.area < 1e-4:
                    continue
                pts = " ".join(self._xy(x, y) for x, y in coords)
                self.items.append(
                    f'\t(gr_poly\n\t\t(pts {pts})\n\t\t(stroke (width 0) (type default))\n'
                    f'\t\t(fill yes)\n\t\t(layer "{layer}")\n\t\t(uuid "{uuid.uuid4()}")\n\t)\n')

    def circle(self, r, layer, width=0.05, at=(0, 0)):
        x, y = at
        self.items.append(
            f'\t(gr_circle\n\t\t(center {x + self.cx:.4f} {y + self.cy:.4f})\n\t\t(end {x + r + self.cx:.4f} {y + self.cy:.4f})\n'
            f'\t\t(stroke (width {width}) (type default))\n\t\t(fill no)\n\t\t(layer "{layer}")\n\t\t(uuid "{uuid.uuid4()}")\n\t)\n')

    def rect(self, x0, y0, x1, y1, layer, width=0.05):
        self.items.append(
            f'\t(gr_rect\n\t\t(start {x0 + self.cx:.4f} {y0 + self.cy:.4f})\n\t\t(end {x1 + self.cx:.4f} {y1 + self.cy:.4f})\n'
            f'\t\t(stroke (width {width}) (type default))\n\t\t(fill no)\n\t\t(layer "{layer}")\n\t\t(uuid "{uuid.uuid4()}")\n\t)\n')

    def hole(self, d, at=(0, 0), plated=False, pad=None, ref="H"):
        """A drilled hole as a one-pad footprint: NPTH, or plated with copper ring `pad`."""
        x, y = at
        kind = "thru_hole" if plated else "np_thru_hole"
        size = pad if plated else d
        layers = '"*.Cu" "*.Mask"' if plated else '"*.Cu" "*.Mask"'
        self.items.append(
            f'\t(footprint "Hole_{d}mm"\n\t\t(layer "F.Cu")\n\t\t(uuid "{uuid.uuid4()}")\n'
            f'\t\t(at {x + self.cx:.4f} {y + self.cy:.4f})\n'
            f'\t\t(property "Reference" "{ref}" (at 0 0 0) (layer "F.Fab") (hide yes) (uuid "{uuid.uuid4()}") (effects (font (size 1 1) (thickness 0.15))))\n'
            f'\t\t(property "Value" "hole" (at 0 0 0) (layer "F.Fab") (hide yes) (uuid "{uuid.uuid4()}") (effects (font (size 1 1) (thickness 0.15))))\n'
            f'\t\t(attr exclude_from_pos_files exclude_from_bom)\n'
            f'\t\t(pad "" {kind} circle (at 0 0) (size {size} {size}) (drill {d}) (layers {layers}) (uuid "{uuid.uuid4()}"))\n'
            f'\t)\n')

    def write(self, path):
        template = (HERE / "template.kicad_pcb").read_text()
        head, tail = template.rsplit("\t(embedded_fonts no)", 1)
        pathlib.Path(path).write_text(head + "".join(self.items) + "\t(embedded_fonts no)" + tail)


# ---- footprints, nets, tracks and pours (for boards with real parts) ---------------------------

KICAD_FOOTPRINTS = pathlib.Path("/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints")


class Sym(str):
    """An unquoted s-expression atom (a keyword or number), as opposed to a quoted string."""


def sexpr_parse(text):
    """Parse KiCad s-expression text into nested lists of Sym and str."""
    tokens, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if c in "()":
            tokens.append(c)
            i += 1
        elif c.isspace():
            i += 1
        elif c == '"':
            j, buf = i + 1, []
            while text[j] != '"':
                if text[j] == "\\":
                    j += 1
                buf.append(text[j])
                j += 1
            tokens.append("".join(buf))  # plain str: a quoted string
            i = j + 1
        else:
            j = i
            while j < n and not text[j].isspace() and text[j] not in '()"':
                j += 1
            tokens.append(Sym(text[i:j]))
            i = j
    stack = [[]]
    for t in tokens:
        if t == "(":
            stack.append([])
        elif t == ")":
            done = stack.pop()
            stack[-1].append(done)
        else:
            stack[-1].append(t)
    return stack[0][0]


def sexpr_dump(node, indent=1):
    if not isinstance(node, list):
        if isinstance(node, Sym):
            return str(node)
        return '"' + str(node).replace("\\", "\\\\").replace('"', '\\"') + '"'
    simple = all(not isinstance(x, list) for x in node)
    if simple:
        return "(" + " ".join(sexpr_dump(x) for x in node) + ")"
    tab = "\t" * indent
    parts = [sexpr_dump(x, indent + 1) for x in node]
    head = [p for p, x in zip(parts, node) if not isinstance(x, list)]
    body = [p for p, x in zip(parts, node) if isinstance(x, list)]
    return "(" + " ".join(head) + "".join("\n" + tab + "\t" + b for b in body) + "\n" + tab + ")"


def _num(v):
    return Sym(f"{v:.4f}".rstrip("0").rstrip(".") if abs(v) > 1e-9 else "0")


def _find(node, key):
    return [c for c in node if isinstance(c, list) and c and c[0] == key]


def rot_kicad(x, y, deg):
    """KiCad's RotatePoint: positive angles turn anticlockwise on screen (y down)."""
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    return x * c + y * s, -x * s + y * c


FLIP_LAYER = {"F": "B", "B": "F"}


def _flip_layer_name(name):
    if len(name) > 2 and name[1] == "." and name[0] in FLIP_LAYER:
        return FLIP_LAYER[name[0]] + name[1:]
    return name


class Part:
    """A library footprint placed on the board. `pad(n)` gives a pad's board position (mm,
    relative to the board centre), computed exactly as KiCad does."""

    def __init__(self, lib, name, ref, value, at, rot=0.0, back=False, nets=None, lcsc=None, bom=True,
                 silk=True):
        self.lib, self.name, self.ref, self.value = lib, name, ref, value
        self.silk = silk
        self.at, self.rot, self.back, self.nets = at, rot, back, dict(nets or {})
        self.lcsc, self.bom = lcsc, bom
        local_lib = HERE / "footprints" / f"{lib}.pretty"   # project libraries (e.g. imported JLCPCB parts)
        base = local_lib if local_lib.is_dir() else KICAD_FOOTPRINTS / f"{lib}.pretty"
        self.node = sexpr_parse((base / f"{name}.kicad_mod").read_text())
        self.pads = {}
        for p in _find(self.node, "pad"):
            at_ = _find(p, "at")[0]
            x, y = float(at_[1]), float(at_[2])
            self.pads.setdefault(str(p[1]), []).append((x, -y if back else y))

    def pad(self, n, which=0):
        x, y = self.pads[str(n)][which]
        dx, dy = rot_kicad(x, y, self.rot)
        return (self.at[0] + dx, self.at[1] + dy)

    def to_board(self, board):
        # Project libraries aren't in KiCad's library table: leave their link off (as for holes).
        local = (HERE / "footprints" / f"{self.lib}.pretty").is_dir()
        node = [Sym("footprint"), self.name if local else f"{self.lib}:{self.name}"]
        side = "B.Cu" if self.back else "F.Cu"
        node += [[Sym("layer"), side], [Sym("uuid"), str(uuid.uuid4())],
                 [Sym("at"), _num(self.at[0] + board.cx), _num(self.at[1] + board.cy), _num(self.rot)]]
        if self.lcsc:
            node.append([Sym("property"), "LCSC", self.lcsc, [Sym("at"), Sym("0"), Sym("0"), Sym("0")],
                         [Sym("layer"), "F.Fab" if not self.back else "B.Fab"], [Sym("hide"), Sym("yes")],
                         [Sym("uuid"), str(uuid.uuid4())],
                         [Sym("effects"), [Sym("font"), [Sym("size"), Sym("1"), Sym("1")], [Sym("thickness"), Sym("0.15")]]]])
        for child in self.node[2:]:
            if not isinstance(child, list) or child[0] in ("version", "generator", "generator_version", "layer"):
                continue
            child = self._place_child(child)
            if child is not None:
                node.append(child)
        if not self.bom:
            node.append([Sym("attr"), Sym("exclude_from_bom")])
        board.items.append("\t" + sexpr_dump(node, 1) + "\n")

    def _place_child(self, c):
        c = _deep_copy(c)
        kind = c[0]
        if not self.silk and kind.startswith("fp_") and any(
                isinstance(x, list) and x[0] == "layer" and str(x[1]).endswith("SilkS") for x in c):
            return None
        if kind == "property":
            if c[1] == "Reference":
                c[2] = self.ref
            elif c[1] == "Value":
                c[2] = self.value
            # Keep the silkscreen clean: every field hidden.
            c[:] = [x for x in c if not (isinstance(x, list) and x and x[0] == "hide")]
            c.insert(4, [Sym("hide"), Sym("yes")])
            if self.back:
                for e in _find(c, "effects"):
                    if not _find(e, "justify"):
                        e.append([Sym("justify"), Sym("mirror")])
        if kind == "fp_text" and self.back:
            for e in _find(c, "effects"):
                if not _find(e, "justify"):
                    e.append([Sym("justify"), Sym("mirror")])
        if kind == "pad":
            _transform(c, self.back, self.rot, pad=True)
            net = self.nets.get(str(c[1]))
            if net is not None:
                c[:] = [x for x in c if not (isinstance(x, list) and x and x[0] == "net")]
                uid = [i for i, x in enumerate(c) if isinstance(x, list) and x and x[0] == "uuid"]
                c.insert(uid[0] if uid else len(c), [Sym("net"), Sym(str(net[0])), net[1]])
            return c
        if kind == "model":
            return c
        _transform(c, self.back, self.rot)
        return c


def _deep_copy(n):
    if isinstance(n, list):
        return [_deep_copy(x) for x in n]
    return n


def _transform(node, back, rot, pad=False):
    """Mirror a footprint child for the back side and give pads/texts their board angle."""
    for i, x in enumerate(node):
        if not isinstance(x, list) or not x:
            continue
        k = x[0]
        if k in ("at", "start", "end", "center", "mid", "xy") and len(x) >= 3:
            if back:
                x[2] = _num(-float(x[2]))
            if k == "at":
                ang = float(x[3]) if len(x) > 3 else 0.0
                if back:
                    ang = -ang
                if pad or node[0] in ("property", "fp_text"):
                    ang += rot
                if len(x) > 3:
                    x[3] = _num(ang)
                elif ang:
                    x.append(_num(ang))
        elif k == "layers" and back:
            for j in range(1, len(x)):
                x[j] = _flip_layer_name(x[j])
        elif k == "layer" and back:
            x[1] = _flip_layer_name(x[1])
        elif k == "justify" and back:
            if Sym("mirror") not in x:
                x.append(Sym("mirror"))
        elif k in ("pts", "effects", "font", "stroke", "primitives", "gr_poly", "gr_line", "gr_circle", "gr_arc"):
            _transform(x, back, rot)
    if back and node[0] in ("property", "fp_text") and not _find(node, "effects"):
        pass


class Nets:
    """Net numbering for a board: Nets()['GND'] -> (number, name)."""

    def __init__(self):
        self.names = [""]

    def __getitem__(self, name):
        if name not in self.names:
            self.names.append(name)
        return (self.names.index(name), name)

    def sexprs(self):
        return "".join(f'\t(net {i} "{n}")\n' for i, n in enumerate(self.names))


def board_track(board, pts, width, layer, net):
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        if math.dist((x0, y0), (x1, y1)) < 1e-6:
            continue
        board.items.append(
            f'\t(segment (start {x0 + board.cx:.4f} {y0 + board.cy:.4f}) (end {x1 + board.cx:.4f} {y1 + board.cy:.4f}) '
            f'(width {width}) (layer "{layer}") (net {net[0]}) (uuid "{uuid.uuid4()}"))\n')


def board_via(board, at, net, d=0.6, drill=0.3):
    board.items.append(
        f'\t(via (at {at[0] + board.cx:.4f} {at[1] + board.cy:.4f}) (size {d}) (drill {drill}) '
        f'(layers "F.Cu" "B.Cu") (net {net[0]}) (uuid "{uuid.uuid4()}"))\n')


def board_zone(board, outline, layer, net, clearance=0.25, min_width=0.2, priority=0, thermal=True):
    """A copper pour over `outline` (a Shapely polygon); fill it with fill_zones.py."""
    pts = " ".join(f"(xy {x + board.cx:.4f} {y + board.cy:.4f})" for x, y in list(outline.exterior.coords)[:-1])
    board.items.append(
        f'\t(zone (net {net[0]}) (net_name "{net[1]}") (layer "{layer}") (uuid "{uuid.uuid4()}") '
        f'(hatch edge 0.5) (priority {priority}) (connect_pads {"" if thermal else "yes "}(clearance {clearance})) (min_thickness {min_width}) '
        f'(filled_areas_thickness no) (fill yes (thermal_gap 0.5) (thermal_bridge_width 0.5) (island_removal_mode 0)) '
        f'(polygon (pts {pts})))\n')

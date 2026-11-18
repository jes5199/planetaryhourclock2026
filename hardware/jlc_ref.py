#!/usr/bin/env python3
"""Work out how JLCPCB's footprint for each part sits relative to ours, for the placement file.

JLCPCB places its own library footprint for each LCSC part at the CPL's position and rotation.
Its footprint often has a different origin (it centres connectors; KiCad puts pin 1 at the
origin) or a different zero angle (pin 1 in another corner). This fetches JLCPCB's footprint for
every LCSC number in the BOM (with easyeda2kicad), matches its pads to ours by pad number, and
finds the quarter-turn d and offset t with  jlc_pad = rot(our_pad, d) + t.  cpl.py applies them.

Run with the venv:  .venv/bin/python jlc_ref.py leds/leds-bom.csv out/leds-jlc/jlc_ref.json
"""
import csv
import itertools
import json
import math
import pathlib
import subprocess
import sys

from kicadart import Part, _find, rot_kicad, sexpr_parse

HERE = pathlib.Path(__file__).resolve().parent
CACHE = HERE / "out" / "jlc-ref"

# Parts where JLCPCB's pad numbers don't follow the part's function, so the pad-number fit
# would turn them the wrong way. Checked by hand against the footprint's polarity marks.
OVERRIDES = {
    # SS54 (no longer on the LED board, kept as the example of why this table exists): JLCPCB's
    # SMA footprint numbers the cathode end "2" (its cathode band and pin-1 dot are both at
    # that end). Our pad 1 is the cathode, at the same end: no turn.
    "C22452": dict(turn=0, dx=0.0, dy=0.0, why="cathode band and pin-1 dot at JLCPCB's pad 2 end"),
}


def jlc_pads(lcsc):
    """Pads (number, x, y) of JLCPCB's footprint for an LCSC part, fetched once and cached."""
    lib = CACHE / lcsc / "ref.pretty"
    if not lib.is_dir() or not list(lib.glob("*.kicad_mod")):
        lib.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([str(HERE / ".venv" / "bin" / "easyeda2kicad"), "--footprint", "--lcsc_id", lcsc,
                        "--output", str(lib.parent / "ref"), "--overwrite"],
                       check=True, capture_output=True)
    node = sexpr_parse(next(lib.glob("*.kicad_mod")).read_text())
    pads = []
    for p in _find(node, "pad"):
        num = str(p[1])
        if not num:
            continue                      # mounting pegs
        at = _find(p, "at")[0]
        pads.append((num, float(at[1]), float(at[2])))
    return pads


def our_pads(footprint):
    lib, name = footprint.split(":", 1)
    part = Part(lib, name, "X", "", (0, 0))
    return [(n, x, y) for n, pts in part.pads.items() if n for x, y in pts]


def fit(ours, theirs):
    """Best quarter turn d and offset t taking our pads onto theirs, and the worst pad error."""
    best = None
    for d in (0, 90, 180, 270):
        rotated = [(n, *rot_kicad(x, y, d)) for n, x, y in ours]
        # Pair pads with the same number (some numbers appear twice, e.g. the jack's pin 1).
        pairs = []
        # Pads numbered in both footprints (mounting tabs are often unnumbered or "MP" in one).
        for num in {n for n, _, _ in rotated} & {n for n, _, _ in theirs}:
            a = [(x, y) for n, x, y in rotated if n == num]
            b = [(x, y) for n, x, y in theirs if n == num]
            if len(a) != len(b):
                return None
            best_perm = min(itertools.permutations(b), key=lambda perm: sum(
                math.dist(p, q) for p, q in zip(a, perm)))
            pairs += list(zip(a, best_perm))
        tx = sum(q[0] - p[0] for p, q in pairs) / len(pairs)
        ty = sum(q[1] - p[1] for p, q in pairs) / len(pairs)
        err = max(math.dist((p[0] + tx, p[1] + ty), q) for p, q in pairs)
        if best is None or err < best[3]:
            best = (d, tx, ty, err)
    return best


def main(bom_path, out_path):
    result = {}
    with open(bom_path) as f:
        rows = list(csv.DictReader(f))
    print(f"{'LCSC':10} {'footprint':52} {'turn':>5} {'offset (mm)':>16} {'worst pad err':>14}")
    for row in rows:
        lcsc, fp = row["LCSC Part #"], row["Footprint"]
        d, tx, ty, err = fit(our_pads(fp), jlc_pads(lcsc))
        result[lcsc] = dict(footprint=fp, turn=d, dx=round(tx, 4), dy=round(ty, 4), err=round(err, 3))
        flag = "  <-- pads don't match well" if err > 0.3 else ""
        if lcsc in OVERRIDES:
            o = OVERRIDES[lcsc]
            result[lcsc].update(turn=o["turn"], dx=o["dx"], dy=o["dy"], override=o["why"])
            d, tx, ty = o["turn"], o["dx"], o["dy"]
            flag = f"  <-- override: {o['why']}"
        print(f"{lcsc:10} {fp:52} {d:5} {tx:8.3f},{ty:7.3f} {err:14.3f}{flag}")
    pathlib.Path(out_path).write_text(json.dumps(result, indent=1) + "\n")


if __name__ == "__main__":
    main(*sys.argv[1:])

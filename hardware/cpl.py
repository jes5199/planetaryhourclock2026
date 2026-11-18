"""Write JLCPCB's placement file (CPL) from a board, corrected to JLCPCB's own footprints.

Run with KiCad's bundled Python (jlc.sh does): cpl.py BOARD.kicad_pcb jlc_ref.json cpl.csv

For each part with an LCSC number, jlc_ref.json gives the quarter turn d and offset t that take
our footprint's pads onto JLCPCB's (jlc_pad = rot(our_pad, d) + t, both seen from the top).
  Top:     rotation = R - d,          position = P - rot(t, R - d)
  Bottom:  JLCPCB mirrors its footprint and expects 180 - angle (the kicad-jlcpcb-tools
           convention): angle = R + d, rotation = 180 - angle, position = P - rot(mirror(t), angle)
where P and R are the footprint's origin and rotation on the board. Positions are written the
way KiCad's own position files are (Y up). JLCPCB's assembly preview remains the final check.
"""
import csv
import json
import math
import sys

import pcbnew


def rot(x, y, deg):  # KiCad's RotatePoint: anticlockwise on screen, y down
    a = math.radians(deg)
    return x * math.cos(a) + y * math.sin(a), -x * math.sin(a) + y * math.cos(a)


def main(board_path, ref_path, out_path):
    board = pcbnew.LoadBoard(board_path)
    refs = json.load(open(ref_path))
    rows = []
    for fp in board.GetFootprints():
        if not fp.HasFieldByName("LCSC"):
            continue
        lcsc = fp.GetFieldByName("LCSC").GetText()
        c = refs[lcsc]
        d, tx, ty = c["turn"], c["dx"], c["dy"]
        px, py = fp.GetPosition().x / 1e6, fp.GetPosition().y / 1e6
        r = fp.GetOrientationDegrees()
        if fp.GetLayer() == pcbnew.F_Cu:
            angle = r - d
            ox, oy = rot(tx, ty, angle)
            out_rot, side = angle, "Top"
        else:
            angle = r + d
            ox, oy = rot(tx, -ty, angle)
            out_rot, side = 180 - angle, "Bottom"
        x, y = px - ox, py - oy
        rows.append((fp.GetReference(), f"{x:.4f}mm", f"{-y:.4f}mm", side, f"{out_rot % 360:.2f}"))
    rows.sort(key=lambda r: (r[0].rstrip("0123456789"), int(r[0][len(r[0].rstrip("0123456789")):] or 0)))
    with open(out_path, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
        w.writerows(rows)
    print(f"wrote {out_path} ({len(rows)} parts)")


if __name__ == "__main__":
    main(*sys.argv[1:])

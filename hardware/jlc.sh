#!/bin/sh
# JLCPCB fabrication + assembly files for a generated board:
#   out/<board>-jlc/gerbers.zip   Gerbers and drill files
#   out/<board>-jlc/bom.csv       Comment, Designator, Footprint, LCSC Part #   (from the generator)
#   out/<board>-jlc/cpl.csv       Designator, Mid X, Mid Y, Layer, Rotation      (from KiCad's positions)
# Usage: hardware/jlc.sh hardware/leds/leds.kicad_pcb
set -eu
K=/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli
FILE="${1:?usage: jlc.sh BOARD.kicad_pcb}"
HERE="$(cd "$(dirname "$0")" && pwd)"
stem="$(basename "$FILE" .kicad_pcb)"
OUT="$HERE/out/$stem-jlc"
rm -rf "$OUT" && mkdir -p "$OUT/gerbers"
"$K" pcb export gerbers --layers F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts \
  --no-protel-ext --subtract-soldermask -o "$OUT/gerbers/" "$FILE" >/dev/null
"$K" pcb export drill --format excellon --excellon-separate-th --generate-map --map-format gerberx2 \
  -o "$OUT/gerbers/" "$FILE" >/dev/null
(cd "$OUT/gerbers" && zip -q ../gerbers.zip ./*)
"$K" pcb export pos --format csv --units mm --side both --exclude-dnp -o "$OUT/pos.csv" "$FILE" >/dev/null
"$HERE/.venv/bin/python" - "$OUT/pos.csv" "$OUT/cpl.csv" <<'PY'
import csv, sys
src, dst = sys.argv[1:]
with open(src) as f, open(dst, "w", newline="") as g:
    w = csv.writer(g)
    w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
    for row in csv.DictReader(f):
        w.writerow([row["Ref"], f'{float(row["PosX"]):.4f}mm', f'{float(row["PosY"]):.4f}mm',
                    "Top" if row["Side"] == "top" else "Bottom", f'{float(row["Rot"]) % 360:.2f}'])
PY
rm "$OUT/pos.csv"
cp "$(dirname "$FILE")/$stem-bom.csv" "$OUT/bom.csv"
echo "wrote $OUT: gerbers.zip ($(ls "$OUT/gerbers" | wc -l | tr -d ' ') files), bom.csv, cpl.csv ($(($(wc -l < "$OUT/cpl.csv") - 1)) parts)"

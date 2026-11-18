#!/bin/sh
# JLCPCB fabrication + assembly files for a generated board:
#   out/<board>-jlc/gerbers.zip   Gerbers and drill files
#   out/<board>-jlc/bom.csv       Comment, Designator, Footprint, LCSC Part #   (from the generator)
#   out/<board>-jlc/cpl.csv       Designator, Mid X, Mid Y, Layer, Rotation
# The CPL is corrected to JLCPCB's own footprints: jlc_ref.py fetches each LCSC part's footprint
# and finds its turn and offset against ours, cpl.py applies them (bottom side mirrored, 180 - angle).
# Always check the rotations in JLCPCB's assembly preview.
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
"$K" pcb export drill --format excellon --excellon-separate-th -o "$OUT/gerbers/" "$FILE" >/dev/null
(cd "$OUT/gerbers" && zip -q ../gerbers.zip ./*)
PY=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3
(cd "$HERE" && .venv/bin/python jlc_ref.py "$(dirname "$FILE")/$stem-bom.csv" "$OUT/jlc_ref.json")
"$PY" "$HERE/cpl.py" "$FILE" "$OUT/jlc_ref.json" "$OUT/cpl.csv" 2>&1 | grep -v traits
cp "$(dirname "$FILE")/$stem-bom.csv" "$OUT/bom.csv"
echo "wrote $OUT: gerbers.zip ($(ls "$OUT/gerbers" | wc -l | tr -d ' ') files), bom.csv, cpl.csv ($(($(wc -l < "$OUT/cpl.csv") - 1)) parts)"

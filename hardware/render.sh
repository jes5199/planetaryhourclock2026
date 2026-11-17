#!/bin/sh
# Render a board: design-rule check, SVG of the art layers, and a 3D image in its stackup colours.
# Usage: hardware/render.sh hardware/dial/dial.kicad_pcb   (writes into hardware/out/)
set -eu
K=/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli
FILE="${1:?usage: render.sh BOARD.kicad_pcb}"
OUT="$(dirname "$0")/out"
mkdir -p "$OUT"
stem="$(basename "$FILE" .kicad_pcb)"
"$K" pcb drc --severity-all -o "$OUT/$stem-drc.txt" "$FILE" | grep -E "violations|unconnected"
"$K" pcb export svg --layers F.Cu,F.Mask,F.SilkS,Edge.Cuts --exclude-drawing-sheet --fit-page-to-board \
  -o "$OUT/$stem.svg" "$FILE" >/dev/null
"$K" pcb render --use-board-stackup-colors --quality high --width 2000 --height 2000 --background opaque \
  -o "$OUT/$stem-render.png" "$FILE" >/dev/null
echo "rendered $OUT/$stem-render.png"

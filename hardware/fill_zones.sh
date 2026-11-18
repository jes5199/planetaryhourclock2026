#!/bin/sh
# Fill a board's copper pours with KiCad's zone filler (KiCad's bundled Python has pcbnew).
set -eu
PY=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3
"$PY" "$(dirname "$0")/fill_zones.py" "$1"
# pcbnew.Save leaves project files next to the board; the generators own everything here.
rm -f "${1%.kicad_pcb}.kicad_prl"

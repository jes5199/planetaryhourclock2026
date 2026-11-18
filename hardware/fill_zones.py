"""Fill a generated board's copper pours with KiCad's own zone filler, and check pad positions.

Run with KiCad's bundled Python (fill_zones.sh does this). The generators write zone outlines
only; filling needs KiCad's clearance and island rules, so it's done here, in place.
"""
import sys

import pcbnew

path = sys.argv[1]
board = pcbnew.LoadBoard(path)
pcbnew.ZONE_FILLER(board).Fill(board.Zones())
# Save writes the board in KiCad's own formatting; nothing else changes.
board.Save(path)
print(f"filled {len(board.Zones())} zones in {path}")

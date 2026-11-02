#!/usr/bin/env python3
"""Add and fill the B.Cu ground pour on an NSR-1 board."""
from __future__ import annotations

import argparse
import pcbnew

BOARDS = {
    "control": ("/work/hardware/control/control.kicad_pcb", 120, 90, True),
    "keyboard": ("/work/hardware/keyboard/keyboard.kicad_pcb", 356, 100, False),
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("board", choices=sorted(BOARDS))
    args = parser.parse_args()
    path, width, height, solid = BOARDS[args.board]
    board = pcbnew.LoadBoard(path)
    net = board.FindNet("GND")
    if net is None:
        raise SystemExit("board has no GND net")
    for zone in list(board.Zones()):
        board.Remove(zone)
    zone = pcbnew.ZONE(board)
    zone.SetLayer(pcbnew.B_Cu)
    zone.SetNet(net)
    zone.SetLocalClearance(pcbnew.FromMM(0.2))
    zone.SetMinThickness(pcbnew.FromMM(0.25))
    if solid:
        zone.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
    outline = zone.Outline()
    outline.NewOutline()
    for x, y in ((0.5, 0.5), (width - 0.5, 0.5),
                 (width - 0.5, height - 0.5), (0.5, height - 0.5)):
        outline.Append(pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y)))
    board.Add(zone)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(path, board)
    print(f"filled GND pour on {path}")


if __name__ == "__main__":
    main()

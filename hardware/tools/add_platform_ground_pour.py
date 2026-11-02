#!/usr/bin/env python3
"""Add and fill a B.Cu GND pour on the platform boards."""
import sys
from pathlib import Path
import pcbnew

ROOT = Path(__file__).resolve().parents[2]
BOARDS = {
    "brain": ROOT / "hardware" / "brain" / "brain.kicad_pcb",
    "power": ROOT / "hardware" / "power" / "power.kicad_pcb",
}


def pour(path):
    board = pcbnew.LoadBoard(str(path))
    for zone in list(board.Zones()):
        if zone.GetLayer() == pcbnew.B_Cu:
            board.Remove(zone)
    bb = board.GetBoardEdgesBoundingBox()
    margin = pcbnew.FromMM(0.5)
    zone = pcbnew.ZONE(board)
    zone.SetLayer(pcbnew.B_Cu)
    zone.SetNet(board.FindNet("GND"))
    zone.SetLocalClearance(pcbnew.FromMM(0.2))
    zone.SetMinThickness(pcbnew.FromMM(0.25))
    zone.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
    outline = zone.Outline()
    outline.NewOutline()
    pts = (
        (bb.GetX() + margin, bb.GetY() + margin),
        (bb.GetRight() - margin, bb.GetY() + margin),
        (bb.GetRight() - margin, bb.GetBottom() - margin),
        (bb.GetX() + margin, bb.GetBottom() - margin),
    )
    for x, y in pts:
        outline.Append(pcbnew.VECTOR2I(x, y))
    board.Add(zone)
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    pcbnew.SaveBoard(str(path), board)
    print("poured", path)


if __name__ == "__main__":
    for name in (sys.argv[1:] or sorted(BOARDS)):
        pour(BOARDS[name])

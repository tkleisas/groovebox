#!/usr/bin/env python3
"""Create the NSR-1 brain-carrier and power-board outlines.

Run inside the KiCad container:
  docker run --rm -v "$PWD:/work" -w /work kicad/kicad:10.0 \
    python3 hardware/tools/make_platform_outlines.py --force
"""
import argparse
from pathlib import Path

import pcbnew

ROOT = Path(__file__).resolve().parents[2]
MOUNT_LIB = "/usr/share/kicad/footprints/MountingHole.pretty"
BOARDS = (
    ("control", 130, 100, "NSR-1 CONTROL - 130x100mm - M3"),
    ("brain", 110, 90, "NSR-1 BRAIN CARRIER - Pi Zero 2W - 110x90mm - M3"),
    ("power", 100, 70, "NSR-1 POWER BOARD - 1S 5000mAh - 100x70mm - M3"),
)


def mm(v):
    return pcbnew.FromMM(v)


def point(x, y):
    return pcbnew.VECTOR2I(mm(x), mm(y))


def edge_line(board, x1, y1, x2, y2):
    shape = pcbnew.PCB_SHAPE(board)
    shape.SetShape(pcbnew.SHAPE_T_SEGMENT)
    shape.SetStart(point(x1, y1))
    shape.SetEnd(point(x2, y2))
    shape.SetLayer(pcbnew.Edge_Cuts)
    shape.SetWidth(mm(0.05))
    board.Add(shape)


def mounting_hole(board, ref, x, y):
    fp = pcbnew.FootprintLoad(MOUNT_LIB, "MountingHole_3.2mm_M3")
    if fp is None:
        raise RuntimeError("MountingHole_3.2mm_M3 not found")
    fp.SetReference(ref)
    fp.SetPosition(point(x, y))
    board.Add(fp)


def board_text(board, text, x, y):
    item = pcbnew.PCB_TEXT(board)
    item.SetText(text)
    item.SetLayer(pcbnew.F_SilkS)
    item.SetPosition(point(x, y))
    item.SetTextSize(pcbnew.VECTOR2I(mm(1.5), mm(1.5)))
    item.SetTextThickness(mm(0.25))
    board.Add(item)


def make_board(name, width, height, title):
    board = pcbnew.BOARD()
    edge_line(board, 0, 0, width, 0)
    edge_line(board, width, 0, width, height)
    edge_line(board, width, height, 0, height)
    edge_line(board, 0, height, 0, 0)
    for ref, (x, y) in zip(("H1", "H2", "H3", "H4"),
                            ((5, 5), (width - 5, 5),
                             (5, height - 5), (width - 5, height - 5))):
        mounting_hole(board, ref, x, y)
    out = ROOT / "hardware" / name / f"{name}.kicad_pcb"
    pcbnew.SaveBoard(str(out), board)
    print("wrote", out)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--only", choices=[b[0] for b in BOARDS])
    args = parser.parse_args()
    for name, width, height, title in BOARDS:
        if args.only and name != args.only:
            continue
        out = ROOT / "hardware" / name / f"{name}.kicad_pcb"
        if out.exists() and not args.force:
            print("skip existing", out)
            continue
        make_board(name, width, height, title)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Export Specctra DSN or import SES for the platform boards."""
import argparse
from pathlib import Path
import pcbnew

ROOT = Path(__file__).resolve().parents[2]
BOARDS = {
    "brain": ROOT / "hardware" / "brain" / "brain.kicad_pcb",
    "power": ROOT / "hardware" / "power" / "power.kicad_pcb",
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("board", choices=sorted(BOARDS))
    parser.add_argument("mode", choices=("export", "import"))
    parser.add_argument("file")
    args = parser.parse_args()
    board = pcbnew.LoadBoard(str(BOARDS[args.board]))
    if args.mode == "export":
        ok = pcbnew.ExportSpecctraDSN(board, str(Path(args.file).resolve()))
    else:
        ok = pcbnew.ImportSpecctraSES(board, str(Path(args.file).resolve()))
        if ok:
            pcbnew.SaveBoard(str(BOARDS[args.board]), board)
    raise SystemExit(0 if ok else 1)


if __name__ == "__main__":
    main()

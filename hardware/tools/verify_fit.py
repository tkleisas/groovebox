#!/usr/bin/env python3
"""Check the rev B2 board envelope against the NSR-1 panel."""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PANEL = (400.0, 170.0)
KEYBOARD_ORIGIN = (0.9, 86.0)
CONTROL_ORIGIN = (140.0, 65.0)
LINE = re.compile(
    r"\(gr_line\s+\(start\s+([-\d.]+)\s+([-\d.]+)\)\s+"
    r"\(end\s+([-\d.]+)\s+([-\d.]+)\)(.*?)\(layer \"Edge\.Cuts\"\)",
    re.S)


def board_size(path: Path) -> tuple[float, float]:
    text = path.read_text(encoding="utf-8")
    xs, ys = [], []
    for match in LINE.finditer(text):
        xs.extend((float(match.group(1)), float(match.group(3))))
        ys.extend((float(match.group(2)), float(match.group(4))))
    if not xs:
        raise SystemExit(f"no Edge.Cuts outline in {path}")
    return max(xs) - min(xs), max(ys) - min(ys)


def contains(outer, origin, size) -> bool:
    x, y = origin
    return x >= 0 and y >= 0 and x + size[0] <= outer[0] and y + size[1] <= outer[1]


def main() -> None:
    keyboard = board_size(ROOT / "hardware" / "keyboard" / "keyboard.kicad_pcb")
    control = board_size(ROOT / "hardware" / "control" / "control.kicad_pcb")
    errors = []
    if keyboard != (356.0, 80.0):
        errors.append(f"keyboard is {keyboard}, expected (356.0, 80.0)")
    if control != (130.0, 100.0):
        errors.append(f"control is {control}, expected (130.0, 100.0)")
    if not contains(PANEL, KEYBOARD_ORIGIN, keyboard):
        errors.append("keyboard does not fit the 400 x 170 panel")
    if not contains((KEYBOARD_ORIGIN[0] + keyboard[0], KEYBOARD_ORIGIN[1] + keyboard[1]), CONTROL_ORIGIN, control):
        errors.append("control board does not fit under the keyboard board")
    if errors:
        for error in errors:
            print("ERROR:", error)
        raise SystemExit(1)
    print(f"panel {PANEL[0]:.0f} x {PANEL[1]:.0f} mm")
    print(f"keyboard {keyboard[0]:.0f} x {keyboard[1]:.0f} mm at "
          f"{KEYBOARD_ORIGIN[0]:.0f},{KEYBOARD_ORIGIN[1]:.0f}")
    print(f"control {control[0]:.0f} x {control[1]:.0f} mm under panel at "
          f"{CONTROL_ORIGIN[0]:.0f},{CONTROL_ORIGIN[1]:.0f}")
    print("vertical stack: panel 3 + keyboard 1.6 + switch 5 + standoff 10 + "
          "control 1.6 + Pico 11 = 32.2 mm")
    print("NSR-1 mechanical fit OK")


if __name__ == "__main__":
    main()

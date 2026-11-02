#!/usr/bin/env python3
"""Build the NSR-1 400 x 170 mm faceplate with the rev B2 cutouts."""
from __future__ import annotations

import os

import FreeCAD as App
import Part

OUT = os.path.dirname(os.path.abspath(__file__))
W, H, T = 400.0, 170.0, 3.0
KB_ORIGIN = (0.9, 86.0)
PITCH = 19.05
WHITE_Y = 40.0
BLACK_Y = 18.0
STEP_Y = 70.0
GAPS = (1, 2, 4, 5, 6, 8, 9, 11, 12, 13, 16)


def white_x(i: float) -> float:
    return 6.5 + PITCH / 2 + (i + 1) * PITCH


def gap_x(gap: int) -> float:
    return 6.5 + PITCH / 2 + (gap + 0.5) * PITCH


def rect(x, y, w, h):
    return Part.makeBox(w, h, T + 2, App.Vector(x - w / 2, y - h / 2, -1))


def circle(x, y, r):
    return Part.makeCylinder(r, T + 2, App.Vector(x, y, -1))


def main():
    App.newDocument("NSR1_faceplate")
    plate = Part.makeBox(W, H, T)
    cuts = []
    ox, oy = KB_ORIGIN
    # 16 whites, shifts, 11 blacks, 16 steps.
    for i in range(16):
        cuts.append(rect(ox + white_x(i), oy + WHITE_Y, 14, 14))
        cuts.append(rect(ox + white_x(i), oy + STEP_Y, 14, 14))
    for x in (white_x(-1) - PITCH / 2, white_x(15) + PITCH):
        cuts.append(rect(ox + x, oy + WHITE_Y, 14, 14))
    for gap in GAPS:
        cuts.append(rect(ox + gap_x(gap), oy + BLACK_Y, 14, 14))
    # Screen, soft keys, encoders, pots, transport, slider, joystick, mic.
    cuts.append(rect(8 + 48, 10 + 34, 96, 68))
    for y in (14, 29, 44, 59):
        cuts.append(rect(108 + 7, y + 5.5, 14, 11))
    for x in (30, 55, 80, 105):
        cuts.append(circle(x, 86, 3.5))
    for x in (140, 158):
        cuts.append(circle(x, 32, 3.5))
    for x in (184, 202, 220, 238):
        cuts.append(circle(x, 32, 3.5))
    for x in (310, 327, 344):
        cuts.append(rect(x + 7.5, 14 + 5, 15, 10))
    for x in (310, 328, 346):
        cuts.append(rect(x + 8, 34 + 4.5, 16, 9))
    cuts.append(rect(310 + 31, 85 + 3, 62, 6))
    cuts.append(circle(18, 105, 10))
    cuts.append(circle(112, 5, 1.5))
    for cut in cuts:
        plate = plate.cut(cut)
    shape = App.activeDocument().addObject("Part::Feature", "NSR1_faceplate")
    shape.Shape = plate
    App.activeDocument().recompute()
    Part.export([shape], f"{OUT}/nsr1_faceplate.step")
    shape.Shape.exportStl(f"{OUT}/nsr1_faceplate.stl")
    print(f"faceplate {len(cuts)} cutouts, volume {plate.Volume / 1000:.1f} cm^3")


main()

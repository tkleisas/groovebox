#!/usr/bin/env python3
"""Build the rev B2 NSR-1 tray as a printable solid (FreeCAD Part API).

This is the enclosure shell and board posts. The faceplate/keycap cutout
layout is still a separate design task; the top is an open 400 x 170 mm
panel recess.
"""
from __future__ import annotations

import os

import FreeCAD as App
import Part

OUT = os.path.dirname(os.path.abspath(__file__))
WALL = 2.4
FLOOR = 2.0
OUTER = (406.0, 176.0, 35.0)
PANEL = (400.0, 170.0)
KEYBOARD_POSTS = ((27, 95), (373, 95), (27, 165), (373, 165))
CONTROL_POSTS = ((145, 70), (255, 70), (145, 155), (255, 155))


def box(x, y, z, w, h, d):
    return Part.makeBox(w, h, d, App.Vector(x, y, z))


def main():
    App.newDocument("NSR1")
    shell = box(0, 0, 0, *OUTER)
    shell = shell.cut(box(WALL, WALL, FLOOR,
                          OUTER[0] - 2 * WALL, OUTER[1] - 2 * WALL,
                          OUTER[2] - FLOOR + 1))
    # Panel recess: open the top 400 x 170 mm, leaving a 3 mm lip.
    shell = shell.cut(box((OUTER[0] - PANEL[0]) / 2,
                          (OUTER[1] - PANEL[1]) / 2,
                          OUTER[2] - 2.0, PANEL[0], PANEL[1], 3.0))
    for posts, top in ((KEYBOARD_POSTS, 28.0), (CONTROL_POSTS, 16.0)):
        for x, y in posts:
            post = Part.makeCylinder(3.2, top - FLOOR,
                                     App.Vector(x, y, FLOOR))
            shell = shell.fuse(post)
            hole = Part.makeCylinder(1.6, OUTER[2] + 1,
                                     App.Vector(x, y, -0.5))
            shell = shell.cut(hole)
    shell = shell.removeSplitter()
    shape = App.activeDocument().addObject("Part::Feature", "NSR1_tray")
    shape.Shape = shell
    App.activeDocument().recompute()
    Part.export([shape], f"{OUT}/nsr1_tray.step")
    shape.Shape.exportStl(f"{OUT}/nsr1_tray.stl")
    print(f"tray volume {shell.Volume / 1000:.1f} cm^3, "
          f"bbox {shell.BoundBox}")


main()

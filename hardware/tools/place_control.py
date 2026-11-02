#!/usr/bin/env python3
"""Initial placement pass for the NSR-1 control board.

Loads every component from the generated netlist, assigns its pads to the
schematic nets, and places it on the 130 x 100 mm rev B2 outline.  This is a
floorplan, not a routed board; run it from the KiCad 10 container so the
official footprint libraries are available.
"""
from __future__ import annotations

import sys
from pathlib import Path
import xml.etree.ElementTree as ET

import pcbnew

ROOT = Path("/work")
NETLIST = ROOT / "hardware" / "control" / "control.net.xml"
BOARD = ROOT / "hardware" / "control" / "control.kicad_pcb"
CUSTOM_LIB = ROOT / "hardware" / "nsr1_footprints.pretty"

# ref: (x_mm, y_mm, rotation_deg)
PLACEMENT = {
    "J1": (65, 55, 180),        # Pico module, USB toward the bottom edge

    # Top edge: function keys.
    **{f"J{i}": (13.5 + (i - 7) * 8.5, 10.5, 0) for i in range(7, 17)},

    # Left edge: pots, slider, joystick.
    "J2": (13, 22, 90),
    "J3": (13, 37, 90),
    "J4": (13, 52, 90),
    "J5": (13, 67, 90),
    "J6": (13, 82, 90),

    # Right side: two columns of four encoder headers.
    **{f"J{i}": (100, 10 + (i - 17) * 20, 90) for i in range(17, 21)},
    **{f"J{i}": (115, 10 + (i - 21) * 20, 90) for i in range(21, 25)},

    # Bottom edge: serial + keyboard links.
    "J25": (30, 85, 0),
    "J26": (50, 85, 0),

    "U1": (82, 30, 0),
    "U2": (82, 45, 0),
    "U3": (40, 30, 0),
    "U4": (40, 45, 0),
    "SW1": (82, 72, 0),

    # Analog RC filters, left of the Pico module.
    "R1": (20, 25, 0), "C1": (26, 25, 0),
    "R2": (20, 32, 0), "C2": (26, 32, 0),
    "R3": (20, 39, 0), "C3": (26, 39, 0),
    "R4": (20, 46, 0), "C4": (26, 46, 0),
    "R5": (20, 53, 0), "C5": (26, 53, 0),

    # I2C pull-ups and Pico rail decoupling, lower-left.
    "R6": (20, 70, 0), "R7": (26, 70, 0),
    "C6": (20, 58, 0), "C7": (26, 58, 0), "C8": (32, 58, 0),
    "C9": (20, 64, 0), "C10": (26, 64, 0),
    "C11": (32, 64, 0), "C12": (38, 64, 0),
}


def footprint_path(lib_id: str) -> tuple[Path, str]:
    lib, name = lib_id.split(":", 1)
    if lib == "nsr1_fp":
        return CUSTOM_LIB, name
    return Path("/usr/share/kicad/footprints") / f"{lib}.pretty", name


def main() -> None:
    tree = ET.parse(NETLIST)
    root = tree.getroot()
    components = []
    for comp in root.find("components"):
        components.append({
            "ref": comp.attrib["ref"],
            "value": comp.findtext("value", ""),
            "footprint": comp.findtext("footprint", ""),
        })
    pin_nets = {}
    for net in root.find("nets"):
        name = net.attrib["name"]
        for node in net:
            pin_nets[(node.attrib["ref"], node.attrib["pin"])] = name

    board = pcbnew.LoadBoard(str(BOARD))
    for fp in list(board.GetFootprints()):
        if fp.GetReference().startswith("H"):
            continue
        board.Remove(fp)
    board.GetDesignSettings().m_MinClearance = pcbnew.FromMM(0.15)
    board.GetDesignSettings().m_TrackMinWidth = pcbnew.FromMM(0.1)
    board.GetDesignSettings().m_NetSettings.GetDefaultNetclass().SetClearance(pcbnew.FromMM(0.15))
    nets = {}
    for comp in components:
        ref = comp["ref"]
        if ref not in PLACEMENT:
            raise SystemExit(f"no placement for {ref}")
        lib_path, name = footprint_path(comp["footprint"])
        fp = pcbnew.FootprintLoad(str(lib_path), name)
        if fp is None:
            raise SystemExit(f"missing footprint {comp['footprint']} for {ref}")
        fp.SetReference(ref)
        fp.SetValue(comp["value"])
        x, y, angle = PLACEMENT[ref]
        fp.SetOrientationDegrees(angle)
        target = pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y))
        if ref == "J1":
            pads = list(fp.Pads())
            xs = [p.GetPosition().x for p in pads]
            ys = [p.GetPosition().y for p in pads]
            center = pcbnew.VECTOR2I((min(xs) + max(xs)) // 2,
                                     (min(ys) + max(ys)) // 2)
            fp.SetPosition(target - (center - fp.GetPosition()))
        else:
            bbox = fp.GetBoundingBox(False, False)
            offset = bbox.GetCenter() - fp.GetPosition()
            fp.SetPosition(target - offset)
        board.Add(fp)
        for pad in fp.Pads():
            net_name = pin_nets.get((ref, pad.GetNumber()))
            if not net_name:
                continue
            net = nets.get(net_name)
            if net is None:
                net = pcbnew.NETINFO_ITEM(board, net_name)
                board.Add(net)
                nets[net_name] = net
            pad.SetNet(net)
    board.BuildListOfNets()
    pcbnew.SaveBoard(str(BOARD), board)
    print(f"placed {len(components)} components, {len(nets)} nets")


if __name__ == "__main__":
    main()

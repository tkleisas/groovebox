#!/usr/bin/env python3
"""Initial placement pass for the NSR-1 keyboard board.

Places the 45 MX switches on the 19.05 mm piano/step grid, the 32 in-switch
LEDs and series resistors, the three key expanders, the LED driver, the I2C
link, and decoupling.  Run from the KiCad 10 container; this is a floorplan,
not a routed board.
"""
from __future__ import annotations

from pathlib import Path
import xml.etree.ElementTree as ET

import pcbnew

ROOT = Path("/work")
NETLIST = ROOT / "hardware" / "keyboard" / "keyboard.net.xml"
BOARD = ROOT / "hardware" / "keyboard" / "keyboard.kicad_pcb"
CUSTOM_LIB = ROOT / "hardware" / "nsr1_footprints.pretty"

PITCH = 19.05
FIRST_SLOT = 6.5 + PITCH / 2


def white_x(index: int) -> float:
    """x of white key 0..15 (left to right)."""
    return FIRST_SLOT + (index + 1) * PITCH


def gap_x(gap: int) -> float:
    """x of the gap between white keys gap and gap+1 (1-based)."""
    return FIRST_SLOT + (gap + 0.5) * PITCH


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

    placement = {}
    # Piano row: left shift, 16 whites, right shift.
    placement["KEY38"] = (FIRST_SLOT, 40, 0)
    placement["KEY39"] = (FIRST_SLOT + 17 * PITCH, 40, 0)
    for i in range(16):
        placement[f"KEY{i}"] = (white_x(i), 40, 0)
    # Black keys at the standard two-octave gaps plus the far-right position.
    for key, gap in zip(range(16, 27), (1, 2, 4, 5, 6, 8, 9, 11, 12, 13, 16)):
        placement[f"KEY{key}"] = (gap_x(gap), 18, 0)
    # Dedicated step row.
    for i in range(16):
        placement[f"KEY{40 + i}"] = (white_x(i), 70, 0)

    # LEDs: D1-D16 step row, D17-D32 white keys; LED sits in the switch slot.
    for i in range(16):
        placement[f"D{i + 1}"] = (white_x(i), 70 + 5.08, 0)
        placement[f"D{i + 17}"] = (white_x(i), 40 + 5.08, 0)
        placement[f"R{i + 1}"] = (white_x(i), 52, 0)
        placement[f"R{i + 17}"] = (white_x(i), 58, 0)

    # Bottom service strip.
    placement["J1"] = (10, 88, 0)
    for i, x in enumerate((40, 70, 100, 130)):
        placement[f"U{i + 1}"] = (x, 88, 0)
    for i, x in enumerate((160, 175, 190, 205)):
        placement[f"C{i + 1}"] = (x, 88, 0)

    board = pcbnew.LoadBoard(str(BOARD))
    for fp in list(board.GetFootprints()):
        board.Remove(fp)
    board.GetDesignSettings().m_MinClearance = pcbnew.FromMM(0.15)
    board.GetDesignSettings().m_HoleClearance = pcbnew.FromMM(0.15)
    board.GetDesignSettings().m_NetSettings.GetDefaultNetclass().SetClearance(
        pcbnew.FromMM(0.15))
    nets = {}
    for comp in components:
        ref = comp["ref"]
        if ref.startswith("SW"):
            key = comp["value"]
            if key not in placement:
                raise SystemExit(f"no placement for switch {ref} ({key})")
            x, y, angle = placement[key]
        elif ref in placement:
            x, y, angle = placement[ref]
        else:
            raise SystemExit(f"no placement for {ref}")
        lib, name = comp["footprint"].split(":", 1)
        lib_path = CUSTOM_LIB if lib == "nsr1_fp" else \
            Path("/usr/share/kicad/footprints") / f"{lib}.pretty"
        fp = pcbnew.FootprintLoad(str(lib_path), name)
        if fp is None:
            raise SystemExit(f"missing footprint {comp['footprint']} for {ref}")
        fp.SetReference(ref)
        fp.SetValue(comp["value"])
        if ref.startswith("D"):
            fp.Reference().SetVisible(False)
        fp.SetOrientationDegrees(angle)
        bbox = fp.GetBoundingBox(False, False)
        offset = bbox.GetCenter() - fp.GetPosition()
        target = pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y))
        fp.SetPosition(target - offset)
        if ref.startswith("D"):
            for item in fp.GraphicalItems():
                if item.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS):
                    item.SetLayer(pcbnew.F_Fab)
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

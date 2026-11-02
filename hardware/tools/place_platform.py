#!/usr/bin/env python3
"""Place the NSR-1 platform boards from their generated netlists."""
from __future__ import annotations

import argparse
from pathlib import Path
import xml.etree.ElementTree as ET

import pcbnew

ROOT = Path(__file__).resolve().parents[2]
KICAD_FP = Path("/usr/share/kicad/footprints")
NSR1_FP = ROOT / "hardware" / "nsr1_footprints.pretty"

BOARDS = {
    "brain": {
        "pcb": ROOT / "hardware" / "brain" / "brain.kicad_pcb",
        "net": ROOT / "hardware" / "brain" / "brain.net.xml",
        "clearance": 0.15,
        "track": 0.2,
        "side": {
            "J1": "F", "J2": "F", "J3": "F", "J4": "F", "J5": "F",
            "J6": "F", "J7": "F", "J8": "F", "J9": "F", "J10": "F",
            "J11": "F", "SW1": "F", "U1": "F",
            "U2": "B", "R1": "B", "R2": "B", "R3": "B",
            "R4": "B", "R5": "B", "C1": "B", "C2": "B",
            "C3": "B", "C4": "B",
        },
        "pos": {
            "J1": (42, 50, 90), "J2": (50, 12, 90),
            "J3": (96, 22, 90), "J4": (96, 52, 90),
            "J5": (88, 78, 0), "J6": (14, 82, 0),
            "J7": (28, 82, 0), "J8": (42, 82, 0),
            "J9": (56, 82, 0), "J10": (70, 82, 0),
            "J11": (60, 25, 0), "SW1": (10, 72, 0),
            "U1": (20, 20, 0), "U2": (35, 20, 0),
            "R1": (30, 30, 0), "R2": (30, 36, 0),
            "R3": (42, 30, 0), "R4": (60, 70, 90),
            "R5": (68, 70, 90), "C1": (52, 70, 90),
            "C2": (44, 70, 90), "C3": (36, 70, 90),
            "C4": (28, 70, 90),
        },
    },
    "power": {
        "pcb": ROOT / "hardware" / "power" / "power.kicad_pcb",
        "net": ROOT / "hardware" / "power" / "power.net.xml",
        "clearance": 0.15,
        "track": 0.3,
        "layers": 4,
        "side": {
            "J1": "F", "J2": "F", "J3": "F", "J4": "F",
            "J5": "F", "J6": "F", "L1": "F", "SW1": "F",
            "D1": "F", "D2": "B", "D3": "B", "F1": "F",
            "U1": "B", "U2": "B", "U3": "B",
            "C1": "B", "C2": "B", "C3": "B", "C4": "B",
            "C5": "B", "C6": "B", "C7": "B", "C8": "B",
            "C9": "B", "C10": "B",
            "R1": "B", "R2": "B", "R3": "B", "R4": "B",
            "R5": "B", "R6": "B", "R7": "B", "R8": "B",
            "R9": "B", "R10": "B", "R11": "B", "R12": "B",
            "R13": "B", "R14": "B", "R15": "B", "R16": "B",
            "R17": "B",
        },
        "pos": {
            "J1": (12, 15, 0), "F1": (25, 8, 0),
            "D1": (35, 8, 0), "SW1": (10, 38, 0),
            "J2": (16, 58, 0), "J3": (32, 58, 0),
            "J4": (84, 58, 0), "J5": (68, 58, 0),
            "J6": (88, 38, 0), "L1": (55, 25, 0),
            "U1": (38, 28, 0), "U2": (72, 28, 0),
            "U3": (52, 48, 0),
            "C1": (20, 20, 0), "C2": (32, 38, 0),
            "C3": (62, 34, 0), "C4": (56, 34, 0),
            "C5": (20, 34, 0), "C6": (26, 34, 0),
            "C7": (66, 10, 0), "C8": (40, 10, 0),
            "C9": (56, 10, 0), "C10": (76, 34, 0),
            "R1": (8, 30, 90), "R2": (14, 30, 90),
            "R3": (20, 44, 90), "R4": (26, 44, 90),
            "R5": (32, 44, 90), "R6": (38, 44, 90),
            "R7": (78, 12, 0), "R8": (78, 28, 0),
            "R9": (86, 22, 0), "R10": (86, 28, 0),
            "R11": (60, 18, 0), "R12": (52, 34, 0),
            "R13": (42, 52, 0), "R14": (60, 48, 0),
            "R15": (66, 48, 0), "R16": (82, 14, 0),
            "R17": (86, 14, 0), "D2": (82, 8, 0),
            "D3": (86, 8, 0),
        },
    },
}


def footprint_path(lib_id):
    lib, name = lib_id.split(":", 1)
    if lib == "nsr1_fp":
        return NSR1_FP, name
    path = KICAD_FP / f"{lib}.pretty"
    if not path.exists():
        raise FileNotFoundError(path)
    return path, name


def load_netlist(path):
    root = ET.parse(path).getroot()
    comps = {}
    for comp in root.find("components"):
        comps[comp.get("ref")] = {
            "value": comp.findtext("value", ""),
            "footprint": comp.findtext("footprint", ""),
        }
    nets = {}
    for net in root.find("nets"):
        nets[net.get("name")] = [(n.get("ref"), n.get("pin"))
                                 for n in net.findall("node")]
    return comps, nets


def place(name, spec):
    board = pcbnew.LoadBoard(str(spec["pcb"]))
    board.SetCopperLayerCount(spec.get("layers", 2))
    for fp in list(board.GetFootprints()):
        if fp.GetReference() not in ("H1", "H2", "H3", "H4"):
            board.Remove(fp)
    comps, nets = load_netlist(spec["net"])
    print(f"{name}: {len(comps)} comps, {len(nets)} nets", flush=True)
    settings = board.GetDesignSettings()
    settings.m_MinClearance = pcbnew.FromMM(spec["clearance"])
    settings.m_HoleClearance = pcbnew.FromMM(0.19)
    settings.m_HoleToHoleMin = pcbnew.FromMM(0.2)
    settings.m_MinThroughDrill = pcbnew.FromMM(0.2)
    settings.m_NetSettings.GetDefaultNetclass().SetClearance(pcbnew.FromMM(spec["clearance"]))
    settings.m_NetSettings.GetDefaultNetclass().SetTrackWidth(pcbnew.FromMM(spec["track"]))
    settings.m_NetSettings.GetDefaultNetclass().SetViaDiameter(pcbnew.FromMM(0.6))
    settings.m_NetSettings.GetDefaultNetclass().SetViaDrill(pcbnew.FromMM(0.3))
    board_nets = {}
    for net_name in nets:
        net = pcbnew.NETINFO_ITEM(board, net_name)
        board.Add(net)
        board_nets[net_name] = net
    print(f"{name}: nets added", flush=True)
    by_ref = {}
    for ref, meta in comps.items():
        if ref not in spec["pos"]:
            raise SystemExit(f"{name}: missing placement for {ref}")
        x, y, rot = spec["pos"][ref]
        lib, fp_name = footprint_path(meta["footprint"])
        fp = pcbnew.FootprintLoad(str(lib), fp_name)
        if fp is None:
            raise SystemExit(f"{name}: cannot load {meta['footprint']}")
        print(f"{name}: place {ref} {meta['footprint']}", flush=True)
        fp.SetReference(ref)
        fp.SetValue(meta["value"])
        fp.SetOrientationDegrees(rot)
        target = pcbnew.VECTOR2I(pcbnew.FromMM(x), pcbnew.FromMM(y))
        if ref == "J1":
            pads = list(fp.Pads())
            xs = [p.GetPosition().x for p in pads]
            ys = [p.GetPosition().y for p in pads]
            center = pcbnew.VECTOR2I((min(xs) + max(xs)) // 2,
                                     (min(ys) + max(ys)) // 2)
            fp.SetPosition(target - (center - fp.GetPosition()))
        else:
            fp.SetPosition(target)
        board.Add(fp)
        if spec["side"].get(ref, "F") == "B":
            fp.Flip(fp.GetPosition(), False)
        by_ref[ref] = fp
    for net_name, nodes in nets.items():
        net = board_nets[net_name]
        for ref, pin in nodes:
            fp = by_ref.get(ref)
            if fp is None:
                continue
            found = False
            for pad in fp.Pads():
                if pad.GetNumber() == pin:
                    pad.SetNet(net)
                    found = True
            if not found:
                raise SystemExit(f"{name}: {ref} has no pad {pin}")
    print(f"{name}: assign done", flush=True)
    pcbnew.SaveBoard(str(spec["pcb"]), board)
    print(f"placed {name}: {len(by_ref)} parts, {len(nets)} nets")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("boards", nargs="*", choices=sorted(BOARDS), default=None)
    args = parser.parse_args()
    for name in (args.boards or sorted(BOARDS)):
        place(name, BOARDS[name])


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Export KiCad XML netlists from the generated NSR-1 schematics.

This gives EMD (and any other tool that consumes kicadxml) a deterministic
netlist without requiring a KiCad CLI installation.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import xml.etree.ElementTree as ET

from verify_schematics import EmbeddedSymLib, ROOT, collect_pins


BOARDS = {
    "control": ROOT / "hardware" / "control" / "control.kicad_sch",
    "keyboard": ROOT / "hardware" / "keyboard" / "keyboard.kicad_sch",
    "brain": ROOT / "hardware" / "brain" / "brain.kicad_sch",
}


def export(board, path, lib, output):
    symbols, net_pins, unconnected, _at_net = collect_pins(path, lib)
    if unconnected:
        raise SystemExit(f"{board}: cannot export with unconnected pins: {unconnected}")

    root = ET.Element("export", {"version": "E"})
    design = ET.SubElement(root, "design")
    ET.SubElement(design, "source").text = Path(path).name
    ET.SubElement(design, "date").text = "2026-11-02"
    ET.SubElement(design, "tool").text = "EMD NSR-1 generator"
    components = ET.SubElement(root, "components")
    for ref, value, lib_id, _x, _y, props in sorted(symbols, key=lambda s: s[0]):
        if ref.startswith("#"):
            continue
        comp = ET.SubElement(components, "comp", {"ref": ref})
        ET.SubElement(comp, "value").text = value
        ET.SubElement(comp, "footprint").text = props.get("Footprint", "")
        libsource = ET.SubElement(comp, "libsource",
                                  {"lib": lib_id.split(":", 1)[0],
                                   "part": lib_id.split(":", 1)[-1]})
        libsource.tail = None

    nets = ET.SubElement(root, "nets")
    for code, (name, pins) in enumerate(sorted(net_pins.items()), start=1):
        nodes = [(ref, num) for ref, num, _value in pins if not ref.startswith("#")]
        nodes = sorted(set(nodes))
        if not nodes:
            continue
        net = ET.SubElement(nets, "net", {"code": str(code), "name": name})
        for ref, num in nodes:
            sym = next(s for s in symbols if s[0] == ref)
            lib_id = sym[2]
            pin_name = next((info[3] for n, info in lib.pins(lib_id).items()
                             if n == num), "")
            ET.SubElement(net, "node",
                          {"ref": ref, "pin": num, "pinfunction": pin_name})

    ET.indent(root)
    tree = ET.ElementTree(root)
    tree.write(output, encoding="utf-8", xml_declaration=True)
    print(f"wrote {output} ({len(components)} parts, {len(nets)} nets)")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--board", choices=["control", "keyboard", "brain", "all"], default="all")
    parser.add_argument("--out-dir", default=str(ROOT / "hardware"))
    args = parser.parse_args()
    lib = EmbeddedSymLib([
        ROOT / "nsr2" / "hardware" / "brain" / "brain.kicad_sch",
        ROOT / "nsr2" / "hardware" / "io" / "io.kicad_sch",
    ])
    boards = BOARDS if args.board == "all" else {args.board: BOARDS[args.board]}
    out_dir = Path(args.out_dir)
    for board, path in boards.items():
        export(board, path, lib, out_dir / board / f"{board}.net.xml")


if __name__ == "__main__":
    main()

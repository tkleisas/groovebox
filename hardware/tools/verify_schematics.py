#!/usr/bin/env python3
"""Structural connectivity check for the generated NSR-1 schematics.

This is not a replacement for KiCad ERC.  It verifies the contract that the
generator actually implemented: every symbol pin lands on a label, power
symbol, or no-connect; key, encoder, analog, LED, I2C, and power nets have the
expected endpoint counts.
"""
from __future__ import annotations

import argparse
import json
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from gen_schematics import EmbeddedSymLib, FallbackSymLib  # noqa: E402
import gen_schematics as g  # noqa: E402

POWER = {"GND", "+3V3", "+5V"}


def _walk(node):
    if isinstance(node, list):
        yield node
        for child in node:
            yield from _walk(child)


def _first(node, key):
    for child in node:
        if isinstance(child, list) and child and child[0] == key:
            return child
    return None


def _props(node):
    out = {}
    for child in node:
        if (isinstance(child, list) and child and child[0] == "property" and
                len(child) >= 3):
            out[str(child[1])] = str(child[2])
    return out


def load_sheet(path, lib):
    root = g.kicad.parse(Path(path).read_text(encoding="utf-8"))
    labels = {}
    no_connects = set()
    power_nets = {}
    symbols = []
    for node in _walk(root):
        if not node:
            continue
        if node[0] in ("label", "global_label") and len(node) >= 3:
            at = _first(node, "at")
            if at:
                labels[(round(float(at[1]), 3), round(float(at[2]), 3))] = str(node[1])
        elif node[0] == "no_connect":
            at = _first(node, "at")
            if at:
                no_connects.add((round(float(at[1]), 3), round(float(at[2]), 3)))
        elif node[0] == "symbol":
            lib_id_node = _first(node, "lib_id")
            at = _first(node, "at")
            if not lib_id_node or not at:
                continue
            lib_id = str(lib_id_node[1])
            x, y = float(at[1]), float(at[2])
            props = _props(node)
            ref = props.get("Reference", "")
            value = props.get("Value", "")
            if ref.startswith("#PWR"):
                pins = lib.pins(lib_id)
                for num, (px, py, _pa, _name) in pins.items():
                    key = (round(x + px, 3), round(y - py, 3))
                    power_nets[key] = value
                continue
            symbols.append((ref, value, lib_id, x, y, props))
    return labels, no_connects, power_nets, symbols


def collect_pins(path, lib):
    labels, no_connects, power_nets, symbols = load_sheet(path, lib)
    at_net = dict(labels)
    at_net.update(power_nets)
    net_pins = defaultdict(list)
    unconnected = []
    for ref, value, lib_id, x, y, _props in symbols:
        for num, (px, py, _pa, name) in lib.pins(lib_id).items():
            key = (round(x + px, 3), round(y - py, 3))
            if key in at_net:
                net_pins[at_net[key]].append((ref, num, value))
            elif key in no_connects:
                pass
            else:
                unconnected.append((ref, value, num, name, key))
    return symbols, net_pins, unconnected, at_net


def check_sheet(path, lib, board):
    symbols, net_pins, unconnected, at_net = collect_pins(path, lib)

    errors = []
    if unconnected:
        for item in unconnected:
            errors.append(f"unconnected pin: {item}")

    def endpoints(net):
        return net_pins.get(net, [])

    def has_value(net, value):
        return any(p[2] == value for p in endpoints(net))

    def count_value(net, value):
        return sum(p[2] == value for p in endpoints(net))

    def count_ref(net, prefix):
        return sum(p[0].startswith(prefix) for p in endpoints(net))

    def require(net, predicate, message):
        pins = endpoints(net)
        if not pins or not predicate(pins):
            errors.append(f"{net}: {message}; got {pins}")

    rails = POWER if board == "control" else {"+3V3", "GND"}
    for net in rails:
        require(net, lambda pins: len(pins) >= 2,
                "power rail must have at least two endpoints")

    if board == "control":
        for i in range(8):
            for suffix in ("A", "B"):
                net = f"ENC{i + 1}_{suffix}"
                require(net, lambda pins: count_ref(net, "J") == 2,
                        "encoder phase must reach the Pico and one connector")
        for key in list(range(27, 38)):
            net = f"KEY{key}"
            require(net, lambda pins: count_value(net, "MCP23017") == 1 and
                    count_ref(net, "J") == 1,
                    "must have one control connector and one MCP pin")
        for i in range(1, 6):
            net = f"ENC{i}_SW"
            require(net, lambda pins: count_ref(net, "U3") == 1 and
                    count_ref(net, "J") == 1,
                    "must reach 0x22 and one encoder connector")
        for i in range(6, 9):
            net = f"ENC{i}_SW"
            require(net, lambda pins: count_ref(net, "U4") == 1 and
                    count_ref(net, "J") == 1,
                    "must reach 0x24 and one encoder connector")
        for net in ("CUT", "RES", "SLIDER", "JOY_X", "JOY_Y"):
            require(net, lambda pins: count_value(net, "ADS1115") == 1,
                    "analog signal must reach an ADS1115 input")
        expected_straps = {
            "U3": {"A0": "GND", "A1": "+3V3", "A2": "GND"},
            "U4": {"A0": "GND", "A1": "GND", "A2": "+3V3"},
        }
        for ref, straps in expected_straps.items():
            for pin_name, rail in straps.items():
                got = None
                for net, pins in net_pins.items():
                    for p in pins:
                        if p[0] == ref:
                            # pin name lookup below
                            pass
                # Check by pin number from the symbol pin table.
                sym = next((s for s in symbols if s[0] == ref), None)
                if sym:
                    _ref, _value, lib_id, x, y, _sym_props = sym
                    for num, (px, py, _pa, name) in lib.pins(lib_id).items():
                        if name == pin_name:
                            key = (round(x + px, 3), round(y - py, 3))
                            got = at_net.get(key)
                if got != rail:
                    errors.append(f"{ref}.{pin_name}: expected {rail}, got {got}")
    else:
        for key in range(0, 16):
            net = f"KEY{key}"
            require(net, lambda pins: count_value(net, "MCP23017") == 1 and
                    count_value(net, f"KEY{key}") == 1,
                    "white key must reach an MCP and one switch")
        for key in list(range(16, 27)) + [38, 39]:
            net = f"KEY{key}"
            require(net, lambda pins: count_value(net, "MCP23017") == 1 and
                    count_value(net, f"KEY{key}") == 1,
                    "black/shift key must reach an MCP and one switch")
        for key in range(40, 56):
            net = f"KEY{key}"
            require(net, lambda pins: count_value(net, "MCP23017") == 1 and
                    count_value(net, f"KEY{key}") == 1,
                    "step key must reach an MCP and one switch")
        for i in range(32):
            require(f"LED{i}", lambda pins: count_ref(f"LED{i}", "D") == 1 and
                    count_ref(f"LED{i}", "R") == 1,
                    "LED must have one diode and one series resistor")
            require(f"ROW{i & 7}", lambda pins: count_value(f"ROW{i & 7}", "HT16K33A") == 1
                    and count_ref(f"ROW{i & 7}", "R") == 4,
                    "ROW must reach HT16K33 and four LED resistors")
            require(f"COM{i >> 3}", lambda pins: count_value(f"COM{i >> 3}", "HT16K33A") == 1
                    and count_ref(f"COM{i >> 3}", "D") == 8,
                    "COM must drive eight LED anodes")
        for key in ("SDA", "SCL"):
            require(key, lambda pins: count_value(key, "MCP23017") >= 1 and
                    count_value(key, "HT16K33A") == 1,
                    "I2C net must reach all expanders and the LED driver")

    return {
        "board": board,
        "symbols": len(symbols),
        "nets": len(net_pins),
        "unconnected": len(unconnected),
        "errors": errors,
        "key_nets": sum(1 for net in net_pins if net.startswith("KEY")),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--report", default=str(ROOT / "hardware" / "schematic-report.json"))
    args = parser.parse_args()
    lib = EmbeddedSymLib([
        ROOT / "nsr2" / "hardware" / "brain" / "brain.kicad_sch",
        ROOT / "nsr2" / "hardware" / "io" / "io.kicad_sch",
    ])
    reports = [
        check_sheet(ROOT / "hardware" / "control" / "control.kicad_sch", lib, "control"),
        check_sheet(ROOT / "hardware" / "keyboard" / "keyboard.kicad_sch", lib, "keyboard"),
    ]
    Path(args.report).write_text(json.dumps(reports, indent=2) + "\n",
                                 encoding="utf-8")
    failed = False
    for report in reports:
        print(f"{report['board']}: {report['symbols']} symbols, "
              f"{report['nets']} nets, {report['unconnected']} unconnected, "
              f"{len(report['errors'])} errors")
        for error in report["errors"]:
            print("  ERROR:", error)
            failed = True
    raise SystemExit(1 if failed else 0)


if __name__ == "__main__":
    main()

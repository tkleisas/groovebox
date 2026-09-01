#!/usr/bin/env python3
"""Experiment: does a bare power symbol satisfy power_pin_not_driven?"""
import json
import os
import subprocess
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_schematics import SymLib, Board, KICAD_SYM_DIR

CLI = r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"
TMP = r"C:\projects\groovebox\nsr2\hardware\bisect"

lib = SymLib(KICAD_SYM_DIR)


def run(b, name):
    p = os.path.join(TMP, name + ".kicad_sch")
    b.save(p)
    subprocess.run([CLI, "sch", "erc", p, "--output", os.path.join(TMP, name + ".json"),
                    "--format", "json", "--severity-all"], capture_output=True, text=True)
    try:
        j = json.load(open(os.path.join(TMP, name + ".json")))
        v = j["sheets"][0]["violations"]
        print(name, "->", len(v), dict(Counter(x["type"] for x in v)))
    except Exception as e:
        print(name, "-> no report:", e)


b = Board(lib, "bisect", "t")
b.place("Device:R", "R", "1k", 100, 100, pin_nets={1: "@+3V3", 2: "NETB"})
run(b, "x1_power_only")

b = Board(lib, "bisect", "t")
b.place("Device:R", "R", "1k", 100, 100, pin_nets={1: "@+3V3", 2: "NETB"})
b.place("power:PWR_FLAG", "#FLG", "PWR_FLAG", 120, 100, pin_nets={1: "+3V3"})
run(b, "x2_power_with_flag")

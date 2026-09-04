#!/usr/bin/env python3
"""Bisect KiCad sch load failures: build minimal files feature by feature."""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_schematics import SymLib, Board, KICAD_SYM_DIR, ht16k33a_symbol_text, parse

CLI = r"C:\Program Files\KiCad\10.0\bin\kicad-cli.exe"
TMP = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "hardware", "bisect")
os.makedirs(TMP, exist_ok=True)

lib = SymLib(KICAD_SYM_DIR)
lib.add_custom("nsr2:HT16K33A", parse(ht16k33a_symbol_text())[0])


def test(name, build):
    b = Board(lib, "bisect", f"bisect {name}")
    build(b)
    path = os.path.join(TMP, f"{name}.kicad_sch")
    b.save(path)
    r = subprocess.run([CLI, "sch", "erc", path, "--output", TMP],
                       capture_output=True, text=True)
    ok = "Failed to load" not in (r.stdout + r.stderr)
    print(f"{'LOAD OK ' if ok else 'FAIL    '} {name}")
    if not ok:
        print("   stdout:", (r.stdout or "").strip()[:200])
        print("   stderr:", (r.stderr or "").strip()[:200])
    return ok


def one_resistor(b):
    b.place("Device:R", "R", "1k", 100, 100, "Resistor_SMD:R_0805_2012Metric",
            pin_nets={1: "NETA", 2: "NETB"})


def resistor_with_text(b):
    one_resistor(b)
    b.text("HELLO", 100, 60)


def resistor_with_nc(b):
    one_resistor(b)
    b.nc(120, 120)


def connector(b):
    b.place("Connector_Generic:Conn_01x04", "J", "X", 150, 100,
            pin_nets={1: "NETA", 2: "NETB"}, nc_pins=[3, 4])


def power_symbol(b):
    one_resistor(b)
    b.power("@+3V3", 100, 90)


def two_pin_nets_same_name(b):
    b.place("Device:R", "R", "1k", 100, 100, pin_nets={1: "NETA", 2: "NETB"})
    b.place("Device:R", "R", "1k", 130, 100, pin_nets={1: "NETA", 2: "NETB"})


def big_connector(b):
    b.place("Connector_Generic:Conn_02x20_Odd_Even", "J", "PI", 60, 120,
            pin_nets={1: "@+3V3", 2: "@+5V", 8: "MIDI_TX"},
            nc_pins=[p for p in range(3, 41) if p not in (8,)])


def ic_named_pins(b):
    b.place("Interface_Expansion:MCP23017x-x-SO", "U", "MCP23017", 200, 100,
            pin_names={"GPA0": "KEY0", "V_{DD}": "@+3V3", "V_{SS}": "@GND",
                       "SDA": "SDA", "SCK": "SCL", "~{RESET}": "@+3V3",
                       "A0": "@GND", "A1": "@GND", "A2": "@GND"},
            nc_pins=["NC", "INTA", "INTB"])


def custom_ht16k33a(b):
    ht = {"VDD": "@+3V3", "VSS": "@GND", "SDA": "SDA", "SCL": "SCL",
          "COM0": "COM0", "ROW0": "ROW0"}
    nc = ["COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7"] + \
         [f"ROW{i}" for i in range(1, 16)]
    b.place("nsr2:HT16K33A", "U", "HT16K33A", 240, 100,
            "Package_SO:SSOP-28_5.3x10.2mm_P0.65mm",
            pin_names=ht, nc_pins=nc)


def inherited_adc(b):
    b.place("Analog_ADC:ADS1115IDGS", "U", "ADS1115", 280, 100,
            "Package_SO:VSSOP-10_3x3mm_P0.5mm",
            pin_names={"AIN0": "STICK_X", "VDD": "@+3V3", "GND": "@GND",
                       "SDA": "SDA", "SCL": "SCL", "ADDR": "@GND",
                       "ALERT/RDY": "@NC"},
            nc_pins=["AIN1", "AIN2", "AIN3"])


tests = [
    ("r01_resistor", one_resistor),
    ("r02_with_text", resistor_with_text),
    ("r03_with_nc", resistor_with_nc),
    ("r04_connector", connector),
    ("r05_power", power_symbol),
    ("r06_shared_net", two_pin_nets_same_name),
    ("r07_big_connector", big_connector),
    ("r08_ic_named", ic_named_pins),
    ("r09_custom_ht16k33a", custom_ht16k33a),
    ("r10_inherited_adc", inherited_adc),
]

for name, fn in tests:
    test(name, fn)

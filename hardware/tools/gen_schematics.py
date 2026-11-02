#!/usr/bin/env python3
"""Generate KiCad schematics for the NSR-1 control, keyboard, and brain boards.

The NSR-2 generator already provides the KiCad 10 schematic writer and
embedded symbol reader.  This generator reuses those helpers and pulls the
symbol definitions from the checked-in NSR-2 schematics so it does not depend
on a local KiCad installation.

Usage:  python hardware/tools/gen_schematics.py
Writes: hardware/control/control.kicad_sch
        hardware/keyboard/keyboard.kicad_sch
        hardware/brain/brain.kicad_sch
"""
from __future__ import annotations

import datetime as _dt
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
_spec = importlib.util.spec_from_file_location(
    "nsr2_schematics", ROOT / "nsr2" / "tools" / "gen_schematics.py")
kicad = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(kicad)

OUT_DIR = ROOT / "hardware"


class Nsr1Board(kicad.Board):
    """Board writer that uses global labels for PWR_FLAG power nets."""

    def label(self, net, x, y, angle=0):
        if net not in ("GND", "+3V3", "+5V"):
            return super().label(net, x, y, angle)
        justify = "right" if angle == 180 else "left"
        key = kicad._uuid(self.name + "/glbl/" + net + "/" + str(x) + "/" + str(y))
        self.body.append(
            f'\t(global_label "{net}"\n'
            f"\t\t(shape input)\n"
            f"\t\t(at {x} {y} {angle})\n"
            f"\t\t(fields_autoplaced yes)\n"
            f"\t\t(effects (font (size 1.27 1.27)) (justify {justify}))\n"
            f'\t\t(uuid "{key}")\n'
            f"\t)")


class EmbeddedSymLib(kicad.SymLib):
    """Symbol lookup backed by the lib_symbols blocks in generated sheets."""

    def __init__(self, paths):
        self.cache = {}
        self.custom = {}
        for path in paths:
            root = kicad.parse(Path(path).read_text(encoding="utf-8"))

            def walk(node):
                if not isinstance(node, list):
                    return
                if node and node[0] == "lib_symbols":
                    for child in node[1:]:
                        if (isinstance(child, list) and child and
                                child[0] == "symbol"):
                            self.cache.setdefault(str(child[1]), child)
                for child in node:
                    walk(child)

            walk(root)

    def get(self, lib_id):
        if lib_id in self.cache:
            return self.cache[lib_id]
        raise KeyError(f"symbol '{lib_id}' is not in the NSR-2 symbol set")


def _save(board, path, date, rev="B2"):
    board.save(str(path))
    text = path.read_text(encoding="utf-8")
    text = text.replace('(date "2026-08-31")', f'(date "{date}")')
    text = text.replace('(rev "A")', f'(rev "{rev}")')
    path.write_text(text, encoding="utf-8", newline="\n")
    print("wrote", path)


def _mcp(board, ref, x, y, addr_bits, key_map, push_map, description):
    pins = {
        "V_{DD}": "@+3V3", "V_{SS}": "@GND",
        "SDA": "SDA", "SCK": "SCL", "~{RESET}": "@+3V3",
        "A0": addr_bits[0], "A1": addr_bits[1], "A2": addr_bits[2],
    }
    pins.update({f"GPA{i}": net for i, net in enumerate(key_map[:8]) if net})
    pins.update({f"GPB{i}": net for i, net in enumerate(key_map[8:]) if net})
    nc = ["11", "14", "INTA", "INTB"]
    nc += [f"GPA{i}" for i, net in enumerate(key_map[:8]) if not net]
    nc += [f"GPB{i}" for i, net in enumerate(key_map[8:]) if not net]
    board.place("Interface_Expansion:MCP23017x-x-SO", "U", "MCP23017",
                x, y, "Package_SO:SSOP-28_5.3x10.2mm_P0.65mm",
                description, pin_names=pins, nc_pins=nc)


def _ads(board, ref, x, y, addr, inputs, description):
    pins = {"VDD": "@+3V3", "GND": "@GND", "SDA": "SDA", "SCL": "SCL",
            "ADDR": addr, "ALERT/RDY": "@NC"}
    pins.update({f"AIN{i}": net for i, net in enumerate(inputs)})
    board.place("Analog_ADC:ADS1115IDGS", "U", "ADS1115",
                x, y, "Package_SO:MSOP-10_3x3mm_P0.5mm",
                description, pin_names=pins)


def control(lib, date):
    b = Nsr1Board(lib, "control", "NSR-1 control board (Pico panel module, rev B2)")
    b.text("NSR-1 CONTROL - Pico, key expanders, encoders, analog", 25, 25, 3.0)

    pico = {
        1: "ENC1_A", 2: "ENC1_B", 4: "ENC2_A", 5: "ENC2_B",
        6: "SDA", 7: "SCL", 9: "ENC3_A", 10: "ENC3_B",
        11: "ENC4_A", 12: "ENC4_B", 19: "ENC5_A", 20: "ENC5_B",
        21: "UART_TX", 22: "UART_RX",
        24: "ENC6_A", 25: "ENC6_B", 26: "ENC7_A", 27: "ENC7_B",
        29: "ENC8_A", 31: "ENC8_B", 30: "RUN_SW",
        36: "@+3V3", 40: "@+5V",
    }
    for p in (3, 8, 13, 18, 23, 28, 33, 38):
        pico[p] = "@GND"
    pico_nc = [14, 15, 16, 17, 32, 34, 35, 37, 39]
    b.place("Connector_Generic:Conn_02x20_Odd_Even", "J", "Pi_Pico",
            45, 110, "Connector_PinSocket_2.54mm:PinSocket_2x20_P2.54mm_Vertical",
            "2x20 female headers (Raspberry Pi Pico module)", pico, pico_nc)

    _ads(b, "U1", 270, 55, "@GND",
         ["CUT", "RES", "SLIDER", "JOY_Y"], "CUT/RES/slider/joystick Y (0x48)")
    _ads(b, "U2", 270, 115, "@+3V3",
         ["JOY_X", "@GND", "@GND", "@GND"], "joystick X (0x49)")

    # Control-board keys: 10 function keys + joystick button + 5 pushes.
    mcp22 = ["KEY27", "KEY28", "KEY29", "KEY31", "KEY32", "KEY33", "KEY34", "KEY35",
             "KEY36", "KEY37", "KEY30", "ENC1_SW", "ENC2_SW", "ENC3_SW",
             "ENC4_SW", "ENC5_SW"]
    _mcp(b, "U3", 145, 55, ("@GND", "@+3V3", "@GND"), mcp22, None,
         "function keys + joystick SW + encoder pushes 1-5 (0x22)")
    mcp24 = ["ENC6_SW", "ENC7_SW", "ENC8_SW", None, None, None, None, None,
             None, None, None, None, None, None, None, None]
    _mcp(b, "U4", 145, 155, ("@GND", "@GND", "@+3V3"), mcp24, None,
         "encoder pushes 6-8 (0x24)")

    # Raw analog connectors.
    b.place("Connector_Generic:Conn_01x03", "J", "POT_CUT", 25, 40,
            "Connector_JST:JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical",
            "CUT pot", {1: "@GND", 2: "CUT_RAW", 3: "@+3V3"})
    b.place("Connector_Generic:Conn_01x03", "J", "POT_RES", 25, 62,
            "Connector_JST:JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical",
            "RES pot", {1: "@GND", 2: "RES_RAW", 3: "@+3V3"})
    b.place("Connector_Generic:Conn_01x03", "J", "SLIDER", 25, 84,
            "Connector_JST:JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical",
            "data slider", {1: "@GND", 2: "SLIDER_RAW", 3: "@+3V3"})
    b.place("Connector_Generic:Conn_01x04", "J", "JOYSTICK", 25, 106,
            "Connector_JST:JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical",
            "pitch/mod thumbstick", {1: "@GND", 2: "@+3V3",
                                     3: "JOY_X_RAW", 4: "JOY_Y_RAW"})
    b.place("Connector_Generic:Conn_01x02", "J", "JOY_SW", 25, 128,
            "Connector_JST:JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical",
            "joystick push", {1: "KEY30", 2: "@GND"})

    for i, (raw, filt, x) in enumerate((
            ("CUT_RAW", "CUT", 220), ("RES_RAW", "RES", 245),
            ("SLIDER_RAW", "SLIDER", 295), ("JOY_X_RAW", "JOY_X", 320),
            ("JOY_Y_RAW", "JOY_Y", 345))):
        b.place("Device:R", "R", "10k", x, 190,
                "Resistor_SMD:R_0805_2012Metric", "analog RC series",
                {1: raw, 2: filt})
        b.place("Device:C", "C", "100n", x, 210,
                "Capacitor_SMD:C_0805_2012Metric", "analog RC shunt",
                {1: filt, 2: "@GND"})

    function_keys = ["KEY27", "KEY28", "KEY29", "KEY31", "KEY32",
                     "KEY33", "KEY34", "KEY35", "KEY36", "KEY37"]
    for i, key in enumerate(function_keys):
        b.place("Connector_Generic:Conn_01x02", "J", key, 25, 150 + i * 14,
                "Connector_JST:JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical",
                f"function key {key}", {1: key, 2: "@GND"})

    for i in range(8):
        b.place("Connector_Generic:Conn_01x05", "J", f"ENCODER{i + 1}",
                25 + (i % 2) * 18, 300 + (i // 2) * 18,
                "Connector_JST:JST_XH_B5B-XH-A_1x05_P2.50mm_Vertical",
                "EC11 encoder", {
                    1: f"ENC{i + 1}_A", 2: f"ENC{i + 1}_B", 3: "@GND",
                    4: f"ENC{i + 1}_SW", 5: "@GND"})

    b.place("Connector_Generic:Conn_01x04", "J", "KEYBOARD_LINK", 420, 40,
            "Connector_JST:JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical",
            "I2C link to keyboard board",
            {1: "@+3V3", 2: "@GND", 3: "SDA", 4: "SCL"})
    b.place("Connector_Generic:Conn_01x04", "J", "UART_LINK", 420, 62,
            "Connector_JST:JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical",
            "serial transport header",
            {1: "@+3V3", 2: "@GND", 3: "UART_TX", 4: "UART_RX"})
    b.place("Switch:SW_Push", "SW", "RUN", 420, 90,
            "Button_Switch_THT:SW_PUSH_6mm", "Pico RUN/BOOTSEL access",
            pin_nets={1: "RUN_SW", 2: "@GND"})

    b.place("Device:R", "R", "4k7", 380, 190,
            "Resistor_SMD:R_0805_2012Metric", "I2C SDA pull-up",
            {1: "@+3V3", 2: "SDA"})
    b.place("Device:R", "R", "4k7", 400, 210,
            "Resistor_SMD:R_0805_2012Metric", "I2C SCL pull-up",
            {1: "@+3V3", 2: "SCL"})
    for i in range(5):
        b.place("Device:C", "C", "100n", 380 + i * 18, 250,
                "Capacitor_SMD:C_0805_2012Metric", "IC decoupling",
                {1: "@+3V3", 2: "@GND"})
    b.place("Device:C", "C", "100n", 470, 250,
            "Capacitor_SMD:C_0805_2012Metric", "Pico 3V3 decoupling",
            {1: "@+3V3", 2: "@GND"})
    b.place("Device:C", "C", "100n", 490, 270,
            "Capacitor_SMD:C_0805_2012Metric", "Pico 5V bulk decoupling",
            {1: "@+5V", 2: "@GND"})
    b.power_flags(("+3V3", "+5V", "GND"), 25, 340)
    _save(b, OUT_DIR / "control" / "control.kicad_sch", date)


def keyboard(lib, date):
    b = Nsr1Board(lib, "keyboard", "NSR-1 keyboard board (piano + step row, rev B2)")
    b.text("NSR-1 KEYBOARD - 45 MX keys, 32 LEDs", 25, 25, 3.0)

    white = [f"KEY{i}" for i in range(16)]
    black = [f"KEY{16 + i}" for i in range(11)]
    shifts = ["KEY38", "KEY39"]
    step = [f"KEY{40 + i}" for i in range(16)]

    mcp20 = white
    _mcp(b, "U1", 130, 60, ("@GND", "@GND", "@GND"), mcp20, None,
         "white keys 0-15 (0x20)")
    mcp21 = black + shifts + [None, None, None]
    _mcp(b, "U2", 130, 150, ("@+3V3", "@GND", "@GND"), mcp21, None,
         "black keys + shifts (0x21)")
    mcp23 = step
    _mcp(b, "U3", 130, 240, ("@+3V3", "@+3V3", "@GND"), mcp23, None,
         "step row 40-55 (0x23)")

    # 45 MX hot-swap sockets, laid out as a schematic grid (the physical
    # piano/step geometry is owned by the PCB layout, not this sheet).
    for i, key in enumerate([f"KEY{i}" for i in range(16)] + black + shifts + step):
        row, col = divmod(i, 10)
        b.place("Switch:SW_Push", "SW", key, 240 + col * 22, 55 + row * 26,
                "nsr1_fp:SW_Hotswap_Kailh_MX_Plated_1.00u",
                "MX hot-swap socket", pin_nets={1: key, 2: "@GND"})

    ht = {"VDD": "@+3V3", "VSS": "@GND", "SDA": "SDA", "SCL": "SCL"}
    for i in range(8):
        ht[f"ROW{i}"] = f"ROW{i}"
    for i in range(4):
        ht[f"COM{i}"] = f"COM{i}"
    b.place("nsr2:HT16K33A", "U", "HT16K33A", 330, 240,
            "Package_SO:SSOP-28_5.3x10.2mm_P0.65mm",
            "32 LED driver (0x70)", pin_names=ht,
            nc_pins=["COM4", "COM5", "COM6", "COM7"] +
                    [f"ROW{i}" for i in range(8, 16)])

    # LED i: 0-15 step row, 16-31 white keys.
    for i in range(32):
        row, col = divmod(i, 8)
        x, y = 240 + col * 19.05, 150 + row * 22
        b.place("Device:LED", "D", "red", x, y,
                "LED_THT:LED_D3.0mm", "in-switch LED",
                pin_names={"A": f"COM{i >> 3}", "K": f"LED{i}"})
        b.place("Device:R", "R", "1k", x, y + 10,
                "Resistor_SMD:R_0805_2012Metric", "LED series",
                pin_nets={1: f"LED{i}", 2: f"ROW{i & 7}"})

    b.place("Connector_Generic:Conn_01x04", "J", "I2C_LINK", 25, 40,
            "Connector_JST:JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical",
            "I2C link to control board",
            {1: "@+3V3", 2: "@GND", 3: "SDA", 4: "SCL"})
    for i in range(4):
        b.place("Device:C", "C", "100n", 25 + i * 18, 70,
                "Capacitor_SMD:C_0805_2012Metric", "IC decoupling",
                {1: "@+3V3", 2: "@GND"})
    b.power_flags(("+3V3", "GND"), 25, 100)
    _save(b, OUT_DIR / "keyboard" / "keyboard.kicad_sch", date)


def brain(lib, date):
    b = Nsr1Board(lib, "brain", "NSR-1 brain carrier (Pi Zero 2W, rev A)")
    b.text("NSR-1 BRAIN CARRIER - Pi Zero 2W, display, DIN MIDI, panel USB", 25, 25, 3.0)

    pi = {
        1: "@+3V3", 2: "@+5V", 4: "@+5V",
        3: "SDA", 5: "SCL",
        8: "MIDI_TX", 10: "MIDI_RX",
        11: "LOW_BATT", 15: "SHUTDOWN",
        13: "LCD_BL", 18: "LCD_DC", 19: "LCD_MOSI", 22: "LCD_RST",
        23: "LCD_SCLK", 24: "LCD_CS", 17: "@+3V3",
    }
    for p in (6, 9, 14, 20, 25, 30, 34, 39):
        pi[p] = "@GND"
    pi_nc = [7, 12, 16, 21, 26, 27, 28, 29, 31, 32, 33, 35, 36, 37, 38, 40]
    b.place("Connector_Generic:Conn_02x20_Odd_Even", "J", "Pi_Zero_2W",
            45, 100, "Connector_PinSocket_2.54mm:PinSocket_2x20_P2.54mm_Vertical",
            "2x20 female header (Pi Zero 2W socket)", pi, pi_nc)

    b.place("Connector_Generic:Conn_01x08", "J", "ILI9488_4.0in",
            230, 45, "Connector_PinHeader_2.54mm:PinHeader_1x08_P2.54mm_Horizontal",
            "4.0 inch 480x320 SPI TFT", {
                1: "@+3V3", 2: "@GND", 3: "LCD_CS", 4: "LCD_RST",
                5: "LCD_DC", 6: "LCD_MOSI", 7: "LCD_SCLK", 8: "LCD_BL"})

    b.place("Connector_Generic:Conn_01x05", "J", "DIN5_MIDI_OUT",
            290, 110, "nsr1_fp:DIN5-RA-PTH",
            "MIDI OUT right-angle DIN-5", {
                2: "@GND", 4: "MIDI_OUT_P4", 5: "@GND"}, [1, 3])
    b.place("Connector_Generic:Conn_01x05", "J", "DIN5_MIDI_IN",
            290, 145, "nsr1_fp:DIN5-RA-PTH",
            "MIDI IN right-angle DIN-5", {
                2: "@GND", 4: "MIDI_IN_P4", 5: "MIDI_IN_K"}, [1, 3])
    b.place("Device:R", "R", "33R", 225, 105,
            "Resistor_SMD:R_0805_2012Metric", "MIDI OUT series",
            {1: "MIDI_TX", 2: "MIDI_OUT_P4"})
    b.place("Device:R", "R", "220R", 225, 130,
            "Resistor_SMD:R_0805_2012Metric", "MIDI IN LED series",
            {1: "MIDI_IN_P4", 2: "MIDI_IN_A"})
    b.place("Isolator:6N138", "U", "6N138", 225, 170,
            "Package_DIP:DIP-8_W7.62mm", "MIDI IN optocoupler",
            pin_nets={2: "MIDI_IN_A", 3: "MIDI_IN_K", 5: "@GND",
                      6: "MIDI_RX", 8: "@+5V"}, nc_pins=[1, 4, 7])
    b.place("Device:R", "R", "10k", 175, 175,
            "Resistor_SMD:R_0805_2012Metric", "MIDI RX pull-up",
            {1: "@+5V", 2: "MIDI_RX"})

    b.place("Power_Protection:USBLC6-2P6", "U", "USBLC6-2P6", 225, 220,
            "Package_TO_SOT_SMD:SOT-23-6", "USB ESD protection",
            pin_nets={1: "USB_DP", 6: "USB_DP", 3: "USB_DM", 4: "USB_DM",
                      2: "@GND", 5: "@+5V"})
    b.place("Connector:USB_A", "J", "USB_A_PANEL", 290, 220,
            "Connector_USB:USB_A_Connfly_DS1098_Horizontal",
            "panel USB-A host port (Pi OTG breakout)",
            pin_names={"VBUS": "@+5V", "D-": "USB_DM", "D+": "USB_DP",
                       "GND": "@GND", "Shield": "@GND"})

    b.place("Connector_Generic:Conn_01x02", "J", "PWR_IN", 290, 260,
            "Connector_JST:JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical",
            "5V input from power board", {1: "@+5V", 2: "@GND"})
    b.place("Connector_Generic:Conn_01x04", "J", "PWR_GAUGE", 250, 260,
            "Connector_JST:JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical",
            "power-board fuel gauge I2C",
            {1: "@+3V3", 2: "@GND", 3: "SDA", 4: "SCL"})
    b.place("Connector_Generic:Conn_01x02", "J", "LOW_BATT", 205, 260,
            "Connector_JST:JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical",
            "low-battery alert from power board",
            {1: "LOW_BATT", 2: "@GND"})
    b.place("Connector_Generic:Conn_01x02", "J", "SHUTDOWN", 160, 260,
            "Connector_JST:JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical",
            "external shutdown button",
            {1: "SHUTDOWN", 2: "@GND"})
    b.place("Switch:SW_Push", "SW", "SHUTDOWN", 120, 262,
            "Button_Switch_THT:SW_PUSH_6mm", "shutdown button (GPIO22)",
            pin_nets={1: "SHUTDOWN", 2: "@GND"})
    b.place("Connector_Generic:Conn_01x02", "J", "MIC", 75, 260,
            "Connector_JST:JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical",
            "electret mic capsule",
            {1: "MIC_SIG", 2: "@GND"})
    b.place("Connector_Generic:Conn_01x02", "J", "MIC_TO_HAT", 75, 285,
            "Connector_JST:JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical",
            "mic signal to Audio Injector input",
            {1: "MIC_SIG", 2: "@GND"})

    b.place("Device:R", "R", "4k7", 140, 70,
            "Resistor_SMD:R_0805_2012Metric", "I2C SDA pull-up",
            {1: "@+3V3", 2: "SDA"})
    b.place("Device:R", "R", "4k7", 165, 70,
            "Resistor_SMD:R_0805_2012Metric", "I2C SCL pull-up",
            {1: "@+3V3", 2: "SCL"})
    for i, rail in enumerate(("@+5V", "@+3V3", "@+5V", "@+3V3")):
        b.place("Device:C", "C", "100n", 140 + i * 25, 300,
                "Capacitor_SMD:C_0805_2012Metric", "decoupling",
                {1: rail, 2: "@GND"})
    b.power_flags(("+5V", "+3V3", "GND"), 25, 340)
    _save(b, OUT_DIR / "brain" / "brain.kicad_sch", date, rev="A")


def main():
    today = _dt.date.today().isoformat()
    lib = EmbeddedSymLib([
        ROOT / "nsr2" / "hardware" / "brain" / "brain.kicad_sch",
        ROOT / "nsr2" / "hardware" / "io" / "io.kicad_sch",
    ])
    control(lib, today)
    keyboard(lib, today)
    brain(lib, today)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Add readable pin/connector silkscreen labels to the NSR-1 boards.

Labels are inserted as board-level gr_text s-expressions after the layout is
routed. Run this from the KiCad container so pcbnew can read pad positions.
Run it once on a clean routed board; regenerating from the outline removes
the labels, so the normal sequence is outline -> place -> route -> pour ->
labels.
"""
from pathlib import Path
import json
import uuid

import pcbnew

ROOT = Path(__file__).resolve().parents[2]
BOARDS = {
    "control": ROOT / "hardware" / "control" / "control.kicad_pcb",
    "keyboard": ROOT / "hardware" / "keyboard" / "keyboard.kicad_pcb",
    "brain": ROOT / "hardware" / "brain" / "brain.kicad_pcb",
    "power": ROOT / "hardware" / "power" / "power.kicad_pcb",
}
NS = uuid.UUID("9a2c0e77-4b1d-4c9a-8d3e-6f1a2b3c4d5e")
PICO_NAMES = {
    1: "GP0", 2: "GP1", 3: "GND", 4: "GP2", 5: "GP3", 6: "GP4",
    7: "GP5", 8: "GND", 9: "GP6", 10: "GP7", 11: "GP8", 12: "GP9",
    13: "GND", 14: "GP10", 15: "GP11", 16: "GP12", 17: "GP13",
    18: "GND", 19: "GP14", 20: "GP15", 21: "GP16", 22: "GP17",
    23: "GND", 24: "GP18", 25: "GP19", 26: "GP20", 27: "GP21",
    28: "GND", 29: "GP22", 30: "RUN", 31: "GP26", 32: "GP27",
    33: "AGND", 34: "GP28", 35: "VREF", 36: "3V3", 37: "3V3EN",
    38: "GND", 39: "VSYS", 40: "VBUS",
}
PI_NAMES = {
    1: "3V3", 2: "5V", 3: "SDA", 4: "5V", 5: "SCL", 6: "GND",
    7: "GP4", 8: "TX", 9: "GND", 10: "RX", 11: "GP17", 12: "BCK",
    13: "BL", 14: "GND", 15: "GP22", 16: "GP23", 17: "3V3", 18: "DC",
    19: "MOSI", 20: "GND", 21: "MISO", 22: "RST", 23: "SCLK",
    24: "CE0", 25: "GND", 26: "CE1", 27: "IDSD", 28: "IDSC",
    29: "GP5", 30: "GND", 31: "GP6", 32: "GP12", 33: "GP13",
    34: "GND", 35: "GP19", 36: "GP16", 37: "GP26", 38: "GP20",
    39: "GND", 40: "GP21",
}


def find_fp(board, ref):
    for fp in board.GetFootprints():
        if fp.GetReference() == ref:
            return fp
    return None


def add(labels, value, x, y, size=0.8, hjust=None):
    labels.append((value, x, y, size, hjust))


def header_labels(board, labels, ref, names, show_names=True, size=0.8):
    fp = find_fp(board, ref)
    if fp is None:
        return
    pads = list(fp.Pads())
    xs = [p.GetPosition().x for p in pads]
    ys = [p.GetPosition().y for p in pads]
    cx = (min(xs) + max(xs)) // 2
    cy = (min(ys) + max(ys)) // 2
    for pad in pads:
        num = pad.GetNumber()
        if not num.isdigit() or int(num) not in names:
            continue
        p = pad.GetPosition()
        label = f"{num} {names[int(num)]}" if show_names else str(num)
        if (max(xs) - min(xs)) > (max(ys) - min(ys)):
            y = pcbnew.ToMM(min(ys)) - 2.4 if p.y < cy else pcbnew.ToMM(max(ys)) + 2.4
            add(labels, label, pcbnew.ToMM(p.x), y, size=size)
        else:
            if p.y < cy:
                add(labels, label, pcbnew.ToMM(p.x) - 2.2, pcbnew.ToMM(p.y),
                    size=size, hjust="right")
            else:
                add(labels, label, pcbnew.ToMM(p.x) + 2.2, pcbnew.ToMM(p.y),
                    size=size, hjust="left")


def connector_label(board, labels, ref, value, dx=0, dy=0):
    fp = find_fp(board, ref)
    if fp is None:
        return
    p = fp.GetPosition()
    add(labels, f"{ref} {value}", pcbnew.ToMM(p.x) + dx,
        pcbnew.ToMM(p.y) + dy)


def pin1_marker(board, labels, ref, dx=1.8, dy=1.8):
    fp = find_fp(board, ref)
    if fp is None:
        return
    pad = fp.FindPadByNumber("1")
    if pad is None:
        return
    p = pad.GetPosition()
    add(labels, "1", pcbnew.ToMM(p.x) + dx, pcbnew.ToMM(p.y) + dy)


def control_labels(board, labels):
    header_labels(board, labels, "J1", PICO_NAMES)
    add(labels, "RASPBERRY PI PICO", 55, 18, size=1.0)
    mapping = {
        "J2": ("CUT", 7, 0), "J3": ("RES", 7, 0), "J4": ("SLIDER", 7, 0),
        "J5": ("JOY", 7, 0), "J6": ("JOY SW", 7, 0),
        "J7": ("PLAY", 0, 6), "J8": ("STOP", 0, 6), "J9": ("REC", 0, 6),
        "J10": ("MODE", 0, 6), "J11": ("<", 0, 6), "J12": (">", 0, 6),
        "J13": ("S1", 0, 6), "J14": ("S2", 0, 6), "J15": ("S3", 0, 6),
        "J16": ("S4", 0, 6), "J17": ("ENC1", -7, 0),
        "J18": ("ENC2", -7, 0), "J19": ("ENC3", -7, 0),
        "J20": ("ENC4", -7, 0), "J21": ("ENC5", -7, 0),
        "J22": ("ENC6", -7, 0), "J23": ("ENC7", -7, 0),
        "J24": ("ENC8", -7, 0), "J25": ("KEYBED", 0, -6),
        "J26": ("UART", 0, -6),
    }
    for ref, (value, dx, dy) in mapping.items():
        connector_label(board, labels, ref, value, dx, dy)
        pin1_marker(board, labels, ref)


def brain_labels(board, labels):
    header_labels(board, labels, "J1", PI_NAMES, show_names=False, size=0.65)
    add(labels, "PI ZERO 2W", 42, 28, size=1.0)
    mapping = {
        "J2": ("LCD", 0, -6), "J3": ("MIDI OUT", -12, -2),
        "J4": ("MIDI IN", -12, -2), "J5": ("PANEL USB", -12, -2),
        "J6": ("5V IN", 6, 0), "J7": ("GAUGE I2C", 0, 6),
        "J8": ("LOW BATT", 0, 6), "J9": ("SHUTDOWN", 0, 6),
        "J10": ("MIC", 0, 6), "J11": ("MIC HAT", 0, 6),
    }
    for ref, (value, dx, dy) in mapping.items():
        connector_label(board, labels, ref, value, dx, dy)
        pin1_marker(board, labels, ref)
    connector_label(board, labels, "SW1", "SHUTDOWN", 0, -6)


def power_labels(board, labels):
    mapping = {
        "J1": ("USB-C CHARGE", 0, -8), "J2": ("BATTERY", 0, -7),
        "J3": ("NTC", 0, -7), "J4": ("5V OUT", 0, -7),
        "J5": ("LOW BATT", 0, -7), "J6": ("I2C", -10, 0),
        "SW1": ("POWER", 0, -7),
    }
    for ref, (value, dx, dy) in mapping.items():
        connector_label(board, labels, ref, value, dx, dy)
        pin1_marker(board, labels, ref)


def keyboard_labels(board, labels):
    connector_label(board, labels, "J1", "I2C LINK", 0, -6)
    pin1_marker(board, labels, "J1")


BUILDERS = {
    "control": control_labels,
    "keyboard": keyboard_labels,
    "brain": brain_labels,
    "power": power_labels,
}


def block(name, value, x, y, size, hjust):
    key = uuid.uuid5(NS, f"{name}/{value}/{x:.4f}/{y:.4f}")
    justify = f"\n\t\t\t(justify {hjust})" if hjust else ""
    return (
        f'\t(gr_text {json.dumps(value)}\n'
        f"\t\t(at {x:.4f} {y:.4f} 0)\n"
        f'\t\t(layer "F.SilkS")\n'
        f'\t\t(uuid "{key}")\n'
        f"\t\t(effects\n"
        f"\t\t\t(font\n"
        f"\t\t\t\t(size {size:.2f} {size:.2f})\n"
        f"\t\t\t\t(thickness 0.12)\n"
        f"\t\t\t){justify}\n"
        f"\t\t)\n"
        f"\t)"
    )


def main():
    import sys
    names = sys.argv[1:] or list(BOARDS)
    for name in names:
        path = BOARDS[name]
        board = pcbnew.LoadBoard(str(path))
        labels = []
        BUILDERS[name](board, labels)
        text = path.read_text(encoding="utf-8")
        blocks = "\n".join(block(name, *item) for item in labels)
        marker = "\t(embedded_fonts no)\n)"
        if marker in text:
            text = text.replace(marker, blocks + "\n" + marker)
        else:
            pos = text.rfind(")")
            text = text[:pos] + blocks + "\n" + text[pos:]
        path.write_text(text, encoding="utf-8", newline="\n")
        print(f"labelled {path} ({len(labels)} labels)")


if __name__ == "__main__":
    main()

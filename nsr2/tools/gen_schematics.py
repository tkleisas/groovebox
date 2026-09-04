#!/usr/bin/env python3
"""Generate KiCad 10 schematics for the NSR-2 boards (brain + IO).

Emits .kicad_sch files by:
  1. parsing the installed KiCad symbol libraries,
  2. embedding needed symbol defs verbatim (renamed to Lib:Name),
  3. placing instances (rotation 0) and connecting every pin with a
     local label or a power symbol -- no wire routing needed.

Pin connections are keyed by pin NAME (ICs) or pin NUMBER (discrete,
connectors). Net "@GND", "@+3V3", "@+5V" place the matching power
symbol directly on the pin. Everything else becomes a local label.

Usage:  python gen_schematics.py
Writes: ../hardware/brain/brain.kicad_sch, ../hardware/io/io.kicad_sch
"""
import os
import sys
import uuid as uuidlib

KICAD_SYM_DIR = r"C:\Program Files\KiCad\10.0\share\kicad\symbols"
OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "hardware")
NS = uuidlib.UUID("6f9a2c1e-4b7d-4e3a-9c85-2f1d0a7b6e55")  # deterministic uuids


def _uuid(key):
    return str(uuidlib.uuid5(NS, key))


def _esc(s):
    """Escape a string for direct interpolation into the s-expr text."""
    return str(s).replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")


def _snap(v):
    """Snap a coordinate to KiCad's 1.27 mm connection grid."""
    return round(round(v / 1.27) * 1.27, 3)


# ── s-expression tokenizer / parser / emitter ───────────────────────────
class QStr(str):
    """A quoted string token (as opposed to a bare atom)."""


def tokenize(text):
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c in " \t\r\n":
            i += 1
        elif c == "(" or c == ")":
            yield c
            i += 1
        elif c == '"':
            j = i + 1
            buf = []
            while j < n:
                if text[j] == "\\" and j + 1 < n:
                    buf.append(text[j:j + 2])
                    j += 2
                elif text[j] == '"':
                    break
                else:
                    buf.append(text[j])
                    j += 1
            yield QStr("".join(buf))
            i = j + 1
        else:
            j = i
            while j < n and text[j] not in " \t\r\n()":
                j += 1
            yield text[i:j]
            i = j


def parse(text):
    stack, root = [], []
    for tok in tokenize(text):
        if tok == "(":
            node = []
            stack.append(node)
        elif tok == ")":
            node = stack.pop()
            (stack[-1] if stack else root).append(node)
        else:
            stack[-1].append(tok)
    return root


def _emit_inline(node):
    return all(not isinstance(c, list) for c in node) and \
        sum(len(str(c)) for c in node) < 60


def emit(node, indent=0):
    pad = "\t" * indent
    if isinstance(node, QStr):
        v = node.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
        return f'"{v}"'
    if not isinstance(node, list):
        return str(node)
    if _emit_inline(node):
        return "(" + " ".join(emit(c, 0) for c in node) + ")"
    inner = "\n".join(pad + "\t" + emit(c, indent + 1) for c in node)
    return f"(\n{inner}\n{pad})"


# ── symbol library access ───────────────────────────────────────────────
class SymLib:
    def __init__(self, libdir):
        self.libdir = libdir
        self.cache = {}
        self.custom = {}

    def add_custom(self, lib_id, node):
        self.custom[lib_id] = node

    def get(self, lib_id):
        if lib_id in self.cache:
            return self.cache[lib_id]
        if lib_id in self.custom:
            self.cache[lib_id] = self.custom[lib_id]
            return self.custom[lib_id]
        lib, name = lib_id.split(":", 1)
        path = os.path.join(self.libdir, lib + ".kicad_sym")
        if not os.path.exists(path):
            raise FileNotFoundError(f"symbol library not found: {path}")
        with open(path, encoding="utf-8") as f:
            parsed = parse(f.read())
        node = None
        for top in parsed:
            for child in (top if isinstance(top, list) else [top]):
                if isinstance(child, list) and child and child[0] == "symbol" \
                        and isinstance(child[1], QStr) and str(child[1]) == name:
                    node = child
        if node is None:
            raise KeyError(f"symbol '{name}' not found in {path}")
        self.cache[lib_id] = node
        return node

    def pins(self, lib_id):
        """pin number/name -> (px, py, angle); and number->name map."""
        node = self.get(lib_id)
        base = str(node[1]).split(":")[-1]
        # symbol inheritance: an (extends "PARENT") def borrows the
        # parent's unit bodies -- resolve pins from there
        for child in node[2:]:
            if isinstance(child, list) and child and child[0] == "extends":
                parent_id = lib_id.split(":", 1)[0] + ":" + str(child[1])
                return self.pins(parent_id)
        pins = {}
        for child in node[2:]:
            if not (isinstance(child, list) and child and child[0] == "symbol"):
                continue
            suffix = str(child[1])[len(base):]
            parts = suffix.lstrip("_").split("_")
            if len(parts) != 2 or parts[0] not in ("0", "1"):
                continue  # only common (0) + unit 1 (1) graphics
            for sub in child[2:]:
                if not (isinstance(sub, list) and sub and sub[0] == "pin"):
                    continue
                px = py = pa = None
                name = num = None
                for e in sub[1:]:
                    if isinstance(e, list) and e:
                        if e[0] == "at":
                            px, py, pa = float(e[1]), float(e[2]), float(e[3])
                        elif e[0] == "name":
                            name = str(e[1])
                        elif e[0] == "number":
                            num = str(e[1])
                if num is not None:
                    pins[num] = (px, py, pa, name)
        return pins


# ── schematic writer ────────────────────────────────────────────────────
POWER = ("@GND", "@+3V3", "@+5V")
POWERSYM = {"@GND": "power:GND", "@+3V3": "power:+3V3", "@+5V": "power:+5V"}


class Board:
    def __init__(self, lib, name, title):
        self.lib = lib
        self.name = name
        self.title = title
        self.root = _uuid(f"{name}/root")
        self.body = []
        self.libs = {}          # lib_id -> embedded node
        self.pin_cache = {}
        self.counters = {}
        self.pwr = 0

    def _next(self, prefix):
        self.counters[prefix] = self.counters.get(prefix, 0) + 1
        return f"{prefix}{self.counters[prefix]}"

    def _load(self, lib_id):
        if lib_id not in self.libs:
            node = [list(x) if isinstance(x, list) else x for x in self.lib.get(lib_id)]
            node[1] = QStr(lib_id)
            # flatten inheritance: a schematic's embedded defs must carry
            # full unit bodies -- resolve (extends) here
            ext = next((c for c in node[2:]
                        if isinstance(c, list) and c and c[0] == "extends"), None)
            if ext is not None:
                parent_id = lib_id.split(":", 1)[0] + ":" + str(ext[1])
                parent = self.lib.get(parent_id)
                pbase = str(parent[1])
                cbase = str(node[1]).split(":")[-1]
                for pc in parent[2:]:
                    if isinstance(pc, list) and pc and pc[0] == "symbol":
                        newname = str(pc[1]).replace(pbase, cbase, 1)
                        node.append([pc[0], QStr(newname)] +
                                    [list(x) if isinstance(x, list) else x
                                     for x in pc[2:]])
                node = [c for c in node
                        if not (isinstance(c, list) and c and c[0] == "extends")]
            self.libs[lib_id] = node
        if lib_id not in self.pin_cache:
            self.pin_cache[lib_id] = self.lib.pins(lib_id)
        return self.pin_cache[lib_id]

    def label(self, net, x, y, angle=0):
        just = "right bottom" if angle == 180 else "left bottom"
        self.body.append(
            f'\t(label "{net}"\n'
            f'\t\t(at {x} {y} {angle})\n'
            f'\t\t(effects (font (size 1.27 1.27)) (justify {just}))\n'
            f'\t\t(uuid "{_uuid(self.name + "/lbl/" + net + "/" + str(x) + "/" + str(y) + "/" + str(angle))}")\n'
            f"\t)")

    def power(self, net, x, y):
        self.pwr += 1
        lib_id = POWERSYM[net]
        pins = self._load(lib_id)
        node = [list(x) if isinstance(x, list) else x
                for x in self.lib.get(lib_id)]
        node = [list(x) if isinstance(x, list) else x for x in node]
        node[1] = QStr(lib_id)
        if lib_id not in self.libs:
            self.libs[lib_id] = node
        px, py, _pa, _n = pins["1"]
        ix, iy = x - px, y + py
        ref = f"#PWR{self.pwr:02d}"
        pin_uuids = "\n".join(
            f'\t\t(pin "{num}"\n\t\t\t(uuid "{_uuid(self.name + "/pwrpin/" + ref + "/" + num)}")\n\t\t)'
            for num in pins)
        self.body.append(
            f"\t(symbol\n"
            f'\t\t(lib_id "{lib_id}")\n'
            f"\t\t(at {ix} {iy} 0)\n"
            f"\t\t(unit 1)\n"
            f"\t\t(exclude_from_sim no)\n"
            f"\t\t(in_bom yes)\n"
            f"\t\t(on_board yes)\n"
            f"\t\t(dnp no)\n"
            f'\t\t(uuid "{_uuid(self.name + "/pwr/" + ref)}")\n'
            f'\t\t(property "Reference" "{_esc(ref)}"\n'
            f"\t\t\t(at {ix} {iy + 4} 0)\n"
            f"\t\t\t(hide yes)\n"
            f"\t\t\t(effects (font (size 1.27 1.27)))\n"
            f"\t\t)\n"
            f'\t\t(property "Value" "{_esc(net[1:])}"\n'
            f"\t\t\t(at {ix} {iy - 4} 0)\n"
            f"\t\t\t(effects (font (size 1.27 1.27)))\n"
            f"\t\t)\n"
            f'\t\t(property "Footprint" ""\n'
            f"\t\t\t(at {ix} {iy} 0)\n"
            f"\t\t\t(hide yes)\n"
            f"\t\t\t(effects (font (size 1.27 1.27)))\n"
            f"\t\t)\n"
            f'\t\t(property "Datasheet" ""\n'
            f"\t\t\t(at {ix} {iy} 0)\n"
            f"\t\t\t(hide yes)\n"
            f"\t\t\t(effects (font (size 1.27 1.27)))\n"
            f"\t\t)\n"
            f'\t\t(property "Description" "Power symbol"\n'
            f"\t\t\t(at {ix} {iy} 0)\n"
            f"\t\t\t(hide yes)\n"
            f"\t\t\t(effects (font (size 1.27 1.27)))\n"
            f"\t\t)\n"
            f"{pin_uuids}\n"
            f"\t\t(instances\n"
            f'\t\t\t(project "{self.name}"\n'
            f'\t\t\t\t(path "/{self.root}"\n'
            f'\t\t\t\t\t(reference "{ref}")\n'
            f"\t\t\t\t\t(unit 1)\n"
            f"\t\t\t\t)\n"
            f"\t\t\t)\n"
            f"\t\t)\n"
            f"\t)"
        )

    def power_flags(self, rails, x, y):
        """One PWR_FLAG per rail: our power arrives from off-sheet
        sources (PowerBoost / USB), so each rail needs a power-output
        pin to satisfy ERC's driven check."""
        for i, rail in enumerate(rails):
            self.place("power:PWR_FLAG", "#FLG", rail, x + i * 15.24, y,
                       pin_nets={1: rail})

    def nc(self, x, y):
        self.body.append(
            f"\t(no_connect\n"
            f"\t\t(at {x} {y})\n"
            f'\t\t(uuid "{_uuid(self.name + "/nc/" + str(x) + "/" + str(y))}")\n'
            f"\t)")

    def text(self, s, x, y, size=2.5):
        self.body.append(
            f"\t(text \"{_esc(s)}\"\n"
            f"\t\t(exclude_from_sim no)\n"
            f"\t\t(at {x} {y} 0)\n"
            f"\t\t(effects (font (size {size} {size})))\n"
            f'\t\t(uuid "{_uuid(self.name + "/txt/" + s + str(x) + str(y))}")\n'
            f"\t)")

    def place(self, lib_id, prefix, value, x, y, fp="", desc="",
              pin_nets=None, nc_pins=None, pin_names=None):
        """pin_nets: {pin_number: net}; pin_names: {pin_name: net};
        '@' nets become power symbols, others local labels."""
        pins = self._load(lib_id)
        x, y = _snap(x), _snap(y)
        by_name = {info[3]: num for num, info in pins.items() if info[3]}
        resolved = {}
        for num, net in (pin_nets or {}).items():
            resolved[str(num)] = net
        for pname, net in (pin_names or {}).items():
            if pname not in by_name:
                raise KeyError(f"{lib_id}: pin '{pname}' not found; "
                               f"available: {sorted(by_name)}")
            resolved[by_name[pname]] = net
        for pname in (nc_pins or []):
            if isinstance(pname, str) and not pname.isdigit():
                if pname not in by_name:
                    raise KeyError(f"{lib_id}: nc pin '{pname}' not found")
                resolved[by_name[pname]] = "@NC"
            else:
                resolved[str(pname)] = "@NC"
        ref = self._next(prefix)
        key = _uuid(f"{self.name}/sym/{ref}")
        pin_uuids = "\n".join(
            f'\t\t(pin "{num}"\n\t\t\t(uuid "{_uuid(self.name + "/pin/" + ref + "/" + num)}")\n\t\t)'
            for num in sorted(resolved, key=lambda s: (len(s), s)))
        self.body.append(
            f"\t(symbol\n"
            f'\t\t(lib_id "{lib_id}")\n'
            f"\t\t(at {x} {y} 0)\n"
            f"\t\t(unit 1)\n"
            f"\t\t(exclude_from_sim no)\n"
            f"\t\t(in_bom yes)\n"
            f"\t\t(on_board yes)\n"
            f"\t\t(dnp no)\n"
            f'\t\t(uuid "{key}")\n'
            f'\t\t(property "Reference" "{_esc(ref)}"\n'
            f"\t\t\t(at {x} {y - 5.08} 0)\n"
            f"\t\t\t(effects (font (size 1.27 1.27)))\n"
            f"\t\t)\n"
            f'\t\t(property "Value" "{_esc(value)}"\n'
            f"\t\t\t(at {x} {y + 5.08} 0)\n"
            f"\t\t\t(effects (font (size 1.27 1.27)))\n"
            f"\t\t)\n"
            f'\t\t(property "Footprint" "{_esc(fp)}"\n'
            f"\t\t\t(at {x} {y} 0)\n"
            f"\t\t\t(hide yes)\n"
            f"\t\t\t(effects (font (size 1.27 1.27)))\n"
            f"\t\t)\n"
            f'\t\t(property "Datasheet" "~"\n'
            f"\t\t\t(at {x} {y} 0)\n"
            f"\t\t\t(hide yes)\n"
            f"\t\t\t(effects (font (size 1.27 1.27)))\n"
            f"\t\t)\n"
            f'\t\t(property "Description" "{_esc(desc)}"\n'
            f"\t\t\t(at {x} {y} 0)\n"
            f"\t\t\t(hide yes)\n"
            f"\t\t\t(effects (font (size 1.27 1.27)))\n"
            f"\t\t)\n"
            f"{pin_uuids}\n"
            f"\t\t(instances\n"
            f'\t\t\t(project "{self.name}"\n'
            f'\t\t\t\t(path "/{self.root}"\n'
            f'\t\t\t\t\t(reference "{ref}")\n'
            f"\t\t\t\t\t(unit 1)\n"
            f"\t\t\t\t)\n"
            f"\t\t\t)\n"
            f"\t\t)\n"
            f"\t)"
        )
        for num, net in resolved.items():
            px, py, pa, _n = pins[num]
            sx, sy = x + px, y - py
            if net == "@NC":
                self.nc(sx, sy)
            elif net in POWER:
                self.power(net, sx, sy)
            else:
                self.label(net, sx, sy, int((pa + 180) % 360))
        return ref

    def save(self, path):
        for node in self.libs.values():
            if not any(isinstance(c, list) and c and c[0] == "embedded_fonts"
                       for c in node[2:]):
                node.append(["embedded_fonts", "no"])
        lib_blocks = "\n".join("\t" + emit(node, 1) for node in self.libs.values())
        out = (
            "(kicad_sch\n"
            "\t(version 20250610)\n"
            '\t(generator "eeschema")\n'
            '\t(generator_version "10.0")\n'
            f'\t(uuid "{self.root}")\n'
            '\t(paper "A3")\n'
            "\t(title_block\n"
            f'\t\t(title "{self.title}")\n'
            '\t\t(date "2026-08-31")\n'
            '\t\t(rev "A")\n'
            '\t\t(company "groovebox / ВОКОИТЕР")\n'
            "\t)\n"
            "\t(lib_symbols\n"
            f"{lib_blocks}\n"
            "\t)\n"
            + "\n".join(self.body) + "\n"
            "\t(sheet_instances\n"
            '\t\t(path "/"\n'
            '\t\t\t(page "1")\n'
            "\t\t)\n"
            "\t)\n"
            "\t(embedded_fonts no)\n"
            ")\n"
        )
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(out)
        print(f"wrote {path}  ({len(self.body)} body items, {len(self.libs)} symbols)")


def try_place(board, candidates, *args, **kwargs):
    last = None
    for lib_id in candidates:
        try:
            board._load(lib_id)
        except (FileNotFoundError, KeyError) as e:
            last = e
            continue
        return board.place(lib_id, *args, **kwargs)
    raise last


# ── custom symbol: Holtek HT16K33A, 28 SOP-A/SSOP-A ────────────────────
# Pinout verbatim from HT16K33A Rev 1.11 datasheet, "Pin Assignment",
# 28 SOP-A/SSOP-A figure. Address pins A2/A1/A0 are multiplexed on
# ROW0/ROW1/ROW2 (named here by their row function; leave floating for
# slave address 0x70 -- internal pull-low at power-on reset).

def ht16k33a_symbol_text():
    left = [
        ("26", "SCL", "input", 20.32),
        ("27", "SDA", "bidirectional", 15.24),
    ] + [
        (str(25 - i), f"ROW{i}", "bidirectional", 10.16 - i * 5.08)
        for i in range(8)           # ROW0-7 on pins 25..18
    ] + [
        (str(17 - i), f"ROW{8 + i}", "bidirectional", -30.48 - i * 5.08)
        for i in range(8)           # ROW8-15 on pins 17..10
    ]
    right = [
        (str(2 + i), f"COM{i}", "output", 20.32 - i * 5.08)
        for i in range(8)           # COM0-7 on pins 2..9
    ]

    def pin(num, name, etype, x, y, ang):
        return (f'(pin {etype} line (at {x} {y} {ang}) (length 5.08)\n'
                f'\t\t\t(name "{name}" (effects (font (size 1.27 1.27))))\n'
                f'\t\t\t(number "{num}" (effects (font (size 1.27 1.27))))\n'
                f"\t\t)")

    pins = [pin(n, nm, t, -17.78, y, 0) for n, nm, t, y in left]
    pins += [pin(n, nm, t, 17.78, y, 180) for n, nm, t, y in right]
    pins.append(pin("28", "VDD", "power_in", 0, 30.48, 270))
    pins.append(pin("1", "VSS", "power_in", 0, -73.66, 90))
    body = "\n\t\t".join(pins)
    return f'''
(symbol "HT16K33A"
\t\t(exclude_from_sim no)
\t\t(in_bom yes)
\t\t(on_board yes)
\t\t(property "Reference" "U" (at 0 27.94 0) (effects (font (size 1.27 1.27))))
\t\t(property "Value" "HT16K33A" (at 0 -76.2 0) (effects (font (size 1.27 1.27))))
\t\t(property "Footprint" "Package_SO:SSOP-28_5.3x10.2mm_P0.65mm" (at 0 0 0)
\t\t\t(hide yes) (effects (font (size 1.27 1.27))))
\t\t(property "Datasheet" "HT16K33Av111" (at 0 0 0) (hide yes) (effects (font (size 1.27 1.27))))
\t\t(property "Description" "16x8 LED driver, keyscan, I2C 0x70 (address on ROW0-2 at POR)" (at 0 0 0)
\t\t\t(hide yes) (effects (font (size 1.27 1.27))))
\t\t(symbol "HT16K33A_0_1"
\t\t\t(rectangle (start -12.7 25.4) (end 12.7 -68.58)
\t\t\t\t(stroke (width 0.254) (type default)) (fill (type background)))
\t\t)
\t\t(symbol "HT16K33A_1_1"
\t\t\t{body}
\t\t)
)'''


def ht16k33a_write_project_lib(path):
    txt = ('(kicad_symbol_lib\n'
           '\t(version 20250610)\n'
           '\t(generator "gen_schematics")\n'
           + ht16k33a_symbol_text() + '\n)\n')
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(txt)
    print("wrote", path)


# ── BRAIN board ─────────────────────────────────────────────────────────
def brain(lib):
    b = Board(lib, "brain", "NSR-2 brain board (Pi carrier)")
    b.text("NSR-2 BRAIN - Pi Zero 2W carrier", 25, 25, 3.0)

    # Pi Zero 2W header (2x20, pin numbers = Pi physical numbering)
    pi = {
        1: "@+3V3", 2: "@+5V", 4: "@+5V", 5: "SHUTDOWN",
        8: "MIDI_TX", 10: "MIDI_RX",
        13: "LCD_BL", 18: "LCD_DC", 19: "LCD_MOSI", 22: "LCD_RST",
        23: "LCD_SCLK", 24: "LCD_CS",
        17: "@+3V3",
    }
    for p in (6, 9, 14, 20, 25, 30, 34, 39):
        pi[p] = "@GND"
    nc = [3, 7, 11, 12, 15, 16, 21, 26, 27, 28, 29, 31, 32, 33, 35, 36, 37, 38, 40]
    b.place("Connector_Generic:Conn_02x20_Odd_Even", "J", "Pi_Zero_2W",
            45, 100, "Connector_PinSocket_2.54mm:PinSocket_2x20_P2.54mm_Vertical",
            "2x20 female headers (Pi Zero 2W stacks on it)", pi, nc)

    # screen module (right-angle header)
    b.place("Connector_Generic:Conn_01x08", "J", "ILI9488_4.0in",
            230, 45, "Connector_PinHeader_2.54mm:PinHeader_1x08_P2.54mm_Horizontal",
            "4.0\" 480x320 SPI TFT module", {
                1: "@+3V3", 2: "@GND", 3: "LCD_CS", 4: "LCD_RST",
                5: "LCD_DC", 6: "LCD_MOSI", 7: "LCD_SCLK", 8: "LCD_BL"})

    # MIDI out (DIN-5) and in (DIN-5 + 6N138)
    b.place("Connector_Generic:Conn_01x05", "J", "DIN5_MIDI_OUT",
            290, 110, "nsr2_fp:DIN5-RA-PTH", "MIDI OUT (PCB-edge right-angle DIN-5)", {
                2: "@GND", 4: "MIDI_OUT_P4", 5: "@GND"}, [1, 3])
    b.place("Connector_Generic:Conn_01x05", "J", "DIN5_MIDI_IN",
            290, 145, "nsr2_fp:DIN5-RA-PTH", "MIDI IN (PCB-edge right-angle DIN-5)", {
                2: "@GND", 4: "MIDI_IN_P4", 5: "MIDI_IN_K"}, [1, 3])
    b.place("Device:R", "R", "33R", 225, 105, "Resistor_SMD:R_0805_2012Metric",
            "MIDI out series", {1: "MIDI_TX", 2: "MIDI_OUT_P4"})
    b.place("Device:R", "R", "220R", 225, 130, "Resistor_SMD:R_0805_2012Metric",
            "MIDI in LED series", {1: "MIDI_IN_P4", 2: "MIDI_IN_A"})
    try_place(b, ["Isolator:6N138"], "U", "6N138", 225, 170,
              "Package_DIP:DIP-8_W7.62mm", "MIDI in optocoupler",
              pin_nets={2: "MIDI_IN_A", 3: "MIDI_IN_K", 5: "@GND",
                        6: "MIDI_RX", 8: "@+5V"}, nc_pins=[1, 4, 7])
    b.place("Device:R", "R", "10k", 175, 175, "Resistor_SMD:R_0805_2012Metric",
            "MIDI RX pull-up", {1: "@+5V", 2: "MIDI_RX"})

    # USB OTG breakout to the IO board (ESD-protected)
    try_place(b, ["Power_Protection:USBLC6-2P6"], "U", "USBLC6-2P6", 225, 220,
              "Package_TO_SOT_SMD:SOT-23-6", "USB ESD protection",
              pin_nets={1: "USB_DP", 6: "USB_DP", 3: "USB_DM", 4: "USB_DM",
                        2: "@GND", 5: "@+5V"})
    try_place(b, ["Connector:USB_A", "Interface_USB:USB_A"],
              "J", "USB_A_HOST", 290, 220, "Connector_USB:USB_A_Connfly_DS1098_Horizontal",
              "Pi OTG breakout (A->micro-B cable to IO board)",
              pin_names={"VBUS": "@+5V", "D-": "USB_DM", "D+": "USB_DP",
                         "GND": "@GND", "Shield": "@GND"})

    # power input (PowerBoost 1000C), switches, mic
    b.place("Connector_Generic:Conn_01x04", "J", "POWERBOOST",
            290, 260, "Connector_JST:JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical",
            "PowerBoost 1000C: 5V GND EN GND", {
                1: "@+5V", 2: "@GND", 3: "PWR_EN", 4: "@GND"})
    b.place("Switch:SW_SPDT", "SW", "POWER", 230, 262,
            "nsr2_fp:SW_SPDT_SLIDE_THT", "power switch (PowerBoost EN)",
            pin_nets={1: "@GND", 2: "PWR_EN"}, nc_pins=[3])
    b.place("Switch:SW_Push", "SW", "SHUTDOWN", 175, 262,
            "Button_Switch_THT:SW_PUSH_6mm", "shutdown button (GPIO3)",
            pin_nets={1: "SHUTDOWN", 2: "@GND"})
    b.place("Connector_Generic:Conn_01x02", "J", "MIC",
            130, 262, "Connector_JST:JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical",
            "electret mic capsule -> Audio Injector mic in",
            {1: "MIC_SIG", 2: "@GND"})

    # decoupling
    for i, (x, y, net) in enumerate(((140, 45, "@+5V"), (165, 45, "@+5V"),
                                     (190, 45, "@+3V3"))):
        b.place("Device:C", "C", "100n", x, y, "Capacitor_SMD:C_0805_2012Metric",
                "decoupling", {1: net, 2: "@GND"})
    b.power_flags(("+3V3", "+5V", "GND"), 25, 285)
    b.save(os.path.join(OUT_DIR, "brain", "brain.kicad_sch"))


# ── IO board ────────────────────────────────────────────────────────────
def io(lib):
    b = Board(lib, "io", "NSR-2 IO board (panel faceplate)")
    b.text("NSR-2 IO - Pico panel controller", 25, 25, 3.0)

    # Pico (2x20 header, physical pin numbering)
    pico = {1: "ENC1_A", 2: "ENC1_B", 4: "ENC2_A", 5: "ENC2_B",
            6: "SDA", 7: "SCL", 9: "ENC3_A", 10: "ENC3_B",
            11: "ENC4_A", 12: "ENC4_B", 14: "ENC1_SW", 15: "ENC2_SW",
            16: "ENC3_SW", 17: "ENC4_SW", 21: "UART_TX", 22: "UART_RX",
            30: "RUN_SW", 36: "@+3V3", 40: "@+5V"}
    for p in (3, 8, 13, 18, 23, 28, 33, 38):
        pico[p] = "@GND"
    pico_nc = [19, 20, 24, 25, 26, 27, 29, 31, 32, 34, 35, 37, 39]
    b.place("Connector_Generic:Conn_02x20_Odd_Even", "J", "Pi_Pico",
            40, 110, "Connector_PinSocket_2.54mm:PinSocket_2x20_P2.54mm_Vertical",
            "2x20 female headers (Raspberry Pi Pico module)", pico, pico_nc)

    # expander #1 @0x20: grid rows 1-2 (keys 0-15)
    mcp1 = {f"GPA{i}": f"KEY{i}" for i in range(8)}
    mcp1.update({f"GPB{i}": f"KEY{8 + i}" for i in range(8)})
    mcp1.update({"V_{DD}": "@+3V3", "V_{SS}": "@GND", "SDA": "SDA",
                 "SCK": "SCL", "~{RESET}": "@+3V3",
                 "A0": "@GND", "A1": "@GND", "A2": "@GND"})
    b.place("Interface_Expansion:MCP23017x-x-SO", "U", "MCP23017",
            130, 60, "Package_SO:SSOP-28_5.3x10.2mm_P0.65mm",
            "grid keys 0-15 expander (0x20)", pin_names=mcp1,
            nc_pins=["NC", "INTA", "INTB"])
    # expander #2 @0x21: grid rows 3-4 (keys 16-31)
    mcp2 = {f"GPA{i}": f"KEY{16 + i}" for i in range(8)}
    mcp2.update({f"GPB{i}": f"KEY{24 + i}" for i in range(8)})
    mcp2.update({"V_{DD}": "@+3V3", "V_{SS}": "@GND", "SDA": "SDA",
                 "SCK": "SCL", "~{RESET}": "@+3V3",
                 "A0": "@+3V3", "A1": "@GND", "A2": "@GND"})
    b.place("Interface_Expansion:MCP23017x-x-SO", "U", "MCP23017",
            130, 120, "Package_SO:SSOP-28_5.3x10.2mm_P0.65mm",
            "grid keys 16-31 expander (0x21)", pin_names=mcp2,
            nc_pins=["NC", "INTA", "INTB"])
    # expander #3 @0x22: function keys 32-44 (3 spare)
    mcp3 = {"GPA0": "KEY32", "GPA1": "KEY33", "GPA2": "KEY34", "GPA3": "KEY35",
            "GPA4": "KEY36", "GPA5": "KEY37", "GPA6": "KEY38", "GPA7": "KEY39",
            "GPB0": "KEY40", "GPB1": "KEY41", "GPB2": "KEY42", "GPB3": "KEY43",
            "GPB4": "KEY44",
            "V_{DD}": "@+3V3", "V_{SS}": "@GND", "SDA": "SDA",
            "SCK": "SCL", "~{RESET}": "@+3V3",
            "A0": "@GND", "A1": "@+3V3", "A2": "@GND"}
    b.place("Interface_Expansion:MCP23017x-x-SO", "U", "MCP23017",
            130, 180, "Package_SO:SSOP-28_5.3x10.2mm_P0.65mm",
            "function key expander (0x22)", pin_names=mcp3,
            nc_pins=["NC", "INTA", "INTB", "GPB5", "GPB6", "GPB7"])

    # HT16K33A LED driver (custom symbol from the Holtek datasheet):
    # COM0-3 x ROW0-7 drive the 32 under-socket LEDs; COM4-7, ROW8-15
    # and ROW15/INT unused; ROW0-2 left floating -> address 0x70 at POR.
    ht = {"VDD": "@+3V3", "VSS": "@GND", "SDA": "SDA", "SCL": "SCL"}
    for i in range(8):
        ht[f"ROW{i}"] = f"ROW{i}"
    for i in range(4):
        ht[f"COM{i}"] = f"COM{i}"
    b.place("nsr2:HT16K33A", "U", "HT16K33A",
            380, 150, "Package_SO:SSOP-28_5.3x10.2mm_P0.65mm",
            "32 grid LED driver (0x70)", pin_names=ht,
            nc_pins=["COM4", "COM5", "COM6", "COM7"] +
                    [f"ROW{i}" for i in range(8, 16)])

    # ADS1115: thumbstick X/Y + crossfader
    b.place("Analog_ADC:ADS1115IDGS", "U", "ADS1115",
            260, 250, "Package_SO:MSOP-10_3x3mm_P0.5mm",
            "analog: bend / mod / crossfader (0x48)",
            pin_names={"AIN0": "STICK_X", "AIN1": "STICK_Y", "AIN2": "XFADE",
                       "AIN3": "@GND", "VDD": "@+3V3", "GND": "@GND",
                       "SDA": "SDA", "SCL": "SCL", "ADDR": "@GND",
                       "ALERT/RDY": "@NC"})

    # 45 key switches (32 grid + 13 function) -- Choc V1 hot-swap sockets
    for i in range(45):
        row, col = divmod(i, 8)
        b.place("Switch:SW_Push", "SW", f"KEY{i}", 210 + col * 17.78, 40 + row * 17.78,
                "nsr2_fp:SW_Hotswap_Kailh_Choc_V1", "Choc V1 hot-swap socket",
                pin_nets={1: f"KEY{i}", 2: "@GND"})

    # 32 under-socket LEDs: anode -> COM(i>>3), cathode -> 1k -> ROW(i&7)
    for i in range(32):
        row, col = divmod(i, 8)
        x, y = 210 + col * 17.78, 140 + row * 25.4
        b.place("Device:LED", "D", "red", x, y, "LED_SMD:LED_0603_1608Metric",
                "under-socket LED",
                pin_names={"A": f"COM{i >> 3}", "K": f"LEDR{i}"})
        b.place("Device:R", "R", "1k", x, y + 12.7,
                "Resistor_SMD:R_0805_2012Metric", "LED series",
                pin_nets={1: f"LEDR{i}", 2: f"ROW{i & 7}"})

    # analog connectors + RC filters
    b.place("Connector_Generic:Conn_01x04", "J", "THUMBSTICK",
            25, 210, "Connector_JST:JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical",
            "pitch/mod thumbstick", {
                1: "@GND", 2: "@+3V3", 3: "STICK_X_RAW", 4: "STICK_Y_RAW"})
    b.place("Connector_Generic:Conn_01x03", "J", "CROSSFADER",
            25, 235, "Connector_JST:JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical",
            "crossfader", {1: "@GND", 2: "XFADE_RAW", 3: "@+3V3"})
    for i, (raw, filt) in enumerate((("STICK_X_RAW", "STICK_X"),
                                     ("STICK_Y_RAW", "STICK_Y"),
                                     ("XFADE_RAW", "XFADE"))):
        x = 65 + i * 25.4
        b.place("Device:R", "R", "10k", x, 215, "Resistor_SMD:R_0805_2012Metric",
                "analog RC series", {1: raw, 2: filt})
        b.place("Device:C", "C", "100n", x, 235, "Capacitor_SMD:C_0805_2012Metric",
                "analog RC shunt", {1: filt, 2: "@GND"})

    # encoders
    for i in range(4):
        b.place("Connector_Generic:Conn_01x05", "J", f"ENCODER{i + 1}",
                25, 40 + i * 17.78,
                "Connector_JST:JST_XH_B5B-XH-A_1x05_P2.50mm_Vertical",
                "EC11 encoder", {
                    1: f"ENC{i + 1}_A", 2: f"ENC{i + 1}_B", 3: "@GND",
                    4: f"ENC{i + 1}_SW", 5: "@GND"})

    # UART link (the USB cable from the brain plugs into the Pico's own
    # micro-B, flush with the top edge -- no receptacle on this board)
    b.place("Connector_Generic:Conn_01x04", "J", "UART_LINK",
            25, 262, "Connector_JST:JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical",
            "serial transport header", {
                1: "@+3V3", 2: "@GND", 3: "UART_TX", 4: "UART_RX"})
    b.place("Switch:SW_Push", "SW", "RUN", 145, 262,
            "Button_Switch_THT:SW_PUSH_6mm", "Pico RUN/BOOTSEL access",
            pin_nets={1: "RUN_SW", 2: "@GND"})

    # I2C pull-ups + decoupling
    b.place("Device:R", "R", "4k7", 175, 215, "Resistor_SMD:R_0805_2012Metric",
            "I2C SDA pull-up", {1: "@+3V3", 2: "SDA"})
    b.place("Device:R", "R", "4k7", 175, 235, "Resistor_SMD:R_0805_2012Metric",
            "I2C SCL pull-up", {1: "@+3V3", 2: "SCL"})
    for i, x in enumerate((175, 195, 215, 235)):
        b.place("Device:C", "C", "100n", x, 262, "Capacitor_SMD:C_0805_2012Metric",
                "decoupling", {1: "@+3V3", 2: "@GND"})
    b.power_flags(("+3V3", "+5V", "GND"), 260, 285)
    b.save(os.path.join(OUT_DIR, "io", "io.kicad_sch"))


def main():
    lib = SymLib(KICAD_SYM_DIR)
    lib.add_custom("nsr2:HT16K33A",
                   parse(ht16k33a_symbol_text())[0])
    ht16k33a_write_project_lib(
        os.path.join(OUT_DIR, "nsr2.kicad_sym"))
    brain(lib)
    io(lib)


if __name__ == "__main__":
    sys.exit(main())

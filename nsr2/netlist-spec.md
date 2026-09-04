# NSR-2 netlist specification — rev A

Definitive hardware↔firmware contract for the two NSR-2 boards. Firmware
side lives in `panel-fw/src/board_config.h` (`PANEL_NSR2` variant); any
change here must update that file (and vice versa) in the same commit.

Manufacturing: 2-layer, 1.6 mm, ENIG, **deep-red soldermask** (bokontep
`--red-dark` family) with cream/white silkscreen — on the IO board the
PCB **is the faceplate**: red mask + cream print replace the aluminum
panel. SMD placed by the fab (JLC economic assembly); every
hand-soldered part is THT.

## Architecture

    ┌─ BRAIN board (100×80) ─────────────┐      ┌─ IO board / faceplate (180×210) ──┐
    │ Pi Zero 2W (2×20 female headers)   │      │ Pico (2×20 female, micro-B flush  │
    │  └ Audio Injector Zero stacks on   │ USB  │   top edge) — USB-MIDI panel      │
    │    the Pi's 40-pin header          │ A→µB │  ├ 3× MCP23017   45 keys          │
    │ ILI9488 4.0" SPI (right-angle hdr) │cable │  ├ HT16K33A    32 LEDs           │
    │ 2× DIN-5 MIDI (6N138 in, R out)    │─────►│  ├ 1× ADS1115    3 analog         │
    │ PowerBoost 1000C + LiPo + switch   │      │  ├ 4× EC11 encoders (XH)          │
    │ USB-A receptacle = Pi OTG breakout │      │  └ 45× Choc hot-swap sockets      │
    └────────────────────────────────────┘      └───────────────────────────────────┘

Two boards exactly as proposed: **Brain** (zero + screen + codec + power +
DIN MIDI) and **IO** (pico + buttons + encoders + LEDs + analog). The only
inter-board link is one USB cable (5V / D+ / D− / GND) — the panel stays an
independently testable USB-MIDI device, as in NSR-1.

I²C map (one bus on the IO board, 400 kHz, Pico master):

| Addr | Device | Role |
|---|---|---|
| 0x20 | MCP23017 | grid keys 0–15 (rows 1–2: GPA0–7, GPB0–7) |
| 0x21 | MCP23017 | grid keys 16–31 (rows 3–4: GPA0–7, GPB0–7) |
| 0x22 | MCP23017 | function keys: 13 inputs (GPA0–5, GPA6–7, GPB0–4), 3 spare |
| 0x48 | ADS1115 | AIN0 = thumbstick X (bend), AIN1 = Y (mod), AIN2 = crossfader, AIN3 → GND |
| 0x70 | HT16K33A | 32 LED driver only (28-SOP brings out COM0–7; address on ROW0–2 at POR, left floating = 0x70) |

**Three expanders, as NSR-1** — the MCP23017 has 16 I/O pins, so 45 keys
need 48 expander pins. I²C pull-ups 4.7 kΩ on the IO board only.

Power: PowerBoost 1000C → 5V rail on the brain board feeds the Pi's 5V
pins; the Pi's OTG port powers the IO board (~115 mA worst case: 32 LEDs
at 1 kΩ + Pico + expanders + HT16K33A + ADS). LiPo 2000–3000 mAh on the
PowerBoost's JST, panel/rear power switch THT on the brain board.

---

## Brain board — 100 × 80 mm

M3 holes (3.2 mm) at (5,5) (95,5) (5,75) (95,75). Pi Zero 2W on 2×20
machined female headers (Audio Injector Zero stacks on the Pi's exposed
male header above it — codec, bias and I2S never touch this board).

### Pi net usage

| Pi pin | Net / function |
|---|---|
| 2, 4 | +5V rail from PowerBoost |
| 1, 17 | 3V3 → screen module logic/backlight |
| 6, 9, 14, 20, 25, 30, 34, 39 | GND |
| 8 / 10 (GPIO14/15) | DIN MIDI out / in (31250 baud, `pi3-disable-bt`) |
| 24 (GPIO8 CE0), 19 (GPIO10 MOSI), 23 (GPIO11 SCLK) | screen SPI0 |
| 18 / 22 / 13 (GPIO24/25/27) | screen DC / RST / BL |
| 5 (GPIO3) | optional shutdown button (rootfs read-only, per design.md) |

### Connectors (THT)

| Ref | Type | Pinout |
|---|---|---|
| J1 | USB-A receptacle | Pi OTG breakout: 1=VBUS, 2=D−, 3=D+, 4=GND (+ shell GND) — short A→micro-B cable to the IO board's Pico |
| J2, J3 | DIN-5 ×2 | MIDI OUT (TX + 33 Ω → pin 4/5, 2=GND) · MIDI IN (6N138: 5→RX, 2/3 opto input + 220 Ω/1 kΩ network) |
| J4 | 2×8 right-angle header | ILI9488 module: VCC(3V3), GND, CS, RST, DC, MOSI, SCLK, LED — plugs up to the screen module fixed in the IO faceplate window |
| J5 | XH-2P | electret mic capsule → Audio Injector Zero mic/line-in jack (3.5 mm pigtail), strain relief |
| SW1 | SPDT THT, rear edge | power switch (PowerBoost EN) |
| J6 | 2×5 header | PowerBoost 1000C mount (5V, GND, EN, LiPo± pigtail to its JST) |

The brain board sits **below** the IO faceplate on enclosure standoffs
(≈18 mm level) under the screen/encoder zone. The Pi's microSD slot on
the Zero's underside stays reachable through an enclosure slot — align
it with the rear wall.

### ICs and discretes

| Ref | Part | Notes |
|---|---|---|
| U1 | 6N138 | MIDI-IN optocoupler (never feed a bare UART) |
| U2 | USBLC6-2SC6 | ESD on USB D+/D− at J1 |
| R1–R5 | MIDI network: 33 Ω, 220 Ω, 1 kΩ, 2× 10 kΩ | per MIDI 1.0 electrical spec |
| C1–C3 | 100 nF 0805 | decoupling U1, U2, screen header |

---

## IO board (faceplate) — 180 × 210 mm

M3 holes at (5,5) (175,5) (30,5) (150,5) (5,205) (30,205) (150,205)
(175,205) — corners plus offset mid pairs (x30/x150), keeping every
standoff clear of the screen window, the mic hole and the shift/space
row. Board outline matches `nsr2/panel_mockup_nsr2.png` with one addition:
a **screen window** cut out of Edge.Cuts at (40, 8)–(140, 80) — the
4.0" ILI9488 module mounts to the faceplate with 4× M2 screws over the
window and connects **down** to the brain board's right-angle header
(the brain board rides ~18 mm below on enclosure standoffs). Mic hole
⌀3 mm at (90, 5.5); capsule glues behind it, wired to brain J5.
Placement by face: Choc sockets + LEDs on the **top** face; all ICs,
Pico and connectors on the **bottom** face (clean faceplate top). Pico
bottom-mounted on 2×20 female headers, micro-B flush with the top edge
(short cable to the brain board's USB-A — internal, no external port).

### Pico pin map — identical to NSR-1 rev A (reused verbatim)

| Pico pin | GPIO | Net / function |
|---|---|---|
| 1 / 2 | GP0 / GP1 | ENC1 A / B |
| 4 / 5 | GP2 / GP3 | ENC2 A / B |
| 6 / 7 | GP4 / GP5 | I²C SDA / SCL (bus master) |
| 9 / 10 | GP6 / GP7 | ENC3 A / B |
| 11 / 12 | GP8 / GP9 | ENC4 A / B |
| 14–17 | GP10–13 | ENC1–4 push |
| 19 / 20 | GP14 / GP15 | spare (rev B) |
| 21 / 22 | GP16 / GP17 | UART link header (optional serial transport) |
| 24–27 | GP18–21 | spare (rev B) |
| 30 | RUN | SW1 reset button → GND |
| 36 | 3V3(OUT) | +3V3 rail |
| 3,8,13,18,23,28,38 | — | GND (use ≥ 4) |

### Key expander maps

MCP #1 @0x20 — grid rows 1–2:

| Pins | Keys |
|---|---|
| GPA0–7 | grid 0–7 (row 1, top) |
| GPB0–7 | grid 8–15 (row 2) |

MCP #2 @0x21 — grid rows 3–4:

| Pins | Keys |
|---|---|
| GPA0–7 | grid 16–23 (row 3) |
| GPB0–7 | grid 24–31 (row 4, bottom) |

MCP #3 @0x22 — function keys (A0→VDD, A1/A2→GND):

| Pin | Key | | Pin | Key |
|---|---|---|---|---|
| GPA0 | PLAY (32) | | GPB0 | S3 (40) |
| GPA1 | STOP (33) | | GPB1 | S4 (41) |
| GPA2 | REC (34) | | GPB2 | SH-L (42) |
| GPA3 | MODE (35) | | GPB3 | SH-R (43) |
| GPA4 | < (36) | | GPB4 | SPACE (44) |
| GPA5 | > (37) | | GPB5–7 | spare (test points) |
| GPA6 | S1 (38) | | | |
| GPA7 | S2 (39) | | | |

All 45 keys: **Kailh Choc V1 hot-swap sockets** (CPG151101S11), switch to
GND, expander pull-ups, active low — no matrix, no diodes. Soft/transport
keys are 1u Choc like the grid (one switch type for the whole panel).

### LEDs — documented deviation from the mockup

**Choc switches have no LED slot** (unlike NSR-1's MX). Per-key lighting
is under-socket instead: 32× 0603 LED centered under each grid socket,
glowing through **translucent/clear low-profile caps**. Drive unchanged:
HT16K33A COM0–3 × ROW0–7, LED i → RAM[i & 7] bit (i >> 3), i = grid index
(row-major), **1 kΩ** series (32 × ~3 mA ≈ 96 mA worst case — inside the
Pico's ~200 mA external budget; 470 Ω would not be).

### Connectors (all JST-XH, THT)

| Ref | Type | Pinout |
|---|---|---|
| J1 | XH-4P | thumbstick: 1=GND, 2=3V3, 3=VRx, 4=VRy |
| J2 | XH-3P | crossfader: 1=GND, 2=wiper, 3=3V3 |
| J3–J6 | XH-5P ×4 | encoders 1–4: 1=A, 2=B, 3=C→GND, 4=D→SW, 5=E→GND (tie 5–3) |
| J7 | XH-4P | UART link: 1=3V3, 2=GND, 3=TX(GP16), 4=RX(GP17) |

No USB receptacle on this board — the brain's USB-A cable plugs into the
**Pico's own micro-B connector** (flush with the top edge); VBUS then
appears on Pico header pin 40 and feeds the +5V rail.

### ICs and discretes

| Ref | Part | Notes |
|---|---|---|
| U1 | MCP23017 (SSOP-28) @0x20 | A0–A2→GND; GPA=keys 0–7, GPB=keys 8–15 |
| U2 | MCP23017 (SSOP-28) @0x21 | A0→VDD, A1/A2→GND; GPA=keys 16–23, GPB=keys 24–31 |
| U3 | MCP23017 (SSOP-28) @0x22 | A1→VDD, A0/A2→GND; function keys per table above |
| U4 | HT16K33A (SSOP-28) @0x70 | LED driver only; custom symbol in `nsr2.kicad_sym` (28-SOP has COM0–7 only; ROW0–2 double as address inputs — leave floating for 0x70) |
| U5 | ADS1115 (MSOP-10) @0x48 | ADDR→GND; AIN0=VRx, AIN1=VRy, AIN2=fader, AIN3→GND, RC filtered |
| R1, R2 | 4.7 kΩ 0805 | I²C pull-ups — this board only |
| R-f ×3 | 10 kΩ 0805 | series, wiper → AINx (RC at the ADS1115 side) |
| C-f ×3 | 100 nF 0805 | AINx → GND |
| LED 1–32 | 0603 red | under grid sockets; anode → COM(i>>3), cathode → 1 kΩ → ROW(i&7) |
| R 1–32 | 1 kΩ 0805 | LED series |
| C1–C4 | 100 nF 0805 | decoupling U1–U4 |

Unused ADS1115 input (AIN3) → GND. ADC_VREF (pin 35) NC.

---

## BOM split

- **Fab assembles (SMD):** IO — 3× MCP23017, ADS1115, HT16K33A, 32× 0603
  LED + 32× 1 kΩ, 3× 10 k + RC set, 2× 4.7 k, decoupling; brain — 6N138,
  USBLC6, MIDI R network, decoupling.
- **Hand-solder (THT):** 45× Choc hot-swap sockets, Pico + Pi female
  headers, screen right-angle header, USB-A receptacle, 2×
  DIN-5, all XH connectors, EC11s (panel-wired via XH), PowerBoost,
  power switch, tact (RUN).
- **Modules (bought, not designed):** Pi Zero 2W, Audio Injector Zero,
  ILI9488 4.0" SPI module, Pico, thumbstick, 60 mm slide pot, LiPo +
  PowerBoost 1000C, translucent Choc cap set (incl. 2× 1u + 1× ~3u).

## Schematics

The schematics are **generated**: `nsr2/tools/gen_schematics.py` emits
both `.kicad_sch` files (and the `nsr2.kicad_sym` project library with
the custom HT16K33A symbol) from this spec — rerun it after any change
here, then re-check with `kicad-cli sch erc`. Status: **0 ERC errors**
on both boards; remaining warnings are benign and enumerated in
`nsr2/README.md` (embedded-copy vs library mismatch, the intentional
single-pin MIC label, footprint names to finalize at layout: DIN-5
sockets, power switch, Choc hot-swap sockets). Board outlines
(180×210 faceplate with screen window, 100×80 brain) are already
captured in the `.kicad_pcb` files. See `nsr2/README.md` for the
collaboration contract (what may be hand-edited and when).

## Bring-up checklist

1. IO I²C scan must list exactly: `0x20 0x21 0x22 0x48 0x70`.
2. LEDs: Note On ch2 (note 0–31) — LED i glows under grid key i+1.
3. Keys: every key emits 36 + index (protocol table); ghost-free by
   construction (no matrix).
4. Analog: sweep thumbstick X/Y + crossfader; verify bend, CC 1, CC 26.
5. Encoders: detents → CC 16–19 (65/63); pushes → CC 32–35.
6. GRV SysEx: QUERY/REPLY (feature bit 3 set = NSR-2 layout),
   velocity-source switching, persistence round-trip.
7. Brain: USB-A port enumerates the Pico; MIDI DIN loopback; screen on
   SPI0 at 32 MHz; boot-to-sound with the codec stacked.
8. UART header continuity (GND/3V3/TX/RX) for the future serial transport.

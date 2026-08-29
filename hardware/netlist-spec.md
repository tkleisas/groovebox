# Hardware netlist specification — rev A

Definitive hardware↔firmware contract for the two control-surface boards.
Firmware side lives in `panel-fw/src/board_config.h`; any change here must
update that file (and vice versa) in the same commit.

Manufacturing target: 2-layer, 1.6 mm, ENIG, green→**deep-red soldermask**
(faceplate aesthetic), white silkscreen. SMD parts placed by the fab
(PCBWay/JLC economic assembly); every hand-soldered part is THT.

## Architecture

    Pi Zero 2W ──USB(OTG)── [Pico] ──I2C──┬── 3× MCP23017   39 keys, uniform
             (power+MIDI)   │  GP4/GP5    ├── HT16K33      16 step LEDs
                            │             └── 3× ADS1115    9 analog inputs
                            ├─ 4× EC11 encoders (direct GPIO)
                            ├─ RUN button, UART link header (optional serial)
                            └─ micro-USB → Pi OTG port

All 39 keys are **single-ended inputs on MCP23017 expanders** (switch to GND,
expander pull-ups, active low): no matrix, no per-key diodes, one uniform
scan path in firmware. The HT16K33 is a pure LED driver. This supersedes the
design.md rev-0 idea of HT16K33 key scanning (documented deviation).

I²C map (one bus, 400 kHz):

| Addr | Device | Board | Role |
|---|---|---|---|
| 0x20 | MCP23017 | keyboard | white keys 0–15 |
| 0x21 | MCP23017 | keyboard | black keys 16–26, shifts 38/39, 3 spare |
| 0x22 | MCP23017 | control | function keys + joystick SW, 5 spare |
| 0x48 | ADS1115 | control | pots 1–4 |
| 0x49 | ADS1115 | control | pots 5–6, slider, joystick Y |
| 0x4A | ADS1115 | control | joystick X (pitch bend) |
| 0x70 | HT16K33  | keyboard | 16 LED driver only |

Power: Pico is powered from the Pi's OTG port over its own micro-USB;
everything runs from 3V3(OUT) (pin 36). Budget: LEDs 16×6 mA ≈ 96 mA +
4×MCP + 3×ADS + HT16K33 ≈ **~130 mA worst case** — inside the Pico's ~200 mA
external budget. Keep LED resistors ≥ 470 Ω.

---

## Control board — 90 × 70 mm

M3 mounting holes (3.2 mm) at (5,5) (85,5) (5,65) (85,65).
Pico on 2×20 machined female headers (J1A pins 1–20, J1B pins 21–40),
module micro-USB flush with the **bottom edge** (cable to Pi OTG), BOOTSEL
reachable through an enclosure hole.

### Pico pin map

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
| 29 | GP22 | spare (rev B) |
| 30 | RUN | SW1 reset button → GND |
| 31–34 | GP26–28 | spare (rev B; ADC-capable) |
| 36 | 3V3(OUT) | +3V3 rail |
| 3,8,13,18,23,28,38 | — | GND (use ≥ 4) |

### Connectors (all JST-XH, THT)

| Ref | Type | Pinout |
|---|---|---|
| J2–J7 | XH-3P ×6 | pots 1–6: 1=GND, 2=wiper, 3=3V3 |
| J8 | XH-3P | data slider: 1=GND, 2=wiper, 3=3V3 |
| J9 | XH-4P | joystick module: 1=GND, 2=3V3, 3=VRx, 4=VRy |
| J10–J13 | XH-5P ×4 | encoders 1–4: 1=A, 2=B, 3=C→GND, 4=D→SW, 5=E→GND (tie 5–3 on board) |
| J14 | XH-4P | keyboard link: 1=3V3, 2=GND, 3=SDA, 4=SCL |
| J15 | XH-4P | UART link: 1=3V3, 2=GND, 3=TX(GP16), 4=RX(GP17) |
| J16–J25 | XH-2P ×10 | function keys: 1=U4 GPIO, 2=GND (PLAY, STOP, REC, MODE, <, >, S1–S4 in that order) |
| J26 | XH-2P | joystick SW |
| J27, J28 | XH-2P | spare inputs (U4 pins 12–15 remain for rev B) |

### ICs and discretes

| Ref | Part | Notes |
|---|---|---|
| U1 | ADS1115 (VSSOP-10) @0x48 | ADDR→GND. AIN0–AIN3 ← pots 1–4 |
| U2 | ADS1115 @0x49 | ADDR→3V3. AIN0←pot5, AIN1←pot6, AIN2←slider, AIN3←VRy |
| U3 | ADS1115 @0x4A | ADDR→SDA. AIN0←VRx; AIN1–3 → GND |
| U4 | MCP23017 (SSOP-28 or SPDIP-28) @0x22 | ADDR strap A1→VDD, A0/A2→GND. All 16 GPA/B inputs (J16–J28) |
| SW1 | 6 mm tact, THT | RUN → GND |
| R-f ×9 | 10 kΩ 0805 | series, wiper → AINx (RC filter, place at the ADS1115 side) |
| C-f ×9 | 100 nF 0805 | AINx → GND |
| R1, R2 | 4.7 kΩ 0805 | I²C pull-ups — **control board only**, never duplicate on keyboard |
| C1–C4 | 100 nF 0805 | decoupling, one per IC |

Unused ADS1115 inputs → GND. ADC_VREF (pin 35) NC.

---

## Keyboard board — 356 × 62 mm

M3 holes at (5,5) (351,5) (5,57) (351,57).
29 MX hot-swap sockets (Kailh, THT): SH-L, 16 whites, 11 blacks, SH-R.
Whites at 19.05 mm pitch; blacks on the upper row (y offset ≈ −22 mm) at the
piano gaps {1,2, 4,5,6, 8,9, 11,12,13} plus one far-right position; shifts at
the row ends. Gateron Red / Silent Red, 3 mm LED slot in whites.

| Ref | Part | Role |
|---|---|---|
| U1 | MCP23017 @0x20 (A0–A2→GND) | GPA/B = white keys 0–15, in white-key order |
| U2 | MCP23017 @0x21 (A0→VDD) | pins 0–10 = blacks 16–26, pins 11/12 = shifts 38/39, 3 spare (test points) |
| U3 | HT16K33 (SOP-28) @0x70 | LED driver only — key-scan RAM unused |
| J1 | XH-4P | I²C link to control board (same pinout as J14) |
| LED 1–16 | 3 mm THT, in switch slots | anode → COM(i>>3), cathode → R 470 Ω → ROW(i&7), i = white key index − 1 |
| R1–R16 | 470 Ω 0805 | LED series |
| C1–C3 | 100 nF 0805 | decoupling U1–U3 |

LED/HT16K33 mapping must stay in lockstep with `panel-fw/src/leds.c`
(LED i → RAM[i & 7] bit (i >> 3)). No I²C pull-ups on this board.

---

## BOM split

- **Fab assembles (SMD):** keyboard — 2× MCP23017 (SPDIP-28 if keeping it
  fully hand-solderable, else SOIC-28), HT16K33, 16× 470 Ω, 3× 100 nF;
  control — 3× ADS1115, MCP23017 (SSOP-28), 9× 10 k + 13× 100 nF, 2× 4.7 k.
- **Hand-solder (THT):** all hot-swap sockets, all XH connectors, Pico
  sockets, tact switch, LEDs, encoder/pot/slider/joystick modules (wired).

## Bring-up checklist

1. I²C scan must list exactly: 0x20 0x21 0x22 0x48 0x49 0x4A 0x70.
2. LEDs: Note On ch2 (note 0–15) — verify LED i maps to white key i+1.
3. Keys: every key emits 36 + index (protocol table); chords ghost-free by
   construction (no matrix).
4. Analog: sweep each pot/slider/joystick; verify CC 20–26, CC 1, bend.
5. Encoders: detents → CC 16–19 (65/63); pushes → CC 32–35.
6. GRV SysEx: QUERY/REPLY, velocity source switching, persistence round-trip.
7. UART header continuity (GND/3V3/TX/RX) for the future serial transport.

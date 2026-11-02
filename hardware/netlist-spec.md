# Hardware netlist specification — rev B2

Definitive hardware↔firmware contract for the two NSR-1 control-surface
boards. Firmware side lives in `panel-fw/src/board_config.h`; any change here
must update that file (and vice versa) in the same commit.

Manufacturing target: 2-layer, 1.6 mm, ENIG, green→**deep-red soldermask**
(faceplate aesthetic), white silkscreen. SMD parts placed by the fab
(PCBWay/JLC economic assembly); every hand-soldered part is THT.

Rev B2 keeps NSR-1's split-board architecture and adds the dedicated 16-key
step row, the rev B2 eight-encoder surface, two ADS1115s, and a fifth
MCP23017 on the control board. The fifth expander is the only architectural
change forced by the board split: the keyboard board owns the 45 piano/step
keys and LEDs, while the control board owns the function keys, joystick
switch, and all eight encoder pushes.

## Architecture

    Pi Zero 2W ──USB(OTG)── [Pico] ──I2C──┬── 5× MCP23017    55 keys + 8 encoder pushes
             (power+MIDI)   │  GP4/GP5    ├── HT16K33       32 LEDs (16 step + 16 white)
                            │             └── 2× ADS1115     5 analog inputs
                            ├─ 8× EC11 encoders (direct GPIO A/B)
                            ├─ RUN button, UART link header
                            └─ micro-USB → Pi OTG port

All keys are **single-ended inputs on MCP23017 expanders** (switch to GND,
expander pull-ups, active low): no matrix, no per-key diodes, one uniform
scan path in firmware. The HT16K33 is a pure LED driver. Encoder quadrature
stays on direct Pico GPIO; encoder push-buttons ride spare expander pins.

I²C map (one bus, 400 kHz):

| Addr | Device | Board | Role |
|---|---|---|---|
| 0x20 | MCP23017 | keyboard | white keys 0–15 |
| 0x21 | MCP23017 | keyboard | black keys 16–26, shifts 38/39, 3 spare |
| 0x22 | MCP23017 | control | function keys 27–37, joystick SW 30, encoder pushes 1–5 |
| 0x23 | MCP23017 | keyboard | step-row keys 40–55 |
| 0x24 | MCP23017 | control | encoder pushes 6–8, 13 spare |
| 0x48 | ADS1115 | control | CUT, RES, slider, joystick Y |
| 0x49 | ADS1115 | control | joystick X, spare inputs to GND |
| 0x70 | HT16K33 | keyboard | 32 LED driver only |

Power: Pico is powered from the Pi's OTG port over its own micro-USB;
everything runs from 3V3(OUT) (pin 36). Budget: 32 LEDs × ~3 mA ≈ 96 mA +
5×MCP + 2×ADS + HT16K33 + Pico ≈ **~160 mA worst case** — inside the Pico's
~200 mA external budget. Keep LED resistors ≥ 1 kΩ. The control board carries
the only I²C pull-ups.

---

## Control board — 130 × 100 mm

M3 mounting holes (3.2 mm) at (5,5) (125,5) (5,95) (125,95).
Pico module footprint `Module:RaspberryPi_Pico_Common_THT` (2×20, 17.78 mm row spacing, four mounting holes; pins 1–20 left, 21–40 right),
module micro-USB flush with the **bottom edge** (cable to Pi OTG), BOOTSEL
reachable through an enclosure hole.

> The rev A outline was 90 × 70 mm. Rev B2 initially targeted 100 × 80 mm.
> The module-accurate Pico footprint and enlarged encoder field grow it to
> 130 × 100 mm so the two pin rows, eight encoder headers, and five analog/key
> connectors fit without courtyard collisions.

### Pico pin map

| Pico pin | GPIO | Net / function |
|---|---|---|
| 1 / 2 | GP0 / GP1 | ENC1 A / B |
| 4 / 5 | GP2 / GP3 | ENC2 A / B |
| 6 / 7 | GP4 / GP5 | I²C SDA / SCL (bus master) |
| 9 / 10 | GP6 / GP7 | ENC3 A / B |
| 11 / 12 | GP8 / GP9 | ENC4 A / B |
| 14–17 | GP10–GP13 | spare |
| 19 / 20 | GP14 / GP15 | ENC5 A / B |
| 21 / 22 | GP16 / GP17 | UART link header (optional serial transport) |
| 24 / 25 | GP18 / GP19 | ENC6 A / B |
| 26 / 27 | GP20 / GP21 | ENC7 A / B |
| 29 / 31 | GP22 / GP26 | ENC8 A / B |
| 30 | RUN | SW1 reset button → GND |
| 32 / 34 | GP27 / GP28 | spare (ADC-capable) |
| 36 | 3V3(OUT) | +3V3 rail |
| 40 | VBUS | +5V from Pi USB |
| 3,8,13,18,23,28,33,38 | — | GND (use ≥ 4) |

Encoder push mapping: ENC1–ENC5 → MCP 0x22 GPB3–GPB7 (CC 32–36);
ENC6–ENC8 → MCP 0x24 GPA0–GPA2 (CC 37–39). Push switches are active low.

### Connectors (all JST-XH, THT)

| Ref | Type | Pinout |
|---|---|---|
| J2, J3 | XH-3P | pots CUT / RES: 1=GND, 2=wiper, 3=3V3 |
| J4 | XH-3P | data slider: 1=GND, 2=wiper, 3=3V3 |
| J5 | XH-4P | joystick module: 1=GND, 2=3V3, 3=VRx, 4=VRy |
| J6 | XH-2P | joystick SW: 1=KEY30, 2=GND |
| J7–J16 | XH-2P ×10 | function keys: 1=U3 GPIO, 2=GND (PLAY 27, STOP 28, REC 29, MODE 31, < 32, > 33, S1 34, S2 35, S3 36, S4 37) |
| J17–J24 | XH-5P ×8 | encoders 1–8: 1=A, 2=B, 3=C→GND, 4=D→SW, 5=E→GND (tie 5–3 on board) |
| J25 | XH-4P | keyboard link: 1=3V3, 2=GND, 3=SDA, 4=SCL |
| J26 | XH-4P | UART link: 1=3V3, 2=GND, 3=TX(GP16), 4=RX(GP17) |

### ICs and discretes

| Ref | Part | Notes |
|---|---|---|
| U1 | ADS1115 (VSSOP-10) @0x48 | ADDR→GND. AIN0=CUT, AIN1=RES, AIN2=SLIDER, AIN3=JOY_Y |
| U2 | ADS1115 @0x49 | ADDR→3V3. AIN0=JOY_X; AIN1–3 → GND |
| U3 | MCP23017 (SSOP-28) @0x22 | A1→VDD, A0/A2→GND. GPA0–7 = keys 27/28/29/31/32/33/34/35; GPB0–1 = 36/37; GPB2 = KEY30; GPB3–7 = ENC1–5 push |
| U4 | MCP23017 (SSOP-28) @0x24 | A2→VDD, A0/A1→GND. GPA0–2 = ENC6–8 push; remaining pins spare/no-connect |
| SW1 | 6 mm tact, THT | RUN → GND |
| R-f ×5 | 10 kΩ 0805 | series, wiper → AINx (RC filter, place at the ADS1115 side) |
| C-f ×5 | 100 nF 0805 | AINx → GND |
| R1, R2 | 4.7 kΩ 0805 | I²C pull-ups — **control board only**, never duplicate on keyboard |
| C1–C5 | 100 nF 0805 | decoupling, one per IC |
| C6 | 100 nF 0805 | Pico 3V3 rail decoupling |
| C7 | 100 nF 0805 | Pico 5V/VBUS bulk decoupling |

Unused ADS1115 inputs → GND. ADC_VREF (pin 35) NC.

---

## Keyboard board — 356 × 80 mm

M3 holes at (5,5) (351,5) (5,75) (351,75).
45 MX hot-swap sockets (Kailh, THT; `nsr1_fp:SW_Hotswap_Kailh_MX_Plated_1.00u`): SH-L, 16 whites, 11 blacks, SH-R,
16 step-row keys. Whites at 19.05 mm pitch; blacks on the upper row (y offset
≈ −22 mm) at the piano gaps {1,2, 4,5,6, 8,9, 11,12,13} plus one far-right
position; shifts at the row ends. Step row sits below the piano row at
19.05 mm pitch. Gateron Red / Silent Red, 3 mm LED slot in whites and step
keys.

> The rev A outline was 356 × 62 mm with only the piano row. Rev B2 adds the
> step row and initially used 356 × 100 mm; the panel-fit pass compacts the
> service parts into the inter-row gaps and uses 356 × 80 mm.

| Ref | Part | Role |
|---|---|---|
| U1 | MCP23017 @0x20 (A0–A2→GND) | GPA0–7 = white keys 0–7; GPB0–7 = white keys 8–15 |
| U2 | MCP23017 @0x21 (A0→VDD) | GPA0–7 = blacks 16–23; GPB0–2 = blacks 24–26; GPB3/4 = shifts 38/39; GPB5–7 = spare (test points) |
| U3 | MCP23017 @0x23 (A0/A1→VDD) | GPA0–7 = step keys 40–47; GPB0–7 = step keys 48–55 |
| U4 | HT16K33 (SOP-28) @0x70 | LED driver only — key-scan RAM unused |
| J1 | XH-4P | I²C link to control board (same pinout as J25) |
| LED 1–32 | 3 mm THT, in switch slots | anode → COM(i>>3), cathode → R 1 kΩ → ROW(i&7) |
| R1–R32 | 1 kΩ 0805 | LED series |
| C1–C4 | 100 nF 0805 | decoupling U1–U4 |

LED mapping: **LED 0–15 = step row 1–16; LED 16–31 = white keys 1–16**
(channel-2 notes 0–31). Must stay in lockstep with
`panel-fw/src/leds.c` (LED i → RAM[i & 7] bit (i >> 3)). No I²C pull-ups on
this board.

---

## Key-index contract

| Index | Note | Key | Expander / pin |
|---|---|---|---|
| 0–15 | 36–51 | white keys 1–16 | 0x20 GPA0–GPB7 |
| 16–26 | 52–62 | black keys | 0x21 GPA0–GPB2 |
| 27–29 | 63–65 | PLAY, STOP, REC | 0x22 GPA0–2 |
| 30 | 66 | joystick SW | 0x22 GPB2 |
| 31 | 67 | MODE | 0x22 GPA3 |
| 32, 33 | 68, 69 | < , > | 0x22 GPA4–5 |
| 34–37 | 70–73 | soft keys S1–S4 | 0x22 GPA6–7, GPB0–1 |
| 38, 39 | 74, 75 | SHIFT-L, SHIFT-R | 0x21 GPB3–4 |
| 40–55 | 76–91 | step row 1–16 | 0x23 GPA0–GPB7 |

Encoder pushes are not key indices: CC 32 + push index, push index 0–7 =
ENC1–ENC8.

## BOM split

- **Fab assembles (SMD):** keyboard — 3× MCP23017 (SOIC-28), HT16K33,
  32× 1 kΩ, 4× 100 nF; control — 2× ADS1115, 2× MCP23017 (SSOP-28),
  5× 10 k + 6× 100 nF, 2× 4.7 k.
- **Hand-solder (THT):** all hot-swap sockets, all XH connectors, Pico
  sockets, tact switch, LEDs, encoder/pot/slider/joystick modules (wired).

## Bring-up checklist

1. I²C scan must list exactly: 0x20 0x21 0x22 0x23 0x24 0x48 0x49 0x70.
2. LEDs: Note On ch2 (note 0–31) — 0–15 step row, 16–31 white keys.
3. Keys: every key emits 36 + index (table above); chords ghost-free by
   construction (no matrix).
4. Analog: sweep CUT/RES/slider/joystick; verify CC 20–21, CC 26, CC 1, bend.
5. Encoders: detents → CC 16–23 (65/63); pushes → CC 32–39.
6. GRV SysEx: QUERY/REPLY, velocity source switching, persistence round-trip.
7. UART header continuity (GND/3V3/TX/RX) for the future serial transport.

## Revision history

- **Rev A** — 39-key piano/function surface, 3× MCP23017, 3× ADS1115,
  4 encoders, 16 LEDs, 90 × 70 control board + 356 × 62 keyboard board.
- **Rev B** — dedicated 16-key step row, 4× MCP23017, 2 pots, 4 encoders.
- **Rev B2** — 8 encoders (4 pageable + 4 ADSR), 2× ADS1115, 32 LEDs,
  5× MCP23017 (board split), 130 × 100 control board + 356 × 80 keyboard
  board.

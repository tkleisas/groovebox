# NSR-1 brain carrier — Raspberry Pi Zero 2W rev A

Status: **rev A schematic and layout.** This is the first SBC-specific carrier. It is not
a universal brain board. A Radxa Zero 3W or Orange Pi Zero 3W carrier is a
separate PCB with its own spec and validation.

Source of truth for this board. The control/keyboard boards remain governed by
[`../netlist-spec.md`](../netlist-spec.md). Platform policy is in
[`../../docs/mission.md`](../../docs/mission.md).

## Scope

The carrier owns:

- the Pi Zero 2W socket and mechanical support;
- the Audio Injector Zero stack (the HAT plugs into the Pi's male header);
- the 4.0" ILI9488 SPI display header;
- DIN MIDI in/out;
- the panel USB link to the control board's Pico;
- the 5V input from the separate power/battery board;
- the low-battery and fuel-gauge connections;
- the Pi shutdown button and mic capsule connector.

The carrier does **not** own the battery charger, boost converter, battery
protection, or power switch. Those live on the power board.

## Architecture

```
Power/battery board ──5V/GND──────────────┐
        │ LOW_BATT, SDA/SCL               │
        ▼                                 ▼
┌──────────────── brain carrier (110×90) ──────────────┐
│  Pi Zero 2W (2×20 female header)                     │
│   └ Audio Injector Zero stacks on the Pi male header │
│  ILI9488 display header (right angle)                │
│  DIN MIDI in/out + 6N138                             │
│  USB-A host port ── cable ── panel Pico micro-B      │
│  Pi micro-B OTG ── cable ── USB-A host port          │
│  shutdown button, low-battery input, gauge I²C, mic  │
└──────────────────────────────────────────────────────┘
```

The Pi still sees the panel as a stock USB-MIDI device. The Pi keeps only
display, codec, and DIN MIDI on its own pins; all panel I/O remains on the
Pico.

## Board outline and mounting

- Board: **110 × 90 mm**, 2-layer, 1.6 mm, green soldermask, white silk.
- M3 mounting holes (3.2 mm) at `(5,5)`, `(105,5)`, `(5,85)`, `(105,85)`.
- Pi Zero 2W: 2×20 female header. Add four M2.5 slotted holes/standoffs at the
  Pi's 58 × 23 mm pattern so the SBC is not supported by the header alone.
- Audio Injector Zero: stacks on the Pi's male header; keep the HAT outline
  and its 3.5 mm jack edge clear of the carrier and enclosure.
- The Pi micro-USB OTG edge and the USB-A panel port must be reachable with
  the carrier installed.
- Mounting datum is the 40-pin header; the four M2.5 slots allow the small
  production tolerances seen on Pi/Radxa-class boards.
- The carrier fits the NSR-1 brain adapter plate. The enclosure provides the
  M3 inserts; this PCB provides the SBC-specific pattern.

## Pi header usage

| Pi pin | Net / function |
|---|---|
| 1, 17 | +3V3 to display and carrier logic |
| 2, 4 | +5V from power board |
| 3, 5 | I²C1 SDA/SCL to the Audio Injector and power-board gauge |
| 6, 9, 14, 20, 25, 30, 34, 39 | GND |
| 8, 10 | UART TX/RX to DIN MIDI (31250 baud) |
| 11 | LOW_BATT input from power board (GPIO17) |
| 15 | shutdown button to GND (GPIO22) |
| 18, 22, 13 | display DC, RST, BL (GPIO24/25/27) |
| 19, 23, 24 | display SPI MOSI, SCLK, CE0 (GPIO10/11/8) |
| 12, 35, 38, 40 | I²S to the Audio Injector Zero (GPIO18/19/20/21) |

Pins not listed are no-connect on this carrier. The Pi's micro-USB OTG is not
on the 40-pin header; it connects to the panel USB port through a short
micro-B-to-USB-A cable.

> Note: the shutdown button must not use GPIO3/pin 5. That pin is I²C1 SCL and
> is needed by the WM8731 codec. GPIO22/pin 15 is used instead.

## Connectors

| Ref | Type | Pinout |
|---|---|---|
| J1 | 2×20 socket module | `Module:Raspberry_Pi_Zero_Socketed_THT_FaceDown_MountingHoles`; 2.54 mm row spacing, four mounting holes, physical pin numbering |
| J2 | DIN-5 right-angle | MIDI OUT: 2=GND, 4=TX via 33 Ω, 5=GND |
| J3 | DIN-5 right-angle | MIDI IN: 2=GND, 4=opto input via 220 Ω, 5=opto return; 6N138 → RX |
| J4 | 1×08 right-angle header | ILI9488: 1=3V3, 2=GND, 3=CS, 4=RST, 5=DC, 6=MOSI, 7=SCLK, 8=BL |
| J5 | USB-A receptacle | Panel link: 1=VBUS, 2=D−, 3=D+, 4=GND; ESD-protected; cable to Pico micro-B |
| J6 | XH-2P | 5V input from power board: 1=+5V, 2=GND |
| J7 | XH-4P | Power-board gauge: 1=3V3, 2=GND, 3=SDA, 4=SCL |
| J8 | XH-2P | Low battery: 1=LOW_BATT (GPIO17), 2=GND |
| J9 | XH-2P | Shutdown button: 1=GPIO22, 2=GND |
| J10 | XH-2P | Electret mic capsule: 1=MIC_SIG, 2=GND |
| J11 | XH-2P | MIC_SIG to Audio Injector mic input: 1=MIC_SIG, 2=GND |

The Pi's own micro-B OTG port connects to J5 with a short micro-B-to-USB-A
cable. J5 is the service/ESD-protected panel port; it is not a second USB
host controller.

## ICs and discretes

| Ref | Part | Notes |
|---|---|---|
| U1 | 6N138 | MIDI-IN optocoupler |
| U2 | USBLC6-2SC6 | ESD on J5 D+/D− |
| R1 | 33 Ω 0805 | MIDI OUT series |
| R2 | 220 Ω 0805 | MIDI IN LED series |
| R3 | 10 kΩ 0805 | MIDI RX pull-up to +5V |
| R4, R5 | 4.7 kΩ 0805 | I²C pull-ups — fit only if the HAT/power board do not already provide them |
| C1–C4 | 100 nF 0805 | decoupling U1, U2, display, Pi 5V |
| SW1 | 6 mm tact, THT | shutdown button (GPIO22 to GND) |

Do not duplicate I²C pull-ups blindly: the Audio Injector Zero and the power
board may both provide them. Populate R4/R5 only after measuring the bus.

## Power

- Input: 5V from the power board, 2A continuous / 3A peak.
- Pi Zero 2W + Audio Injector + display + panel: design for 5V/1.5A average,
  2.5A peak.
- The carrier has no battery charging or boost conversion.
- J5 VBUS is tied to the carrier +5V rail so the panel Pico is powered from the
  power board, not from the Pi's USB port.
- Add a 100 nF local decoupling capacitor at J6 and at the display header.

## Audio

- Audio Injector Zero (WM8731) is the reference codec; it stacks on the Pi's
  40-pin male header.
- The carrier routes I²S and I²C1 to the Pi header for the HAT.
- The mic capsule connects to J10; J11 carries the same signal to the HAT's
mic/line input. Two connectors keep the net testable and serviceable.
- The HAT's 3.5 mm line out/in jacks must align with the NSR-1 rear I/O
  opening. Verify the real board's jack positions before freezing the
  enclosure cutout.
- A future non-Pi carrier must solve audio separately; it cannot assume the
  Audio Injector Zero.

## Bring-up checklist

1. Power only: 5V at J6, 3.3V at the display header, no backfeed into the
   power board.
2. Pi boots from microSD with the display and codec overlays; no panel
   required for first boot.
3. Display: SPI0 at 32 MHz, 480×320, correct orientation and backlight.
4. Audio: WM8731 probes at I²C 0x1a; playback and mic capture work; ALSA
   mixer controls are visible.
5. DIN MIDI: loopback in/out at 31250 baud; UART console disabled.
6. Panel: J5 enumerates the Pico as `groovebox-panel`; keys/LEDs work.
7. Shutdown button on GPIO22 triggers a clean shutdown (not GPIO3).
8. Fuel gauge appears on I²C; LOW_BATT asserts before the pack cutoff.
9. Thermal soak with the Audio Injector stack installed; check the Pi and
   HAT clearances.

## BOM split

- **Fab SMD:** 6N138, USBLC6-2SC6, MIDI resistors, I²C pull-up positions,
  decoupling.
- **Hand-solder THT:** Pi 2×20 female header, display right-angle header,
  USB-A, 2× DIN-5, XH connectors, tact switch, mic pigtail.
- **Modules:** Pi Zero 2W, Audio Injector Zero, ILI9488 module, Pico (on the
  control board), 5000 mAh pack + power board.

## Deliberate exclusions

- No battery charger, boost converter, or battery connector.
- No universal SBC socket or adapter.
- No second USB host controller.
- No codec other than the Audio Injector Zero in rev A.

## Future carriers

Radxa Zero 3W and Orange Pi Zero 3W will each get a separate carrier with the
same J6/J7/J8 power/telemetry contract and the same panel USB role. Their
differences in USB host, audio, display overlays, mounting holes, and power
input are documented in their own specs, not hidden behind an adapter.

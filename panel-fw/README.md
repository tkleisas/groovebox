# groovebox-panel firmware

Panel module firmware for the groovebox control surface: a Raspberry Pi Pico
scanning 55 single-ended keys plus 8 encoder pushes on 4x MCP23017 expanders,
2x ADS1115 analog inputs (CUT/RES pots, slider, joystick X/Y), 8 encoders
(5-8 = ADSR) and 32 LEDs driven by an HT16K33 (LED driver only,
no key matrix), presenting everything as a stock USB-MIDI device named
`groovebox-panel` — protocol v1 per [docs/panel-protocol.md](../docs/panel-protocol.md)
(keys as Note On/Off ch1, pots/slider/joystick as CC, encoders relative CC,
LEDs and the host velocity register on ch2, GRV SysEx 0x7D configuration).

## Layout

    CMakeLists.txt          build definition (pico-sdk 1.5.x)
    build.ps1               Windows build helper
    src/board_config.h      pins, I2C addresses, expander key maps — PCB-facing constants
    src/main.c              superloop + 1 ms tick
    src/usb_descriptors.c   "groovebox-panel" MIDI device (VID 0x2E8A, PID 0x4720)
    src/midi.c/.h           TX helpers + RX parser (ch2 notes/CC27/SysEx)
    src/keys.c/.h           55 keys + 8 encoder pushes on 4x MCP23017, debounced scan
    src/ads1115.c/.h        2 chips round-robin, ~125 Hz/channel, hysteresis
    src/encoder.c/.h        8x quadrature IRQ (pushes ride the key scan)
    src/leds.c/.h           32-LED RAM mapping (HT16K33 LED driver) + re-assert
    src/config.c/.h         velocity source routing, GRV SysEx, flash persistence

## Building (Windows)

One-time setup (no admin needed, everything user-scoped under
%USERPROFILE%\pico-tools):

    # 1. pico-sdk 1.5.1 with submodules (last 1.x release; there is no 1.5.2 tag)
    git clone --depth 1 --branch 1.5.1 --recurse-submodules `
        --shallow-submodules https://github.com/raspberrypi/pico-sdk `
        "$env:USERPROFILE\pico-tools\pico-sdk"

    # 2. Arm GNU toolchain 13.2.Rel1 (mingw-w64-i686 zip)
    #    -> extract into %USERPROFILE%\pico-tools
    # 3. ninja-win.zip -> extract into %USERPROFILE%\pico-tools\ninja

Then:

    .\build.ps1 -Configure   # first run
    .\build.ps1              # incremental -> build\panel.uf2

Linux/macOS: standard pico-sdk flow — `cmake -B build -G Ninja` with
arm-none-eabi-gcc on PATH; no Python needed (no PIO/WL features used).

## Flashing

Hold BOOTSEL, plug the Pico, drag `build\panel.uf2` onto the RPI-RP2 drive.
SWD (Pico H 3-pin) works too.

## Testing without the Pi

The panel is a plain class-compliant USB-MIDI device: plug it into any PC and
watch events in a MIDI monitor (amidi / MIDI-OX / web MIDI). Key presses emit
Note On ch1 (36 + key index, velocity from the active source); pots emit
CC20/21 (CUT/RES), the slider CC26, joystick CC1/bend; encoders CC16..23
relative (5-8 = ADSR) and CC32..39 push. Drive LEDs: Note On/Off ch2, note
0..31 (0..15 step row, 16..31 white keys). Configuration SysEx:

    set velocity source = fixed 100   F0 7D 47 52 56 01 08 64 F7
    query capabilities                F0 7D 47 52 56 02 F7
    persistence on                    F0 7D 47 52 56 04 01 F7
    factory reset                     F0 7D 47 52 56 7F F7

## Bring-up knobs (hardware-verification points)

- HT16K33 init sequence in `leds.c` (oscillator on 0x21, brightness 0xE0|8,
  display on 0x81): the chip boots in standby — if no LED lights on first
  boot, verify these writes land with an I2C monitor.
- LED RAM mapping in `leds.c` (LED i -> RAM[i & 7] bit (i >> 3)) must match
  the keyboard PCB routing (ROW anode / COM cathode pairing).
- `KEY_CHIPS` maps (board_config.h): MCP23017 pin -> panel key index; route
  the keyboard PCB to match these tables (or update the tables).
- Encoders: if one detent advances two steps per click, flip the qdec table
  orientation in `encoder.c`.

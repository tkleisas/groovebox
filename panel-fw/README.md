# groovebox-panel firmware

Panel module firmware for the groovebox control surface: a Raspberry Pi Pico
scanning the 39-key HT16K33 matrix, 3x ADS1115 analog inputs, 4 push-encoders
and 16 step LEDs, presenting everything as a stock USB-MIDI device named
`groovebox-panel` — protocol v1 per [docs/panel-protocol.md](../docs/panel-protocol.md)
(keys as Note On/Off ch1, pots/slider/joystick as CC, encoders relative CC,
LEDs and the host velocity register on ch2, GRV SysEx 0x7D configuration).

## Layout

    CMakeLists.txt          build definition (pico-sdk 1.5.x)
    build.ps1               Windows build helper
    src/board_config.h      pins, I2C addresses, keymap — PCB-facing constants
    src/main.c              superloop + 1 ms tick
    src/usb_descriptors.c   "groovebox-panel" MIDI device (VID 0x2E8A, PID 0x4720)
    src/midi.c/.h           TX helpers + RX parser (ch2 notes/CC27/SysEx)
    src/matrix.c/.h         HT16K33 13x3 key scan, 3-sample debounce
    src/ads1115.c/.h        3 chips round-robin, ~83 Hz/channel, hysteresis
    src/encoder.c/.h        quadrature IRQ + 30 ms push debounce
    src/leds.c/.h           16-LED RAM mapping + periodic re-assert
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
CC20..25, the slider CC26, joystick CC1/bend; encoders CC16..19 relative and
CC32..35 push. Drive LEDs: Note On/Off ch2, note 0..15. Configuration SysEx:

    set velocity source = fixed 100   F0 7D 47 52 56 01 08 64 F7
    query capabilities                F0 7D 47 52 56 02 F7
    persistence on                    F0 7D 47 52 56 04 01 F7
    factory reset                     F0 7D 47 52 56 7F F7

## Bring-up knobs (hardware-verification points)

- `HT16K33_KEY_BYTES` (board_config.h): key RAM read length — datasheet says
  6 bytes for the 13x3 matrix; verify with an I2C monitor on first boot.
- LED RAM mapping in `leds.c` (LED i -> RAM[i & 7] bit (i >> 3)) must match
  the keyboard PCB routing (ROW anode / COM cathode pairing).
- `KEYMAP` (board_config.h): matrix cell -> panel key index; route the
  keyboard PCB to match this identity table (or update the table).
- Encoders: if one detent advances two steps per click, flip the qdec table
  orientation in `encoder.c`.

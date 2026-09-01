# groovebox — ВОКОИТЕР / ΟΡΓΑΝΟ

A standalone, battery-powered groovebox: a **Raspberry Pi Zero 2W** brain
running the [yawn](https://github.com/tkleisas/yawn) audio engine, with a
**Raspberry Pi Pico** panel module presenting the whole control surface to
the Pi as a plain USB-MIDI device. Screen, stepsequencer LEDs, encoders,
mic — everything else hangs off those two chips.

Design language: Soviet-cosmonaut mission control per
[bokontep.gr](https://www.bokontep.gr) — deep-red faceplate, cream/black
keys, bilingual Greek/Cyrillic UI strings, terminal-green telemetry.

## Two designs, one repo

| | NSR-1 (ΟΡΓΑΝΟ 1) | NSR-2 |
|---|---|---|
| Concept | full-size panel, 400 × 150 mm | compact, 180 × 210 mm faceplate PCB |
| Keys | 27 MX keys (piano row) + 12 function | 45 Choc keys (4×8 grid) + 13 function |
| Control | 4 encoders, 6 pots, data slider, thumbstick | 4 encoders, thumbstick, crossfader |
| Status | design + hardware spec rev A, KiCad skeletons | schematic-complete, **0 ERC errors**, footprints assigned |
| Docs | [`docs/design.md`](docs/design.md) | [`docs/nsr2-design.md`](docs/nsr2-design.md) + [`nsr2/README.md`](nsr2/README.md) |

## Repository layout

| Path | What it is |
|---|---|
| `docs/` | design documents: NSR-1 `design.md`, panel ↔ brain protocol (`panel-protocol.md`), NSR-2 `nsr2-design.md`, mockups |
| `emulator/` | SDL3 device emulator — the groovebox app runs here first, linked natively against yawn; includes the monochrome widget library (26 widgets) |
| `panel-fw/` | Raspberry Pi Pico firmware (TinyUSB USB-MIDI panel controller, protocol v1 + GRV SysEx config) |
| `hardware/` | NSR-1 hardware: netlist spec + KiCad projects (keyboard & control boards) |
| `os-image/` | Raspberry Pi OS Lite customization — boots straight into the app (~10 s to sound) |
| `tools/` | mockup generators (panel, UI), rgb565 conversion |
| `yawn/` | the audio engine (git submodule) |
| `nsr2/` | the NSR-2 compact variant: netlist spec, generated KiCad schematics, footprint library, enclosure spec, mockup |

## Architecture

```
  Pi Zero 2W (brain)                      Pico (panel module)
  ├ yawn engine (I2S codec)      ◄─USB─►  ├ 45 keys   (3× MCP23017 / MCP23017+MX)
  ├ fbdev UI → 480×320 SPI TFT            ├ 32 LEDs   (HT16K33A)
  ├ DIN MIDI in/out (UART)                ├ 4 encoders (GPIO)
  └ mic (codec input)                     └ analog    (ADS1115)
```

The panel is deliberately dumb — it reports physical events and accepts
routing SysEx; **all musical semantics live in the brain app**. The same
app binary runs on the PC (SDL3 emulator with keyboard/mouse standing in
for hardware) and on the Pi (fbdev + panel over USB-MIDI) through a HAL,
which is how everything is developed before hardware exists. See
[`docs/panel-protocol.md`](docs/panel-protocol.md) for the wire protocol.

## Building

| Target | How | Needs |
|---|---|---|
| Emulator | CMake in `emulator/` (binary `groovebox_sim`; `--widgets` = widget showcase) | SDL3, a yawn checkout (`git submodule update --init`) |
| Panel firmware | `panel-fw/build.ps1` or CMake with the Pico SDK (see `panel-fw/README.md`) | Pico SDK + toolchain |
| OS image | Raspberry Pi OS Lite + `os-image/customize/` (first-run script, systemd unit) | a Pi / imager |
| Hardware | KiCad 10; NSR-2 schematics are **generated** — see [`nsr2/README.md`](nsr2/README.md) before touching them | KiCad 10.0+ |

## Status & roadmap

NSR-2 is the active design: schematics generated and ERC-clean, board
outlines drawn (180 × 210 faceplate with screen window, 100 × 80 brain
carrier), 3D-printed enclosure specified. Next: PCB layout, firmware
`PANEL_NSR2` variant, emulator `--panel=nsr2` profile. NSR-1 remains the
reference for the audio engine bring-up and the panel protocol.

## Conventions

- Commit messages: `area: summary` (`panel-fw:`, `os-image:`, `nsr2:`, `docs:`).
- Generated files are marked as such in their directories — read
  [`nsr2/README.md`](nsr2/README.md) before hand-editing anything in
  `nsr2/hardware/`.
- Per-device credentials never enter the repo (see `os-image/customize/`).

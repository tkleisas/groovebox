# ВОКОИТЕР – ΟΡΓΑΝΟ 1

A standalone, battery-powered groovebox. Raspberry Pi Zero 2W brain, a
Pico-based control panel speaking USB-MIDI, a real mechanical (MX) keyboard,
and the [yawn](https://github.com/tkleisas/yawn) audio engine as the DSP core.
Soviet-cosmonaut mission-control aesthetic; trilingual UI (Latin / Greek /
Cyrillic) in a blocky 240×160 monochrome style.

**Status: software-first development.** The full instrument already runs in a
PC emulator with a virtual front panel; hardware milestones follow.
See [Milestones](#milestones) for where things stand.

![virtual front panel](docs/panel_sim.png)

## What it does

- **4 tracks** × **1–256 step patterns** (default 16, 16-step paged window).
  Per-step: on/off, velocity (data slider), accent (black keys), pitch and
  gate (hold-step + encoders, synth track).
- **Play mode**: the keyboard row becomes a 2-octave chromatic keyboard
  (16 white + 11 black), velocity from the slider, joystick = pitch bend /
  mod. REC-arm + play records quantized notes into the pattern.
- **Sound**: yawn engine instruments (SubtractiveSynth, DrumRack, FM,
  Wavetable, Granular, …) + one insert FX slot per track (29 effect types).
- **Controls**: 6 dedicated pots (filter cutoff/resonance + amp ADSR — strict
  one-parameter-one-control rule), 4 pageable encoders with push, data
  slider, 2-axis spring-return thumbstick, transport, MODE, </>, 4 soft keys,
  dual SHIFT keys at the keyboard row ends (one-hand combos).
- **UI**: Elektron-style monochrome, 3 themes (mono / red / green phosphor),
  ПОЕХАЛИ! boot splash, full widget library (sequencer, mixer, waveform,
  ADSR, FM-algo, spectrum, phase pie, tuner, file browser, virtual keyboard…).
- **Sync/IO**: DIN MIDI in/out, Ableton Link (engine), USB-MIDI on OTG,
  integrated mic + line in for sampling (WM8731 codec).

## Hardware (rev A)

| | |
|---|---|
| Brain | Raspberry Pi Zero 2W (≥5 h battery is a hard requirement) |
| Panel controller | Raspberry Pi Pico — presents as USB-MIDI device `groovebox-panel` |
| Audio codec | Audio Injector Zero (WM8731: stereo out + mic/line in). Bench rig: CJMCU-1334 (UDA1334A, playback only) |
| Display | 4.0" SPI TFT 480×320 (ILI9488); UI rendered at 240×160, 2× integer scale |
| Keys | 39 MX-style switches (Gateron Red / Silent Red), hot-swap sockets, per-key LEDs (whites), 19.05 mm pitch |
| Panel I/O | 3× MCP23017 (39 single-ended keys), HT16K33 (16 LEDs), 3× ADS1115 (6 pots + slider + joystick) |
| Power | LiPo + Adafruit PowerBoost 1000C |
| Panel size | 400×150 mm, deep red |

Full hardware design, GPIO maps and BOM: [docs/design.md](docs/design.md).
Panel/brain protocol (USB-MIDI + SysEx config):
[docs/panel-protocol.md](docs/panel-protocol.md).

## Architecture

```
groovebox app (pages, sequencer, controller logic)
      │  HAL: display / keys / encoders / analogs / LEDs
      ├── Sim backend (PC): SDL3 virtual front panel
      └── Pi backend (later): fbdev SPI + USB-MIDI panel
              │
        yawn engine (yawn_core static lib: instruments, FX,
        mixer, transport — PortAudio, lock-free command queue)
```

The **emulator is the development platform**: the same app binary that will
run on the Pi runs on PC with the real engine (PortAudio/WASAPI), a
geometry-driven virtual panel (mouse-interactive keys/pots/slider/joystick),
and deterministic scripted test modes.

## Build & run (Windows, MSVC)

```bash
CMAKE="/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" -B emulator/build -S emulator -G "Visual Studio 17 2022" -A x64
"$CMAKE" --build emulator/build --target groovebox_sim --config Debug -- -m
emulator/build/Debug/groovebox_sim.exe            # interactive sim
```

The yawn engine is built lean (VST3 / NAM / Link / 3D / ONNX / video OFF).

### Interactive controls

- **Mouse** (panel view): click keys/buttons; drag pots & slider; 2D-drag
  joystick (springs back); wheel over encoders; click encoder = push.
- **PC keyboard**: Z-row + Q-row = piano whites (with standard black-key
  letters/digits), Space = play/stop, Enter = rec, Tab = MODE,
  arrows = </>, F1–F4 = soft keys S1–S4, Shift = SHIFT, F5–F10 + `-`/`=` =
  pots, `[`/`]` = slider, F11 = panel/screen view, F12 = theme, ESC = quit.
- **Track select**: S3/S4 (TRK-/TRK+) or Shift + white keys 1–4;
  Shift + white 5–8 = mute.

### Scripted test / asset modes

```
groovebox_sim --smoke       engine boot + notes self-test
groovebox_sim --uitest      UI interaction assertions (8/8)
groovebox_sim --seqtest     sequencer scheduling assertions (6/6)
groovebox_sim --longtest    1–256 step pattern assertions (9/9)
groovebox_sim --paramtest   pot pickup / encoder ownership / FX (11/11)
groovebox_sim --sampletest  mic capture → trim → assign → browser (8/8)
groovebox_sim --panelprobe  synthetic mouse → HAL events (3/3)
groovebox_sim --widgets     widget showcase → docs/widgets.png
groovebox_sim --fontchart   font chart → docs/fontchart.png
groovebox_sim --themes      3-theme chart → docs/themes.png
groovebox_sim --paneldump   panel render → docs/panel_sim.png
groovebox_sim --splashdump  boot splash → docs/bootsplash.png
```

## Repository layout

```
docs/         design.md, panel-protocol.md, mockups & generated charts
emulator/     the groovebox app + sim backend (CMake target groovebox_sim)
  src/        hal.h, SimBackend, PanelView, ui, widgets, font5x7, pattern…
tools/        mockup generators (panel, UI style)
yawn/         the audio engine (clone of github.com/tkleisas/yawn)
os-image/     Raspberry Pi OS image + first-boot customization staging
```

## Milestones

- **M1 done** — emulator skeleton: engine headless on PC, HAL, sim window,
  font (Latin/Greek/Cyrillic + symbols), 26 widgets.
- **M2 in progress** — full instrument in the emulator:
  - done: page framework, live step sequencer, play mode, long patterns,
    per-step pitch/gate, virtual front panel, themes, boot splash,
    pot pickup, encoder parameter pages, FX page, toasts (M2a/M2b)
  - done: SAMPLE page (mic capture → trim/gain → WAV → DrumRack pad),
    LOAD page (file browser), SETTINGS page (theme/velocity-source/LED
    stub, persisted to settings.json), confirm dialogs (M2c)
  - next: Sampler instrument for T1, pattern save/load
- **M3** — cross-compile to Pi, Pi HAL backend, CPU profiling (Zero 2W gate).
- **M4** — bench rig: codec, Pico panel firmware (TinyUSB MIDI, this
  protocol), display.
- **M5** — battery, enclosure, custom PCB if it earns one.

## Design notes worth reading

- `docs/design.md` — the living design document (hardware, UI spec,
  aesthetic direction, core decision rationale).
- `docs/panel-protocol.md` — panel↔brain USB-MIDI contract incl. the SysEx
  configuration layer (velocity source routing, capability handshake).
- Panel/brain split: the panel is dumb by design — it reports physical
  events; all musical semantics live in the brain.

ПОЕХАЛИ!

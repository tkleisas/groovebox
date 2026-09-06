# Groovebox — Hardware & Software Design

A standalone, battery-powered groovebox built on Raspberry Pi Zero 2W,
using the [yawn](https://github.com/tkleisas/yawn) audio engine as the DSP core.

**Identity: ВОКОИТЕР – ΟΡΓΑΝΟ 1.** Deep-red faceplate (Nord/Elektron red
family), white/black MX keys, near-black shift keys, off-white panel print;
brand badge center-panel in the UI's own multilingual 5×7 font.

Aesthetic direction (per bokontep.gr): Soviet-cosmonaut mission control —
boot splash "ПОЕХАЛИ!", mission-register terminology (project = ΑΠΟΣΤΟΛΗ),
bilingual Greek/Cyrillic UI strings, telemetry-style status pages.
Three screen themes: MONO (white/gray on near-black), RED (bright/light red —
matches the faceplate), GREEN (bright/light green phosphor).

## Core decision

**Pi Zero 2W, fixed by a hard requirement: ≥5 h battery life.**

Considered alternatives:
- Pi 4B: ~3–4× CPU headroom and up to 8 GB RAM, but 3–5 W draw cuts
  battery life to ~2–3 h and it wants a heatsink. Rejected on the battery
  requirement alone.
- CM4: right answer only for a custom carrier PCB / production run.
  Revisit if the box ever goes beyond a one-off.
- Pi 5: power draw and active cooling make it wrong for portable use.

Consequences:
- The core is a swappable module — everything hangs off the standard 40-pin
  header, so a Pi 4B could drop in later with only battery/heatsink changes.
- Milestone 1 must include profiling on the Zero 2W: target workload =
  4 tracks (drum rack + synth + 2–3 FX per track) at 128 frames / 48 kHz.
  Ship criterion: sustained CPU ≤ ~60–65% with no xruns. If it fails, the
  fallback is reducing scope (fewer tracks / lighter FX), not switching
  cores, since battery life is the hard constraint.

## Concept

- Mixed sample + synth instrument: yawn's session grid, instruments
  (Sampler, Drum Rack, Subtractive, FM, Wavetable, ...) and effects,
  driven by a physical control surface.
- 4 tracks to start (session-style clip launching), piano-style 16-key step
  row, dedicated filter/ADSR pots, one data slider (default: velocity).
- **Patterns: 1–256 steps, global length, default 16**, grid shows a 16-step
  window (follow-playhead + manual paging via shift+</>). No formal time
  signature — length in steps IS the meter (12 = 3/4, 14 = 7/8); the clock
  counts 16-step bars. Per-step: on/off, velocity (slider at entry),
  accent (black keys), pitch + gate (hold-step + encoders, synth track).
- No desktop OS involvement: Raspberry Pi OS Lite, systemd unit boots
  straight into the app (~10 s boot-to-sound).

## Bill of materials (~$70–80)

| Part | Choice | ~Cost |
|---|---|---|
| Brain | Raspberry Pi Zero 2W | $15 |
| Panel controller | Raspberry Pi Pico (RP2040) — USB-MIDI bridge for all panel I/O | $5 |
| Audio codec (final) | Audio Injector Zero (WM8731: stereo out + mic/line in, stacks on header) | $18 |
| Audio out (bench rig) | CJMCU-1334 = **UDA1334A** I2S DAC, already on hand + proven wiring (`hifiberry-dac` overlay). Playback only — no ADC | $0 |
| Mic | Electret capsule behind top-panel hole → codec mic input | $2 |
| Display | 4.0" SPI TFT 480×320 (ILI9488) — bigger pixels for eyesight; same driver, bus and 240×160@2× UI as 3.5" | $18 |
| MX keyboard kit | 55 MX-style switches (Gateron Red / Silent Red, linear ~45 gf) + Kailh hot-swap sockets + keycap set (16 white, 11 black, 16 step row, colored function keys) | $65 |
| Button/LED driver | HT16K33 (I2C, drives 32 LEDs: 16 step row + 16 white keys) | $3 |
| Key input expanders | 4× MCP23017 (I2C @ 0x20–0x23, 55 single-ended key inputs) | $8 |
| Analog inputs | 3× ADS1115 16-bit ADC (I2C, addresses 0x48 / 0x49 / 0x4a) | $9 |
| Pitch/mod | 2-axis spring-return thumbstick (X = pitch bend, Y = mod) | $4 |
| Pots | 6× 10k linear: cutoff, resonance, A, D, S, R | $7 |
| Data slider | 60 mm slide pot (velocity default; live filter/FX sweeps) | $3 |
| Encoders | 4× rotary encoder with push | $5 |
| DIN MIDI | 2× 5-pin DIN sockets (in+out), 6N138 optocoupler, resistors | $5 |
| Power | LiPo 2000–3000 mAh + Adafruit PowerBoost 1000C | $20 |
| Misc | Power switch, perfboard, headers, enclosure | $5 |

## Control surface

- **Dedicated step row (rev B)**: 16 MX switches *under* the keyboard, one
  per step of the current 16-step window, each with an LED. The step row is
  always the sequencer; the piano row is always playable — no mode split.
  Gestures: tap step = toggle; **hold step + tap piano key = set that
  step's pitch** (p-lock); hold step + encoder = gate; shift+step = accent;
  shift+step on tracks 1–4 area = track select/mute.
- Piano row: 16 white + 11 offset black keys in piano pattern,
  **MX-style switches at 19.05 mm pitch** (Gateron Red / Silent Red, linear
  ~45 gf) in **hot-swap sockets**, per-key LEDs (whites show held notes /
  scale guides), always playable — 2-octave chromatic from C4, velocity
  from the slider. MODE toggles keyboard scale-lock (chromatic ↔ scale).
  External keybeds via DIN MIDI in (USB-MIDI on OTG also possible).
- Transport: play, stop, rec.
- **Two SHIFT keys** (1u MX keys), same logical modifier, at the two ends of
  the keyboard row — thumb/pinky pins shift while the other fingers hit the
  target, so every shift combo is a one-hand move:
  - shift + step row → accent / track select / mute
  - shift + encoder → fine adjust / reset to default
- **MODE** key: toggles keyboard scale-lock (chromatic ↔ scale-locked);
  the sequencer and keyboard are always both live — no mode split.
- **< / >** keys: previous/next event, page, or preset.
- **4 soft menu keys** under the screen: functions shown as on-screen
  labels directly above each key (MPC-style menus, no legend needed).
- 4 encoders with push (pageable parameters).
- **8 encoders total (rev B2)**: 4 pageable + 4 dedicated amp-ENV
  A/D/S/R (relative control — no pickup; envelope follows the track).
  Encoder push-buttons live on spare MCP23017 inputs (quadrature stays on
  16 direct GPIO).
- 2 pots: filter cutoff + resonance (absolute performance controls).
- 1 data slider (velocity entry; mappable).
- 2-axis spring-return thumbstick left of the keyboard: X = pitch bend
  (centers on release), Y = mod, assignable (vibrato / filter / FX depth).
- LEDs mirror pattern + playhead even when the screen shows another page.

Front-panel layout: `docs/panel_mockup.png` (to scale, **400×170 mm panel** —
desktop-instrument class, Push 2 width; regenerate with
`python tools/mockup_panel.py`). Screen-centric design: screen top-left with
the 4 soft menu keys in a column beside it (menu labels drawn on screen next
to each key) and the 4 encoders in a row directly beneath it (parameter
labels + values on screen above each encoder). CUT/RES/A/D/S/R pots
top-middle, transport + MODE/</> cluster top-right, pitch/mod thumbstick
bottom-left, full-width MX piano keyboard (16 white @ 19.05 mm + 11 offset
black, always playable) above a **dedicated 16-key step row** (always the
sequencer, per-step LEDs), 1u SHIFT keys at the piano row ends, horizontal
60 mm data slider above the keyboard's right end. The center holds the
ВОКОИТЕР – ΟΡΓΑΝΟ 1 badge.

## GPIO / I2C allocation

**Pi Zero 2W (40-pin header)** — Pi keeps only display, codec, and DIN MIDI:

| Bus | Pins | Device |
|---|---|---|
| I2S | GPIO 18 (BCK), 19 (LRCK), 21 (DOUT), 20 (DIN) | WM8731 codec (Audio Injector Zero: playback + mic capture) |
| SPI0 | GPIO 8 (CE0), 10 (MOSI), 11 (SCLK) + DC/RST/BL on GPIO 24/25/27 | ILI9488 display |
| I2C1 | GPIO 2 (SDA), 3 (SCL) | WM8731 ctrl @ 0x1a |
| UART | GPIO 14 (TXD), 15 (RXD) | **DIN MIDI out / in** (31250 baud) |
| USB OTG | micro-USB "USB" port | **Panel module (Pico, USB-MIDI device)** |

**Pico (panel module)** — everything else moves here:

| Peripheral | Connection | Role |
|---|---|---|
| HT16K33 | I2C @ 0x70 | 32 LEDs: 16 step-row + 16 white-key (driver only) |
| 4× MCP23017 | I2C @ 0x20 / 0x21 / 0x22 / 0x23 | 55 keys + 8 encoder push-buttons, single-ended (64 available: 63 used) |
| 2× ADS1115 | I2C @ 0x48 / 0x49 | 5 analog inputs: 2 pots (CUT/RES) + slider + joystick (X/Y) |
| 8 encoders | direct GPIO (16 pins, quadrature A/B only) | encoders 1–4 pageable, 5–8 = ADSR |

Notes:
- All 55 keys (16 white + 11 black piano + **16 step row (rev B)** +
  3 transport + 4 soft + MODE + < + > + 2 SHIFT) are **single-ended inputs
  on four MCP23017 I²C expanders** — one uniform scan path, no matrix and
  no per-key diodes (rev A deviation from the original HT16K33-matrix plan:
  the function keys are physically distributed across the panel, and
  expanders keep the board-to-board link at 4 wires with every key
  identical in hardware and firmware). The HT16K33 is solely an LED driver:
  16 step-row LEDs + 16 white-key LEDs.
- Integrated mic: electret capsule wired to the WM8731 mic input (codec bias
  + PGA gain via ALSA mixer). Uses the codec's capture path, no extra bus or
  pins. The WM8731 runs in master mode off an onboard crystal since the Pi's
  I2S has no MCLK output; the stock `audioinjector-wm8731-audio` overlay
  drives it (probes WM8731 at I2C 0x1a).
- Prototyping uses the on-hand CJMCU-1334 (**UDA1334A**, playback-only DAC,
  `hifiberry-dac` overlay — the same recipe as the 2019 intsynth card).
  The mic path arrives with the Audio Injector Zero; UDA1334A + separate
  I2S mic was rejected (custom overlay work, two boards instead of one).
- **DIN MIDI in/out is a core feature.** MIDI out is a TX pin + resistor
  network; MIDI in needs the 6N138 optocoupler (never feed a bare UART).
- On the Zero 2W the PL011 UART is claimed by Bluetooth by default — remap
  it back to GPIO 14/15 via the `pi3-disable-bt` (or `miniuart-bt`) device
  tree overlay, and disable the serial console.
- Software: `ttymidi` bridges `/dev/ttyAMA0` into the ALSA sequencer, so
  yawn's existing RtMidi stack sees DIN MIDI as just another MIDI port —
  clock sync in/out, note in, and MIDI Learn all come along for free.

## Software architecture

**Device emulator first.** All groovebox software is developed against a
hardware abstraction layer (HAL) so the same app binary runs on the PC
(sim backend: SDL3 window showing the exact 240×160 framebuffer at 2×,
keyboard/mouse standing in for keys/encoders/pots) and on the Pi
(fbdev SPI display + **panel module over USB-MIDI**). The PC emulator links the
real yawn engine natively (PortAudio/WASAPI) — UI, page flow, controller
mapping and sequencer logic are all proven before hardware exists.
Milestones: M1 emulator skeleton + headless yawn on PC → M2 full UI +
controller mapping in emulator → M3 cross-compile to Pi, Pi HAL backend,
CPU profiling (Zero 2W viability gate) → M4 bench rig → M5 battery +
enclosure.

**Panel module (Raspberry Pi Pico).** All panel I/O — 39 single-ended keys
on 3× MCP23017, 16 in-switch LEDs on HT16K33 (driver only), 4 encoders,
3× ADS1115 analog — is handled by a Pico (RP2040, native USB device via
TinyUSB) which presents itself to the Pi as a **USB-MIDI controller**. Keys
→ note on/off, pots/slider/joystick → CCs, encoders → relative CCs; LEDs
are driven by MIDI back to the panel; a SysEx layer (protocol doc)
configures velocity-source routing with capability handshake and optional
flash persistence. The Pico is I2C bus master for all expanders (its 26
GPIO can't absorb keys + LEDs + encoders directly). Benefits: no custom
I/O drivers on the Pi (yawn's RtMidi sees a stock USB-MIDI device),
jitter-free scanning, and a panel that is independently testable on any
MIDI host (PC/DAW) and swappable as a unit. The Pi's USB-OTG port goes to
the panel; external keyboards use DIN MIDI in. Firmware: `panel-fw/`
(builds to `panel.uf2` via pico-sdk; see `panel-fw/README.md`).

```
┌─ Input thread (libgpiod: encoders | HT16K33: buttons | ADS1115×2: pots/slider)
│        │  translates to CC / note / shift-layer events
│        ▼
│  "GROOVEBOX" controller backend ──► yawn engine
│        │  (PortAudio/ALSA, 64–128 frame buffer @ 48 kHz,
│        │   existing instruments / FX / session grid,
│        │   Lua 5.4 controller script)
│        ▼  state out: params, playhead, meters, LED state
├─ LED update (HT16K33)
└─ fbdev UI thread → ILI9488, ~15–20 fps, partial redraws
   (C++ + stb_truetype; no GPU — see constraints below)
```

Constraints / decisions:
- yawn's fw2 UI (SDL3 + OpenGL 3.3) cannot run on the Zero 2W
  (VideoCore IV = GL 2.1 / GLES 2.0 at best — fw2's shaders won't compile,
  regardless of the scanout path). The groovebox UI is a new, minimal
  software renderer writing to the Linux framebuffer (fbdev/`fb_ili9488`).
  GPU rendering (GLES2 → FBO → glReadPixels → SPI copy) remains an option
  for future shader-based visualizers (oscilloscope/spectrum), not for
  the core UI.
- **UI style: Elektron-inspired, blocky, near-monochrome** (mockup:
  `docs/ui_mockup.png`, regenerate with `python tools/mockup_ui.py`).
  Rendered at an effective 240×160 and integer-doubled to the display's
  native 480×320 during the SPI blit — 4× less raster work, large glyphs.
  Design language: inverted header bar (page title + context), parameter
  rows (dim name / bright value / thin bar), inverse-video selection
  cursor, footer strip showing the 4 encoders' current assignments,
  white/gray on near-black with at most one accent color (rec-arm,
  playhead, clip state). Partial-row updates to save SPI bandwidth.
- **Widget library** (`emulator/src/widgets.h`, 26 widgets): PhasePie,
  Waveform, Slice, ADSR, FM-algo, Spectrum, Sequencer, StepLane, Mixer,
  Wavetable, Knob, Switch, Radio, VirtualKeyboard, EditBox, FileBrowser,
  List, Toast, TransportClock, NumericField, Dialog, Battery, StatusIcons,
  Tuner, PadGrid, TabBar — all monochrome and data-driven.
  **Grid rule: every widget bbox is a multiple of 8 px (placement prefers
  16 px)** — 240×160 = 30×20 cells; widgets tile without pixel negotiation.
  Showcase: `docs/widgets.png` (regenerate with `groovebox_sim --widgets`).
- 512 MB RAM: ONNX / Demucs / Neural-Amp / video features stay OFF for this
  target (existing CMake feature flags).
- Rootfs mostly read-only + a shutdown button so battery pulls can't
  corrupt the SD card.

## Milestones

1. **Engine on the Pi** — headless yawn build (heavy features off), driven by
   a Lua script, audio out via PCM5102A. De-risks everything; pure software.
2. **Bench rig** — breadboard: encoders, a few buttons, display;
   input → controller backend → engine loop working.
3. **fbdev UI** on the 480×320.
4. **Full control surface** — HT16K33 buttons/LEDs, ADS1115 pots/slider.
5. **Battery + enclosure**, custom PCB if more than one unit is wanted.

# groovebox — ВОКОИТЕР / ΟΡΓΑΝΟ

**Mission:** an open hardware and software groovebox that can be serviced
indefinitely. If a subsystem becomes unavailable, an equivalent one can be
substituted without discarding the panel, power system, enclosure, or
software. The reference brain is a **Raspberry Pi Zero 2W** on a replaceable
carrier, running the [yawn](https://github.com/tkleisas/yawn) audio engine;
a **Raspberry Pi Pico** panel module presents the whole control surface to
the brain as a plain USB-MIDI device.

Each SBC gets its own brain carrier and its own validated software profile;
the panel, power board, battery, and enclosure are the common platform. See
[`docs/mission.md`](docs/mission.md) for the mission, support tiers, and the
honest path for future Radxa/Orange Pi carriers.

Design language: Soviet-cosmonaut mission control per
[bokontep.gr](https://www.bokontep.gr) — deep-red faceplate, cream/black
keys, bilingual Greek/Cyrillic UI strings, terminal-green telemetry.

![NSR-1 exploded assembly](docs/renders/nsr1_exploded_labeled.png)

*Exploded NSR-1 rev B2 assembly, rendered in Blender. More views and the
[15-second detail animation](docs/renders/nsr1_animation.mp4):
[`docs/renders/`](docs/renders/).*

## Two designs, one repo

| | NSR-1 (ΟΡΓΑΝΟ 1) | NSR-2 |
|---|---|---|
| Concept | full-size panel, 400 × 170 mm | compact, 180 × 210 mm faceplate PCB |
| Keys | 55 MX keys (piano + 16-step row + function) | 45 Choc keys (4×8 grid) + 13 function |
| Control | 8 encoders, 2 pots, data slider, thumbstick | 4 encoders, thumbstick, crossfader |
| Status | **active target.** rev B2 control/keyboard verified; Pi brain carrier routed; power board routed except four rev A airwires | **deferred.** schematic-complete reference design; not the current build |
| Docs | [`docs/design.md`](docs/design.md) + [`hardware/README.md`](hardware/README.md) | [`docs/nsr2-design.md`](docs/nsr2-design.md) + [`nsr2/README.md`](nsr2/README.md) |

## What it does (emulator, today)

- **4 tracks** × **1–256 step patterns** (default 16, 16-step paged window).
  Per-step: on/off, velocity (data slider), accent (black keys), pitch and
  gate (hold-step + encoders, synth track).
- **Play mode**: the keyboard row becomes a 2-octave chromatic keyboard,
  velocity from the slider, joystick = pitch bend / mod. REC-arm + play
  records quantized notes into the pattern. On **drum tracks**
  (DrumSynth/DrumRack/DrumSlop) the 27 keys instead walk the
  instrument's drum voices left→right (whites and interleaved blacks as
  one physical strip, position modulo voice count — wraps at the end,
  so every key always sounds); toasts show the voice name.
- **Sound**: yawn engine instruments (SubtractiveSynth, DrumRack, FM,
  Wavetable, Granular, …) + one insert FX slot per track (29 effect types).
- **Sampling**: SAMPLE page — record from mic/line-in, trim, normalize,
  assign to a drum pad; LOAD page browses samples.
- **Controls (NSR-1 rev B2)**: 2 dedicated pots (filter cutoff/resonance),
  8 encoders with push (4 pageable + 4 amp-ADSR), data slider, 2-axis
  spring-return thumbstick, transport, MODE, </>, 4 soft keys, dual SHIFT
  keys at the keyboard row ends, dedicated 16-step row.
- **UI**: Elektron-style monochrome, 3 themes (mono / red / green phosphor),
  ПОЕХАЛИ! boot splash, full widget library (sequencer, mixer, waveform,
  ADSR, FM-algo, spectrum, phase pie, tuner, file browser, virtual keyboard…).
- **Sync/IO**: DIN MIDI in/out, Ableton Link (engine), USB-MIDI on OTG,
  integrated mic + line in for sampling (WM8731 codec).

## Hardware (NSR-1, rev B2)

| | |
|---|---|
| Brain | Raspberry Pi Zero 2W on the first SBC-specific carrier; separate carriers planned per SBC |
| Panel controller | Raspberry Pi Pico — presents as USB-MIDI device `groovebox-panel` |
| Audio codec | Audio Injector Zero (WM8731: stereo out + mic/line in) on the Pi carrier. Bench rig: CJMCU-1334 (UDA1334A, playback only) |
| Display | 4.0" SPI TFT 480×320 (ILI9488); UI rendered at 240×160, 2× integer scale |
| Keys | 55 MX-style switches (Gateron Red / Silent Red), hot-swap sockets, 32 LEDs, 19.05 mm pitch |
| Panel I/O | 5× MCP23017 (55 keys + 8 encoder pushes), HT16K33 (32 LEDs), 2× ADS1115 (2 pots + slider + joystick) |
| Power | Separate 1S power/battery board (USB-C charger, power path, 5V/3A boost, fuel gauge); 5000 mAh target |
| Panel size | 400×170 mm, deep red |

Full hardware design, GPIO maps and BOM: [docs/design.md](docs/design.md).
New platform board specs: [Pi Zero 2W brain carrier](hardware/brain/netlist-spec.md)
and [power/battery board](hardware/power/netlist-spec.md).
Panel/brain protocol (USB-MIDI + SysEx config):
[docs/panel-protocol.md](docs/panel-protocol.md).

### NSR-1 PCB views

Bare-board 3D renders, top and bottom, for all four NSR-1 boards. Headers
and connectors carry silkscreen pin numbers; the Pico header also has GPIO
names. The Pi carrier and power board are rev A; the power board still has
four open airwires (two `BAT_NEG`, two `SCL`). Full-size images are in
[`docs/renders/pcb/`](docs/renders/pcb/).

| Board | Top | Bottom |
|---|---|---|
| Control | <a href="docs/renders/pcb/control_top.png"><img src="docs/renders/pcb/control_top.png" width="380"></a> | <a href="docs/renders/pcb/control_bottom.png"><img src="docs/renders/pcb/control_bottom.png" width="380"></a> |
| Keyboard | <a href="docs/renders/pcb/keyboard_top.png"><img src="docs/renders/pcb/keyboard_top.png" width="380"></a> | <a href="docs/renders/pcb/keyboard_bottom.png"><img src="docs/renders/pcb/keyboard_bottom.png" width="380"></a> |
| Pi brain carrier | <a href="docs/renders/pcb/brain_top.png"><img src="docs/renders/pcb/brain_top.png" width="380"></a> | <a href="docs/renders/pcb/brain_bottom.png"><img src="docs/renders/pcb/brain_bottom.png" width="380"></a> |
| Power/battery | <a href="docs/renders/pcb/power_top.png"><img src="docs/renders/pcb/power_top.png" width="380"></a> | <a href="docs/renders/pcb/power_bottom.png"><img src="docs/renders/pcb/power_bottom.png" width="380"></a> |

## ElectroMechanicalDesigner integration

The NSR-1 hardware is imported and validated with
[**ElectroMechanicalDesigner (EMD)**](https://github.com/tkleisas/ElectroMechanicalDesigner):

- `hardware/tools/export_netlist.py` exports KiCad XML netlists.
- `hardware/tools/verify_emd.py` imports both netlists and routed PCBs,
  validates them, and builds a combined **170-part / 522-connection** project.
- The combined project is placed in the 400 × 170 mm panel envelope and its
  package is verified with `emd package verify`.
- Generated projects/packages live in `hardware/emd-packages/` (gitignored);
  regenerate with
  `python3 hardware/tools/verify_emd.py --combined hardware/emd-packages/groovebox.json --package-dir hardware/emd-packages`.

EMD also validates the KiCad ERC/DRC flow, mounting-hole mechanical ports, and
fabrication exports used by this repo.

## Architecture

```
  Pi Zero 2W on brain carrier             Pico (panel module)
  ├ yawn engine (I2S codec)      ◄─USB─►  ├ keys    (5× MCP23017)
  ├ fbdev UI → 480×320 SPI TFT            ├ LEDs    (HT16K33)
  ├ DIN MIDI in/out (UART)                ├ encoders (GPIO)
  └ mic (codec input)                     └ analog  (2× ADS1115)
            ▲ 5V / power signals
            │
  Power/battery board — USB-C charge, 1S pack, gauge, 5V boost
```

The panel is deliberately dumb — it reports physical events and accepts
routing SysEx; **all musical semantics live in the brain app**. The same
app binary runs on the PC (SDL3 emulator with a geometry-driven,
mouse-interactive virtual front panel) and on the Pi (fbdev + panel over
USB-MIDI) through a HAL, which is how everything is developed before
hardware exists. See [`docs/panel-protocol.md`](docs/panel-protocol.md)
for the wire protocol.

## Building

| Target | How | Needs |
|---|---|---|
| Emulator | CMake in `emulator/` (binary `groovebox_sim`) | MSVC or GCC, SDL3, yawn submodule (`git submodule update --init`) |
| Panel firmware | `panel-fw/build.ps1` or CMake with the Pico SDK (see `panel-fw/README.md`) | Pico SDK + toolchain |
| OS image | Raspberry Pi OS Lite + `os-image/customize/` (first-run script, systemd unit) | a Pi / imager |
| Hardware | KiCad 10; NSR-1 and NSR-2 schematics are **generated** — see [`hardware/README.md`](hardware/README.md) and [`nsr2/README.md`](nsr2/README.md) before touching them | KiCad 10.0+ |

Windows/MSVC emulator build:

```bash
CMAKE="/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
"$CMAKE" -B emulator/build -S emulator -G "Visual Studio 17 2022" -A x64
"$CMAKE" --build emulator/build --target groovebox_sim --config Debug -- -m
emulator/build/Debug/groovebox_sim.exe            # interactive sim
```

The yawn engine is built lean (VST3 / NAM / Link / 3D / ONNX / video OFF).

### Pi cross-build (arm64, Docker)

`tools/build-pi-arm64.sh` builds `groovebox_sim` for 64-bit Raspberry Pi
OS (bookworm, aarch64) in a Docker container — no Pi needed on the desk:

```bash
tools/build-pi-arm64.sh     # image + cross-build + provenance + qemu smoke
```

The image (`emulator/pi/Dockerfile`) is Debian bookworm with the
`aarch64-linux-gnu` toolchain and the **arm64** `libasound2-dev` (ALSA is
the one target-arch dependency — RtMidi and PortAudio are compiled from
source by yawn's FetchContent but link against the target's ALSA). The
toolchain file is `emulator/pi/aarch64-toolchain.cmake`; CMake is driven
with `-DGB_PI_BACKEND=ON`, which swaps the SDL3 SimBackend for the
headless PiBackend (fbdev + RtMidi) and drops the SDL3 link. The script
finishes by proving the binary (`readelf -h` → `AArch64`) and
smoke-running it under `qemu-aarch64` (the container has no ALSA device
PortAudio accepts, so the engine can't boot there — the qemu run proves
execution + graceful peripheral degradation; `--profile` numbers need a
Pi with a codec or the Windows sim).

### Pi backend (PiBackend, `GB_PI_BACKEND=ON`)

- **Display**: fbdev — opens `/dev/fb1` (ILI9488 SPI TFT via fbtft
  overlay), falls back to `/dev/fb0`, mmaps the framebuffer and writes
  the 240×160 RGB565 UI frame 2×-doubled (480×320), centered. 16 bpp
  writes through; 32 bpp (XRGB8888) converts per pixel.
- **Panel in**: RtMidi input on the port named `groovebox-panel`,
  decoded per [`docs/panel-protocol.md`](docs/panel-protocol.md)
  (channel 1: notes 36–91 → keys, CC 20/21 pots, CC 26 slider, CC 1
  joystick Y, CC 16–23 encoder deltas, CC 32–39 encoder push,
  pitch bend → joystick X).
- **LEDs**: note on/off on channel 2 back to the panel.
- Neither peripheral is fatal — a Pi booted without the panel or the
  display logs a warning and keeps the engine running (CI / profiling).
- SIGINT/SIGTERM route to the app's quit path, so the systemd unit or
  Ctrl-C shuts the engine down cleanly.

Deployment note: the arm64 binary dynamically links `libportaudio.so.2`
(yawn builds PortAudio shared). Ship `emulator/build-pi-arm64/lib/libportaudio.so.2`
alongside the binary (e.g. in `/usr/local/lib` + `ldconfig`, or next to
the binary with an rpath). `libasound.so.2` is part of Raspberry Pi OS.

### Heavy-load profiling (`--profile`)

```
groovebox_sim --profile 10
```

Worst-case musical load for the Zero 2W gate: all 4 tracks programmed on
all 16 steps (vel 120), 140 BPM, a reverb inserted on T1+T2, transport
rolling, UI rendering every frame. Prints PortAudio CPU% and the
PortAudio callback status flags (xrun bits) once per second, then exits
0. Runs on every backend (Windows sim, Pi hardware, qemu).

### Interactive controls (sim)

- **Mouse** (panel view): click keys/buttons; drag pots & slider; 2D-drag
  joystick (springs back); wheel over encoders; click encoder = push.
- **PC keyboard**: Z-row + Q-row = piano whites (with standard black-key
  letters/digits), Space = play/stop, Enter = rec, Tab = MODE,
  arrows = </>, F1–F4 = soft keys S1–S4, Shift = SHIFT, F5–F10 + `-`/`=` =
  pots, `[`/`]` = slider, F11 = panel/screen view, F12 = theme, ESC = quit.
- **Track select**: S3/S4 (TRK-/TRK+) or Shift + white keys 1–4;
  Shift + white 5–8 = mute.

### Drum editing (INST page on drum tracks)

For pad instruments (DrumSynth / DrumRack / DrumSlop) the INST page
edits the **last-played pad**: hitting a key in play mode (or a
hold-step voice p-lock) makes that voice the edit pad — the header
shows it (`SNARE T1 140`), an `EDIT SNARE` toast confirms, and all
bindings rebuild to that voice. Sequencer playback never steals the
edit focus.

- **Encoder pages**: PAD / PAD2 = the edited pad's params (DrumSynth:
  Tune/Drive, kick adds Sine/White/Pink; DrumRack: Pad Vol/Pan/Pitch/
  Choke/Start/End; DrumSlop: Pad Vol/Pan/Pitch/Rev/Cutoff/Reso),
  KIT = instrument globals (Volume, OS 2×, slice controls). S2 = PG+
  cycles them.
- **Macro encoders 5–8** (factory binding): A/D = the edited pad's
  attack/decay (S/R also bound on DrumSlop). Pots stay inert on
  DrumSynth/DrumRack (CUT/RES bind only where the instrument exposes
  them — DrumSlop pads).
- DrumSynth has no selected-slot concept — its 36 params are globally
  indexed per voice (kick = 7 @ index 0, others = 4 @ 7+(slot−1)·4),
  so the edit pad is pure index arithmetic; DrumRack/DrumSlop get the
  pad via their `selectedPad` setter.

### Scripted test / asset modes

```
groovebox_sim --smoke       engine boot + notes self-test
groovebox_sim --uitest      UI interaction assertions (8/8)
groovebox_sim --seqtest     sequencer scheduling assertions (6/6)
groovebox_sim --longtest    1–256 step pattern assertions (9/9)
groovebox_sim --paramtest   pot pickup / encoder ownership / FX (11/11)
groovebox_sim --sampletest  mic capture → trim → assign → browser (8/8)
groovebox_sim --panelprobe  synthetic mouse → HAL events (3/3)
groovebox_sim --panel=nsr2 --nsr2test    NSR-2 grid semantics (6/6)
groovebox_sim --revbtest    NSR-1 rev B: step row, p-lock, scale lock (6/6)
groovebox_sim --macrotest   macro encoder assign/bind/reset/persist (6/6)
groovebox_sim --tracktest   engine tracks, INST swap, MFX, type toggle (4/4)
groovebox_sim --bouncetest  clip record, bounce, FX slot 2 (7/7)
groovebox_sim --drumtest    drum key map: voices, wrap, p-lock (9/9)
groovebox_sim --padtest     last-played-pad edit focus (8/8)
groovebox_sim --panel=nsr2 --panelprobe  synthetic mouse on NSR-2 (3/3)
groovebox_sim --widgets     widget showcase → docs/widgets.png
groovebox_sim --profile 10  heavy-load profiling: CPU% + xrun flags/s
groovebox_sim --fontchart   font chart → docs/fontchart.png
groovebox_sim --themes      3-theme chart → docs/themes.png
groovebox_sim --paneldump   panel render → docs/panel_sim.png
groovebox_sim --splashdump  boot splash → docs/bootsplash.png
```

`--panel=<nsr1|nsr2>` selects the panel profile (faceplate, key
semantics, LED count) for interactive, test, and dump modes
(`--panel=nsr2 --paneldump panel.png` renders the NSR-2 faceplate).

## Repository layout

| Path | What it is |
|---|---|
| `docs/` | design documents: mission/strategy (`mission.md`), NSR-1 `design.md`, panel ↔ brain protocol (`panel-protocol.md`), NSR-2 `nsr2-design.md`, mockups & generated charts |
| `emulator/` | the groovebox app + device emulator (`hal.h`, SimBackend = SDL3 desktop, PiBackend = headless Pi fbdev+RtMidi, PanelView, ui, widgets, font5x7, pattern…), `pi/` = arm64 cross-build (Dockerfile + toolchain) |
| `panel-fw/` | Raspberry Pi Pico firmware (TinyUSB USB-MIDI panel controller, protocol v1 + GRV SysEx config) |
| `hardware/` | **active NSR-1 platform:** control/keyboard, Pi carrier, power board, enclosure/faceplate, and ElectroMechanicalDesigner integration/package checks |
| `nsr2/` | the deferred NSR-2 compact reference design: netlist spec, generated KiCad schematics, footprint library, enclosure spec, mockup |
| `os-image/` | Raspberry Pi OS Lite customization — boots straight into the app (~10 s to sound) |
| `tools/` | mockup generators (panel, UI), rgb565 conversion |
| `yawn/` | the audio engine (git submodule) |

## Status & roadmap

NSR-1 is the active hardware target. The rev B2 control and keyboard
boards are routed, DRC-clean, and EMD-verified. Next: the Raspberry Pi Zero
2W brain carrier, the separate power/battery board, and the NSR-1 enclosure
update for both. NSR-2 is retained as a schematic-complete reference but is
deferred; it is not the current build.

Software milestones (emulator-first):

- **M1 done** — emulator skeleton: engine headless on PC, HAL, sim window,
  font (Latin/Greek/Cyrillic + symbols), 26 widgets.
- **M2 done** — full instrument in the emulator: live step sequencer
  (1–256 steps), play mode, per-step pitch/gate, virtual front panel,
  themes, boot splash, pot pickup, encoder parameter pages, FX page,
  SAMPLE/LOAD/SETTINGS pages, dialogs. M2d adds one engine track per UI
  track (engine-level mutes, per-track FX), INST page (instrument
  picker with full param rebind), MFX page (MIDI-effect slot), TRACK
  page (channel type MIDI/AUDIO + vol/pan/input/monitor), macro
  encoders (assignable, persisted). AUDIO tracks record input into
  looping clips (REC arm + PLAY/STOP); BOUNCE renders a MIDI track's
  pattern offline to a WAV + looping clip on the first AUDIO track.
  FX page has two insert slots per track (shift+S2 switches).
- **M3 done** — Pi software path: headless PiBackend (fbdev 2× display +
  RtMidi panel per `docs/panel-protocol.md`) behind `-DGB_PI_BACKEND=ON`,
  Docker arm64 cross-build (`tools/build-pi-arm64.sh`, ELF64 AArch64
  verified), `--profile N` heavy-load harness (4 tracks dense, 140 BPM,
  reverb ×2, per-second CPU% + xrun flags).
- **M4** — bench rig: codec, panel firmware on real hardware, display.
- **M5 in progress** — battery, enclosure, rev B2 routed PCBs, and the verified EMD assembly package. The NSR-1 brain carrier and power board are the current hardware work.

## Conventions

- Commit messages: `area: summary` (`panel-fw:`, `os-image:`, `nsr2:`, `docs:`).
- Generated files are marked as such in their directories — read
  [`hardware/README.md`](hardware/README.md) and
  [`nsr2/README.md`](nsr2/README.md) before hand-editing generated
  schematics.
- Per-device credentials never enter the repo (see `os-image/customize/`).
- License: MIT (see [`LICENSE`](LICENSE)). Vendored third-party hardware
  libraries keep their own licenses — see the footprints table in
  [`nsr2/README.md`](nsr2/README.md).

ПОЕХАЛИ!

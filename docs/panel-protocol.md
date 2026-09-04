# Panel ↔ Brain Protocol (USB-MIDI)

The panel module (Pico) presents as a USB-MIDI device named `groovebox-panel`.
All panel→brain traffic rides this protocol; the brain sends LED state back
on channel 2. The emulator's sim backend emits/consumes the same logical
messages, so app code never cares which side is real.

Channel 1 = panel → brain. Channel 2 = brain → panel (LEDs).

## Panel → brain

### Keys (39 total)

Every panel key sends Note On (press, velocity = current value of the
configured **velocity source**, 1–127 — slider by default, see
[Panel configuration](#panel-configuration-sysex-id-0x7d)) and Note Off
(release). Note number = 36 + key index.

| Index | Note | Key |
|---|---|---|
| 0–15 | 36–51 | white keys 1–16 (bottom row) |
| 16–26 | 52–62 | black keys, left to right (C♯ pattern order) |
| 27–30 | 63–66 | transport: PLAY, STOP, REC, joystick push (30) |
| 31 | 67 | MODE |
| 32, 33 | 68, 69 | < , > |
| 34–37 | 70–73 | soft keys S1–S4 |
| 38, 39→(see note) | 74, 75 | SHIFT-L, SHIFT-R |

Note: the matrix holds 39 keys; index 38/39 use notes 74/75 — both shifts are
logically identical, the brain may merge them.

### Analog (CC, channel 1, 7-bit MSB only for rev A)

| CC | Source |
|---|---|
| 20 | pot CUTOFF |
| 21 | pot RESONANCE |
| 22 | pot ATTACK |
| 23 | pot DECAY |
| 24 | pot SUSTAIN |
| 25 | pot RELEASE |
| 26 | data slider (default velocity source — see Panel configuration) |
| 1  | joystick Y (mod) |
| pitch bend | joystick X (spring-returned, center = 8192) |

### Encoders (relative CC, channel 1)

| CC | Source |
|---|---|
| 16–19 | encoder 1–4 rotation: two's-complement relative (64 = no movement, 65 = +1 detent, 63 = −1 detent) |
| 32–35 | encoder 1–4 push: 127 on press, 0 on release |

## Brain → panel (LEDs, channel 2)

Note On/Off on channel 2, note number = LED index 0–15 (white-key LEDs).
Velocity ignored for rev A (single intensity); velocities 1–127 may later
map to PWM brightness.

CC 27 on channel 2 is the **host velocity register**: the brain writes the
live velocity value (1–127) it wants stamped on subsequent Note Ons while
the velocity source is `host register` (see Panel configuration). All other
channel-2 CCs are reserved.

## Panel configuration (SysEx, ID 0x7D)

Panel routing is host-configurable via vendor SysEx on the free
non-commercial manufacturer ID `0x7D`. Configuration is **routing, not
semantics**: the panel still only reports physical events — the brain merely
chooses which physical value stamps Note On velocity. All musical meaning
stays app-side (final section).

Frame: `F0 7D 47 52 56 <cmd> [args…] F7` — `47 52 56` is ASCII `GRV`.

| Cmd | Name | Args | Effect |
|---|---|---|---|
| 0x01 | SET_VELOCITY_SOURCE | `src` (+ `value` when `src` = 0x08) | velocity source for Note On, captured at the press instant |
| 0x02 | QUERY | — | panel replies with 0x03 (below) |
| 0x03 | REPLY | `proto_ver`, `feature_mask` | panel → brain: protocol version (currently 1) and capability bits |
| 0x04 | SET_PERSISTENCE | `mode` | 0 = volatile (default), 1 = save-on-change |
| 0x7F | RESET_DEFAULTS | — | velocity source = slider, persistence = volatile; erases any saved config |

Velocity source codes for 0x01:

| `src` | Meaning |
|---|---|
| 0x00 | data slider (power-up default — rev A behavior) |
| 0x01–0x06 | pot 0–5 (the CC 20–25 sources) |
| 0x07 | joystick Y (the CC 1 source) |
| 0x08 | fixed value, next byte 1–127 |
| 0x09 | host register (CC 27 on channel 2) |

Feature mask bits (REPLY): bit 0 = velocity source assignment, bit 1 =
config persistence, bit 2 = host velocity register. A brain that wants
optional behavior gates on these bits, not on the version byte.

Rules:

- **Volatile by default.** With persistence off, the panel powers up with
  velocity source = slider and no stored config. The brain re-sends its
  configuration after the QUERY handshake at boot and on mode changes.
- **Save-on-change.** With persistence on, each accepted config command is
  written to flash and restored at power-up. The firmware coalesces writes
  (at most one flash write per second of sustained changes) to spare the
  flash; the brain should still avoid high-rate configuration streams.
- **Unknown commands are ignored, never rejected** — a rev B panel may
  accept commands an older brain does not know, and vice versa. QUERY plus
  the feature bits is the discovery mechanism.
- **Unknown `src` codes fall back to the slider**, never to a dead velocity.

## NSR-2 panel variant

NSR-2 is the compact portrait panel (180x210 mm, 4x8 Choc grid — see
`docs/nsr2-design.md`). Same protocol v1 mechanics; different key map,
LED count, and defaults.

### Key map (45 keys, channel 1, note = 36 + index)

| Index | Note | Key |
|---|---|---|
| 0–31 | 36–67 | grid keys, row-major (top-left = 0) |
| 32, 33, 34 | 68, 69, 70 | PLAY, STOP, REC |
| 35 | 71 | MODE |
| 36, 37 | 72, 73 | < , > |
| 38–41 | 74–77 | soft keys S1–S4 |
| 42, 43 | 78, 79 | SHIFT-L, SHIFT-R |
| 44 | 80 | SPACE (wide bar) |

### Differences vs NSR-1

- **No pots, no data slider.** The 60 mm control is a **crossfader** on
  **CC 26** (same channel/index as NSR-1's slider). Thumbstick stays:
  CC 1 (Y / mod) and pitch bend (X), unchanged.
- **32 LEDs** (one per grid key): brain → panel note on/off on channel 2,
  note number = LED index **0–31** (row-major, same formula as NSR-1
  extended to 4x8).
- **Velocity source default = fixed 100** (`src` 0x08 @ 100) — the
  crossfader is mix/performance duty by default; the brain may
  SET_VELOCITY_SOURCE to 0x00 (crossfader) or 0x09 (host register) as
  on NSR-1.
- **REPLY feature mask gains bit 3 = "32-grid layout (NSR-2)"** so one
  brain binary gates its grid mapping on the QUERY handshake.
- MODE cycles three keyboard-row modes: **step → play → text** (NSR-1
  has step ↔ play only). Play mode is a scale-locked isomorphic grid
  (row up = +4 semitones, column = +1, base C3); text mode is a
  QWERTY-flavored grid feeding the EditBox flow.

## Semantics live in the brain

The panel is dumb: it reports physical events, nothing more. Key → musical
note mapping (scale/octave/play-vs-step mode), shift layers, velocity
interpretation (the panel's source binding is mere routing — see Panel
configuration), and all mode logic are app-side. This keeps the panel
firmware stable and the behavior firmware-free.

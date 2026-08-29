# Panel ↔ Brain Protocol (USB-MIDI)

The panel module (Pico) presents as a USB-MIDI device named `groovebox-panel`.
All panel→brain traffic rides this protocol; the brain sends LED state back
on channel 2. The emulator's sim backend emits/consumes the same logical
messages, so app code never cares which side is real.

Channel 1 = panel → brain. Channel 2 = brain → panel (LEDs).

## Panel → brain

### Keys (39 total)

Every panel key sends Note On (press, velocity = current slider value 1–127)
and Note Off (release). Note number = 36 + key index.

| Index | Note | Key |
|---|---|---|
| 0–15 | 36–51 | white keys 1–16 (bottom row) |
| 16–26 | 52–62 | black keys, left to right (C♯ pattern order) |
| 27–30 | 63–66 | transport: PLAY, STOP, REC, (reserved) |
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
| 26 | data slider (live velocity for play mode) |
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

## Semantics live in the brain

The panel is dumb: it reports physical events, nothing more. Key → musical
note mapping (scale/octave/play-vs-step mode), shift layers, velocity from
slider, and all mode logic are app-side. This keeps the panel firmware
stable and the behavior firmware-free.

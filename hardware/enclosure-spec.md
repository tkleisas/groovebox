# NSR-1 enclosure envelope — rev B2

This is the mechanical-fit contract for the routed control and keyboard
boards. It is not a finished enclosure model; it fixes the panel envelope and
board stack so the PCB outlines, standoffs, and connector heights stay
compatible.

## Panel and board positions

- Faceplate/panel: **400 × 170 mm**.
- Keyboard board: **356 × 100 mm**, placed at panel **(22, 65)**.
- Control board: **120 × 90 mm**, mounted **under** the keyboard board at
  panel **(140, 70)** on 10 mm standoffs.
- The top 65 mm strip carries the screen, soft keys, encoders, pots, slider,
  joystick, transport, and function keys; the keyboard/step rows sit below it.
- Verify that all encoder, pot, slider, joystick, and function-key JST
  harnesses clear the keyboard board and its switch pins.

## Vertical stack

| Layer | Height |
|---|---|
| Panel | 3.0 |
| Keyboard PCB | 1.6 |
| MX hot-swap socket + keycap clearance | ~5.0 |
| Standoff (keyboard → control) | 10.0 |
| Control PCB | 1.6 |
| Pico socket + Pico | ~11.0 |
| **Total internal** | **~32.2** |

Use an external case height of **35 mm** minimum. Fit the control board with
its Pico and connectors facing down into the rear cavity; the keyboard board
bottom side has only switch/LED pins.

## Fasteners

- M3 heat-set inserts in the case for the keyboard board (H1–H4).
- M3 standoffs from the keyboard board to the control board (H1–H4), with the
  control board orientation shown by its USB edge.

Run `python3 hardware/tools/verify_fit.py` after changing either outline.

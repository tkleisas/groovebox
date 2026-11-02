# NSR-1 enclosure envelope — rev B2 + platform update

This is the mechanical-fit contract for the routed control and keyboard
boards. It is not a finished enclosure model; it fixes the panel envelope and
board stack so the PCB outlines, standoffs, and connector heights stay
compatible.

## Panel and board positions

- Faceplate/panel: **400 × 170 mm**.
- Keyboard board: **356 × 80 mm**, placed at panel **(0.9, 86)**.
- Control board: **120 × 90 mm**, mounted under the keyboard/top-control area
  at panel **(140, 65)** on 10 mm standoffs.
- The top 86 mm strip carries the screen, soft keys, encoders, pots, slider,
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

## Platform update: brain, power, and battery

The rev B2 control/keyboard stack above stays unchanged. The serviceable
platform adds three rear-cavity elements. Their exact X/Y positions are still
provisional; the mounting concept is fixed now so the carrier and power-board
outlines can be designed against it.

These elements sit side by side in the rear cavity. Do not stack the brain
under the control board: the control/Pico stack already uses ~32 mm. Individually,
the brain + HAT stack is ~18 mm, the power board assembly ~8 mm, and the
battery ~12 mm, so each fits within the 35 mm external-height envelope.

### Brain adapter plate

- The tray provides **one** 4× M3 heat-set insert pattern for a removable
  brain deck, not a separate hole pattern for every SBC.
- Pi carrier PCB: 110 × 90 mm. The deck is slightly larger to carry the SBC
  mounting slots and a rear I/O lip; reserve about 130 × 110 mm.
- The deck carries the SBC-specific mounting holes and slots, the 40-pin
  header datum, board-edge stops, and the rear I/O opening for that SBC.
- Pi Zero 2W deck: M2.5 mounting holes/slots at the 58 × 23 mm pattern.
- Future Radxa/Orange Pi decks use the same outer M3 pattern; their SBC holes
  and connector cutouts live on their own deck.
- The Pi + Audio Injector Zero stack is ~18 mm tall. Reserve clearance above
  the deck for the HAT, the display header, and cable service loops.
- Keep the brain deck clear of the control-board/Pico stack and the keyboard
  switch pins.

### Power-board bay

- Separate 4× M3 insert pattern for the power/battery board.
- Board: 100 × 70 mm, 4-layer, with connector access from the rear or bottom
  service hatch.
- USB-C charge port, power button, and charge-status light pipe must be
  reachable with the enclosure closed.
- The power board must not sit directly under the SBC heat source.

### Battery bay

- Replaceable 1S 5000 mAh-class pack, not a soldered pouch.
- Target envelope: approximately 12 × 65 × 95 mm plus 2 mm swelling
  clearance; confirm against the chosen pack before printing.
- Keyed battery connector, strain relief, and a service disconnect.
- Bottom service hatch for battery and SBC access without removing the
  faceplate.
- Flame-retardant, non-abrasive bay; keep the pack away from the charger,
  boost converter, and SBC heatsink.

### Rear I/O and service access

- Swappable rear I/O plate for the SBC's USB/HDMI/SD layout. The Pi plate
  differs from the Radxa/Orange plates.
- USB-C charging port and power switch remain on the common rear panel.
- Bottom service hatch with captive M3 screws; no glued modules.
- Thermal vent path near the brain deck for Radxa/Orange-class SBCs; the Pi
  reference still needs Audio Injector and display clearance.

## Fasteners

- M3 heat-set inserts in the case for the keyboard board (H1–H4).
- M3 standoffs from the keyboard board to the control board (H1–H4), with the
  control board orientation shown by its USB edge.
- 4× M3 inserts for the brain adapter plate.
- 4× M3 inserts for the power-board bay.
- Captive M3 screws for the bottom service hatch.

Run `python3 hardware/tools/verify_fit.py` after changing either control or
keyboard outline. The brain/power/battery fit check is added when their
outlines are frozen.

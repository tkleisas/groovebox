# NSR-1 power and thermal review — rev B2

## 3V3 budget

| Load | Current |
|---|---|
| Pico + USB MIDI | ~40 mA |
| 5× MCP23017 | ~5 mA |
| 2× ADS1115 | <1 mA |
| HT16K33 logic | ~1 mA |
| 32 LEDs, 1 kΩ from 3V3, red Vf≈2.0 V | ~42 mA |
| **Total** | **~89 mA** |

The routed 3V3 traces are 0.15 mm and the B.Cu GND pour is solid-connected.
At ~90 mA the track and pour margins are comfortable; no thermal vias or
copper pours on the 3V3 net are needed. LED resistors dissipate under 2 mW
each.

## 5V and connectors

The control board only carries Pico VBUS plus a 100 nF bulk capacitor; the
keyboard is powered over the Pico USB link. No 5V load requires a wider trace.

## Signals

- I²C: 400 kHz, 4.7 kΩ pull-ups on the control board only.
- Analog: 10 kΩ / 100 nF RC filters, ~159 Hz cutoff, suitable for pots,
  slider, and joystick.
- Keyboard matrix: no matrix diodes; every key is a single-ended MCP input.

## Layout checks

- Routed DRC: 0 violations, 0 unconnected items on both boards.
- Minimum track width 0.15 mm; keyboard minimum clearance 0.12 mm (JLC-class
  2-layer process).
- B.Cu GND pours filled on both boards.
- Mounting holes: 4× M3 per board.

## Platform power board (rev A)

The NSR-1 platform moves battery charging, boost conversion, and fuel gauging
to a separate power board; see
[`power/netlist-spec.md`](power/netlist-spec.md). The current control/keyboard
3V3 budget above remains unchanged.

Target: 1S 5000 mAh-class pack, USB-C charge input, 5V/2.5A continuous output
for the Pi carrier, and 5V/3A peak. At 85% conversion, a 5000 mAh pack is
about 15.7 Wh delivered: roughly 6.3 h at a 2.5 W average load, 3.1 h at 5 W,
and 2.1 h at 7.5 W. Measure the real Pi + Audio Injector + display + panel
load before publishing a runtime claim.

Thermal rules: keep the battery away from the boost inductor, charger, and SBC
heatsink; use 2 oz copper and thermal vias on the power board; validate the
closed enclosure at 2.5A output. The battery needs swelling clearance and a
replaceable connector, not a soldered pouch.

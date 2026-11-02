# NSR-1 hardware — rev B2

**NSR-1 is the active hardware target.** NSR-2 is a deferred reference
design. The platform mission, support tiers, and the separate-carrier-per-SBC
policy are in [`../docs/mission.md`](../docs/mission.md).

The NSR-1 platform has four boards:

| Board | Spec | Status |
|---|---|---|
| Keyboard | [`netlist-spec.md`](netlist-spec.md) | routed, 0 DRC, EMD-verified |
| Control | [`netlist-spec.md`](netlist-spec.md) | routed, 0 DRC, EMD-verified |
| Brain carrier (Pi Zero 2W) | [`brain/netlist-spec.md`](brain/netlist-spec.md) | 110 × 90, routed, 0 unconnected, 4× M3 |
| Power/battery | [`power/netlist-spec.md`](power/netlist-spec.md) | 100 × 70, 4-layer, routed except 2 BAT_NEG links |

The control and keyboard boards are the existing rev B2 design. The brain
carrier and power board are new platform boards; their specs are the source of
truth for those boards. A future SBC gets its own carrier under `hardware/`,
not an adapter on the Pi carrier.

This hardware is imported and packaged by
[ElectroMechanicalDesigner](https://github.com/tkleisas/ElectroMechanicalDesigner);
see [`../README.md`](../README.md#electromechanicaldesigner-integration) and
`hardware/tools/verify_emd.py`.

Source of truth for the control/keyboard: [`netlist-spec.md`](netlist-spec.md).
The firmware contract is `panel-fw/src/board_config.h`; change both together.

## Generated schematics

The control and keyboard schematics are generated. Do not hand-edit the
`.kicad_sch` files while the generator is active; change the generator and
regenerate.

```bash
python3 hardware/tools/gen_schematics.py control keyboard brain
python3 hardware/tools/verify_schematics.py
# In the KiCad 10 container:
python3 hardware/tools/place_control.py
python3 hardware/tools/place_keyboard.py
python3 hardware/tools/add_ground_pour.py control
python3 hardware/tools/add_ground_pour.py keyboard
python3 hardware/tools/verify_fit.py
# New platform outlines (run in the KiCad container):
python3 hardware/tools/make_platform_outlines.py
```

`gen_schematics.py` uses the KiCad 10 symbol definitions embedded in the
checked-in NSR-2 schematics for control/keyboard/brain. The power board also
needs the installed KiCad symbol libraries, so generate it inside the KiCad
container with `-e KICAD_SYMBOL_DIR=/usr/share/kicad/symbols`.
`verify_schematics.py` checks pin coverage, key/encoder/analog nets, I²C
endpoints, address straps, LED ROW/COM grouping, and power endpoints.

Export netlists and validate them with EMD:

```bash
python3 hardware/tools/export_netlist.py
# power netlist needs the KiCad symbol directory:
docker run --rm -v "$PWD:/work" -w /work \
  -e KICAD_SYMBOL_DIR=/usr/share/kicad/symbols kicad/kicad:10.0 \
  python3 hardware/tools/export_netlist.py --board power
python3 hardware/tools/verify_emd.py --emd /path/to/emd
python3 hardware/tools/verify_emd.py --package-dir hardware/emd-packages \
  --combined hardware/emd-packages/groovebox.json
```

Check the firmware map against the five-expander contract:

```bash
gcc -std=c11 -Ipanel-fw/src hardware/tools/check_board_config.c -o /tmp/check_board_config
/tmp/check_board_config
```

The control/keyboard board outlines are rev B2: control 120 × 90 mm,
keyboard 356 × 80 mm. Both boards are placed, routed, and checked. The MX
hot-swap footprint is vendored from the keyswitch-kicad-library (`nsr1_fp`,
CC-BY-SA 4.0) and is assigned to all 45 switch symbols.

The brain carrier is the first SBC-specific board. It is a Raspberry Pi Zero
2W carrier by design; Radxa Zero 3W and Orange Pi Zero 3W would each get a
separate carrier and validation effort. The power board is common to all
carriers. Brain carrier: 110 × 90 mm, routed, 0 unconnected, EMD-valid
netlist. Power board: 100 × 70 mm, 4-layer, routed except two BAT_NEG links
that remain as rev A airwires. Start from their netlist specs.

## KiCad checks

With KiCad 10 in Docker:

```bash
docker run --rm -v "$PWD:/work" -w /work kicad/kicad:10.0 \
  kicad-cli sch erc hardware/control/control.kicad_sch \
  --output /work/control-erc.json --format json --severity-all
```

Current result: **0 ERC errors** for control, keyboard, brain, and power.
Expected warnings are `lib_symbol_mismatch` from the embedded symbol copies
(cosmetic). Routed control/keyboard/brain DRC is clean with 0 unconnected
items; the power board is routed except two `BAT_NEG` airwires.

Generate Gerbers, drill files, position CSV, BOM, STEP, stats, and zip
archives with:

```bash
hardware/tools/export_fabrication.sh
```

Outputs land in `hardware/fabrication/` and are gitignored; regenerate them
from the routed boards when needed. The STEP export defaults to a board-only
model because the KiCad container does not ship the 3D model library. Set
`STEP_FLAGS="--subst-models"` when `kicad-packages3d` is mounted or installed.

## Current scope

- **Control board:** Pico, 2× ADS1115, 2× MCP23017, 8 encoder connectors,
  CUT/RES pots, slider, joystick, 10 function-key connectors, joystick switch,
  keyboard/UART links.
- **Keyboard board:** 45 MX hot-swap sockets (29 piano/shift + 16 step row),
  3× MCP23017, HT16K33, 32 LEDs.
- **Control board:** placed and routed with 0 DRC violations and 0 unconnected
  items. Routing was done with Freerouting 2.5 via Specctra DSN/SES at
  0.15 mm minimum track width/clearance; the board grew to 120 × 90 mm to
  clear the eight encoder headers. Four M3 mounting-hole footprints are
  present for EMD mechanical ports.
- **Keyboard board:** all 45 switches, 32 LEDs, 32 resistors, 3 key expanders,
  HT16K33, link, and decoupling placed with
  `hardware/tools/place_keyboard.py`, then routed with Freerouting; DRC
  reports 0 violations and 0 unconnected items. Four M3 mounting-hole
  footprints are present for EMD mechanical ports.
- **Ground pour:** control, keyboard, brain, and power boards have a filled
  B.Cu GND zone.
- **Brain carrier:** Raspberry Pi Zero 2W carrier rev A is 110 × 90 mm,
  routed with 0 unconnected items, 0 ERC errors, and an EMD-valid netlist.
  See [`brain/netlist-spec.md`](brain/netlist-spec.md). It owns the Pi socket,
  Audio Injector Zero stack, display header, DIN MIDI, panel USB breakout, and
  5V input. It deliberately does not contain the battery charger or boost.
- **Power/battery board:** 100 × 70 mm, 4-layer, schematic generated and
  routed except two BAT_NEG links (rev A airwires); see
  [`power/netlist-spec.md`](power/netlist-spec.md). It owns USB-C charging,
  1S pack protection/gauging, power switching, and the 5V/3A rail.
- **Mechanical envelope:** see [`enclosure-spec.md`](enclosure-spec.md);
  the 356 × 80 mm keyboard and under-mounted 120 × 90 mm control board fit the
  400 × 170 mm panel, with a ~32 mm internal stack. `verify_fit.py` checks this.
- **EMD package:** the current combined project imports the control and
  keyboard netlists/PCBs, aligns the control board under the keyboard, validates
  170 parts / 522 connections, and produces a verified package. The brain and
  power boards join the combined package after they are routed.
- **Enclosure:** printable tray and cutout faceplate are in
  [`enclosure/`](enclosure/README.md); rear I/O openings remain.
- **Power/thermal:** see [`power-thermal-review.md`](power-thermal-review.md).

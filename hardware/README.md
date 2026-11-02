# NSR-1 hardware — rev B2

Source of truth: [`netlist-spec.md`](netlist-spec.md). The firmware contract is
`panel-fw/src/board_config.h`; change both together.

## Generated schematics

The control and keyboard schematics are generated. Do not hand-edit the
`.kicad_sch` files while the generator is active; change the generator and
regenerate.

```bash
python3 hardware/tools/gen_schematics.py
python3 hardware/tools/verify_schematics.py
# In the KiCad 10 container:
python3 hardware/tools/place_control.py
python3 hardware/tools/place_keyboard.py
```

`gen_schematics.py` uses the KiCad 10 symbol definitions embedded in the
checked-in NSR-2 schematics, so it does not need a local KiCad installation.
`verify_schematics.py` checks pin coverage, key/encoder/analog nets, I²C
endpoints, address straps, LED ROW/COM grouping, and power endpoints.

Export netlists and validate them with EMD:

```bash
python3 hardware/tools/export_netlist.py
python3 hardware/tools/verify_emd.py --emd /path/to/emd
```

Check the firmware map against the five-expander contract:

```bash
gcc -std=c11 -Ipanel-fw/src hardware/tools/check_board_config.c -o /tmp/check_board_config
/tmp/check_board_config
```

The board outlines are updated to rev B2: control 120 × 90 mm, keyboard
356 × 100 mm. Placement and routing are hand-owned and remain to be done. The
MX hot-swap footprint is vendored from the keyswitch-kicad-library
(`nsr1_fp`, CC-BY-SA 4.0) and is assigned to all 45 switch symbols.

## KiCad checks

With KiCad 10 in Docker:

```bash
docker run --rm -v "$PWD:/work" -w /work kicad/kicad:10.0 \
  kicad-cli sch erc hardware/control/control.kicad_sch \
  --output /work/control-erc.json --format json --severity-all
```

Current result: **0 ERC errors** for both boards. Expected warnings:
`lib_symbol_mismatch` from the embedded symbol copies (cosmetic; 92 control,
75 keyboard). Empty-outline DRC is clean (0 violations).

## Current scope

- **Control board:** Pico, 2× ADS1115, 2× MCP23017, 8 encoder connectors,
  CUT/RES pots, slider, joystick, 10 function-key connectors, joystick switch,
  keyboard/UART links.
- **Keyboard board:** 45 MX hot-swap sockets (29 piano/shift + 16 step row),
  3× MCP23017, HT16K33, 32 LEDs.
- **Control placement:** initial floorplan done with
  `hardware/tools/place_control.py`; DRC reports 0 violations (156 unrouted
  ratsnest connections remain). The board grew to 120 × 90 mm to clear the
  eight encoder headers.
- **Keyboard placement:** all 45 switches, 32 LEDs, 32 resistors, 3 key
  expanders, HT16K33, link, and decoupling placed with
  `hardware/tools/place_keyboard.py`; DRC reports 0 violations (222 unrouted
  ratsnest connections remain).
- **Not yet done:** routing both boards, fabrication outputs, and final DRC on
  the routed boards.

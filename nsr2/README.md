# NSR-2 — Directory Guide & Collaboration Contract

NSR-2 is the compact sibling of NSR-1 (this repo's `docs/design.md` rev A).
Design docs: `docs/nsr2-design.md`, `nsr2/netlist-spec.md`,
`nsr2/enclosure-spec.md`.

## What generates what

| File | Status | Produced by |
|---|---|---|
| `netlist-spec.md` | **SOURCE OF TRUTH** for all circuits | hand-edited (both of us) |
| `tools/gen_schematics.py` | generator | hand-edited (both of us) |
| `hardware/brain/brain.kicad_sch` | **GENERATED — do not hand-edit** | `gen_schematics.py` |
| `hardware/io/io.kicad_sch` | **GENERATED — do not hand-edit** | `gen_schematics.py` |
| `hardware/nsr2.kicad_sym` | **GENERATED — do not hand-edit** (custom HT16K33A symbol) | `gen_schematics.py` |
| `mockup_panel.py` → `panel_mockup_nsr2.png` | generator + output | hand-edited |
| `hardware/*/​*.kicad_pcb` | board outlines machine-made; **layout becomes HAND-OWNED the moment layout work starts in KiCad** | — |
| `hardware/*.kicad_prl`, `erc*.json`, `*_sch.pdf/png`, `*_outline.*`, `io_net.net` | disposable artifacts | KiCad / scripts |

## The collaboration contract

1. **The chain of truth is: `netlist-spec.md` → `gen_schematics.py` →
   `.kicad_sch`.** Any circuit change goes: update the spec → update the
   generator → regenerate → re-verify (ERC + netlist + render) → review.
2. **Never hand-edit the generated `.kicad_sch` files** (or
   `nsr2.kicad_sym`) while this contract is active — regeneration wipes
   hand edits. Spot something wrong? Report it; it gets fixed in the
   generator so the fix survives.
3. **The KiCad GUI is for review and measurement only** during this
   phase (ERC, net highlighter, reading values). Saving a session is
   fine — it only writes view settings (`.kicad_prl`).
4. **Freeze procedure**: when the schematic review passes, we record the
   freeze here (date + commit), after which the `.kicad_sch` files are
   handed over — hand edits in KiCad become the norm, the generator is
   retired for schematics (kept for reference). The `.kicad_pcb` layout
   is hand-owned regardless, from the first moved footprint.
5. Regenerate + verify before every commit that touches hardware.

## Regenerate & verify

```
python nsr2/tools/gen_schematics.py
kicad-cli sch erc  nsr2/hardware/brain/brain.kicad_sch --output out --format json --severity-all
kicad-cli sch erc  nsr2/hardware/io/io.kicad_sch    --output out --format json --severity-all
kicad-cli sch export netlist --format kicadsexpr nsr2/hardware/io/io.kicad_sch --output io_net.net
```

## Known ERC warnings (expected — do not "fix" blindly)

As of the 2026-08-31 generation (with footprints assigned), with
`--severity-all`:

| Board | Count | Class | Meaning |
|---|---|---|---|
| brain | 37 | `lib_symbol_mismatch` | my generated copies differ cosmetically from the installed library defs. **Zero effect** on connectivity, netlist or PCB. Clearable later via *Update Symbols from Libraries*. |
| brain | 1 | `isolated_pin_label` | `MIC_SIG` — intentional: the mic capsule's far end is the codec's physical jack. |
| io | 107 | `lib_symbol_mismatch` | same as above |

**Red flags — these must NEVER appear.** If any of these show up in ERC,
a generation regression happened; stop and diff the generator:

`power_pin_not_driven`, `pin_to_pin`, `label_dangling`,
`pin_not_connected`, `endpoint_off_grid`, `multiple_net_names`,
`footprint_link_issues`.

(Historical note: all of these were real bugs during generation and were
fixed — see `tools/bisect_sch.py` and `tools/exp_power.py`, kept as
regression probes.)

## Footprints

All schematic symbols now carry footprints. The project library
`nsr2_footprints.pretty` (registered in both projects' `fp-lib-table` as
`nsr2_fp`) holds the three that standard KiCad lacks:

| Footprint | Provenance |
|---|---|
| `SW_Hotswap_Kailh_Choc_V1` | [kiswitch library](https://github.com/kiswitch/keyswitch-kicad-library) (CC-BY-4.0), converted to current format |
| `DIN5-RA-PTH` | SparkFun-Connectors.pretty (vendored copy), converted |
| `SW_SPDT_SLIDE_THT` | generated (generic EG1218-class SPDT slide — verify against the bought part) |

Also note: the Pi and Pico sit on **female** headers — their 2×20
symbols use `PinSocket_2x20_P2.54mm_Vertical`, and the screen header is
`PinHeader_1x08_P2.54mm_Horizontal` (right-angle).

## KiCad quick notes for this project

- The schematics use **named labels instead of wires**: every `KEY17`
  label is the same electrical node. Use the net-highlighter (target
  icon, right toolbar) and click a label to see its whole net.
- ERC: *Inspect → Electrical Rules Checker*.
- The custom HT16K33A symbol is embedded, so it displays without extra
  setup; `sym-lib-table` makes the `nsr2` library resolvable for ERC.
- Both boards: io = 180×210 faceplate (screen window at 40,8–140,80),
  brain = 100×80. Outlines are already on Edge.Cuts.

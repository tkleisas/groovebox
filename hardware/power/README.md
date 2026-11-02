# NSR-1 power/battery board

Source of truth: [`netlist-spec.md`](netlist-spec.md). This board is common to
the NSR-1 platform and future SBC carriers.

## Status

- `power.kicad_pcb` — 100 × 70 mm, 4-layer, 4× M3 holes; routed except four rev A airwires (two BAT_NEG, two SCL).
- `power.kicad_sch` — generated from `hardware/tools/gen_schematics.py`;
  0 ERC errors with expected embedded-symbol warnings.
- Power-board and battery bays are specified in
  [`../enclosure-spec.md`](../enclosure-spec.md).

## Checks

```bash
docker run --rm -v "$PWD:/work" -w /work \
  -e KICAD_SYMBOL_DIR=/usr/share/kicad/symbols kicad/kicad:10.0 \
  python3 hardware/tools/gen_schematics.py power
docker run --rm -v "$PWD:/work" -w /work \
  -e KICAD_SYMBOL_DIR=/usr/share/kicad/symbols kicad/kicad:10.0 \
  python3 hardware/tools/export_netlist.py --board power
cd hardware/power
kicad-cli sch erc power.kicad_sch --output /tmp/power-erc.json --format json --severity-all
kicad-cli pcb drc power.kicad_pcb --output /tmp/power-drc.json --format json --severity-all
```

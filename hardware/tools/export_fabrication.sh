#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/hardware/fabrication"
KICAD_IMAGE="${KICAD_IMAGE:-kicad/kicad:10.0}"
STEP_FLAGS="${STEP_FLAGS:---board-only}"
LAYERS="F.Cu,B.Cu,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts"

for board in control keyboard; do
  dir="$OUT/$board"
  rm -rf "$dir"
  mkdir -p "$dir"
  docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$ROOT:/work" -w /work "$KICAD_IMAGE" sh -c "
      kicad-cli pcb export gerbers hardware/$board/$board.kicad_pcb \
        --output hardware/fabrication/$board --layers '$LAYERS' --no-protel-ext
      kicad-cli pcb export drill hardware/$board/$board.kicad_pcb \
        --output hardware/fabrication/$board --format excellon --drill-origin absolute \
        --excellon-units mm --excellon-separate-th --generate-report \
        --report-path hardware/fabrication/$board/drill-report.txt
      kicad-cli pcb export pos hardware/$board/$board.kicad_pcb \
        --output hardware/fabrication/$board/pos-front.csv --side front \
        --format csv --units mm --smd-only --exclude-dnp
      kicad-cli pcb export step hardware/$board/$board.kicad_pcb \
        --output hardware/fabrication/$board/board.step --force --no-dnp $STEP_FLAGS
      kicad-cli pcb export stats hardware/$board/$board.kicad_pcb \
        --output hardware/fabrication/$board/stats.json --format json --units mm
      kicad-cli sch export bom hardware/$board/$board.kicad_sch \
        --output hardware/fabrication/$board/bom.csv \
        --fields 'Reference,Value,Footprint,QUANTITY' \
        --labels 'Refs,Value,Footprint,Qty' --group-by 'Value,Footprint' \
        --sort-field Reference --exclude-dnp
    "
  (cd "$dir" && zip -q -r "../$board-fabrication.zip" .)
  echo "wrote $dir and $OUT/$board-fabrication.zip"
done

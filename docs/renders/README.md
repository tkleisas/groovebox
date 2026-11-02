# NSR-1 renders

Generated with Blender 4.0 from the FreeCAD tray/faceplate and the rev B2 board
layout:

- `nsr1_exploded_labeled.png` — exploded assembly (faceplate, keyboard PCB +
  switches, control PCB, tray)
- `nsr1_exploded.png` — unlabeled exploded render
- `nsr1_assembled.png` — assembled frame
- `nsr1_exploded.blend` — editable scene with the explode/assemble keyframes

Regenerate:

```bash
blender -b -P hardware/enclosure/render_exploded.py
blender -b docs/renders/nsr1_exploded.blend -P hardware/enclosure/render_assembled.py
python3 hardware/enclosure/label_render.py
```

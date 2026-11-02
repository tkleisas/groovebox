# NSR-1 renders

Generated with Blender 4.0 from the FreeCAD tray/faceplate and the rev B2 board
layout. The camera matches the reference panel orientation (screen top-left,
keyboard/step rows at the bottom); the exploded boards fan to the left.

- `nsr1_exploded_labeled.png` — exploded assembly (faceplate, keyboard PCB +
  switches, control PCB, tray)
- `nsr1_exploded.png` — unlabeled exploded render
- `nsr1_assembled.png` — assembled frame
- `nsr1_exploded.blend` — editable scene with the explode/assemble keyframes
- `nsr1_animation.mp4` — 15 s, 30 fps detail animation: exploded view,
  convergence, assembled hero, screen/encoder and key-row close-ups
- `nsr1_animation.blend` + `hardware/enclosure/animate_nsr1.py` — editable
  animation scene and camera/assembly keyframes

Regenerate:

```bash
blender -b -P hardware/enclosure/render_exploded.py
blender -b docs/renders/nsr1_exploded.blend -P hardware/enclosure/render_assembled.py
python3 hardware/enclosure/label_render.py
blender -b -P hardware/enclosure/animate_nsr1.py
blender -b docs/renders/nsr1_animation.blend -a
ffmpeg -y -framerate 30 -i docs/renders/frames/frame_%04d.png \
  -c:v libx264 -preset slow -crf 20 -pix_fmt yuv420p \
  -movflags +faststart docs/renders/nsr1_animation.mp4
```

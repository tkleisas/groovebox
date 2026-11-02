#!/usr/bin/env python3
"""Turn the NSR-1 exploded scene into a 15-second detail animation."""
from __future__ import annotations

import bpy

SRC = "/home/tkleisas/Projects/groovebox/docs/renders/nsr1_exploded.blend"
OUT = "/home/tkleisas/Projects/groovebox/docs/renders/nsr1_animation.blend"


def main():
    bpy.ops.wm.open_mainfile(filepath=SRC)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 16
    scene.cycles.use_denoising = False
    scene.render.resolution_x = 960
    scene.render.resolution_y = 540
    scene.render.resolution_percentage = 100
    scene.render.fps = 30
    scene.frame_start = 1
    scene.frame_end = 450
    scene.render.image_settings.file_format = "PNG"
    scene.render.filepath = "/home/tkleisas/Projects/groovebox/docs/renders/frames/frame_"
    scene.view_settings.exposure = 0.55
    scene.camera.data.clip_end = 5000

    groups = {name: bpy.data.objects[name] for name in (
        "control group", "keyboard group", "faceplate group",
        "keys group", "controls group", "face labels",
        "key labels", "screen labels")}
    offsets = {
        "control group": (-330, 0, 90),
        "keyboard group": (-165, 0, 160),
        "faceplate group": (0, 0, 220),
        "keys group": (0, 0, 245),
        "controls group": (0, 0, 240),
        "face labels": (0, 0, 220),
        "key labels": (0, 0, 245),
        "screen labels": (0, 0, 240),
    }
    for name, group in groups.items():
        group.animation_data_clear()
        group.location = offsets[name]
        group.keyframe_insert("location", frame=1)
        group.keyframe_insert("location", frame=150)
        group.location = (0, 0, 0)
        group.keyframe_insert("location", frame=180)
        group.keyframe_insert("location", frame=450)

    camera = scene.camera
    target = bpy.data.objects["target"]
    camera.animation_data_clear()
    target.animation_data_clear()
    camera.data.animation_data_clear()
    keys = [
        (1,   (-150, -450, 1300), (-150, 0, 90),  860),
        (75,  (-360, -520, 1200), (-150, 0, 90),  840),
        (150, (-100, -760, 1000), (-80, 0, 90),   780),
        (180, (0, -900, 760),     (0, 0, 80),     650),
        (240, (160, -820, 700),   (0, 0, 80),     650),
        (300, (-180, -450, 310),  (-144, -41, 45), 250),
        (360, (0, -520, 420),     (0, 0, 45),     460),
        (405, (250, -720, 700),   (0, 0, 80),     650),
        (450, (0, -900, 650),     (0, 0, 80),     620),
    ]
    for frame, location, target_location, ortho in keys:
        camera.location = location
        camera.keyframe_insert("location", frame=frame)
        target.location = target_location
        target.keyframe_insert("location", frame=frame)
        camera.data.ortho_scale = ortho
        camera.data.keyframe_insert("ortho_scale", frame=frame)
    for frame in (1, 150, 180):
        scene.frame_set(frame)
    scene.frame_set(1)
    bpy.ops.wm.save_as_mainfile(filepath=OUT)
    print("wrote", OUT)


main()

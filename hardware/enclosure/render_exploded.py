#!/usr/bin/env python3
"""Build and render a stylized NSR-1 exploded view in Blender 4.x."""
from __future__ import annotations

import math
import os

import bpy
from mathutils import Vector

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ENCLOSURE = os.path.join(ROOT, "hardware", "enclosure")
OUT = os.path.join(ROOT, "docs", "renders")
os.makedirs(OUT, exist_ok=True)

PITCH = 19.05
KB_ORIGIN = (0.9, 86.0)
WHITE_Y, BLACK_Y, STEP_Y = 40.0, 18.0, 70.0
GAPS = (1, 2, 4, 5, 6, 8, 9, 11, 12, 13, 16)


def white_x(i: float) -> float:
    return 6.5 + PITCH / 2 + (i + 1) * PITCH


def gap_x(gap: int) -> float:
    return 6.5 + PITCH / 2 + (gap + 0.5) * PITCH


def mat(name, color, rough=0.4, metal=0.0, emit=None, emit_strength=0.0):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*color, 1)
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Metallic"].default_value = metal
    if emit:
        bsdf.inputs["Emission Color"].default_value = (*emit, 1)
        bsdf.inputs["Emission Strength"].default_value = emit_strength
    return m


def assign(obj, material):
    obj.data.materials.append(material)


def box(name, x, y, z, sx, sy, sz, material, bevel=0.0):
    bpy.ops.mesh.primitive_cube_add(size=1, location=(x, y, z))
    obj = bpy.context.object
    obj.name = name
    obj.scale = (sx / 2, sy / 2, sz / 2)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel:
        mod = obj.modifiers.new("bevel", "BEVEL")
        mod.width = bevel
        mod.segments = 2
    assign(obj, material)
    return obj


def cyl(name, x, y, z, radius, depth, material, vertices=48):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth,
                                        location=(x, y, z))
    obj = bpy.context.object
    obj.name = name
    bpy.ops.object.shade_smooth()
    assign(obj, material)
    return obj


def parent_group(name, offset_z):
    empty = bpy.data.objects.new(name, None)
    bpy.context.collection.objects.link(empty)
    empty.location.z = offset_z
    return empty


def parent(obj, empty):
    obj.parent = empty


def import_stl(name, path, material, x, y, z):
    bpy.ops.import_mesh.stl(filepath=path)
    obj = bpy.context.object
    obj.name = name
    obj.location = (x, y, z)
    assign(obj, material)
    return obj


def text_obj(name, body, x, y, z, size, group, material):
    curve = bpy.data.curves.new(name, "FONT")
    curve.body = body
    curve.size = size
    curve.align_x = "CENTER"
    curve.align_y = "CENTER"
    curve.extrude = 0.12
    obj = bpy.data.objects.new(name, curve)
    bpy.context.collection.objects.link(obj)
    obj.location = (x, y, z)
    obj.data.materials.append(material)
    obj.parent = group
    return obj


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 128
    scene.cycles.use_denoising = False
    scene.render.resolution_x = 1800
    scene.render.resolution_y = 1200
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.filepath = os.path.join(OUT, "nsr1_exploded.png")
    scene.view_settings.view_transform = "AgX"
    scene.view_settings.look = "AgX - Medium High Contrast"
    scene.view_settings.exposure = 0.55

    root = bpy.data.objects.new("panel root", None)
    bpy.context.collection.objects.link(root)
    root.scale.y = -1

    red = mat("faceplate red", (0.10, 0.002, 0.004), rough=0.62)
    red.node_tree.nodes["Principled BSDF"].inputs["Specular IOR Level"].default_value = 0.18
    tray_mat = mat("tray", (0.035, 0.04, 0.05), rough=0.55)
    pcb_mat = mat("pcb", (0.015, 0.12, 0.075), rough=0.5)
    chip_mat = mat("chip", (0.035, 0.04, 0.05), rough=0.3)
    cream = mat("cream keys", (0.84, 0.81, 0.70), rough=0.42)
    black_key = mat("black keys", (0.018, 0.018, 0.022), rough=0.36)
    shift_key = mat("shift keys", (0.16, 0.025, 0.03), rough=0.4)
    step_key = mat("step keys", (0.12, 0.12, 0.13), rough=0.45)
    silver = mat("silver", (0.55, 0.57, 0.62), rough=0.22, metal=1.0)
    dark_metal = mat("dark metal", (0.10, 0.11, 0.13), rough=0.3, metal=0.6)
    screen_mat = mat("screen", (0.01, 0.03, 0.06), rough=0.18,
                     emit=(0.05, 0.35, 0.55), emit_strength=1.4)
    connector_mat = mat("connectors", (0.72, 0.68, 0.55), rough=0.5)

    # Static tray and background.
    tray = import_stl("tray", os.path.join(ENCLOSURE, "nsr1_tray.stl"),
                      tray_mat, -203, -88, 0)
    tray.parent = root
    bpy.ops.mesh.primitive_plane_add(size=1400, location=(0, 0, -2))
    floor = bpy.context.object
    assign(floor, mat("floor", (0.018, 0.02, 0.025), rough=0.7))

    control = parent_group("control group", 60)
    keyboard = parent_group("keyboard group", 105)
    faceplate = parent_group("faceplate group", 145)
    keys = parent_group("keys group", 165)
    controls = parent_group("controls group", 160)
    for group in (control, keyboard, faceplate, keys, controls):
        group.parent = root

    # Under-panel control board.
    cp = box("control pcb", 0, 25, 12, 120, 90, 1.6, pcb_mat)
    parent(cp, control)
    pico = box("pico", 0, 30, 14.2, 21, 51, 2.0, pcb_mat)
    parent(pico, control)
    box("pico usb", 0, 5.5, 14.2, 9, 8, 2.2, silver).parent = control
    for i, (x, y) in enumerate(((-20, 10), (-20, 25), (20, 10), (20, 25))):
        box(f"control ic {i}", x, y, 13.4, 10, 11, 1.3, chip_mat).parent = control
    for i, x in enumerate((-52, -42, -32, -22, -12)):
        box(f"analog connector {i}", x, 5, 13.8, 7, 10, 4, connector_mat).parent = control
    for i, y in enumerate((4, 14, 24, 34, 44, 54, 64, 74)):
        box(f"encoder connector {i}", 43 if i < 4 else 53, y - 39, 13.8,
            7, 14, 4, connector_mat).parent = control

    # Keyboard PCB and its back-side chips.
    kp = box("keyboard pcb", -21.1, 41, 26, 356, 80, 1.6, pcb_mat)
    parent(kp, keyboard)
    for i, x in enumerate((-139.1, -79.1, -19.1, 40.9)):
        box(f"keyboard ic {i}", x, 56, 27.4, 9.4, 10.8, 1.2, chip_mat).parent = keyboard
    box("keyboard link", -79.1, 6, 27.8, 13, 7, 4, connector_mat).parent = keyboard

    # Faceplate.
    fp = import_stl("faceplate", os.path.join(ENCLOSURE, "nsr1_faceplate.stl"),
                    red, -200, -85, 32)
    parent(fp, faceplate)

    # Keycaps.
    ox, oy = KB_ORIGIN
    for i in range(16):
        for y, m, name in ((WHITE_Y, cream, "white"), (STEP_Y, step_key, "step")):
            cap = box(f"{name} key {i}", ox + white_x(i) - 200, oy + y - 85, 40,
                      18, 18, 8, m, bevel=1.0)
            parent(cap, keys)
    for x, m, name in ((white_x(-1) - PITCH / 2, shift_key, "shift l"),
                       (white_x(15) + PITCH, shift_key, "shift r")):
        cap = box(name, ox + x - 200, oy + WHITE_Y - 85, 40, 18, 18, 8, m, bevel=1.0)
        parent(cap, keys)
    for gap in GAPS:
        cap = box(f"black key {gap}", ox + gap_x(gap) - 200, oy + BLACK_Y - 85, 40,
                  18, 18, 8, black_key, bevel=1.0)
        parent(cap, keys)

    # LED dots on the step keys.
    for i in range(16):
        led = cyl(f"step led {i}", ox + white_x(i) - 200, oy + STEP_Y - 85,
                  44.7, 0.9, 0.5, mat(f"led {i}", (0.8, 0.02, 0.02), rough=0.2,
                                      emit=(1.0, 0.02, 0.01), emit_strength=4.0))
        parent(led, keys)

    # MX switch bodies sit on the keyboard PCB in the exploded view.
    for i in range(16):
        for y in (WHITE_Y, STEP_Y):
            sw = box(f"switch {i}-{y}", ox + white_x(i) - 200,
                     oy + y - 85, 29.5, 14, 14, 5, chip_mat, bevel=0.4)
            parent(sw, keyboard)
    for gap in GAPS:
        sw = box(f"switch black {gap}", ox + gap_x(gap) - 200,
                 oy + BLACK_Y - 85, 29.5, 14, 14, 5, chip_mat, bevel=0.4)
        parent(sw, keyboard)

    # Screen and panel controls.
    screen = box("display", 56 - 200, 44 - 85, 36, 96, 68, 2, screen_mat, bevel=0.6)
    parent(screen, controls)
    for i, x in enumerate((30, 55, 80, 105)):
        knob = cyl(f"encoder {i}", x - 200, 88 - 85, 40, 7, 10, dark_metal)
        parent(knob, controls)
        cyl(f"encoder cap {i}", x - 200, 86 - 85, 45.2, 5.5, 1.0, silver).parent = controls
        box(f"encoder mark {i}", x - 200, 86 - 85, 45.7, 1.0, 4.2, 0.5,
            cream).parent = controls
    for i, x in enumerate((140, 158)):
        knob = cyl(f"pot {i}", x - 200, 32 - 85, 40, 6, 10, dark_metal)
        parent(knob, controls)
        box(f"pot mark {i}", x - 200, 32 - 85, 45.2, 1.0, 3.8, 0.5,
            cream).parent = controls
    for i, x in enumerate((184, 202, 220, 238)):
        knob = cyl(f"adsr {i}", x - 200, 32 - 85, 40, 6, 10, dark_metal)
        parent(knob, controls)
        box(f"adsr mark {i}", x - 200, 32 - 85, 45.2, 1.0, 3.8, 0.5,
            cream).parent = controls
    ring = cyl("joystick ring", 18 - 200, 105 - 85, 36.4, 11, 1.0, silver)
    parent(ring, controls)
    stick = cyl("joystick base", 18 - 200, 105 - 85, 37, 10, 4, dark_metal)
    parent(stick, controls)
    ball = bpy.ops.mesh.primitive_uv_sphere_add(radius=5, location=(18 - 200, 105 - 85, 44))
    ball_obj = bpy.context.object
    ball_obj.name = "joystick"
    assign(ball_obj, silver)
    parent(ball_obj, controls)
    slider = box("slider cap", 341 - 200, 88 - 85, 38, 13, 11, 5, dark_metal, bevel=0.8)
    parent(slider, controls)
    for i, (x, y, w, h) in enumerate(((317.5, 19, 15, 10), (334.5, 19, 15, 10),
                                      (351.5, 19, 15, 10), (318, 38.5, 16, 9),
                                      (336, 38.5, 16, 9), (354, 38.5, 16, 9))):
        key = box(f"transport {i}", x - 200, y - 85, 37, w, h, 5, black_key, bevel=0.6)
        parent(key, controls)
    for i, y in enumerate((19.5, 34.5, 49.5, 64.5)):
        key = box(f"soft key {i}", 115 - 200, y - 85, 37, 14, 11, 5, black_key, bevel=0.6)
        parent(key, controls)

    # Faceplate/step legends. These live outside the Y-mirrored root so text
    # reads normally.
    face_labels = parent_group("face labels", 220)
    key_labels = parent_group("key labels", 245)
    screen_labels = parent_group("screen labels", 240)
    for group in (face_labels, key_labels, screen_labels):
        group.parent = None
    for body, x, y, size in (
        ("FILTER", 149, 17, 5.5), ("AMP ENVELOPE", 211, 17, 5.5),
        ("TRANSPORT", 334.5, 9, 5.5), ("SOFT KEYS", 115, 4.5, 4.5),
        ("STEPS", 350, 160, 5.5), ("DATA SLIDER", 341, 94, 5.5),
    ):
        text_obj(f"label {body}", body, x - 200, 85 - y, 35.4, size,
                 face_labels, cream)
    text_obj("badge", "ВОКОИТЕР · ΟΡΓΑΝΟ 1", 150 - 200, 85 - 62, 35.4, 9.5,
             face_labels, cream)
    for i in range(16):
        text_obj(f"step {i + 1}", str(i + 1), ox + white_x(i) - 200,
                 85 - (oy + STEP_Y), 44.8, 4.0, key_labels, cream)
    text_obj("screen title", "SEQUENCER", 56 - 200, 85 - 40, 37.4, 7.0,
             screen_labels, cream)
    text_obj("screen sub", "A1  B2  C3  D4", 56 - 200, 85 - 48, 37.4, 4.5,
             screen_labels, cream)

    # Camera, lighting and world.
    cam_data = bpy.data.cameras.new("camera")
    cam = bpy.data.objects.new("camera", cam_data)
    bpy.context.collection.objects.link(cam)
    cam_data.type = "ORTHO"
    cam_data.ortho_scale = 860
    cam_data.clip_start = 1.0
    cam_data.clip_end = 5000.0
    cam.location = (-150, 0, 1400)
    target = bpy.data.objects.new("target", None)
    bpy.context.collection.objects.link(target)
    target.location = (-150, 0, 90)
    con = cam.constraints.new("TRACK_TO")
    con.target = target
    con.track_axis = "TRACK_NEGATIVE_Z"
    con.up_axis = "UP_Y"
    scene.camera = cam

    for name, loc, energy, size in (
        ("key", (300, -300, 900), 11000000, 500),
        ("fill", (-400, -300, 600), 6000000, 450),
        ("rim", (100, 400, 800), 9000000, 350),
    ):
        light_data = bpy.data.lights.new(name, "AREA")
        light_data.energy = energy
        light_data.size = size
        light = bpy.data.objects.new(name, light_data)
        bpy.context.collection.objects.link(light)
        light.location = loc
        con = light.constraints.new("TRACK_TO")
        con.target = target
        con.track_axis = "TRACK_NEGATIVE_Z"
        con.up_axis = "UP_Y"

    world = bpy.data.worlds.new("world")
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs[0].default_value = (0.012, 0.016, 0.028, 1)
    bg.inputs[1].default_value = 0.5
    scene.world = world
    scene.world.node_tree.nodes["Background"].inputs[1].default_value = 1.0

    # Animation: exploded at frame 1, assembled at frame 70.
    for empty, offset in ((control, (-330, 0, 90)),
                          (keyboard, (-165, 0, 160)),
                          (faceplate, (0, 0, 220)),
                          (keys, (0, 0, 245)),
                          (controls, (0, 0, 240)),
                          (face_labels, (0, 0, 220)),
                          (key_labels, (0, 0, 245)),
                          (screen_labels, (0, 0, 240))):
        empty.location = offset
        empty.keyframe_insert("location", frame=1)
        empty.location = (0, 0, 0)
        empty.keyframe_insert("location", frame=70)
    scene.frame_start = 1
    scene.frame_end = 70
    scene.frame_set(1)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "nsr1_exploded.blend"))
    bpy.ops.render.render(write_still=True)
    print(f"rendered {scene.render.filepath}")


main()

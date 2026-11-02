#!/usr/bin/env python3
"""Render the assembled frame from the saved NSR-1 Blender scene."""
import bpy

scene = bpy.context.scene
scene.frame_set(70)
scene.camera.location.x = 0
scene.camera.location.y = 0
scene.camera.location.z = 1400
scene.camera.data.ortho_scale = 620
for obj in scene.objects:
    if obj.name == "target":
        obj.location.x = 0
        obj.location.y = 0
        obj.location.z = 90
scene.render.filepath = "/home/tkleisas/Projects/groovebox/docs/renders/nsr1_assembled.png"
bpy.ops.render.render(write_still=True)

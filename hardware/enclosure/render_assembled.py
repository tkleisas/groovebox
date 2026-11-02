#!/usr/bin/env python3
"""Render the assembled frame from the saved NSR-1 Blender scene."""
import bpy

scene = bpy.context.scene
scene.frame_set(70)
scene.render.filepath = "/home/tkleisas/Projects/groovebox/docs/renders/nsr1_assembled.png"
bpy.ops.render.render(write_still=True)

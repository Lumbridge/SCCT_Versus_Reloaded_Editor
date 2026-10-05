"""Quick look in Blender: Workbench renders of whatever .npz parts or .blend objects are given.

blender -b --python look.py -- <parts.npz or scene.blend> <out.png> [views] [zoom box: cx cy cz half]
"""
import sys
import math
from pathlib import Path

import bpy
import numpy as np
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
src, out_png = Path(argv[0]).resolve(), Path(argv[1]).resolve()
views = [float(a) for a in argv[2].split(",")] if len(argv) > 2 else [0, 90, 180, 270]
box = [float(a) for a in argv[3:7]] if len(argv) > 6 else None

if src.suffix == ".blend":
    bpy.ops.wm.open_mainfile(filepath=str(src))
    for o in list(bpy.data.objects):
        if o.type == "MESH" and (o.hide_render or "_hi" in o.name or o.name.startswith("merc")):
            o.hide_render = True
else:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    d = np.load(src)
    for key in d.files:
        if not key.endswith("_v"):
            continue
        name = key[:-2]
        v, f = d[key].astype(np.float64), d[name + "_f"]
        me = bpy.data.meshes.new(name)
        me.vertices.add(len(v))
        me.vertices.foreach_set("co", v.ravel())
        me.loops.add(f.size)
        me.loops.foreach_set("vertex_index", f.ravel())
        me.polygons.add(len(f))
        me.polygons.foreach_set("loop_start", np.arange(0, f.size, 3))
        me.update()
        me.shade_smooth() if hasattr(me, "shade_smooth") else None
        bpy.context.scene.collection.objects.link(bpy.data.objects.new(name, me))

scene = bpy.context.scene
scene.render.engine = "BLENDER_WORKBENCH"
scene.display.shading.light = "STUDIO"
scene.display.shading.color_type = "TEXTURE" if src.suffix == ".blend" else "OBJECT"
scene.display.shading.show_cavity = True
scene.display.shading.cavity_type = "BOTH"
scene.render.film_transparent = False
scene.render.resolution_x = 520 if not box else 420
scene.render.resolution_y = 560 if not box else 420
cam_data = bpy.data.cameras.new("look")
cam_data.type = "ORTHO"
cam = bpy.data.objects.new("look", cam_data)
scene.collection.objects.link(cam)
scene.camera = cam
centre = Vector(box[:3]) if box else Vector((0, 0, -3))
half = box[3] if box else 98
cam_data.ortho_scale = half * 2
colors = [(0.95, 0.75, 0.1, 1), (0.15, 0.15, 0.15, 1), (0.2, 0.2, 0.25, 1), (0.6, 0.6, 0.6, 1)]
for i, o in enumerate(bpy.data.objects):
    if o.type == "MESH" and src.suffix != ".blend":
        o.color = colors[0] if o.name == "suit" else colors[1]
frames = []
for k, yaw in enumerate(views):
    a = math.radians(yaw)
    # yaw 0 looks at the front (+Y side).
    direction = Vector((math.sin(a), math.cos(a), 0.08)).normalized()
    cam.location = centre + direction * 400
    cam.rotation_euler = (-direction).to_track_quat("-Z", "Y").to_euler()
    path = out_png.with_name(out_png.stem + f"_{k}.png")
    scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)
    frames.append(path)
# Side by side.
imgs = [bpy.data.images.load(str(p)) for p in frames]
w, h = imgs[0].size
sheet = bpy.data.images.new("sheet", w * len(imgs), h)
px = np.zeros((h, w * len(imgs), 4), np.float32)
for i, im in enumerate(imgs):
    a = np.array(im.pixels[:], np.float32).reshape(h, w, 4)
    px[:, i * w:(i + 1) * w] = a
sheet.pixels = px.ravel()
sheet.filepath_raw = str(out_png)
sheet.file_format = "PNG"
sheet.save()
for p in frames:
    p.unlink()

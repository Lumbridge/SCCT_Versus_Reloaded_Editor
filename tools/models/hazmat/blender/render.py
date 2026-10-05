"""Stage 4 (in Blender): put the finished textures on the game mesh, render previews, save the .blend.

blender -b --python render.py -- <out dir> <assets dir>

Writes <assets>/preview_turnaround.png and preview_head.png, and saves <out>/hazmat.blend with
the textured game mesh (the high-poly parts stay in it, hidden).
"""
import math
import sys
from pathlib import Path

import bpy
import numpy as np
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
OUT, ASSETS = Path(argv[0]).resolve(), Path(argv[1]).resolve()
bpy.ops.wm.open_mainfile(filepath=str(OUT / "hazmat.blend"))
scene = bpy.context.scene

for obj_name, tga, rough in (("HazmatSuit", "HazmatSuit.tga", 0.42), ("HazmatGear", "HazmatGear.tga", 0.3)):
    o = bpy.data.objects[obj_name]
    m = o.material_slots[0].material
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = bpy.data.images.load(str(ASSETS / tga), check_existing=False)
    tex.image.pack()
    tex.location = (-400, 0)
    bsdf.inputs["Roughness"].default_value = rough
    nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
    nt.links.new(bsdf.outputs[0], out.inputs[0])
    o.visible_diffuse = o.visible_glossy = o.visible_shadow = True
for o in scene.objects:
    if o.type == "MESH" and o.name not in ("HazmatSuit", "HazmatGear"):
        o.hide_render = True
# The meshes are in the merc's own (Unreal, left-handed) coordinates. Blender is right-handed, so
# seen directly they are a mirror image of the game; a Y-mirrored parent shows them as the game does.
view = bpy.data.objects.new("Unreal view (Y mirrored)", None)
scene.collection.objects.link(view)
view.scale = (1, -1, 1)
for o in scene.objects:
    if o.type == "MESH":
        o.parent = view
# Drop the bake intermediates; keep the AO and normal maps for anyone who wants them.
for im in list(bpy.data.images):
    if any(im.name.endswith(k) for k in ("_pos", "_region", "_mask")):
        bpy.data.images.remove(im)

scene.render.engine = "BLENDER_EEVEE" if "BLENDER_EEVEE" in [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items] else "BLENDER_EEVEE_NEXT"
scene.render.film_transparent = False
world = scene.world
nt = world.node_tree
nt.nodes["Background"].inputs[0].default_value = (0.16, 0.17, 0.2, 1)
nt.nodes["Background"].inputs[1].default_value = 0.9
sun_data = bpy.data.lights.new("key", "SUN")
sun_data.energy = 2.2
sun_data.angle = math.radians(12)
sun = bpy.data.objects.new("key", sun_data)
sun.rotation_euler = (math.radians(50), 0, math.radians(30))
scene.collection.objects.link(sun)
rim_data = bpy.data.lights.new("rim", "SUN")
rim_data.energy = 1.2
rim = bpy.data.objects.new("rim", rim_data)
rim.rotation_euler = (math.radians(60), 0, math.radians(200))
scene.collection.objects.link(rim)

cam_data = bpy.data.cameras.new("preview")
cam = bpy.data.objects.new("preview", cam_data)
scene.collection.objects.link(cam)
scene.camera = cam
scene.view_settings.view_transform = "Standard"


def shoot(views, centre, distance, lens, size, path):
    cam_data.lens = lens
    scene.render.resolution_x, scene.render.resolution_y = size
    frames = []
    for k, yaw in enumerate(views):
        a = math.radians(yaw)
        # Mirrored, the front (the merc's +Y) faces -Y; yaw 0 looks at it.
        d = Vector((math.sin(a), -math.cos(a), 0.12)).normalized()
        cam.location = centre + d * distance
        cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
        p = path.with_name(path.stem + f"_{k}.png")
        scene.render.filepath = str(p)
        bpy.ops.render.render(write_still=True)
        frames.append(p)
    imgs = [bpy.data.images.load(str(p)) for p in frames]
    w, h = imgs[0].size
    px = np.zeros((h, w * len(imgs), 4), np.float32)
    for i, im in enumerate(imgs):
        px[:, i * w:(i + 1) * w] = np.array(im.pixels[:], np.float32).reshape(h, w, 4)
        bpy.data.images.remove(im)
    sheet = bpy.data.images.new("sheet", w * len(imgs), h)
    sheet.pixels = px.ravel()
    sheet.filepath_raw = str(path)
    sheet.file_format = "PNG"
    sheet.save()
    bpy.data.images.remove(sheet)
    for p in frames:
        p.unlink()


shoot([0, 90, 180, 270], Vector((0, 0, -3)), 330, 50, (480, 600), ASSETS / "preview_turnaround.png")
shoot([0, 35, 120, 180], Vector((0, -2, 66)), 150, 60, (420, 420), ASSETS / "preview_head.png")
shoot([25], Vector((0, 0, -3)), 300, 50, (900, 1100), ASSETS / "preview_hero.png")
bpy.data.objects.remove(cam)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "hazmat.blend"), compress=True)
print("rendered and saved")

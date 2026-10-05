"""Stage 2 of the Blender hazmat build (runs inside Blender).

blender -b --factory-startup --python build.py -- <out dir>

Reads <out>/parts.npz from prep.py and:
- details the high-poly suit: bagginess, cloth folds bunched at the elbows, knees, waist and
  cuffs, drapes from the armpits, taped seams, a zip flap, tape over the glove cuffs and the leg
  hems, a sole lip on the boots;
- models the gear: a wide rounded visor with a rubber gasket, and exhaust valves on the hood;
- makes the game mesh (decimated copies of the parts, about 8000 triangles), unwraps it;
- bakes from the high-poly parts: object-space normals, ambient occlusion, positions, region ids
  and seam/flap/tape masks, saved as .npy for finish.py to paint;
- saves hazmat.blend (high and low objects, baked AO on the low ones) and low.npz.
"""
import math
import sys
from pathlib import Path

import bpy
import bmesh
import numpy as np
from mathutils import Vector, noise
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree

OUT = Path(sys.argv[sys.argv.index("--") + 1]).resolve()
D = np.load(OUT / "parts.npz")
NAMES = [str(n) for n in D["bone_names"]]
BPOS = {n: Vector(p) for n, p in zip(NAMES, D["bone_pos"])}
HOOD_C = Vector(D["hood_center"])
LEG_HEM_Z = float(D["leg_hem_z"])

# "cap": the flat ends where a part was cut (glove openings, leg hems), painted as a dark gap.
REGIONS = ["torso", "hood", "armL", "armR", "legL", "legR", "handL", "handR", "footL", "footR", "cap"]
SUIT_TEX, GEAR_TEX = 2048, 1024          # bake sizes; finish.py halves them
TRIANGLES = {"suit": 5400, "gloveL": 820, "gloveR": 820, "bootL": 420, "bootR": 420}
GEAR_PARTS = ["visor", "gasket", "valve"]


def chain_of(n):
    if n in ("B Neck", "B Head", "Laser", "DEF_01_Casque"):
        return "hood"
    for s in ("L", "R"):
        if n.startswith(f"B {s} Hand") or n.startswith(f"B {s} Finger"):
            return "hand" + s
        if n in (f"B {s} UpperArm", f"B {s} ForeArm"):
            return "arm" + s
        if n in (f"B {s} Thigh", f"B {s} Calf"):
            return "leg" + s
        if n in (f"B {s} Foot", f"B {s} Toe0"):
            return "foot" + s
    return "torso"


# ---------------------------------------------------------------- mesh helpers

def make_mesh(name, v, f):
    me = bpy.data.meshes.new(name)
    me.vertices.add(len(v))
    me.vertices.foreach_set("co", np.asarray(v, np.float64).ravel())
    f = np.asarray(f, np.int64)
    me.loops.add(f.size)
    me.loops.foreach_set("vertex_index", f.ravel())
    me.polygons.add(len(f))
    me.polygons.foreach_set("loop_start", np.arange(0, f.size, 3))
    me.update()
    me.polygons.foreach_set("use_smooth", np.ones(len(f), bool))
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    return o


def verts_of(o):
    a = np.zeros(len(o.data.vertices) * 3)
    o.data.vertices.foreach_get("co", a)
    return a.reshape(-1, 3)


def set_verts(o, v):
    o.data.vertices.foreach_set("co", np.asarray(v, np.float64).ravel())
    o.data.update()


def normals_of(o):
    a = np.zeros(len(o.data.vertices) * 3)
    o.data.vertex_normals.foreach_get("vector", a)
    return a.reshape(-1, 3)


def tris_of(o):
    a = np.zeros(len(o.data.polygons) * 3, np.int64)
    o.data.polygons.foreach_get("vertices", a)
    return a.reshape(-1, 3)


def smooth_field(o, values, iterations):
    """Average a per-vertex array over neighbours (edges)."""
    e = np.zeros(len(o.data.edges) * 2, np.int64)
    o.data.edges.foreach_get("vertices", e)
    e = e.reshape(-1, 2)
    v = values.astype(np.float64)
    deg = np.bincount(e.ravel(), minlength=len(v)).astype(np.float64)
    deg = np.maximum(deg, 1)
    for _ in range(iterations):
        acc = np.zeros_like(v)
        np.add.at(acc, e[:, 0], v[e[:, 1]])
        np.add.at(acc, e[:, 1], v[e[:, 0]])
        v = 0.5 * v + 0.5 * acc / (deg[:, None] if v.ndim > 1 else deg)
    return v


def add_point_attr(o, name, values, kind="FLOAT"):
    me = o.data
    if name in me.attributes:
        me.attributes.remove(me.attributes[name])
    if kind == "COLOR":
        a = me.attributes.new(name, "FLOAT_COLOR", "POINT")
        rgba = np.ones((len(values), 4))
        rgba[:, :values.shape[1]] = values
        a.data.foreach_set("color", rgba.ravel())
    else:
        a = me.attributes.new(name, "FLOAT", "POINT")
        a.data.foreach_set("value", np.asarray(values, np.float64))


# ---------------------------------------------------------------- regions

merc_pts = D["merc_points"]
merc_chain = [chain_of(NAMES[i]) for i in D["merc_weights"].argmax(1)]
merc_kd = KDTree(len(merc_pts))
for i, p in enumerate(merc_pts):
    merc_kd.insert(p, i)
merc_kd.balance()


def regions_for(v, forced=None):
    if forced:
        return np.full(len(v), REGIONS.index(forced))
    return np.array([REGIONS.index(merc_chain[merc_kd.find(p)[1]]) for p in v])


# ---------------------------------------------------------------- folds

def fold_profile(x):
    """Cloth folds: rounded ridges, sharp valleys; x in wavelengths."""
    return np.abs(np.sin(np.pi * x)) ** 0.75 - 0.55


def gauss(x, s):
    return np.exp(-(x / s) ** 2)


def limb_frame(v, a, b):
    """t (units along a->b), theta (around the limb), radius, for points v."""
    ax = np.array(b - a)
    L = np.linalg.norm(ax)
    ax /= L
    up = np.array([0, 0, 1.0]) if abs(ax[2]) < 0.8 else np.array([0, 1.0, 0])
    side = np.cross(ax, up)
    side /= np.linalg.norm(side)
    up = np.cross(side, ax)
    r = v - np.array(a)
    t = r @ ax
    rr = r - t[:, None] * ax
    theta = np.arctan2(rr @ side, rr @ up)
    return t, theta, np.linalg.norm(rr, axis=1), L


def displacement(v, region, fine):
    """Outward offset per vertex. fine=False gives only the broad shapes (for the game mesh)."""
    d = np.zeros(len(v))
    seam = np.zeros(len(v))
    rng = np.random.default_rng(3)
    # Broad bagginess everywhere but the hood top.
    bag = np.array([noise.noise(Vector(p * 0.045 + 7.0)) for p in v])
    d += bag * 1.1 * (region != REGIONS.index("hood"))
    for s, sgn in (("L", 1), ("R", -1)):
        # ---- arms
        m = region == REGIONS.index("arm" + s)
        if m.any():
            t, th, rad, L = limb_frame(v[m], BPOS[f"B {s} UpperArm"], BPOS[f"B {s} Hand"])
            t_elbow = (BPOS[f"B {s} ForeArm"] - BPOS[f"B {s} UpperArm"]).length
            ph = 1.4 * np.sin(th + rng.uniform(0, 6)) + 0.5 * np.sin(2 * th + rng.uniform(0, 6))
            if fine:
                d[m] += 0.85 * gauss(t - t_elbow, 7.5) * fold_profile(t / 3.4 + ph)
                d[m] += 0.7 * gauss(t - (L - 6), 5.0) * fold_profile(t / 2.8 + 0.8 * ph)
            d[m] += 0.55 * fold_profile(t / 9.0 + 0.5 * ph) * gauss(t - 10, 14)
            # Seams along the top and underside of the (T-posed) arm.
            arc = np.minimum(np.abs(np.angle(np.exp(1j * th))), np.abs(np.angle(np.exp(1j * (th - np.pi))))) * rad
            seam[m] = np.maximum(seam[m], gauss(arc, 0.7))
        # ---- legs
        m = region == REGIONS.index("leg" + s)
        if m.any():
            t, th, rad, L = limb_frame(v[m], BPOS[f"B {s} Thigh"], BPOS[f"B {s} Foot"])
            t_knee = (BPOS[f"B {s} Calf"] - BPOS[f"B {s} Thigh"]).length
            ph = 1.9 * np.sin(th + rng.uniform(0, 6)) + 0.8 * np.sin(2 * th + rng.uniform(0, 6)) + 0.6 * np.sin(3 * th + rng.uniform(0, 6))
            hem_t = BPOS[f"B {s} Thigh"].z - LEG_HEM_Z
            if fine:
                d[m] += 0.9 * gauss(t - t_knee, 8.0) * fold_profile(t / 3.6 + ph)
                d[m] += 0.6 * gauss(t - (hem_t - 5), 4.5) * fold_profile(t / 3.0 + 0.8 * ph)
            d[m] += 0.45 * fold_profile(t / 10.0 + 0.5 * ph + 0.3 * th) * gauss(t - 14, 16)
            # Seams down the outside and inside of the leg.
            side_th = np.pi / 2
            arc = np.minimum(np.abs(np.angle(np.exp(1j * (th - side_th)))), np.abs(np.angle(np.exp(1j * (th + side_th))))) * rad
            seam[m] = np.maximum(seam[m], gauss(arc, 0.7))
    # ---- torso: waist creases, armpit drapes, side seams
    m = region == REGIONS.index("torso")
    if m.any():
        p = v[m]
        x, y, z = p[:, 0], p[:, 1], p[:, 2]
        ang = np.arctan2(x, y)
        ph = 1.2 * np.sin(2 * ang + 1.0) + 0.6 * np.sin(5 * ang + 2.0)
        if fine:
            d[m] += 0.7 * gauss(z - 8, 7) * fold_profile(z / 4.2 + ph)
        diag = (np.abs(x) * 0.75 + z * 0.66)
        d[m] += 0.6 * gauss(np.abs(x) - 12, 7) * gauss(z - 36, 12) * fold_profile(diag / 6.5)
        d[m] += 0.5 * gauss(z + 4, 5) * fold_profile(x / 5.0 + 0.4 * ph) * (np.abs(x) < 16)
        rad = np.linalg.norm(p[:, :2], axis=1)
        side = np.minimum(np.abs(np.angle(np.exp(1j * (ang - np.pi / 2)))), np.abs(np.angle(np.exp(1j * (ang + np.pi / 2))))) * rad
        seam[m] = np.maximum(seam[m], gauss(side, 0.7) * (z < 54))
        # Yoke seam across the shoulders, front and back.
        seam[m] = np.maximum(seam[m], gauss(z - 50, 0.6) * (np.abs(x) < 20))
    # ---- hood: a few dents where it meets the shoulders
    m = region == REGIONS.index("hood")
    if m.any() and fine:
        p = v[m]
        ang = np.arctan2(p[:, 0], p[:, 1])
        d[m] += 0.45 * gauss(p[:, 2] - 58, 4) * fold_profile(ang * 2.2 + 0.4 * np.sin(3 * ang))
    return d, seam


def detail_suit(o, fine):
    v = verts_of(o)
    n = normals_of(o)
    region = regions_for(v)
    d, seam = displacement(v, region, fine)
    d = smooth_field(o, d, 2)
    mask = np.zeros((len(v), 3))
    if fine:
        # Zip flap down the front, from the crotch to the visor.
        flap = (np.clip((2.6 - np.abs(v[:, 0])) / 0.35, 0, 1) * (v[:, 1] > 4) *
                np.clip((v[:, 2] + 2) / 1.5, 0, 1) * np.clip((59 - v[:, 2]) / 1.0, 0, 1))
        # Tape over the leg hems (onto the boots); the hem's flat underside is a cap.
        legs = np.isin(region, [REGIONS.index("legL"), REGIONS.index("legR")])
        cap = legs & (v[:, 2] < LEG_HEM_Z + 0.4) & (n[:, 2] < -0.6)
        tape = np.clip((LEG_HEM_Z + 3.4 - v[:, 2]) / 0.3, 0, 1) * legs * ~cap
        seam = seam * (1 - flap) * (1 - tape)
        d += 0.28 * seam + 0.75 * flap + 0.35 * tape
        mask = np.stack([seam, flap, tape], 1)
        region = np.where(cap, REGIONS.index("cap"), region)
    set_verts(o, v + n * d[:, None])
    return region, mask


def detail_glove(o, side):
    v = verts_of(o)
    n = normals_of(o)
    wrist, elbow = BPOS[f"B {side} Hand"], BPOS[f"B {side} ForeArm"]
    ax = np.array(wrist - elbow)
    ax /= np.linalg.norm(ax)
    t = (v - np.array(wrist)) @ ax
    t_open = t.min()
    # The gauntlet's open end (towards the elbow) is a cap; tape wraps the cuff behind it.
    cap = (t < t_open + 0.4) & (n @ ax < -0.6)
    tape = np.clip((t_open + 3.6 - t) / 0.3, 0, 1) * ~cap
    # Wrinkles over the knuckles and the cuff.
    _, th, _, _ = limb_frame(v, elbow, wrist)
    d = 0.3 * gauss(t + 4, 3.5) * fold_profile(t / 1.8 + 0.8 * np.sin(th)) + 0.3 * tape
    set_verts(o, v + n * d[:, None])
    region = np.where(cap, REGIONS.index("cap"), REGIONS.index("hand" + side))
    return region, np.stack([np.zeros(len(v)), np.zeros(len(v)), tape], 1)


def detail_boot(o, side):
    v = verts_of(o)
    n = normals_of(o)
    sole_z = v[:, 2].min()
    lip = np.clip((sole_z + 1.5 - v[:, 2]) / 0.25, 0, 1) * (n[:, 2] > -0.7)
    ankle = BPOS[f"B {side} Foot"].z + 4
    d = 0.35 * lip + 0.35 * gauss(v[:, 2] - ankle, 3) * fold_profile(v[:, 2] / 2.2 + 0.6 * np.sin(np.arctan2(v[:, 0], v[:, 1]) * 2))
    set_verts(o, v + n * d[:, None])
    region = np.full(len(v), REGIONS.index("foot" + side))
    return region, np.stack([lip, np.zeros(len(v)), np.zeros(len(v))], 1)


# ---------------------------------------------------------------- gear

def raycast_surface(bvh, origin, direction):
    hit, normal, _, _ = bvh.ray_cast(origin + direction * 40, -direction, 60)
    return hit, normal


CUTS = []  # where gear covers the suit: the game mesh's faces there are removed


def build_visor(bvh):
    """Rounded-rectangle face window over the front of the hood, plus its gasket."""
    cols, rows = 24, 15
    yaw_half, pitch_half, pitch_mid = math.radians(64), math.radians(27), math.radians(1.5)
    pts, uvs, nors = [], [], []
    centre = HOOD_C + Vector((0, -2.0, -1.0))
    CUTS.append(("window", np.array(centre), yaw_half * 0.9, pitch_half * 0.85, pitch_mid))
    for j in range(rows):
        for i in range(cols):
            s, t = -1 + 2 * i / (cols - 1), -1 + 2 * j / (rows - 1)
            m = max(abs(s), abs(t))
            n4 = (s ** 4 + t ** 4) ** 0.25
            u, w = (s * m / n4, t * m / n4) if n4 > 0 else (0.0, 0.0)
            yaw, pitch = u * yaw_half, pitch_mid + w * pitch_half
            dirv = Vector((math.sin(yaw) * math.cos(pitch), math.cos(yaw) * math.cos(pitch), math.sin(pitch)))
            hit, nor = raycast_surface(bvh, centre, dirv)
            pts.append(hit)
            nors.append(nor)
            uvs.append(((u + 1) / 2, (w + 1) / 2))
    P = np.array(pts)
    N = np.array(nors)
    # The window is flatter than the hood: pull it towards a smooth fit, then stand it off.
    for _ in range(25):
        g = P.reshape(rows, cols, 3)
        a = g.copy()
        a[1:-1, 1:-1] = (g[:-2, 1:-1] + g[2:, 1:-1] + g[1:-1, :-2] + g[1:-1, 2:]) / 4
        P = (0.5 * g + 0.5 * a).reshape(-1, 3)
    border = np.zeros((rows, cols), bool)
    border[0, :] = border[-1, :] = border[:, 0] = border[:, -1] = True
    # Flattening pulls the middle inside the curved hood: push every point back out past it.
    c = np.array(centre)
    H = np.array(pts)
    rel = P - c
    dist = np.linalg.norm(rel, axis=1)
    need = np.linalg.norm(H - c, axis=1) + np.where(border.ravel(), 0.1, 0.8)
    P = c + rel / dist[:, None] * np.maximum(dist, need)[:, None]
    faces = []
    for j in range(rows - 1):
        for i in range(cols - 1):
            a, b, c, d = j * cols + i, j * cols + i + 1, (j + 1) * cols + i, (j + 1) * cols + i + 1
            faces += [(a, b, d), (a, d, c)]
    visor = make_mesh("visor", P, faces)
    # Visor UVs: planar, in the top half of the gear atlas (finish.py paints it there).
    me = visor.data
    uv = me.uv_layers.new(name="UVMap")
    vi = np.zeros(len(me.loops), np.int64)
    me.loops.foreach_get("vertex_index", vi)
    U = np.array(uvs)[vi]
    uv.data.foreach_set("uv", np.stack([0.01 + U[:, 0] * 0.98, 0.51 + U[:, 1] * 0.48], 1).ravel())
    fix_outward(visor, np.array(centre))

    # Gasket: a rubber tube swept around the window's border.
    ring = [(0, i) for i in range(cols)] + [(j, cols - 1) for j in range(1, rows)] + \
           [(rows - 1, i) for i in range(cols - 2, -1, -1)] + [(j, 0) for j in range(rows - 2, 0, -1)]
    ring_p = np.array([P[j * cols + i] for j, i in ring])
    ring_n = np.array([N[j * cols + i] for j, i in ring])
    seg, rad = 7, 0.85
    gp, gf = [], []
    k = len(ring_p)
    for a in range(k):
        tang = ring_p[(a + 1) % k] - ring_p[a - 1]
        tang /= np.linalg.norm(tang)
        nn = ring_n[a] - tang * (ring_n[a] @ tang)
        nn /= np.linalg.norm(nn)
        bn = np.cross(tang, nn)
        c = ring_p[a] + nn * 0.35
        for s in range(seg):
            ang = 2 * math.pi * s / seg
            gp.append(c + (nn * math.cos(ang) + bn * math.sin(ang)) * rad * np.array([1, 1, 1]))
    for a in range(k):
        for s in range(seg):
            p0, p1 = a * seg + s, a * seg + (s + 1) % seg
            q0, q1 = ((a + 1) % k) * seg + s, ((a + 1) % k) * seg + (s + 1) % seg
            gf += [(p0, q0, q1), (p0, q1, p1)]
    gasket = make_mesh("gasket", np.array(gp), gf)
    fix_outward_tube(gasket, ring_p, seg)
    return visor, gasket, centre


def fix_outward(o, centre):
    v = verts_of(o)
    f = tris_of(o)
    fn = np.cross(v[f[:, 1]] - v[f[:, 0]], v[f[:, 2]] - v[f[:, 0]])
    if (fn * (v[f].mean(1) - centre)).sum(1).mean() < 0:
        flip(o)


def fix_outward_tube(o, ring_p, seg):
    v = verts_of(o)
    f = tris_of(o)
    fn = np.cross(v[f[:, 1]] - v[f[:, 0]], v[f[:, 2]] - v[f[:, 0]])
    centres = np.repeat(ring_p, seg, axis=0)[f[:, 0]]
    if (fn * (v[f].mean(1) - centres)).sum(1).mean() < 0:
        flip(o)


def flip(o):
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bmesh.ops.reverse_faces(bm, faces=bm.faces[:])
    bm.to_mesh(o.data)
    bm.free()


def build_valve(bvh, name, origin, direction, radius=2.3):
    hit, nor = raycast_surface(bvh, origin, direction)
    CUTS.append(("disc", np.array(hit), np.array(nor), radius * 0.75))
    bpy.ops.mesh.primitive_cylinder_add(vertices=16, radius=radius, depth=1.3, location=hit + nor * 0.45)
    o = bpy.context.active_object
    o.name = name
    o.rotation_euler = nor.to_track_quat("Z", "Y").to_euler()
    bev = o.modifiers.new("bevel", "BEVEL")
    bev.width = 0.35
    bev.segments = 2
    bev.limit_method = "ANGLE"
    apply_all(o)
    # A raised cap with a smaller boss in the middle.
    bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=radius * 0.45, depth=0.6, location=hit + nor * 1.25)
    cap = bpy.context.active_object
    cap.rotation_euler = nor.to_track_quat("Z", "Y").to_euler()
    apply_all(cap)
    join([o, cap])
    return o


def apply_all(o):
    deps = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(o.evaluated_get(deps))
    old = o.data
    o.modifiers.clear()
    o.data = me
    if old.users == 0:
        bpy.data.meshes.remove(old)
    me.transform(o.matrix_world)
    o.matrix_world.identity()


def join(objs):
    target = objs[0]
    with bpy.context.temp_override(active_object=target, object=target, selected_editable_objects=objs, selected_objects=objs):
        bpy.ops.object.join()
    return target


# ---------------------------------------------------------------- low poly and UVs

def cut_under_gear(o):
    """Removes the game mesh's faces that the gear covers. In the engine the suit drew over
    the visor wherever the two overlapped (the visor stood well clear of the hood and still
    did not show), so nothing of the suit is left behind it; the gasket hides the edge."""
    v = verts_of(o)
    f = tris_of(o)
    c = v[f].mean(1)
    doomed = np.zeros(len(f), bool)
    for cut in CUTS:
        if cut[0] == "window":
            _, centre, yaw_half, pitch_half, pitch_mid = cut
            r = c - centre
            r /= np.linalg.norm(r, axis=1, keepdims=True)
            yaw = np.arctan2(r[:, 0], r[:, 1])
            pitch = np.arcsin(np.clip(r[:, 2], -1, 1)) - pitch_mid
            doomed |= (r[:, 1] > 0) & ((np.abs(yaw) / yaw_half) ** 4 + (np.abs(pitch) / pitch_half) ** 4 < 1)
        else:
            _, hit, nor, radius = cut
            rel = c - hit
            along = rel @ nor
            doomed |= (np.abs(along) < 2.0) & (np.linalg.norm(rel - along[:, None] * nor, axis=1) < radius)
    bm = bmesh.new()
    bm.from_mesh(o.data)
    bm.faces.ensure_lookup_table()
    bmesh.ops.delete(bm, geom=[bm.faces[i] for i in np.nonzero(doomed)[0]], context="FACES")
    bm.to_mesh(o.data)
    bm.free()
    print("cut under the gear:", int(doomed.sum()), "faces")


def decimated_copy(o, triangles, name):
    c = o.copy()
    c.data = o.data.copy()
    c.name = name
    bpy.context.scene.collection.objects.link(c)
    mod = c.modifiers.new("dec", "DECIMATE")
    mod.ratio = min(1.0, triangles / len(c.data.polygons))
    mod.use_collapse_triangulate = True
    apply_all(c)
    c.data.polygons.foreach_set("use_smooth", np.ones(len(c.data.polygons), bool))
    return c


def unwrap(o, angle=62, margin=0.0035):
    bpy.ops.object.select_all(action="DESELECT")
    o.select_set(True)
    bpy.context.view_layer.objects.active = o
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=math.radians(angle), island_margin=margin, area_weight=0.0, scale_to_bounds=False)
    bpy.ops.uv.pack_islands(rotate=True, margin=margin)
    bpy.ops.object.mode_set(mode="OBJECT")


# ---------------------------------------------------------------- baking

def setup_cycles():
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    try:
        prefs = bpy.context.preferences.addons["cycles"].preferences
        for kind in ("OPTIX", "CUDA"):
            try:
                prefs.compute_device_type = kind
                prefs.get_devices()
                if any(d.type == kind for d in prefs.devices):
                    for d in prefs.devices:
                        d.use = d.type == kind
                    scene.cycles.device = "GPU"
                    print("bake device:", kind)
                    break
            except TypeError:
                continue
    except Exception as e:  # noqa: BLE001
        print("GPU setup failed, baking on the CPU:", e)
    scene.cycles.samples = 64
    scene.render.bake.margin = 16
    scene.render.bake.margin_type = "EXTEND"
    world = bpy.data.worlds.new("bake") if not scene.world else scene.world
    scene.world = world
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (1, 1, 1, 1)


def emit_material(name, attr=None, position=False):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    em = nt.nodes.new("ShaderNodeEmission")
    nt.links.new(em.outputs[0], out.inputs[0])
    if position:
        g = nt.nodes.new("ShaderNodeNewGeometry")
        nt.links.new(g.outputs["Position"], em.inputs["Color"])
    elif attr:
        a = nt.nodes.new("ShaderNodeAttribute")
        a.attribute_name = attr
        nt.links.new(a.outputs["Color"], em.inputs["Color"])
    return m


def target_material(name, image):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = image
    nt.nodes.active = tex
    return m, tex


def bake(kind, active, sources, image, tex_node, **kw):
    tex_node.image = image
    bpy.ops.object.select_all(action="DESELECT")
    for s in sources:
        s.select_set(True)
    active.select_set(True)
    bpy.context.view_layer.objects.active = active
    args = dict(type=kind, margin=16, use_clear=True, target="IMAGE_TEXTURES")
    if sources:
        args.update(use_selected_to_active=True, cage_extrusion=kw.get("cage", 1.6), max_ray_distance=kw.get("ray", 3.2))
    if kind == "NORMAL":
        args.update(normal_space="OBJECT")
    bpy.ops.object.bake(**args)


def save_npy(image, path):
    w, h = image.size
    a = np.array(image.pixels[:], np.float32).reshape(h, w, 4)
    np.save(path, a[::-1])  # row 0 = top (v = 1)


def with_material(objs, mat):
    saved = {}
    for o in objs:
        saved[o.name] = [s.material for s in o.material_slots]
        o.data.materials.clear()
        o.data.materials.append(mat)
    return saved


def restore_materials(objs, saved):
    for o in objs:
        o.data.materials.clear()
        for m in saved[o.name]:
            o.data.materials.append(m)


# ---------------------------------------------------------------- main

def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.unit_settings.system = "NONE"

    hi = {}
    regions = {}
    masks = {}
    for part in ("suit", "gloveL", "gloveR", "bootL", "bootR"):
        hi[part] = make_mesh(part + "_hi", D[part + "_v"], D[part + "_f"])

    # The game mesh keeps the broad folds; the fine ones are baked.
    mid = make_mesh("suit_mid", D["suit_v"], D["suit_f"])
    detail_suit(mid, fine=False)
    regions["suit"], masks["suit"] = detail_suit(hi["suit"], fine=True)
    for s in ("L", "R"):
        regions["glove" + s], masks["glove" + s] = detail_glove(hi["glove" + s], s)
        regions["boot" + s], masks["boot" + s] = detail_boot(hi["boot" + s], s)
    for part, o in hi.items():
        add_point_attr(o, "region", np.repeat(((regions[part] + 0.5) / 16.0)[:, None], 3, 1), "COLOR")
        add_point_attr(o, "mask", masks[part], "COLOR")
    print("high-poly parts detailed")

    # Gear, placed on the detailed hood.
    bvh = BVHTree.FromObject(hi["suit"], bpy.context.evaluated_depsgraph_get())
    visor, gasket, centre = build_visor(bvh)
    valves = [build_valve(bvh, "valve_back" + s, HOOD_C + Vector((0, 0, -4)), Vector((x, -1.0, -0.15)).normalized())
              for s, x in (("L", 0.55), ("R", -0.55))]
    valves.append(build_valve(bvh, "valve_chest", Vector((0, 0, 42)), Vector((0.62, 1.0, 0.1)).normalized(), 2.0))
    gear_parts = {"visor": [visor], "gasket": [gasket], "valve": valves}
    for kind, objs in gear_parts.items():
        for o in objs:
            add_point_attr(o, "region", np.full((len(o.data.vertices), 3), (GEAR_PARTS.index(kind) + 0.5) / 16.0), "COLOR")
    print("gear built")

    # Low poly.
    lo = {"suit": decimated_copy(mid, TRIANGLES["suit"], "suit_lo")}
    cut_under_gear(lo["suit"])
    for part in ("gloveL", "gloveR", "bootL", "bootR"):
        lo[part] = decimated_copy(hi[part], TRIANGLES[part], part + "_lo")
    bpy.data.objects.remove(mid)
    part_ids = ["suit", "gloveL", "gloveR", "bootL", "bootR", "visor", "gasket", "valve"]
    for part, o in lo.items():
        add_point_attr(o, "part", np.full(len(o.data.vertices), float(part_ids.index(part))))
    gear_objs = []
    for kind, objs in gear_parts.items():
        for o in objs:
            add_point_attr(o, "part", np.full(len(o.data.vertices), float(part_ids.index(kind))))
            gear_objs.append(o)
    # The visor keeps its planar UVs; the rest of the gear goes below it in the atlas.
    suit_lo = join([lo["suit"], lo["gloveL"], lo["gloveR"], lo["bootL"], lo["bootR"]])
    suit_lo.name = "HazmatSuit"
    unwrap(suit_lo)
    rest = join([gasket] + valves)
    unwrap(rest, angle=50, margin=0.006)
    uv = rest.data.uv_layers.active
    a = np.zeros(len(rest.data.loops) * 2)
    uv.data.foreach_get("uv", a)
    a = a.reshape(-1, 2)
    a = np.stack([0.01 + a[:, 0] * 0.98, 0.01 + a[:, 1] * 0.48], 1)  # bottom half
    uv.data.foreach_set("uv", a.ravel())
    gear_lo = join([visor, rest])
    gear_lo.name = "HazmatGear"
    for o in (suit_lo, gear_lo):
        tri = o.modifiers.new("tri", "TRIANGULATE")
        apply_all(o)
    print("low poly:", len(suit_lo.data.polygons), "suit triangles,", len(gear_lo.data.polygons), "gear triangles")

    # ---- bakes
    setup_cycles()
    hi_objs = list(hi.values())
    imgs = {}
    for name, size in (("suit", SUIT_TEX), ("gear", GEAR_TEX)):
        for kind in ("normal", "ao", "pos", "region", "mask"):
            imgs[name, kind] = bpy.data.images.new(f"{name}_{kind}", size, size, float_buffer=True, alpha=False)
    suit_mat, suit_node = target_material("HazmatSuit", imgs["suit", "ao"])
    gear_mat, gear_node = target_material("HazmatGear", imgs["gear", "ao"])
    suit_lo.data.materials.append(suit_mat)
    gear_lo.data.materials.append(gear_mat)
    # The game mesh must not shadow the parts it is baked from.
    for o in (suit_lo,):
        o.visible_diffuse = o.visible_glossy = o.visible_shadow = o.visible_transmission = False
        o.visible_volume_scatter = False

    pos_mat = emit_material("bake_pos", position=True)
    reg_mat = emit_material("bake_region", attr="region")
    mask_mat = emit_material("bake_mask", attr="mask")
    for o in hi_objs:
        o.data.materials.append(pos_mat)

    scene.cycles.samples = 1
    bake("NORMAL", suit_lo, hi_objs, imgs["suit", "normal"], suit_node)
    bake("NORMAL", gear_lo, [], imgs["gear", "normal"], gear_node)
    bake("EMIT", suit_lo, hi_objs, imgs["suit", "pos"], suit_node)
    # The gear's own emission: swap its material for each pass, keeping the target image node.
    for kind, src in (("pos", pos_mat), ("region", reg_mat)):
        gm = src.copy()
        gn = gm.node_tree.nodes.new("ShaderNodeTexImage")
        gn.image = imgs["gear", kind]
        gm.node_tree.nodes.active = gn
        saved = with_material([gear_lo], gm)
        bake("EMIT", gear_lo, [], imgs["gear", kind], gn)
        restore_materials([gear_lo], saved)
    saved = with_material(hi_objs, reg_mat)
    bake("EMIT", suit_lo, hi_objs, imgs["suit", "region"], suit_node)
    restore_materials(hi_objs, saved)
    saved = with_material(hi_objs, mask_mat)
    bake("EMIT", suit_lo, hi_objs, imgs["suit", "mask"], suit_node)
    restore_materials(hi_objs, saved)
    scene.cycles.samples = 128
    scene.world.cycles_visibility.diffuse = True
    bake("AO", suit_lo, hi_objs, imgs["suit", "ao"], suit_node, cage=1.6, ray=3.2)
    bake("AO", gear_lo, [], imgs["gear", "ao"], gear_node)
    for (name, kind), im in imgs.items():
        save_npy(im, OUT / f"bake_{name}_{kind}.npy")
    print("bakes saved")

    # ---- export the game mesh
    data = {}
    for key, o in (("suit", suit_lo), ("gear", gear_lo)):
        me = o.data
        v = verts_of(o)
        f = tris_of(o)
        uvd = np.zeros(len(me.loops) * 2)
        me.uv_layers.active.data.foreach_get("uv", uvd)
        ls = np.zeros(len(me.polygons), np.int64)
        me.polygons.foreach_get("loop_start", ls)
        uvd = uvd.reshape(-1, 2)
        tri_uv = np.stack([uvd[ls + k] for k in range(3)], 1)
        part = np.zeros(len(v))
        me.attributes["part"].data.foreach_get("value", part)
        data[key + "_v"], data[key + "_f"], data[key + "_uv"], data[key + "_part"] = v, f, tri_uv, part
    data["part_ids"] = np.array(part_ids)
    data["visor_centre"] = np.array(centre)
    np.savez_compressed(OUT / "low.npz", **data)

    # ---- tidy the scene for people who open it: AO on the game mesh, high-poly hidden
    for o in hi_objs:
        o.hide_render = True
        o.hide_set(True)
        o.data.materials.clear()
    suit_node.image = imgs["suit", "ao"]
    gear_node.image = imgs["gear", "ao"]
    for im in imgs.values():
        im.pack()
    for m in (pos_mat, reg_mat, mask_mat):
        m.use_fake_user = False
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / "hazmat.blend"), compress=True)
    print("saved", OUT / "hazmat.blend")


main()

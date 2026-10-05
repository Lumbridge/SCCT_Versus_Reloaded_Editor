"""Builds a hazmat suit for the SCCT Versus merc, rigged to the stock merc skeleton.

    python build_hazmat.py --install "C:\\...\\SVM 4.0 Public Beta Mapping Ver" --out out [--preview]

Writes into --out:
    HazmatMerc.psk     the suit, on DEF_01's 42 bones, so SPerso.Def drives it unchanged
    HazmatSuit.tga     1024x1024 suit atlas (material 0, HazmatSuit)
    HazmatGear.tga     512x512 visor and respirator (material 1, HazmatGear)
    hazmat.json        counts, the goggle light offset for Character Skins, the mesh settings
    preview_*.png      software renders (with --preview)

How the suit is made (no modelling package needed):
  1. The merc's body surface (DEF_01 minus helmet, pouches and holster) is sampled densely.
  2. A distance field is built around it: the suit is the surface a few units out from the
     body (baggier on the torso and legs, tight on the hands and boots), smoothly joined to a
     boxy hood around the head and an air-pack hump on the back. Marching cubes extracts it
     and it is simplified to about 6000 triangles.
  3. Each suit vertex takes the bone weights of the nearest merc skin (helmet bones moved to
     B Head), smoothed over the suit so creases bend softly. The visor and respirator are
     rigid on B Head.
  4. UVs: one chart per body part (cylindrical around the bone, spherical on the hood),
     packed into one atlas. The texture is painted procedurally in 3D (rubber, folds,
     taped seams, gloves, boots, labels) and baked into that atlas.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy.spatial import cKDTree
from scipy import ndimage

from merc_mesh import bone_globals, read_merc

RNG = np.random.default_rng(7)

# ---------------------------------------------------------------- suit shape

# How far the suit stands off the merc's skin, per bone (units; the merc is 176 tall).
STANDOFF = {
    "B Pelvis": 4.6, "B Spine": 4.6, "B Spine1": 4.4, "B Spine2": 4.0, "B Neck": 3.6, "B Head": 3.6,
    "B L Clavicle": 3.6, "B R Clavicle": 3.6, "B L UpperArm": 3.6, "B R UpperArm": 3.6,
    "B L ForeArm": 3.0, "B R ForeArm": 3.0, "B L Thigh": 4.4, "B R Thigh": 4.4,
    "B L Calf": 3.6, "B R Calf": 3.6, "B L Foot": 2.1, "B R Foot": 2.1, "B L Toe0": 2.0, "B R Toe0": 2.0,
}
HAND_STANDOFF = 1.5          # gloves: hand and finger bones
VOXEL = 1.0
TARGET_TRIANGLES = 6200
HOOD_CENTER = np.array([0.0, 1.2, 72.0])
HOOD_RADII = np.array([12.6, 14.2, 15.0])
HOOD_POWER = 2.5             # superellipsoid: >2 is boxier
PACK_CENTER = np.array([0.0, -13.5, 40.0])
PACK_RADII = np.array([11.0, 7.0, 14.0])
CROTCH_Z = -7.0              # legs are carved apart below this height
CROTCH_GAP = 1.1             # half the gap between the legs


def chain_of(name: str) -> str:
    """Body part (UV chart and paint region) of a bone."""
    n = name
    if n in ("B Neck", "B Head", "Laser", "DEF_01_Casque"):
        return "hood"
    for side in ("L", "R"):
        if n.startswith(f"B {side} Hand") or n.startswith(f"B {side} Finger"):
            return f"hand{side}"
        if n in (f"B {side} UpperArm", f"B {side} ForeArm"):
            return f"arm{side}"
        if n in (f"B {side} Thigh", f"B {side} Calf"):
            return f"leg{side}"
        if n in (f"B {side} Foot", f"B {side} Toe0"):
            return f"foot{side}"
    return "torso"


def smin(a, b, k):
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0.0, 1.0)
    return b * (1 - h) + a * h - k * h * (1 - h)


def superellipsoid(p, center, radii, power):
    q = np.abs((p - center) / radii)
    r = (q ** power).sum(-1) ** (1.0 / power)
    return (r - 1.0) * radii.min()


def sample_body(merc, weights):
    """Points on the merc's skin (helmet, pouches and eyes left out) with their bone weights."""
    import scipy.sparse as sp
    import scipy.sparse.csgraph as cg
    fp = merc.wedges[merc.faces]
    n = len(merc.points)
    a = sp.coo_matrix((np.ones(fp.size), (fp.ravel(), np.roll(fp, 1, 1).ravel())), shape=(n, n))
    _, label = cg.connected_components(a, directed=False)
    names = [b.name for b in merc.bones]
    keep_part = []
    for c in range(label.max() + 1):
        pts = np.nonzero(label == c)[0]
        dominant = names[int(weights[pts].sum(0).argmax())]
        keep_part.append(len(pts) >= 25 and dominant not in ("DEF_01_Casque", "Laser", "HOLSTER"))
    keep = np.array([keep_part[label[f[0]]] for f in fp])
    tri = fp[keep]
    p0, p1, p2 = (merc.points[tri[:, i]] for i in range(3))
    area = 0.5 * np.linalg.norm(np.cross(p1 - p0, p2 - p0), axis=1)
    out_p, out_w = [], []
    for t in range(len(tri)):
        k = max(3, int(area[t] / 0.12))
        r1, r2 = RNG.random(k), RNG.random(k)
        s = np.sqrt(r1)
        b0, b1, b2 = 1 - s, s * (1 - r2), s * r2
        out_p.append(b0[:, None] * p0[t] + b1[:, None] * p1[t] + b2[:, None] * p2[t])
        w = weights[tri[t]]
        out_w.append(b0[:, None] * w[0] + b1[:, None] * w[1] + b2[:, None] * w[2])
    return np.concatenate(out_p), np.concatenate(out_w)


def point_weights(merc):
    w = np.zeros((len(merc.points), len(merc.bones)))
    for weight, point, bone in merc.influences:
        w[int(point), int(bone)] += weight
    s = w.sum(1, keepdims=True)
    return w / np.where(s > 0, s, 1)


def build_field(samples, sample_w, names):
    radius_of_bone = np.array([HAND_STANDOFF if chain_of(n).startswith("hand") else STANDOFF.get(n, 4.0) for n in names])
    sample_r = sample_w @ radius_of_bone
    lo = np.minimum(samples.min(0), HOOD_CENTER - HOOD_RADII) - 8
    hi = np.maximum(samples.max(0), HOOD_CENTER + HOOD_RADII) + 8
    axes = [np.arange(lo[i], hi[i] + VOXEL, VOXEL) for i in range(3)]
    gx, gy, gz = np.meshgrid(*axes, indexing="ij")
    grid = np.stack([gx, gy, gz], -1).reshape(-1, 3)
    tree = cKDTree(samples)
    d, _ = tree.query(grid, k=1, workers=-1)
    field = d - 4.0
    near = d < 12
    dk, ik = tree.query(grid[near], k=12, workers=-1)
    wk = 1.0 / (dk + 0.6) ** 2
    r = (sample_r[ik] * wk).sum(1) / wk.sum(1)
    field[near] = dk[:, 0] - r
    # Keep the legs apart below the crotch, or the suit becomes a skirt between them.
    gap = CROTCH_GAP * np.clip((CROTCH_Z - grid[:, 2]) / 5.0, 0, 1)
    field = np.maximum(field, gap - np.abs(grid[:, 0]))
    hood = superellipsoid(grid, HOOD_CENTER, HOOD_RADII, HOOD_POWER)
    pack = superellipsoid(grid, PACK_CENTER, PACK_RADII, 2.2)
    field = smin(field, hood, 3.0)
    field = smin(field, pack, 5.0)
    return field.reshape(gx.shape), lo, axes


def extract_surface(field, lo):
    from skimage import measure
    import fast_simplification
    verts, faces, _, _ = measure.marching_cubes(field, 0.0, spacing=(VOXEL,) * 3)
    verts += lo
    # Keep the outer skin only (the unsigned field also has shells inside thick parts).
    import scipy.sparse as sp
    import scipy.sparse.csgraph as cg
    n = len(verts)
    a = sp.coo_matrix((np.ones(faces.size), (faces.ravel(), np.roll(faces, 1, 1).ravel())), shape=(n, n))
    _, label = cg.connected_components(a, directed=False)
    span = [np.ptp(verts[label == c], axis=0).prod() for c in range(label.max() + 1)]
    main = int(np.argmax(span))
    used = label == main
    remap = -np.ones(n, np.int64)
    remap[used] = np.arange(used.sum())
    faces = remap[faces[used[faces[:, 0]]]]
    verts = verts[used]
    verts = taubin(verts, faces, 6)
    v2, f2 = fast_simplification.simplify(verts.astype(np.float32), faces.astype(np.int32),
                                          target_reduction=1.0 - TARGET_TRIANGLES / len(faces), agg=6)
    v2 = taubin(v2.astype(np.float64), f2.astype(np.int64), 3)
    return v2, f2.astype(np.int64)


def neighbours(n, faces):
    import scipy.sparse as sp
    rows = np.concatenate([faces[:, 0], faces[:, 1], faces[:, 2], faces[:, 1], faces[:, 2], faces[:, 0]])
    cols = np.concatenate([faces[:, 1], faces[:, 2], faces[:, 0], faces[:, 0], faces[:, 1], faces[:, 2]])
    a = sp.coo_matrix((np.ones(len(rows)), (rows, cols)), shape=(n, n)).tocsr()
    a.data[:] = 1.0
    deg = np.asarray(a.sum(1)).ravel()
    return a, deg


def taubin(v, f, iterations, lam=0.5, mu=-0.53):
    a, deg = neighbours(len(v), f)
    for _ in range(iterations):
        for k in (lam, mu):
            v = v + k * ((a @ v) / deg[:, None] - v)
    return v


def vertex_normals(v, f):
    n = np.zeros_like(v)
    fn = np.cross(v[f[:, 1]] - v[f[:, 0]], v[f[:, 2]] - v[f[:, 0]])
    for i in range(3):
        np.add.at(n, f[:, i], fn)
    return n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)


# ---------------------------------------------------------------- noise

def _hash3(ix, iy, iz):
    h = (ix * 73856093) ^ (iy * 19349663) ^ (iz * 83492791)
    h = (h ^ (h >> 13)) * 1274126177
    return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0


def value_noise(p):
    q = np.floor(p).astype(np.int64)
    t = p - q
    t = t * t * (3 - 2 * t)
    out = 0.0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = (t[:, 0] if dx else 1 - t[:, 0]) * (t[:, 1] if dy else 1 - t[:, 1]) * (t[:, 2] if dz else 1 - t[:, 2])
                out = out + w * _hash3(q[:, 0] + dx, q[:, 1] + dy, q[:, 2] + dz)
    return out


def fbm(p, octaves=4):
    total, amp, norm = 0.0, 1.0, 0.0
    for o in range(octaves):
        total = total + amp * value_noise(p * (2 ** o) + o * 17.3)
        norm += amp
        amp *= 0.5
    return total / norm


# ---------------------------------------------------------------- rigging

def transfer_weights(verts, faces, samples, sample_w, names):
    tree = cKDTree(samples)
    d, i = tree.query(verts, k=10, workers=-1)
    w = 1.0 / (d + 0.5) ** 3
    weights = (sample_w[i] * w[:, :, None]).sum(1) / w.sum(1)[:, None]
    # Helmet bones are the head's.
    head = names.index("B Head")
    for extra in ("DEF_01_Casque", "Laser", "HOLSTER"):
        k = names.index(extra)
        target = head if extra != "HOLSTER" else names.index("B Spine1")
        weights[:, target] += weights[:, k]
        weights[:, k] = 0
    # The hood above the neck rides on the head alone; below, it blends into the merc's neck.
    rel = (verts - HOOD_CENTER) / HOOD_RADII
    inside_hood = (np.abs(rel) ** HOOD_POWER).sum(1) ** (1 / HOOD_POWER) < 1.25
    blend = np.clip((verts[:, 2] - 60.0) / 6.0, 0, 1) * inside_hood
    rigid = np.zeros_like(weights)
    rigid[:, head] = 1
    weights = weights * (1 - blend[:, None]) + rigid * blend[:, None]
    a, deg = neighbours(len(verts), faces)
    for _ in range(4):
        weights = 0.5 * weights + 0.5 * (a @ weights) / deg[:, None]
    return limit_weights(weights)


def limit_weights(weights, keep=4, floor=0.03):
    w = weights.copy()
    order = np.argsort(-w, axis=1)
    mask = np.zeros_like(w, bool)
    np.put_along_axis(mask, order[:, :keep], True, axis=1)
    w[~mask] = 0
    w[w < floor] = 0
    return w / w.sum(1, keepdims=True)


# ---------------------------------------------------------------- UV charts

def chart_frames(globals_, names):
    """Per body part: origin and axis of the cylinder its UVs wrap around."""
    pos = {n: g[:3, 3] for n, g in zip(names, globals_)}
    frames = {}
    for s in ("L", "R"):
        sh, wr = pos[f"B {s} UpperArm"], pos[f"B {s} Hand"]
        frames[f"arm{s}"] = (sh, wr - sh)
        tip = (pos[f"B {s} Finger1"] + pos[f"B {s} Finger2"]) / 2
        frames[f"hand{s}"] = (wr, tip - wr)
        hip, ank = pos[f"B {s} Thigh"], pos[f"B {s} Foot"]
        frames[f"leg{s}"] = (hip, ank - hip)
        heel = ank + np.array([0, -6.0, 6.0])
        frames[f"foot{s}"] = (heel, pos[f"B {s} Toe0"] + np.array([0, 9.0, 0]) - heel)
    frames["torso"] = (np.array([0, 0, -10.0]), np.array([0, 0, 70.0]))
    frames["hood"] = (HOOD_CENTER, np.array([0, 0, 1.0]))
    return frames


def face_islands(faces, member):
    """Connected groups (sharing an edge) among the faces listed in member."""
    import scipy.sparse as sp
    import scipy.sparse.csgraph as cg
    edge_owner = {}
    rows, cols = [], []
    for li, f in enumerate(member):
        a, b, c = faces[f]
        for e in ((a, b), (b, c), (c, a)):
            key = (min(e), max(e))
            if key in edge_owner:
                rows.append(edge_owner[key])
                cols.append(li)
            else:
                edge_owner[key] = li
    n = len(member)
    g = sp.coo_matrix((np.ones(len(rows)), (rows, cols)), shape=(n, n))
    k, label = cg.connected_components(g, directed=False)
    return [member[label == i] for i in range(k)]


def make_uvs(verts, faces, chart_of_face, frames, atlas_size, pad_px):
    """Wedges (point, u, v) per face corner. Each body part wraps around its bone
    (cylindrical); faces turned along the bone (shoulder tops, soles, the hood's crown)
    are projected flat instead. Islands are packed into one square atlas at one scale."""
    fn = np.cross(verts[faces[:, 1]] - verts[faces[:, 0]], verts[faces[:, 2]] - verts[faces[:, 0]])
    fn /= np.maximum(np.linalg.norm(fn, axis=1, keepdims=True), 1e-9)
    centroid = verts[faces].mean(1)
    keys = []
    for f in range(len(faces)):
        origin, axis = frames[chart_of_face[f]]
        ax = axis / np.linalg.norm(axis)
        rel = centroid[f] - origin
        radial = rel - ax * (rel @ ax)
        radial /= max(np.linalg.norm(radial), 1e-9)
        if abs(fn[f] @ radial) >= 0.5:
            keys.append((chart_of_face[f], "cyl"))
        else:
            k = int(np.argmax(np.abs(fn[f])))
            keys.append((chart_of_face[f], "box", k, int(np.sign(fn[f][k]))))
    keys_arr = np.array([str(k) for k in keys])
    islands = []
    for key in sorted(set(keys), key=str):
        member = np.nonzero(keys_arr == str(key))[0]
        for isl in face_islands(faces, member):
            islands.append((key, isl))
    raw = []
    for key, fi in islands:
        corners = faces[fi]
        p = verts[corners]                                   # (k, 3, 3)
        if key[1] == "box":
            k = key[2]
            a, b = [i for i in range(3) if i != k]
            u, v = p[..., a] * key[3], -p[..., b] if k != 2 else p[..., b]
        else:
            origin, axis = frames[key[0]]
            ax = axis / np.linalg.norm(axis)
            ref = np.array([0, -1.0, 0]) if abs(ax[1]) < 0.8 else np.array([0, 0, 1.0])
            ref = ref - ax * (ref @ ax)
            ref /= np.linalg.norm(ref)
            side = np.cross(ax, ref)
            rel = p - origin
            along = rel @ ax
            radial = rel - along[..., None] * ax
            ang = np.arctan2(radial @ side, radial @ ref)
            # Put the seam in the island's widest angular gap.
            cang = np.sort(np.mod(ang.mean(1), 2 * np.pi))
            gaps = np.diff(np.concatenate([cang, cang[:1] + 2 * np.pi]))
            seam = cang[int(np.argmax(gaps))] + gaps.max() / 2
            ang = np.mod(ang - seam, 2 * np.pi)
            wrap = (ang.max(1) - ang.min(1)) > np.pi
            ang = np.where(wrap[:, None] & (ang < np.pi), ang + 2 * np.pi, ang)
            r = np.linalg.norm(radial, axis=2).mean()
            u, v = ang * r, along
        raw.append((fi, u - u.min(), v - v.min()))
    sizes = [(u.max(), v.max()) for _, u, v in raw]
    order = sorted(range(len(raw)), key=lambda i: -sizes[i][1])
    scale = 8.0
    while True:
        placed, x, y, shelf, ok = {}, pad_px, pad_px, 0.0, True
        for i in order:
            w, h = sizes[i][0] * scale, sizes[i][1] * scale
            if x + w + pad_px > atlas_size:
                x, y, shelf = pad_px, y + shelf + 2 * pad_px, 0.0
            if x + w + pad_px > atlas_size or y + h + pad_px > atlas_size:
                ok = False
                break
            placed[i] = (x, y)
            x += w + 2 * pad_px
            shelf = max(shelf, h)
        if ok:
            break
        scale *= 0.98
    wedge_key, wedges, tri = {}, [], np.zeros_like(faces)
    for i, (fi, u, v) in enumerate(raw):
        px = (placed[i][0] + u * scale) / atlas_size
        py = (placed[i][1] + v * scale) / atlas_size
        for row, f in enumerate(fi):
            for k in range(3):
                key = (faces[f, k], round(float(px[row, k]), 6), round(float(py[row, k]), 6))
                if key not in wedge_key:
                    wedge_key[key] = len(wedges)
                    wedges.append(key)
                tri[f, k] = wedge_key[key]
    return np.array(wedges, np.float64), tri, scale, len(islands)


# ---------------------------------------------------------------- gear (visor, respirator)

class Gear:
    def __init__(self):
        self.points, self.uvs, self.faces = [], [], []

    def add(self, pts, uvs, faces):
        base = sum(len(p) for p in self.points)
        self.points.append(np.asarray(pts, np.float64))
        self.uvs.append(np.asarray(uvs, np.float64))
        self.faces.append(np.asarray(faces, np.int64) + base)

    def arrays(self):
        return np.concatenate(self.points), np.concatenate(self.uvs), np.concatenate(self.faces)


def surface_along(field_fn, origin, direction, start=4.0, stop=30.0):
    d = direction / np.linalg.norm(direction)
    t = start
    while t < stop:
        if field_fn(origin + d * t) > 0:
            lo_t, hi_t = t - 0.25, t
            for _ in range(20):
                mid = (lo_t + hi_t) / 2
                if field_fn(origin + d * mid) > 0:
                    hi_t = mid
                else:
                    lo_t = mid
            return origin + d * lo_t
        t += 0.25
    raise RuntimeError("no surface along ray")


def grid_faces(rows, cols, flip=False):
    f = []
    for r in range(rows - 1):
        for c in range(cols - 1):
            a, b, cc, d = r * cols + c, r * cols + c + 1, (r + 1) * cols + c, (r + 1) * cols + c + 1
            f += [(a, cc, b), (b, cc, d)] if not flip else [(a, b, cc), (b, d, cc)]
    return f


def build_visor(gear, field_fn, centre):
    """A bubble visor on the front of the hood with a rubber gasket around it."""
    rows, cols = 9, 15
    yaw = np.radians(np.linspace(-58, 58, cols))
    pitch = np.radians(np.linspace(-18, 30, rows))
    pts, uvs, normals = [], [], []
    for i, ph in enumerate(pitch):
        for j, th in enumerate(yaw):
            d = np.array([np.sin(th) * np.cos(ph), np.cos(th) * np.cos(ph), np.sin(ph)])
            s = surface_along(field_fn, centre, d)
            pts.append(s + d * 0.9)
            normals.append(d)
            uvs.append((0.02 + 0.96 * j / (cols - 1), 0.02 + 0.44 * (1 - i / (rows - 1))))
    pts = np.array(pts)
    # Round the corners: pull the four corner vertices in a little.
    gear.add(pts, uvs, grid_faces(rows, cols, flip=True))
    # Gasket: a strip around the border, raised above the visor and sunk into the hood.
    ring = [(0, j) for j in range(cols)] + [(i, cols - 1) for i in range(1, rows)] + \
           [(rows - 1, j) for j in range(cols - 2, -1, -1)] + [(i, 0) for i in range(rows - 2, 0, -1)]
    outer, inner, sunk, ruv = [], [], [], []
    for k, (i, j) in enumerate(ring):
        p = pts[i * cols + j]
        n = normals[i * cols + j]
        c = np.array([(cols - 1) / 2, (rows - 1) / 2])
        toward = np.array([j, i]) - c
        toward = toward / np.linalg.norm(toward)
        # Out from the visor centre along the hood surface.
        tangent_u = np.array([np.cos(yaw[j]), -np.sin(yaw[j]), 0])
        tangent_v = np.cross(n, tangent_u)
        out_dir = tangent_u * toward[0] + tangent_v * toward[1]
        outer.append(p + out_dir * 1.6 - n * 0.2)
        inner.append(p + n * 0.5 - out_dir * 0.3)
        sunk.append(p + out_dir * 2.2 - n * 1.6)
        ruv.append(k / len(ring))
    n_ring = len(ring)
    strip_pts = np.array(inner + outer + sunk)
    strip_uv = [(0.02 + 0.46 * u, 0.53) for u in ruv] + [(0.02 + 0.46 * u, 0.58) for u in ruv] + \
               [(0.02 + 0.46 * u, 0.62) for u in ruv]
    f = []
    for k in range(n_ring):
        a, b = k, (k + 1) % n_ring
        f += [(a, b, n_ring + a), (b, n_ring + b, n_ring + a)]
        f += [(n_ring + a, n_ring + b, 2 * n_ring + a), (n_ring + b, 2 * n_ring + b, 2 * n_ring + a)]
    # The gasket's inner lip also covers the visor edge.
    rim_inner = np.array([pts[i * cols + j] - normals[i * cols + j] * 0.6 for i, j in ring])
    strip_pts = np.concatenate([strip_pts, rim_inner])
    strip_uv += [(0.02 + 0.46 * u, 0.52) for u in ruv]
    for k in range(n_ring):
        a, b = k, (k + 1) % n_ring
        f += [(3 * n_ring + a, 3 * n_ring + b, a), (3 * n_ring + b, b, a)]
    gear.add(strip_pts, strip_uv, fix_winding(strip_pts, f, centre))
    visor_front = surface_along(field_fn, centre, np.array([0, 1.0, np.tan(np.radians(6))])) + np.array([0, 0.9, 0])
    return visor_front


def fix_winding(pts, faces, centre):
    """Faces turned to face away from centre (the outside of the hood)."""
    pts = np.asarray(pts)
    out = []
    for a, b, c in faces:
        n = np.cross(pts[b] - pts[a], pts[c] - pts[a])
        if n @ ((pts[a] + pts[b] + pts[c]) / 3 - centre) < 0:
            out.append((a, c, b))
        else:
            out.append((a, b, c))
    return out


def cylinder(base, axis, radius, length, segments, uv_box, cap_uv=None, taper=1.0, rings=2):
    """Open-ended or capped cylinder along axis; returns pts, uvs, faces (outward)."""
    ax = axis / np.linalg.norm(axis)
    ref = np.array([0, 0, 1.0]) if abs(ax[2]) < 0.9 else np.array([1.0, 0, 0])
    e1 = np.cross(ax, ref)
    e1 /= np.linalg.norm(e1)
    e2 = np.cross(ax, e1)
    pts, uvs, faces = [], [], []
    u0, v0, u1, v1 = uv_box
    for r in range(rings):
        t = r / (rings - 1)
        rad = radius * (1 + (taper - 1) * t)
        for s in range(segments + 1):
            a = 2 * np.pi * s / segments
            pts.append(base + ax * length * t + (e1 * np.cos(a) + e2 * np.sin(a)) * rad)
            uvs.append((u0 + (u1 - u0) * s / segments, v0 + (v1 - v0) * t))
    w = segments + 1
    for r in range(rings - 1):
        for s in range(segments):
            a, b, c, d = r * w + s, r * w + s + 1, (r + 1) * w + s, (r + 1) * w + s + 1
            faces += [(a, b, c), (b, d, c)]
    if cap_uv is not None:
        cu, cv, cr = cap_uv
        centre_i = len(pts)
        tip = base + ax * length
        pts.append(tip)
        uvs.append((cu, cv))
        first = (rings - 1) * w
        rad = radius * taper
        for s in range(segments + 1):
            a = 2 * np.pi * s / segments
            pts.append(tip + (e1 * np.cos(a) + e2 * np.sin(a)) * rad * 0.999)
            uvs.append((cu + cr * np.cos(a), cv + cr * np.sin(a)))
        for s in range(segments):
            faces.append((centre_i, centre_i + 1 + s + 1, centre_i + 1 + s))
    pts = np.array(pts)
    # Outward winding: compare with the radial direction from the axis.
    fixed = []
    for a, b, c in faces:
        n = np.cross(pts[b] - pts[a], pts[c] - pts[a])
        m = (pts[a] + pts[b] + pts[c]) / 3
        rel = m - base
        radial = rel - ax * (rel @ ax)
        ref_dir = radial if np.linalg.norm(radial) > 1e-3 * radius and abs(n @ ax) < 0.9 * np.linalg.norm(n) else ax
        fixed.append((a, b, c) if n @ ref_dir > 0 else (a, c, b))
    return pts, uvs, fixed


def build_respirator(gear, field_fn, centre):
    d = np.array([0, np.cos(np.radians(-25)), np.sin(np.radians(-25))])
    mount = surface_along(field_fn, centre, d)
    out = d / np.linalg.norm(d)
    out = out + np.array([0, 0.3, 0])
    out /= np.linalg.norm(out)
    # Snout: rubber cone from the hood, then a front grille cap.
    p, u, f = cylinder(mount - out * 1.5, out, 4.0, 5.0, 14, (0.0, 0.66, 0.5, 0.98), cap_uv=(0.75, 0.86, 0.11), taper=0.78, rings=3)
    gear.add(p, u, f)
    tip = mount + out * 3.5
    side = np.array([1.0, 0, 0])
    for sgn in (-1, 1):
        axis = side * sgn * 0.75 + out * 0.45 + np.array([0, 0, -0.35])
        axis /= np.linalg.norm(axis)
        base = mount + out * 1.8 + side * sgn * 2.0 + np.array([0, 0, -0.8])
        # Threaded neck, then the canister body with its grille end.
        p, u, f = cylinder(base, axis, 1.6, 2.0, 10, (0.52, 0.52, 0.98, 0.56), rings=2)
        gear.add(p, u, f)
        p, u, f = cylinder(base + axis * 1.6, axis, 3.1, 4.6, 14, (0.52, 0.58, 0.98, 0.74), cap_uv=(0.75, 0.86, 0.11), rings=2)
        gear.add(p, u, f)
    return tip


# ---------------------------------------------------------------- textures

def font(size, bold=True):
    for name in ("arialbd.ttf" if bold else "arial.ttf", "DejaVuSans-Bold.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def biohazard(size, fg=(0, 0, 0, 255)):
    """The biohazard trefoil, drawn from circles."""
    s = size * 4
    img = Image.new("L", (s, s), 0)
    d = ImageDraw.Draw(img)
    c = s / 2
    R = s * 0.25
    for k in range(3):
        a = np.radians(90 + k * 120)
        ox, oy = c + np.cos(a) * R * 0.62, c - np.sin(a) * R * 0.62
        d.ellipse([ox - R, oy - R, ox + R, oy + R], fill=255)
    for k in range(3):
        a = np.radians(90 + k * 120)
        ox, oy = c + np.cos(a) * R * 0.88, c - np.sin(a) * R * 0.88
        r2 = R * 0.74
        d.ellipse([ox - r2, oy - r2, ox + r2, oy + r2], fill=0)
    d.ellipse([c - R * 0.36, c - R * 0.36, c + R * 0.36, c + R * 0.36], fill=0)
    ring_r = R * 0.98
    d.ellipse([c - ring_r, c - ring_r, c + ring_r, c + ring_r], outline=255, width=int(s * 0.035))
    for k in range(3):
        a = np.radians(270 + k * 120)
        x, y = c + np.cos(a) * R * 0.45, c - np.sin(a) * R * 0.45
        d.line([c, c, x, y], fill=0, width=int(s * 0.03))
    d.ellipse([c - R * 0.2, c - R * 0.2, c + R * 0.2, c + R * 0.2], fill=255)
    d.ellipse([c - R * 0.12, c - R * 0.12, c + R * 0.12, c + R * 0.12], fill=0)
    img = img.resize((size, size), Image.LANCZOS)
    out = Image.new("RGBA", (size, size), fg[:3] + (0,))
    out.putalpha(img)
    return out


def label_back():
    img = Image.new("RGBA", (512, 256), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, 511, 255], fill=(15, 15, 15, 255))
    d.rectangle([10, 10, 501, 245], outline=(235, 190, 25, 255), width=6)
    bio = biohazard(150, (235, 190, 25, 255))
    img.alpha_composite(bio, (24, 53))
    d.text((186, 42), "HAZMAT", font=font(70), fill=(235, 190, 25, 255))
    d.text((188, 140), "DECON TEAM", font=font(38), fill=(235, 190, 25, 255))
    return img


def label_chest():
    img = Image.new("RGBA", (256, 256), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.polygon([(128, 6), (250, 128), (128, 250), (6, 128)], fill=(240, 120, 20, 255), outline=(10, 10, 10, 255))
    d.polygon([(128, 22), (234, 128), (128, 234), (22, 128)], outline=(10, 10, 10, 255))
    img.alpha_composite(biohazard(150), (53, 40))
    d.text((84, 182), "BIO", font=font(30), fill=(10, 10, 10, 255))
    return img


def label_sleeve():
    img = Image.new("RGBA", (256, 96), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, 255, 95], fill=(245, 245, 240, 255))
    d.rectangle([0, 0, 255, 26], fill=(200, 25, 20, 255))
    d.text((60, 0), "DANGER", font=font(24), fill=(255, 255, 255, 255))
    d.text((18, 32), "LEVEL A", font=font(40), fill=(10, 10, 10, 255))
    return img


def label_thigh():
    img = Image.new("RGBA", (256, 128), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    stripes = Image.new("RGBA", (256, 128), (240, 190, 20, 255))
    sd = ImageDraw.Draw(stripes)
    for x in range(-128, 384, 40):
        sd.polygon([(x, 0), (x + 20, 0), (x + 20 - 128, 128), (x - 128, 128)], fill=(15, 15, 15, 255))
    img.alpha_composite(stripes)
    d.rectangle([20, 30, 236, 98], fill=(245, 245, 240, 255))
    d.text((32, 34), "CONTAMINATED", font=font(24), fill=(10, 10, 10, 255))
    d.text((52, 64), "DO NOT TOUCH", font=font(22), fill=(200, 25, 20, 255))
    return img


def decal(points, normals, image, centre, right, up, width, facing, min_facing=0.35):
    """Planar decal: colour and alpha per point (alpha 0 outside the decal)."""
    arr = np.asarray(image, np.float64) / 255.0
    h, w = arr.shape[:2]
    rel = points - centre
    u = rel @ right / width + 0.5
    height = width * h / w
    v = 0.5 - rel @ up / height
    inside = (u >= 0) & (u < 1) & (v >= 0) & (v < 1) & (normals @ facing > min_facing)
    col = np.zeros((len(points), 3))
    alpha = np.zeros(len(points))
    px = np.clip((u * w).astype(int), 0, w - 1)
    py = np.clip((v * h).astype(int), 0, h - 1)
    sample = arr[py[inside], px[inside]]
    col[inside] = sample[:, :3]
    alpha[inside] = sample[:, 3]
    return col, alpha


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0, 1)
    return t * t * (3 - 2 * t)


def paint_suit(P, N, region, cavity, joints):
    """Suit colour at bind-pose positions P (n,3), normals N, region names, cavity (0..1)."""
    n = len(P)
    yellow = np.array([0.93, 0.76, 0.10])
    orange = np.array([0.95, 0.62, 0.08])
    tint = fbm(P * 0.05, 3)[:, None]
    col = yellow * (1 - 0.35 * tint) + orange * 0.35 * tint
    # Rubber grain.
    col *= (0.94 + 0.08 * fbm(P * 0.9, 2))[:, None]
    # Folds: wavy creases that follow each limb, strongest near the joints.
    warp = fbm(P * 0.07 + 3.1, 3)
    along = np.zeros(n)
    for name, (origin, axis) in joints["axes"].items():
        m = region == name
        if m.any():
            along[m] = (P[m] - origin) @ (axis / np.linalg.norm(axis))
    torso = region == "torso"
    along[torso] = P[torso, 2] + 0.3 * P[torso, 0]
    hood = region == "hood"
    along[hood] = P[hood, 2] * 0.6
    wave = np.sin(along * 0.55 + warp * 9.0)
    crease = smoothstep(0.82, 0.99, wave)            # thin dark crease
    ridge = smoothstep(0.55, 0.85, -np.sin(along * 0.55 + warp * 9.0 + 0.55))
    joint_gain = np.full(n, 0.45)
    for c in joints["bends"]:
        d = np.linalg.norm(P - c, axis=1)
        joint_gain = np.maximum(joint_gain, 1.0 - smoothstep(4, 14, d))
    col *= (1 - 0.32 * crease * joint_gain)[:, None]
    col *= (1 + 0.10 * ridge * joint_gain)[:, None]
    col *= (1 - 0.3 * cavity)[:, None]
    # Gloves and boots: black rubber, with gauntlet and boot cuffs reaching over the suit.
    black = np.array([0.07, 0.07, 0.075]) * (0.85 + 0.3 * fbm(P * 0.6, 2))[:, None]
    glove = np.zeros(n, bool)
    boot = np.zeros(n, bool)
    tape = np.zeros(n, bool)
    for s in ("L", "R"):
        wrist, wax = joints["wrist" + s]
        t = (P - wrist) @ wax
        glove |= (region == "hand" + s) | ((region == "arm" + s) & (t > -6.0))
        tape |= ((region == "arm" + s) & (t > -9.0) & (t <= -6.0))
        ankle = joints["ankle" + s]
        boot |= (region == "foot" + s) | ((region == "leg" + s) & (P[:, 2] < ankle[2] + 14))
        tape |= (region == "leg" + s) & (P[:, 2] >= ankle[2] + 14) & (P[:, 2] < ankle[2] + 17.5)
    col[glove | boot] = black[glove | boot] * (1 - 0.4 * cavity[glove | boot, None])
    sole = boot & (P[:, 2] < joints["sole"] + 2.6)
    col[sole] = np.array([0.16, 0.13, 0.10]) * (0.8 + 0.4 * (np.sin(P[sole, 1] * 2.6) > 0))[:, None]
    grey = np.array([0.62, 0.63, 0.64]) * (0.85 + 0.25 * fbm(P * 1.3, 2))[:, None]
    col[tape] = grey[tape]
    # Front closure flap with stitching, and taped seams on the shoulders and outer legs.
    front = torso & (N[:, 1] > 0.2) & (P[:, 2] > -2) & (P[:, 2] < 56)
    flap = front & (np.abs(P[:, 0]) < 2.4)
    col[flap] *= 0.86
    stitch = front & (np.abs(np.abs(P[:, 0]) - 2.4) < 0.25) & (np.sin(P[:, 2] * 4.0) > 0)
    col[stitch] *= 0.55
    seam_tape = np.zeros(n, bool)
    for s, sx in (("L", 1), ("R", -1)):
        # Ring where sleeve meets body.
        sh, sax = joints["shoulder" + s]
        t = (P - sh) @ sax
        seam_tape |= (region == "arm" + s) & (np.abs(t - 3.0) < 1.1)
        # Outer leg seam: a thin taped line down the outside of the leg.
        legm = region == "leg" + s
        hip, lax = joints["axes"]["leg" + s]
        t = np.clip((P - hip) @ lax / (lax @ lax), 0, 1)
        rel = P - (hip + t[:, None] * lax)
        seam_tape |= legm & (np.abs(rel[:, 1]) < 0.8) & (rel[:, 0] * sx > 0) & (P[:, 2] > joints["ankle" + s][2] + 17.5)
    col[seam_tape & ~glove & ~boot] = col[seam_tape & ~glove & ~boot] * 0.55 + np.array([0.45, 0.45, 0.42]) * 0.45
    # Waist belt seam.
    belt = torso & (np.abs(P[:, 2] - 4.0) < 1.0)
    col[belt] *= 0.7
    # Labels.
    for img, centre, right, up, width, facing in joints["decals"]:
        c, a = decal(P, N, img, centre, right, up, width, facing)
        col = col * (1 - a[:, None]) + c * a[:, None] * (1 - 0.3 * cavity[:, None])
    return np.clip(col, 0, 1)


def paint_gear(size=512):
    """Visor (top half), gasket strip, respirator rubber and canisters."""
    yy, xx = np.mgrid[0:size, 0:size] / size
    img = np.zeros((size, size, 3))
    # Visor glass: smoky blue-black, a sky reflection at the top and two diagonal glints.
    v = np.clip((yy - 0.02) / 0.44, 0, 1)
    glass = np.stack([0.05 + 0.10 * (1 - v), 0.07 + 0.13 * (1 - v), 0.10 + 0.20 * (1 - v)], -1)
    glint = np.exp(-((xx * 0.9 - v * 0.35 - 0.18) / 0.035) ** 2) * 0.55 + np.exp(-((xx * 0.9 - v * 0.35 - 0.30) / 0.015) ** 2) * 0.35
    glass += glint[..., None] * np.array([0.8, 0.85, 0.9])
    rim = np.exp(-((v - 0.0) / 0.05) ** 2) * 0.25
    glass += rim[..., None]
    img[yy < 0.48] = glass[yy < 0.48]
    # Gasket and respirator: black-grey rubber with fine noise.
    noise = RNG.random((size, size)) * 0.04
    rubber = np.stack([0.11 + noise, 0.11 + noise, 0.12 + noise], -1)
    lower = yy >= 0.48
    img[lower] = rubber[lower]
    # Respirator snout ribs (u 0..0.5, v 0.66..0.98).
    snout = (xx < 0.5) & (yy >= 0.66)
    ribs = (np.sin((yy - 0.66) / 0.32 * np.pi * 7) > 0.6)
    img[snout & ribs] *= 0.6
    # Canister neck (threads) and body: dark grey with a yellow hazard label band.
    neck = (xx >= 0.52) & (yy >= 0.52) & (yy < 0.56)
    img[neck] = np.array([0.25, 0.25, 0.26]) * (0.7 + 0.3 * (np.sin(yy[neck] * 900) > 0))[:, None]
    body = (xx >= 0.52) & (yy >= 0.58) & (yy < 0.74)
    img[body] = np.array([0.20, 0.21, 0.22])
    band = body & (yy >= 0.62) & (yy < 0.70)
    img[band] = np.array([0.92, 0.74, 0.10])
    stripes = band & (((xx * 40 + yy * 40) % 2) < 0.5) & ((yy < 0.635) | (yy > 0.685))
    img[stripes] = np.array([0.05, 0.05, 0.05])
    # Grille cap (centre 0.75, 0.86): ring and a grid of holes.
    d = np.hypot(xx - 0.75, yy - 0.86)
    cap = d < 0.115
    img[cap] = np.array([0.24, 0.24, 0.25])
    holes = cap & (d < 0.09) & ((np.hypot((xx * 60) % 1 - 0.5, (yy * 60) % 1 - 0.5)) < 0.28)
    img[holes] = np.array([0.03, 0.03, 0.03])
    img[cap & (np.abs(d - 0.1) < 0.006)] = np.array([0.4, 0.4, 0.42])
    out = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))
    # Canister label text.
    dr = ImageDraw.Draw(out)
    dr.text((int(0.56 * size), int(0.633 * size)), "P100 FILTER", font=font(int(size * 0.03)), fill=(10, 10, 10))
    return np.asarray(out, np.float64) / 255.0


def write_tga(path, rgb):
    img = Image.fromarray((np.clip(rgb, 0, 1) * 255).astype(np.uint8), "RGB")
    img.save(path)


# ---------------------------------------------------------------- PSK

def chunk(name, type_flag, size, count, data=b""):
    return struct.pack("<20sIii", name.encode(), type_flag, size, count) + data


def write_psk(path, points, wedges, faces, face_mat, materials, bones, influences):
    out = [chunk("ACTRHEAD", 1999801, 0, 0)]
    out.append(chunk("PNTS0000", 1999801, 12, len(points), b"".join(struct.pack("<3f", *p) for p in points)))
    data = b"".join(struct.pack("<HHffBBH", int(w[0]), 0, w[1], w[2], int(m), 0, 0) for w, m in wedges)
    out.append(chunk("VTXW0000", 1999801, 16, len(wedges), data))
    data = b"".join(struct.pack("<3HBBI", int(f[0]), int(f[1]), int(f[2]), int(m), 0, 1) for f, m in zip(faces, face_mat))
    out.append(chunk("FACE0000", 1999801, 12, len(faces), data))
    data = b"".join(struct.pack("<64siIiIii", m.encode(), i, 0, 0, 0, 0, 0) for i, m in enumerate(materials))
    out.append(chunk("MATT0000", 1999801, 88, len(materials), data))
    data = b""
    for b in bones:
        data += struct.pack("<64sIii4f3f4f", b.name.encode(), 0, b.children, b.parent, *b.rotation, *b.position,
                            b.length, *b.size)
    out.append(chunk("REFSKELT", 1999801, 120, len(bones), data))
    data = b"".join(struct.pack("<fii", w, int(p), int(b)) for w, p, b in influences)
    out.append(chunk("RAWWEIGHTS", 1999801, 12, len(influences), data))
    Path(path).write_bytes(b"".join(out))


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--install", required=True, help="SCCT Versus install root (holds Packages)")
    ap.add_argument("--out", default="out")
    ap.add_argument("--preview", action="store_true")
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    merc = read_merc(Path(args.install))
    names = [b.name for b in merc.bones]
    globals_ = bone_globals(merc.bones)
    pos = {n: g[:3, 3] for n, g in zip(names, globals_)}
    pw = point_weights(merc)
    print("merc:", len(merc.points), "points", len(merc.faces), "faces")

    samples, sample_w = sample_body(merc, pw)
    print("skin samples:", len(samples))
    field, lo, axes = build_field(samples, sample_w, names)
    verts, faces = extract_surface(field, lo)
    print("suit:", len(verts), "points", len(faces), "faces")

    from scipy.interpolate import RegularGridInterpolator
    interp = RegularGridInterpolator(axes, field, bounds_error=False, fill_value=10.0)
    field_fn = lambda p: float(interp(p[None])[0])

    # Winding: match the merc's (its faces' cross products point out of the body).
    mf = merc.wedges[merc.faces]
    mn = np.cross(merc.points[mf[:, 1]] - merc.points[mf[:, 0]], merc.points[mf[:, 2]] - merc.points[mf[:, 0]])
    torso_faces = np.abs(merc.points[mf[:, 0], 2] - 25) < 10
    radial = merc.points[mf[torso_faces, 0]] * np.array([1.0, 1.0, 0.0])
    merc_out = np.sign((mn[torso_faces] * radial).sum(1)).mean()
    sn = np.cross(verts[faces[:, 1]] - verts[faces[:, 0]], verts[faces[:, 2]] - verts[faces[:, 0]])
    suit_out = np.sign((sn * (verts[faces[:, 0]] - verts.mean(0))).sum(1)).mean()
    if np.sign(merc_out) != np.sign(suit_out):
        faces = faces[:, ::-1].copy()
    outward_is_ccw = np.sign(merc_out)
    print("merc winding check:", round(float(merc_out), 2), "suit", round(float(suit_out), 2))

    # Wrinkle the cloth a little (low, broad bagginess; fine folds are painted).
    normals = vertex_normals(verts, faces)
    if outward_is_ccw < 0:
        normals = -normals
    weights = transfer_weights(verts, faces, samples, sample_w, names)
    dominant = np.array([chain_of(names[i]) for i in weights.argmax(1)])
    bag = (fbm(verts * 0.06 + 11.0, 3) - 0.5) * 1.6
    not_tight = ~np.isin(dominant, ["handL", "handR", "footL", "footR", "hood"])
    verts = verts + normals * (bag * not_tight)[:, None]
    normals = vertex_normals(verts, faces) * (1 if outward_is_ccw > 0 else -1)

    # Cavity: how far each vertex sits below its neighbours' average (creases, armpits).
    a, deg = neighbours(len(verts), faces)
    lap = (a @ verts) / deg[:, None] - verts
    cav = np.clip((lap * normals).sum(1) / 2.0, 0, 1)
    for _ in range(4):
        cav = 0.5 * cav + 0.5 * (a @ cav) / deg

    # Charts and UVs.
    face_chart = np.array([max(set(dominant[f]), key=list(dominant[f]).count) for f in faces])
    frames = chart_frames(globals_, names)
    wedges_uv, tri_w, texel_scale, n_islands = make_uvs(verts, faces, face_chart, frames, 1024, 4)
    print("suit UVs:", len(wedges_uv), "wedges,", n_islands, "islands, texels per unit:", round(texel_scale, 2))

    # Gear on the head.
    gear = Gear()
    head_centre = HOOD_CENTER + np.array([0, -1.0, 0])
    visor_front = build_visor(gear, field_fn, head_centre)
    build_respirator(gear, field_fn, head_centre)
    gp, guv, gf = gear.arrays()
    # Gear winding was built outward; flip it if the merc convention is the other way.
    if outward_is_ccw < 0:
        gf = gf[:, ::-1].copy()

    # Paint the suit.
    joints = {"axes": {}, "bends": [], "decals": []}
    for s in ("L", "R"):
        joints["axes"]["arm" + s] = (pos[f"B {s} UpperArm"], pos[f"B {s} Hand"] - pos[f"B {s} UpperArm"])
        joints["axes"]["leg" + s] = (pos[f"B {s} Thigh"], pos[f"B {s} Foot"] - pos[f"B {s} Thigh"])
        wax = pos[f"B {s} Hand"] - pos[f"B {s} ForeArm"]
        joints["wrist" + s] = (pos[f"B {s} Hand"], wax / np.linalg.norm(wax))
        sax = pos[f"B {s} ForeArm"] - pos[f"B {s} UpperArm"]
        joints["shoulder" + s] = (pos[f"B {s} UpperArm"], sax / np.linalg.norm(sax))
        joints["ankle" + s] = pos[f"B {s} Foot"]
        joints["bends"] += [pos[f"B {s} ForeArm"], pos[f"B {s} Calf"], pos[f"B {s} UpperArm"], pos[f"B {s} Thigh"]]
    joints["bends"] += [pos["B Spine"], pos["B Neck"]]
    joints["sole"] = verts[:, 2].min()
    # Mesh +X is the character's left; text reads left to right for someone looking at it.
    joints["decals"] = [
        (label_back(), np.array([0, -20, 42.0]), np.array([-1.0, 0, 0]), np.array([0, 0, 1.0]), 25.0, np.array([0, -1.0, 0])),
        (label_chest(), np.array([8.5, 16, 44.0]), np.array([1.0, 0, 0]), np.array([0, 0, 1.0]), 9.0, np.array([0, 1.0, 0])),
        # Sleeves: on top of the T-posed arm, which faces outwards when the arm hangs;
        # the text's up runs towards the shoulder.
        (label_sleeve(), pos["B R UpperArm"] + np.array([-9, 0, 4.5]), np.array([0, 1.0, 0]), np.array([1.0, 0, 0]), 10.0, np.array([0, 0, 1.0])),
        (label_sleeve(), pos["B L UpperArm"] + np.array([9, 0, 4.5]), np.array([0, -1.0, 0]), np.array([-1.0, 0, 0]), 10.0, np.array([0, 0, 1.0])),
        (label_thigh(), pos["B L Thigh"] + np.array([3, 12, -20]), np.array([1.0, 0, 0]), np.array([0, 0, 1.0]), 11.0, np.array([0, 1.0, 0])),
    ]
    region_index = sorted(set(dominant))
    reg_id = np.array([region_index.index(r) for r in dominant], np.float64)

    def suit_shader(P, N, R, C):
        region = np.array(region_index)[np.clip(np.rint(R).astype(int), 0, len(region_index) - 1)]
        Nn = N / np.maximum(np.linalg.norm(N, axis=1, keepdims=True), 1e-6)
        return paint_suit(P, Nn, region, C, joints)

    # Region per texel must not blend across charts: bake it per face from its chart.
    suit_tex = bake_suit(1024, wedges_uv, tri_w, faces, verts, normals, face_chart, region_index, cav, suit_shader)
    gear_tex = paint_gear(512)
    write_tga(out / "HazmatSuit.tga", suit_tex)
    write_tga(out / "HazmatGear.tga", gear_tex)

    # Assemble the PSK: suit (material 0) then gear (material 1, rigid on B Head).
    head = names.index("B Head")
    n_suit = len(verts)
    points = np.concatenate([verts, gp])
    wedge_list = [((int(w[0]), float(w[1]), float(w[2])), 0) for w in wedges_uv]
    gear_w0 = len(wedge_list)
    wedge_list += [((n_suit + i, float(u), float(v)), 1) for i, (u, v) in enumerate(guv)]
    all_faces = np.concatenate([tri_w, gf + gear_w0])
    face_mat = np.concatenate([np.zeros(len(tri_w), int), np.ones(len(gf), int)])
    influences = []
    for i in range(n_suit):
        for b in np.nonzero(weights[i])[0]:
            influences.append((float(weights[i, b]), i, int(b)))
    for i in range(len(gp)):
        influences.append((1.0, n_suit + i, head))
    write_psk(out / "HazmatMerc.psk", points, wedge_list,
              all_faces, face_mat, ["HazmatSuit", "HazmatGear"], merc.bones, influences)

    # Goggle lights: the stock merc's sit on its helmet lamp area; Character Skins moves
    # them by an offset in B Head's axes. Put them on the front of the visor.
    head_m = globals_[head]
    stock_front = merc.points[pw[:, names.index("DEF_01_Casque")] + pw[:, head] > 0.5]
    stock_front = stock_front[np.argmax(stock_front[:, 1])]
    stock_local = np.linalg.inv(head_m) @ np.append(stock_front, 1)
    visor_local = np.linalg.inv(head_m) @ np.append(visor_front, 1)
    summary = {
        "points": int(len(points)), "wedges": len(wedge_list), "faces": int(len(all_faces)),
        "suit_faces": int(len(tri_w)), "gear_faces": int(len(gf)),
        "materials": ["HazmatSuit", "HazmatGear"],
        "rot_origin": list(merc.rot_origin),
        "visor_front_mesh": [round(float(x), 2) for x in visor_front],
        "visor_front_head_local": [round(float(x), 2) for x in visor_local[:3]],
        "merc_front_head_local": [round(float(x), 2) for x in stock_local[:3]],
        "goggle_offset_from_merc_front": [round(float(x), 1) for x in (visor_local - stock_local)[:3]],
    }
    (out / "hazmat.json").write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary, indent=2))

    if args.preview:
        from preview import render, sheet
        pts_w = points[[w[0][0] for w in wedge_list]]
        uvs = np.array([(w[0][1], w[0][2]) for w in wedge_list])
        tex = [np.asarray(Image.open(out / "HazmatSuit.tga").convert("RGB")),
               np.asarray(Image.open(out / "HazmatGear.tga").convert("RGB"))]
        views = [render(pts_w, uvs, all_faces, face_mat, tex, yaw=y, size=480) for y in (0, 90, 180, 270)]
        sheet(views, out / "preview_turnaround.png")
        lo_b, hi_b = HOOD_CENTER - 22, HOOD_CENTER + 22
        heads = [render(pts_w, uvs, all_faces, face_mat, tex, yaw=y, size=360,
                        box=(np.array([-22, -22, 50.0]), np.array([22, 22, 94.0]))) for y in (180, 135, 90)]
        sheet(heads, out / "preview_head.png")


def bake_suit(size, wedges_uv, tri_w, faces, verts, normals, face_chart, region_index, cav, shader):
    attrs = {"P": verts, "N": normals, "C": cav}
    H = W = size
    acc = {k: np.zeros((H, W) + v.shape[1:]) for k, v in attrs.items()}
    reg = np.zeros((H, W))
    hit = np.zeros((H, W), bool)
    uv = wedges_uv[:, 1:3] * size
    for f in range(len(tri_w)):
        w = tri_w[f]
        x, y = uv[w, 0], uv[w, 1]
        x0, x1 = int(max(0, np.floor(x.min()) - 1)), int(min(W - 1, np.ceil(x.max()) + 1))
        y0, y1 = int(max(0, np.floor(y.min()) - 1)), int(min(H - 1, np.ceil(y.max()) + 1))
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        det = (y[1] - y[2]) * (x[0] - x[2]) + (x[2] - x[1]) * (y[0] - y[2])
        if abs(det) < 1e-12:
            continue
        l0 = ((y[1] - y[2]) * (gx - x[2]) + (x[2] - x[1]) * (gy - y[2])) / det
        l1 = ((y[2] - y[0]) * (gx - x[2]) + (x[0] - x[2]) * (gy - y[2])) / det
        l2 = 1 - l0 - l1
        inside = (l0 >= -0.03) & (l1 >= -0.03) & (l2 >= -0.03)
        if not inside.any():
            continue
        p = faces[f]
        for k, v in attrs.items():
            if v.ndim > 1:
                val = l0[..., None] * v[p[0]] + l1[..., None] * v[p[1]] + l2[..., None] * v[p[2]]
            else:
                val = l0 * v[p[0]] + l1 * v[p[1]] + l2 * v[p[2]]
            acc[k][y0:y1 + 1, x0:x1 + 1][inside] = val[inside]
        reg[y0:y1 + 1, x0:x1 + 1][inside] = region_index.index(face_chart[f])
        hit[y0:y1 + 1, x0:x1 + 1] |= inside
    flat = {k: v[hit] for k, v in acc.items()}
    colours = shader(flat["P"], flat["N"], reg[hit], flat["C"])
    img = np.zeros((H, W, 3))
    img[hit] = colours
    idx = ndimage.distance_transform_edt(~hit, return_distances=False, return_indices=True)
    return img[idx[0], idx[1]]


if __name__ == "__main__":
    main()

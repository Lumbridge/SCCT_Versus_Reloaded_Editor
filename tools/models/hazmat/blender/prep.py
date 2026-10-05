"""Stage 1 of the Blender hazmat build: watertight high-resolution parts from the stock merc.

Writes <out>/parts.npz with, for each part (suit, gloveL/R, bootL/R), its vertices and
triangles, plus the merc skin, bone positions and region names that the later stages use.
Parts are distance fields around the merc's skin (per-bone stand-off) cut where the next
part takes over, extracted with marching cubes and lightly smoothed. Blender does the rest.
"""
import argparse
import sys
from pathlib import Path

import numpy as np
from scipy.spatial import cKDTree

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from merc_mesh import read_merc, bone_globals  # noqa: E402
from build_hazmat import point_weights, sample_body, chain_of, smin, superellipsoid, taubin  # noqa: E402

# Suit stand-off from the skin, per bone (units; the merc is about 176 tall).
STANDOFF = {
    "B Pelvis": 6.0, "B Spine": 6.0, "B Spine1": 5.8, "B Spine2": 5.4, "B Neck": 4.5, "B Head": 4.5,
    "B L Clavicle": 4.8, "B R Clavicle": 4.8, "B L UpperArm": 4.6, "B R UpperArm": 4.6,
    "B L ForeArm": 3.8, "B R ForeArm": 3.8, "B L Thigh": 5.6, "B R Thigh": 5.6,
    "B L Calf": 5.0, "B R Calf": 5.0, "B L Foot": 4.0, "B R Foot": 4.0, "B L Toe0": 4.0, "B R Toe0": 4.0,
}
HOOD_CENTER = np.array([0.0, 1.5, 71.5])
HOOD_RADII = np.array([15.0, 15.6, 16.5])
HOOD_POWER = 2.4
YOKE_CENTER = np.array([0.0, 0.0, 57.0])   # fills the neck: the hood runs into the shoulders
YOKE_RADII = np.array([17.0, 13.0, 9.0])
PACK_CENTER = np.array([0.0, -14.5, 40.0])   # the breathing set worn inside the suit
PACK_RADII = np.array([11.5, 7.5, 15.0])
CROTCH_Z = -6.0
CROTCH_GAP = 1.0
LEG_HEM_Z = -67.0          # the suit's legs end over the boots
BOOT_TOP_Z = -58.0
SOLE_THICKNESS = 1.6
GLOVE_STANDOFF = 1.0        # tight rubber over the merc's hand
GAUNTLET_STANDOFF = 5.4     # the glove cuff, over the sleeve
GAUNTLET_LENGTH = 0.5       # fraction of the forearm the cuff covers, from the wrist
SLEEVE_SHORT = 3.0          # the sleeve stops this far before the wrist, inside the cuff
BOOT_STANDOFF = {"foot": 1.9, "calf": 3.2}


def field_on_grid(samples, sample_r, lo, hi, voxel, k=12):
    axes = [np.arange(lo[i], hi[i] + voxel, voxel) for i in range(3)]
    gx, gy, gz = np.meshgrid(*axes, indexing="ij")
    grid = np.stack([gx, gy, gz], -1).reshape(-1, 3)
    tree = cKDTree(samples)
    d, _ = tree.query(grid, k=1, workers=-1)
    field = d - sample_r.max()
    near = d < sample_r.max() * 2.5 + 4
    dk, ik = tree.query(grid[near], k=k, workers=-1)
    wk = 1.0 / (dk + 0.4) ** 2
    r = (sample_r[ik] * wk).sum(1) / wk.sum(1)
    field[near] = dk[:, 0] - r
    return field, grid, gx.shape, axes


def extract(field, shape, lo, voxel, smooth=4, blur=0.0):
    from skimage import measure
    from scipy import ndimage
    if blur:
        # Rounds off the facets and lumps of the low-poly merc underneath.
        field = ndimage.gaussian_filter(field.reshape(shape), blur / voxel)
    import scipy.sparse as sp
    import scipy.sparse.csgraph as cg
    verts, faces, _, _ = measure.marching_cubes(field.reshape(shape), 0.0, spacing=(voxel,) * 3)
    verts += lo
    n = len(verts)
    a = sp.coo_matrix((np.ones(faces.size), (faces.ravel(), np.roll(faces, 1, 1).ravel())), shape=(n, n))
    _, label = cg.connected_components(a, directed=False)
    span = [np.ptp(verts[label == c], axis=0).prod() if (label == c).sum() > 3 else 0 for c in range(label.max() + 1)]
    used = label == int(np.argmax(span))
    remap = -np.ones(n, np.int64)
    remap[used] = np.arange(used.sum())
    faces = remap[faces[used[faces[:, 0]]]]
    verts = taubin(verts[used], faces, smooth)
    # Wound counter-clockwise seen from outside (Blender's outward normals).
    return verts, faces


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--install", required=True)
    ap.add_argument("--out", default="out")
    args = ap.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    merc = read_merc(Path(args.install))
    names = [b.name for b in merc.bones]
    globals_ = bone_globals(merc.bones)
    pos = {n: g[:3, 3] for n, g in zip(names, globals_)}
    pw = point_weights(merc)
    samples, sample_w = sample_body(merc, pw)
    chain = np.array([chain_of(names[i]) for i in sample_w.argmax(1)])
    parts = {}

    # ---- suit: everything but hands and feet, cut at the wrists and above the boots
    radius = np.array([STANDOFF.get(n, 4.0) for n in names])
    keep = ~np.isin(chain, ["handL", "handR"])
    s, r = samples[keep], (sample_w @ radius)[keep]
    voxel = 0.5
    lo = np.minimum(s.min(0), HOOD_CENTER - HOOD_RADII) - 7
    hi = np.maximum(s.max(0), HOOD_CENTER + HOOD_RADII) + 7
    field, grid, shape, _ = field_on_grid(s, r, lo, hi, voxel)
    gap = CROTCH_GAP * np.clip((CROTCH_Z - grid[:, 2]) / 5.0, 0, 1)
    field = np.maximum(field, gap - np.abs(grid[:, 0]))
    field = smin(field, superellipsoid(grid, YOKE_CENTER, YOKE_RADII, 2.0), 5.0)
    field = smin(field, superellipsoid(grid, HOOD_CENTER, HOOD_RADII, HOOD_POWER), 6.0)
    field = smin(field, superellipsoid(grid, PACK_CENTER, PACK_RADII, 2.2), 5.0)
    for side in ("L", "R"):
        wrist, elbow = pos[f"B {side} Hand"], pos[f"B {side} ForeArm"]
        ax = (wrist - elbow) / np.linalg.norm(wrist - elbow)
        along = (grid - wrist) @ ax
        lateral = np.linalg.norm((grid - wrist) - along[:, None] * ax, axis=1)
        sleeve_end = np.where(lateral < 12, along + SLEEVE_SHORT, -1e3)
        field = np.maximum(field, sleeve_end)
    field = np.maximum(field, LEG_HEM_Z - grid[:, 2])
    parts["suit"] = extract(field, shape, lo, voxel, smooth=6, blur=1.9)
    print("suit", len(parts["suit"][0]), "verts", len(parts["suit"][1]), "tris")

    # ---- gloves: tight on the hand and fingers, a wide gauntlet over the end of the sleeve
    for side in ("L", "R"):
        wrist, elbow = pos[f"B {side} Hand"], pos[f"B {side} ForeArm"]
        ax = (wrist - elbow) / np.linalg.norm(wrist - elbow)
        forearm = np.linalg.norm(wrist - elbow)
        t = (samples - wrist) @ ax
        hand = chain == f"hand{side}"
        cuff = (chain == f"arm{side}") & (t > -forearm * GAUNTLET_LENGTH - 3) & (t <= 0.5)
        sel = hand | cuff
        s = samples[sel]
        # The cuff stays outside the sleeve (stand-off 3.8) wherever the sleeve is, flares to its
        # open end, and narrows over the wrist to the hand.
        tt = t[sel]
        flare = np.clip((-tt - SLEEVE_SHORT) / (forearm * GAUNTLET_LENGTH - SLEEVE_SHORT), 0, 1)
        neck = np.clip((tt + SLEEVE_SHORT) / SLEEVE_SHORT, 0, 1)
        cuff_r = (4.6 + (GAUNTLET_STANDOFF - 4.6) * flare) * (1 - neck) + 1.9 * neck
        r = np.where(hand[sel], GLOVE_STANDOFF, cuff_r)
        voxel = 0.22
        lo, hi = s.min(0) - 6, s.max(0) + 6
        field, grid, shape, _ = field_on_grid(s, r, lo, hi, voxel, k=8)
        along = (grid - wrist) @ ax
        field = np.maximum(field, -forearm * GAUNTLET_LENGTH - along)
        parts["glove" + side] = extract(field, shape, lo, voxel, smooth=3, blur=0.35)
        print("glove" + side, len(parts["glove" + side][0]), "verts")

    # ---- boots: around the merc's foot and lower calf, a flat sole, cut below the suit's hem
    for side in ("L", "R"):
        foot = chain == f"foot{side}"
        calf = (chain == f"leg{side}") & (samples[:, 2] < BOOT_TOP_Z + 4)
        sel = foot | calf
        s = samples[sel]
        r = np.where(foot[sel], BOOT_STANDOFF["foot"], BOOT_STANDOFF["calf"])
        voxel = 0.3
        lo, hi = s.min(0) - 6, s.max(0) + 6
        field, grid, shape, _ = field_on_grid(s, r, lo, hi, voxel, k=10)
        sole = merc.points[:, 2].min() - SOLE_THICKNESS
        field = np.maximum(field, sole - grid[:, 2])
        field = np.maximum(field, grid[:, 2] - BOOT_TOP_Z)
        parts["boot" + side] = extract(field, shape, lo, voxel, smooth=4, blur=0.9)
        print("boot" + side, len(parts["boot" + side][0]), "verts")

    # Merc skin (for weights and the region of each point).
    fp = merc.wedges[merc.faces]
    data = {
        "bone_names": np.array(names),
        "bone_pos": np.array([g[:3, 3] for g in globals_]),
        "merc_points": merc.points, "merc_tris": fp, "merc_weights": pw,
        "hood_center": HOOD_CENTER, "hood_radii": HOOD_RADII,
        "leg_hem_z": LEG_HEM_Z, "boot_top_z": BOOT_TOP_Z,
    }
    for k, (v, f) in parts.items():
        data[k + "_v"] = v.astype(np.float32)
        data[k + "_f"] = f.astype(np.int32)
    np.savez_compressed(out / "parts.npz", **data)
    print("wrote", out / "parts.npz")


if __name__ == "__main__":
    main()

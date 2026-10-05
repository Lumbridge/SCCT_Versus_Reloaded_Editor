"""Stage 3 of the Blender hazmat build: paint the textures, rig to the merc, write the PSK.

python finish.py --install <SVM root> --out <out dir> --assets <assets dir>

Reads low.npz and the bake_*.npy maps from build.py. The suit texture is painted per texel
from the baked high-poly position, normal, region and masks (seams, zip flap, tape, boot sole),
with the folds lit from the baked normals and darkened by the baked ambient occlusion. The visor
is painted flat in its own planar UVs. Weights come from the stock merc's skin: the suit from
the whole body, each glove from its hand and forearm, each boot from its foot and calf; the
hood and its gear are rigid on B Head.
"""
import argparse
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy import ndimage
from scipy.spatial import cKDTree

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from merc_mesh import read_merc, bone_globals  # noqa: E402
from build_hazmat import (point_weights, sample_body, chain_of, neighbours, limit_weights,  # noqa: E402
                          write_psk, fbm, smoothstep, decal, biohazard, font,
                          label_back, label_chest, label_sleeve, label_thigh)

REGIONS = ["torso", "hood", "armL", "armR", "legL", "legR", "handL", "handR", "footL", "footR"]
SUIT_OUT, GEAR_OUT = 1024, 512
LIGHT = np.array([0.25, 0.45, 1.0]) / np.linalg.norm([0.25, 0.45, 1.0])


def load(out, name, kind):
    return np.load(out / f"bake_{name}_{kind}.npy")[..., :3]


def blur_normals(n, sigma):
    b = np.stack([ndimage.gaussian_filter(n[..., i], sigma) for i in range(3)], -1)
    return b / np.maximum(np.linalg.norm(b, axis=-1, keepdims=True), 1e-6)


def label_number():
    img = Image.new("RGBA", (256, 160), (0, 0, 0, 0))
    ImageDraw.Draw(img).text((18, 0), "07", font=font(150), fill=(18, 18, 18, 235))
    return img


# ---------------------------------------------------------------- suit texture

def paint_suit(out, bone_pos):
    P = load(out, "suit", "pos")
    N = load(out, "suit", "normal") * 2 - 1  # stored as 0.5 * n + 0.5
    N = N / np.maximum(np.linalg.norm(N, axis=-1, keepdims=True), 1e-6)
    ao = load(out, "suit", "ao")[..., 0]
    region = np.clip(np.rint(load(out, "suit", "region")[..., 0] * 16 - 0.5), 0, len(REGIONS) - 1).astype(int)
    mask = load(out, "suit", "mask")
    H, W = ao.shape
    p = P.reshape(-1, 3)
    n = N.reshape(-1, 3)
    reg = region.ravel()
    seam, flap, tape = (np.clip(mask[..., k].ravel(), 0, 1) for k in range(3))
    is_ = lambda *names: np.isin(reg, [REGIONS.index(x) for x in names])
    suit = is_("torso", "hood", "armL", "armR", "legL", "legR")
    glove = is_("handL", "handR")
    boot = is_("footL", "footR")

    # Base colours.
    yellow = np.array([0.90, 0.70, 0.07])
    deep = np.array([0.86, 0.55, 0.05])
    tint = fbm(p * 0.04, 3)[:, None]
    col = yellow * (1 - 0.4 * tint) + deep * 0.4 * tint
    col *= (0.95 + 0.07 * fbm(p * 0.8 + 5, 2))[:, None]
    rubber = np.array([0.075, 0.08, 0.075]) * (0.9 + 0.2 * fbm(p * 0.7, 2))[:, None]
    col[glove] = rubber[glove]
    col[boot] = (np.array([0.06, 0.06, 0.06]) * (0.9 + 0.2 * fbm(p * 0.7 + 2, 2))[:, None])[boot]
    sole = boot & (seam > 0.5)
    col[sole] = np.array([0.03, 0.03, 0.03])

    # Taped seams: a paler, flatter yellow with darker edges.
    s_tape = seam * suit
    edge = np.clip(1 - np.abs(s_tape - 0.35) / 0.12, 0, 1) * suit
    col = col * (1 - 0.55 * s_tape[:, None]) + np.array([0.80, 0.68, 0.30]) * 0.55 * s_tape[:, None]
    col *= (1 - 0.35 * edge)[:, None]
    # Zip flap: its edges cast a line, with press studs every few units.
    fe = np.clip(1 - np.abs(flap - 0.5) / 0.25, 0, 1) * suit
    col *= (1 - 0.45 * fe)[:, None]
    stud = (flap > 0.8) & (np.abs(np.abs(p[:, 0]) - 0.0) < 0.9) & (np.abs(((p[:, 2] + 1) % 7.0) - 3.5) < 0.55)
    col[stud] = np.array([0.35, 0.35, 0.33])
    # Grey duct tape over the cuffs and hems, with crumpled shading and a torn edge.
    t_amt = np.clip(tape + 0.0, 0, 1)
    crumple = fbm(p * 1.6 + 9, 3)
    grey = np.array([0.55, 0.56, 0.56]) * (0.8 + 0.35 * crumple)[:, None]
    torn = t_amt * smoothstep(0.35, 0.55, t_amt + 0.25 * (fbm(p * 2.2, 2) - 0.5))
    col = col * (1 - torn[:, None]) + grey * torn[:, None]

    # Labels (planar decals; +X is the character's left).
    decals = [
        (label_back(), np.array([0, -24, 40.0]), np.array([-1.0, 0, 0]), np.array([0, 0, 1.0]), 26.0, np.array([0, -1.0, 0])),
        (label_chest(), np.array([-9.0, 18, 43.0]), np.array([1.0, 0, 0]), np.array([0, 0, 1.0]), 8.5, np.array([0, 1.0, 0])),
        (label_sleeve(), bone_pos["B R UpperArm"] + np.array([-9, 0, 6]), np.array([0, 1.0, 0]), np.array([1.0, 0, 0]), 10.0, np.array([0, 0, 1.0])),
        (label_sleeve(), bone_pos["B L UpperArm"] + np.array([9, 0, 6]), np.array([0, -1.0, 0]), np.array([-1.0, 0, 0]), 10.0, np.array([0, 0, 1.0])),
        (label_thigh(), bone_pos["B L Thigh"] + np.array([3, 14, -22]), np.array([1.0, 0, 0]), np.array([0, 0, 1.0]), 11.0, np.array([0, 1.0, 0])),
        (label_number(), np.array([0, -14, 82.0]), np.array([-1.0, 0, 0]), np.array([0, 0.8, 0.6]), 9.0, np.array([0, -0.6, 0.8])),
    ]
    on_suit = suit & (s_tape < 0.3) & (flap < 0.3)
    for img, centre, right, up, width, facing in decals:
        c, a = decal(p, n, img, centre, right, up, width, facing, min_facing=0.4)
        a = a * on_suit
        col = col * (1 - a[:, None]) + c * a[:, None]

    # Wear: grime rising from the boots, scuffs on knees and elbows.
    z = p[:, 2]
    grime = smoothstep(-40, -78, z) * (0.55 + 0.45 * fbm(p * 0.12 + 3, 3))
    dirt = np.array([0.30, 0.24, 0.13])
    col = col * (1 - 0.45 * grime[:, None]) + dirt * 0.45 * grime[:, None] * suit[:, None]
    blotch = smoothstep(0.64, 0.78, fbm(p * 0.09 + 17, 4)) * 0.14
    col = col * (1 - blotch[:, None] * suit[:, None])

    # Lighting baked in: fold detail from the normals, ambient occlusion, a soft rubber sheen.
    nb = blur_normals(N, 10).reshape(-1, 3)
    detail = n @ LIGHT - nb @ LIGHT
    shade = np.clip(1 + 1.1 * detail, 0.5, 1.4)
    occl = 0.42 + 0.58 * np.clip(ao.ravel(), 0, 1) ** 1.2
    sheen = np.clip(detail, 0, None) * 0.35 * (suit | glove | boot)
    col = col * (shade * occl)[:, None] + sheen[:, None] * 0.6
    img = np.clip(col.reshape(H, W, 3), 0, 1)
    return img


# ---------------------------------------------------------------- gear texture

def visor_art(w, h):
    """Smoked glass with the wearer's breathing mask dimly visible behind it."""
    yy, xx = np.mgrid[0:h, 0:w] / np.array([h, w])[:, None, None]
    # Interior: a dark head silhouette, the mask's rubber, its lens and regulator.
    inner = Image.new("RGB", (w, h), (8, 10, 12))
    d = ImageDraw.Draw(inner)
    cx = w * 0.5
    d.ellipse([cx - w * 0.17, h * 0.02, cx + w * 0.17, h * 1.15], fill=(26, 24, 22))       # hood lining / head
    d.ellipse([cx - w * 0.13, h * 0.16, cx + w * 0.13, h * 1.05], fill=(14, 14, 15))       # mask rubber
    d.ellipse([cx - w * 0.105, h * 0.22, cx + w * 0.105, h * 0.62], fill=(52, 60, 66))     # mask lens
    for sx in (-1, 1):
        ex = cx + sx * w * 0.045
        d.ellipse([ex - w * 0.022, h * 0.36, ex + w * 0.022, h * 0.47], fill=(110, 96, 86))  # eyes
        d.ellipse([ex - w * 0.008, h * 0.39, ex + w * 0.008, h * 0.44], fill=(25, 22, 20))
    d.ellipse([cx - w * 0.05, h * 0.66, cx + w * 0.05, h * 0.93], fill=(32, 33, 35))       # regulator
    d.ellipse([cx - w * 0.03, h * 0.72, cx + w * 0.03, h * 0.87], fill=(18, 18, 19))
    for sx in (-1, 1):                                                                      # harness straps
        d.line([(cx + sx * w * 0.12, h * 0.3), (cx + sx * w * 0.2, h * 0.12)], fill=(10, 10, 10), width=int(w * 0.018))
    inner = np.asarray(inner.filter(ImageFilter.GaussianBlur(w * 0.004)), np.float64) / 255.0
    v = 1 - yy  # 0 at the bottom of the window, 1 at the top
    tint = np.array([0.10, 0.20, 0.22])
    glass = inner * 1.05 + tint * 0.14
    # Sky reflection across the top, a bright edge glint and two diagonal streaks.
    sky = smoothstep(0.55, 1.0, v) * 0.32
    glass += sky[..., None] * np.array([0.55, 0.70, 0.80])
    streak = np.exp(-((xx - 0.62 * v - 0.12) / 0.03) ** 2) * 0.35 + np.exp(-((xx - 0.62 * v - 0.2) / 0.012) ** 2) * 0.25
    glass += streak[..., None] * np.array([0.85, 0.9, 0.95])
    edge = np.minimum(np.minimum(xx, 1 - xx) * 2.2, np.minimum(yy, 1 - yy) * 1.1)
    glass *= (0.55 + 0.45 * smoothstep(0.0, 0.12, edge))[..., None]
    glass += (np.exp(-((v - 0.93) / 0.025) ** 2) * 0.25 * smoothstep(0.05, 0.3, edge))[..., None]
    return np.clip(glass, 0, 1)


def paint_gear(out):
    ao = np.clip(load(out, "gear", "ao")[..., 0], 0, 1)
    region = np.clip(np.rint(load(out, "gear", "region")[..., 0] * 16 - 0.5), 0, 2).astype(int)
    N = load(out, "gear", "normal") * 2 - 1
    H, W = ao.shape
    rng = np.random.default_rng(4)
    noise = ndimage.gaussian_filter(rng.random((H, W)), 1.2)
    col = np.zeros((H, W, 3))
    rubber = (0.085 + 0.03 * noise)[..., None] * np.array([1, 1, 1.05])
    col[:] = rubber
    valve = region == 2
    col[valve] = np.array([0.16, 0.16, 0.17]) * (0.9 + 0.2 * noise[valve])[:, None]
    shade = 0.75 + 0.35 * np.clip(N @ LIGHT, 0, 1)
    col *= ((0.45 + 0.55 * ao) * shade)[..., None]
    # The visor's planar UV block (top half of the atlas).
    y0, y1 = int(0.01 * H), int(0.49 * H)
    x0, x1 = int(0.01 * W), int(0.99 * W)
    col[y0:y1, x0:x1] = visor_art(x1 - x0, y1 - y0)
    return np.clip(col, 0, 1)


# ---------------------------------------------------------------- weights

def weights_for(points, faces, samples, sample_w, names, pick):
    tree = cKDTree(samples[pick])
    sw = sample_w[pick]
    d, i = tree.query(points, k=8, workers=-1)
    w = 1.0 / (d + 0.4) ** 3
    return (sw[i] * w[:, :, None]).sum(1) / w.sum(1)[:, None]


def rig(low, merc, names):
    pw = point_weights(merc)
    samples, sample_w = sample_body(merc, pw)
    chain = np.array([chain_of(names[i]) for i in sample_w.argmax(1)])
    head = names.index("B Head")
    # Helmet, laser and holster bones are not the suit's.
    for extra, target in (("DEF_01_Casque", "B Head"), ("Laser", "B Head"), ("HOLSTER", "B Spine1")):
        k = names.index(extra)
        sample_w[:, names.index(target)] += sample_w[:, k]
        sample_w[:, k] = 0
    part_ids = [str(x) for x in low["part_ids"]]
    v = low["suit_v"]
    part = np.rint(low["suit_part"]).astype(int)
    W = np.zeros((len(v), len(names)))
    picks = {
        "suit": np.ones(len(samples), bool),
        "gloveL": np.isin(chain, ["handL", "armL"]), "gloveR": np.isin(chain, ["handR", "armR"]),
        "bootL": np.isin(chain, ["footL", "legL"]), "bootR": np.isin(chain, ["footR", "legR"]),
    }
    for name, pick in picks.items():
        m = part == part_ids.index(name)
        W[m] = weights_for(v[m], None, samples, sample_w, names, pick)
    # The hood is rigid on the head above the shoulders, blending in over the neck.
    suit_m = part == part_ids.index("suit")
    blend = np.clip((v[:, 2] - 57.0) / 7.0, 0, 1) * (np.abs(v[:, 0]) < 17) * suit_m
    rigid = np.zeros_like(W)
    rigid[:, head] = 1
    W = W * (1 - blend[:, None]) + rigid * blend[:, None]
    a, deg = neighbours(len(v), low["suit_f"])
    for _ in range(3):
        W = 0.5 * W + 0.5 * (a @ W) / deg[:, None]
    W = limit_weights(W)
    # Gear: visor, gasket and hood valves on the head; the chest valve follows the suit under it.
    g = low["gear_v"]
    GW = np.zeros((len(g), len(names)))
    GW[:, head] = 1
    chest = g[:, 2] < 55
    if chest.any():
        tree = cKDTree(v[suit_m])
        _, i = tree.query(g[chest], k=1)
        GW[chest] = W[suit_m][i]
    return W, GW


# ---------------------------------------------------------------- main

def to_tga(img, size, path):
    im = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8), "RGB")
    if im.size[0] != size:
        im = im.resize((size, size), Image.LANCZOS)
    im.save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--install", required=True)
    ap.add_argument("--out", default="out/blender")
    ap.add_argument("--assets", default="out/blender/assets")
    args = ap.parse_args()
    out, assets = Path(args.out), Path(args.assets)
    assets.mkdir(parents=True, exist_ok=True)
    low = np.load(out / "low.npz")
    merc = read_merc(Path(args.install))
    names = [b.name for b in merc.bones]
    bone_pos = {nm: g[:3, 3] for nm, g in zip(names, bone_globals(merc.bones))}

    to_tga(paint_suit(out, bone_pos), SUIT_OUT, assets / "HazmatSuit.tga")
    to_tga(paint_gear(out), GEAR_OUT, assets / "HazmatGear.tga")
    print("textures written")

    W, GW = rig(low, merc, names)
    sv, gv = low["suit_v"], low["gear_v"]
    points = np.concatenate([sv, gv])
    wedges, faces, face_mat = [], [], []
    index = {}
    for mat, (f, uv, base) in enumerate(((low["suit_f"], low["suit_uv"], 0), (low["gear_f"], low["gear_uv"], len(sv)))):
        for tri, tuv in zip(f, uv):
            corners = []
            for k in range(3):
                key = (int(tri[k]) + base, round(float(tuv[k, 0]), 6), round(1.0 - float(tuv[k, 1]), 6), mat)
                if key not in index:
                    index[key] = len(wedges)
                    wedges.append(((key[0], key[1], key[2]), mat))
                corners.append(index[key])
            faces.append(corners)
            face_mat.append(mat)
    influences = []
    for i in range(len(sv)):
        for b in np.nonzero(W[i])[0]:
            influences.append((float(W[i, b]), i, int(b)))
    for i in range(len(gv)):
        for b in np.nonzero(GW[i] > 1e-4)[0]:
            influences.append((float(GW[i, b]), len(sv) + i, int(b)))
    # The PSK keeps the merc's winding: compare which way each mesh's torso faces turn.
    def outward(pts, tris):
        c = np.cross(pts[tris[:, 1]] - pts[tris[:, 0]], pts[tris[:, 2]] - pts[tris[:, 0]])
        m = pts[tris].mean(1)
        torso = np.abs(m[:, 2] - 25) < 10
        return np.sign((c[torso] * (m[torso] * np.array([1.0, 1.0, 0.0]))).sum(1)).mean()
    merc_sign = outward(merc.points, merc.wedges[merc.faces])
    suit_sign = outward(sv, low["suit_f"])
    print("winding: merc", round(float(merc_sign), 2), "suit", round(float(suit_sign), 2))
    faces = np.array(faces)
    if np.sign(merc_sign) != np.sign(suit_sign):
        faces = faces[:, ::-1]
    write_psk(assets / "HazmatMerc.psk", points, wedges, faces, np.array(face_mat), ["HazmatSuit", "HazmatGear"],
              merc.bones, influences)

    # Goggle lights: Character Skins moves them by an offset in B Head's axes, from the stock
    # merc's helmet lamp to the front of the visor.
    head = names.index("B Head")
    head_m = bone_globals(merc.bones)[head]
    pw = point_weights(merc)
    stock = merc.points[pw[:, names.index("DEF_01_Casque")] + pw[:, head] > 0.5]
    stock_front = stock[np.argmax(stock[:, 1])]
    visor = gv[np.rint(low["gear_part"]).astype(int) == [str(x) for x in low["part_ids"]].index("visor")]
    # The middle of the window at eye height (the stock lamp's height), on its front.
    visor_front = visor[np.argmin(np.abs(visor[:, 0]) + np.abs(visor[:, 2] - stock_front[2]) - 0.01 * visor[:, 1])]
    inv = np.linalg.inv(head_m)
    stock_local = (inv @ np.append(stock_front, 1))[:3]
    visor_local = (inv @ np.append(visor_front, 1))[:3]
    summary = {
        "points": int(len(points)), "wedges": len(wedges), "faces": int(len(faces)),
        "suit_faces": int(len(low["suit_f"])), "gear_faces": int(len(low["gear_f"])),
        "materials": ["HazmatSuit", "HazmatGear"], "rot_origin": list(merc.rot_origin),
        "visor_front_mesh": [round(float(x), 2) for x in visor_front],
        "goggle_offset_from_merc_front": [round(float(x), 1) for x in visor_local - stock_local],
    }
    (assets / "hazmat.json").write_text(json.dumps(summary, indent=2))
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()

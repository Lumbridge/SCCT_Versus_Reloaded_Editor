"""Renders the suit (and, for comparison, the stock merc) posed by the merc's own
animations (SPerso.Def), to check the rigging without the editor or the game.

    python pose_preview.py --install "C:\\...\\SVM 4.0 Public Beta Mapping Ver" --out out
        [--sequences JoggStAlFd2:6,WalkCrSpFd2:4,WaitStSpFd2:0] [--merc]

Writes out/pose_<sequence>_<frame>.png (front, three-quarter and side views).
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path

import numpy as np
from PIL import Image

from merc_mesh import bone_globals, pose, read_animation, read_merc
from preview import render, sheet


def read_psk(path: Path):
    d = Path(path).read_bytes()
    p, chunks = 0, {}
    while p < len(d):
        name, _flag, size, count = struct.unpack_from("<20sIii", d, p)
        p += 32
        chunks[name.rstrip(b"\0").decode()] = (p, count)
        p += size * count
    o, n = chunks["PNTS0000"]
    points = np.frombuffer(d, np.float32, n * 3, o).reshape(-1, 3).astype(np.float64)
    o, n = chunks["VTXW0000"]
    w = np.frombuffer(d, np.dtype([("p", "<u2"), ("x", "<u2"), ("u", "<f4"), ("v", "<f4"), ("m", "u1"), ("r", "u1"), ("y", "<u2")]), n, o)
    o, n = chunks["FACE0000"]
    f = np.frombuffer(d, np.dtype([("w", "<u2", 3), ("m", "u1"), ("a", "u1"), ("s", "<u4")]), n, o)
    o, n = chunks["RAWWEIGHTS"]
    inf = np.frombuffer(d, np.dtype([("w", "<f4"), ("p", "<i4"), ("b", "<i4")]), n, o)
    return points, w, f, inf


def skin(points, influences, bind, posed):
    out = np.zeros_like(points)
    mats = [p @ np.linalg.inv(b) for p, b in zip(posed, bind)]
    hom = np.c_[points, np.ones(len(points))]
    for wt, pi, bi in influences:
        out[pi] += wt * (mats[bi] @ hom[pi])[:3]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--install", required=True)
    ap.add_argument("--out", default="out")
    ap.add_argument("--sequences", default="WaitStSpFd2:0,JoggStAlFd2:5,JoggStAlFd2:16,WalkCrSpFd2:4,waitCrSpFd2:0")
    ap.add_argument("--merc", action="store_true", help="also render the stock merc in the same poses")
    args = ap.parse_args()
    out = Path(args.out)
    merc = read_merc(Path(args.install))
    anim = read_animation(Path(args.install))
    bind = bone_globals(merc.bones)
    points, w, f, inf = read_psk(out / "HazmatMerc.psk")
    tex = [np.asarray(Image.open(out / "HazmatSuit.tga").convert("RGB")),
           np.asarray(Image.open(out / "HazmatGear.tga").convert("RGB"))]
    uv = np.stack([w["u"], w["v"]], 1).astype(np.float64)
    infl = [(float(a), int(b), int(c)) for a, b, c in zip(inf["w"], inf["p"], inf["b"])]
    merc_infl = [(float(a), int(b), int(c)) for a, b, c in merc.influences]
    merc_tex = None
    if args.merc:
        grey = np.full((8, 8, 3), 150, np.uint8)
        merc_tex = [grey, grey, grey]
    for item in args.sequences.split(","):
        name, frame = item.split(":")
        posed = pose(merc.bones, anim, name, float(frame))
        p = skin(points, infl, bind, posed)
        box_lo = p.min(0) - 5
        box_hi = p.max(0) + 5
        span = (box_hi - box_lo).max() / 2
        c = (box_lo + box_hi) / 2
        views = []
        for yaw in (180, 140, 90):
            box = (c - span, c + span)
            views.append(render(p[w["p"]], uv, f["w"].astype(int), f["m"].astype(int), tex, yaw=yaw, size=360,
                                box=_rotated_box(p, yaw)))
        if args.merc:
            mp = skin(merc.points, merc_infl, bind, posed)
            views.append(render(mp[merc.wedges], merc.uvs, merc.faces, np.minimum(merc.face_material, 2), merc_tex,
                                yaw=140, size=360, box=_rotated_box(mp, 140)))
        sheet(views, out / f"pose_{name}_{frame}.png")
        print("wrote", out / f"pose_{name}_{frame}.png")


def _rotated_box(p, yaw):
    a = np.radians(yaw)
    r = np.array([[np.cos(a), -np.sin(a), 0], [np.sin(a), np.cos(a), 0], [0, 0, 1]])
    q = p @ r.T
    lo, hi = q.min(0), q.max(0)
    pad = 0.06 * (hi - lo).max()
    return lo - pad, hi + pad


if __name__ == "__main__":
    main()

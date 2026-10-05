"""A tiny software renderer for checking the suit without an editor: orthographic views,
z-buffer, textured with simple lighting. Used by build_hazmat.py --preview."""

from __future__ import annotations

import numpy as np
from PIL import Image


def _rot_z(yaw_deg: float) -> np.ndarray:
    a = np.radians(yaw_deg)
    c, s = np.cos(a), np.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def render(points, uvs, faces, face_tex, textures, yaw=0.0, size=512, pitch=0.0, box=None):
    """points (n,3) per wedge; faces (k,3) wedge indices; face_tex (k,) texture index;
    textures: list of HxWx3 uint8 arrays. yaw=0 looks along +Y at the model's back,
    yaw=180 at its front, Z up."""
    r = _rot_z(yaw)
    if pitch:
        a = np.radians(pitch)
        r = np.array([[1, 0, 0], [0, np.cos(a), -np.sin(a)], [0, np.sin(a), np.cos(a)]]) @ r
    p = points @ r.T
    if box is None:
        lo, hi = p.min(0), p.max(0)
    else:
        lo, hi = box
    span = max(hi[0] - lo[0], hi[2] - lo[2]) * 1.05
    cx, cz = (lo[0] + hi[0]) / 2, (lo[2] + hi[2]) / 2
    # Unreal is left-handed: looking along +Y with Z up, +X is on the left.
    sx = size / 2 - (p[:, 0] - cx) / span * size
    sy = size / 2 - (p[:, 2] - cz) / span * size
    depth = p[:, 1]
    img = np.full((size, size, 3), 40, np.float64)
    img[:, :, 2] = 52
    zbuf = np.full((size, size), np.inf)
    light = np.array([0.4, -0.6, 0.7])
    light /= np.linalg.norm(light)
    texs = [t.astype(np.float64) for t in textures]
    for f, ti in zip(faces, face_tex):
        a, b, c = f
        x = np.array([sx[a], sx[b], sx[c]])
        y = np.array([sy[a], sy[b], sy[c]])
        e1 = p[b] - p[a]
        e2 = p[c] - p[a]
        n = np.cross(e1, e2)
        ln = np.linalg.norm(n)
        if ln == 0:
            continue
        n /= ln
        x0, x1 = int(max(0, np.floor(x.min()))), int(min(size - 1, np.ceil(x.max())))
        y0, y1 = int(max(0, np.floor(y.min()))), int(min(size - 1, np.ceil(y.max())))
        if x1 < x0 or y1 < y0:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        det = (y[1] - y[2]) * (x[0] - x[2]) + (x[2] - x[1]) * (y[0] - y[2])
        if abs(det) < 1e-9:
            continue
        l0 = ((y[1] - y[2]) * (gx - x[2]) + (x[2] - x[1]) * (gy - y[2])) / det
        l1 = ((y[2] - y[0]) * (gx - x[2]) + (x[0] - x[2]) * (gy - y[2])) / det
        l2 = 1 - l0 - l1
        inside = (l0 >= -1e-6) & (l1 >= -1e-6) & (l2 >= -1e-6)
        if not inside.any():
            continue
        z = l0 * depth[a] + l1 * depth[b] + l2 * depth[c]
        sub = zbuf[y0:y1 + 1, x0:x1 + 1]
        draw = inside & (z < sub)
        if not draw.any():
            continue
        sub[draw] = z[draw]
        u = l0 * uvs[a, 0] + l1 * uvs[b, 0] + l2 * uvs[c, 0]
        v = l0 * uvs[a, 1] + l1 * uvs[b, 1] + l2 * uvs[c, 1]
        t = texs[ti]
        h, w = t.shape[:2]
        tx = np.clip((u % 1.0) * w, 0, w - 1).astype(int)
        ty = np.clip((v % 1.0) * h, 0, h - 1).astype(int)
        col = t[ty[draw], tx[draw]]
        # Two-sided Lambert plus ambient; the camera looks along +Y.
        shade = 0.35 + 0.65 * abs(float(n @ light))
        img[y0:y1 + 1, x0:x1 + 1][draw] = col * shade
    return Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))


def sheet(images, path):
    w = sum(i.width for i in images)
    h = max(i.height for i in images)
    out = Image.new("RGB", (w, h))
    x = 0
    for i in images:
        out.paste(i, (x, 0))
        x += i.width
    out.save(path)

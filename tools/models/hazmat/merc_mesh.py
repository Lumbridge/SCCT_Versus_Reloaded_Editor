"""Reads the stock merc model (SPerso.ukx, SkeletalMesh DEF_01) from an SCCT Versus install.

Self-contained: the hazmat suit generator needs the merc's skeleton, points, faces and
weights, and nothing else. The layout was reverse-engineered for the UE5 bridge project
(scctpkg/skeletalmesh.py there); this is the part of it DEF_01 needs.

    Package: standard UE2 header; names are XOR-obfuscated with (file offset & 0xFF);
    every name reference is two compact indices (local, cooked global).
    SkeletalMesh export: tagged properties (none on DEF_01), FBox, FSphere, Version,
    VertexCount, Verts, Textures, MeshScale, MeshOrigin, RotOrigin, ..., then a bone
    count and bones {name, flags, quat, pos, length, size, children, parent}; the base
    geometry sits at the end: Points | 0 | Wedges {u16 point, U, V} | 0 |
    Faces {u16 w0 w1 w2, u8 material, u8 flags, i32 smoothing} | 0 |
    Influences {f32 weight, u16 point, u16 bone}.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path
from typing import List, Tuple

import numpy as np

TAG = 0x9E2A83C1


def compact(d: bytes, p: int) -> Tuple[int, int]:
    b = d[p]
    p += 1
    n, neg, more, shift = b & 0x3F, b & 0x80, b & 0x40, 6
    while more:
        b = d[p]
        p += 1
        n |= (b & 0x7F) << shift
        shift += 7
        more = b & 0x80
    return (-n if neg else n), p


def encode_compact(n: int) -> bytes:
    out = bytearray([n & 63])
    n >>= 6
    if n:
        out[0] |= 64
    while n:
        b = n & 127
        n >>= 7
        out.append(b | (128 if n else 0))
    return bytes(out)


@dataclass
class Bone:
    name: str
    parent: int
    rotation: Tuple[float, float, float, float]  # x, y, z, w as stored (inverse for non-root bones)
    position: Tuple[float, float, float]
    length: float
    size: Tuple[float, float, float]
    children: int


@dataclass
class MercMesh:
    bones: List[Bone]
    points: np.ndarray       # (n, 3) mesh space, Z up
    wedges: np.ndarray       # (m,) point index per wedge
    uvs: np.ndarray          # (m, 2)
    faces: np.ndarray        # (k, 3) wedge indices
    face_material: np.ndarray
    influences: np.ndarray   # (j, 3) weight, point, bone
    rot_origin: Tuple[int, int, int]


class Package:
    def __init__(self, path: Path):
        self.data = d = Path(path).read_bytes()
        if struct.unpack_from("<I", d, 0)[0] != TAG:
            raise ValueError(f"{path}: not an uncompressed Unreal package")
        nc, no, ec, eo, ic, io = struct.unpack_from("<6i", d, 12)
        self.names = []
        p = no
        for _ in range(nc):
            n, p = compact(d, p)
            self.names.append(bytes(v ^ ((p + j) & 0xFF) for j, v in enumerate(d[p:p + n])).rstrip(b"\0").decode("latin1"))
            p += n + 4
        self.imports = []
        p = io
        for _ in range(ic):
            _, p = self.name(p)
            cls, p = self.name(p)
            p += 4
            name, p = self.name(p)
            self.imports.append((cls, name))
        self.exports = []
        p = eo
        for _ in range(ec):
            cls, p = compact(d, p)
            _, p = compact(d, p)
            p += 4
            name, p = self.name(p)
            p += 4
            size, p = compact(d, p)
            offset = 0
            if size > 0:
                offset, p = compact(d, p)
            self.exports.append((self.class_name(cls), name, offset, size))

    def name(self, p: int) -> Tuple[str, int]:
        i, p = compact(self.data, p)
        _, p = compact(self.data, p)
        return self.names[i], p

    def class_name(self, ref: int) -> str:
        if ref < 0:
            return self.imports[-ref - 1][1]
        return self.exports[ref - 1][1] if ref > 0 else "Class"

    def export(self, cls: str, name: str) -> bytes:
        for c, n, offset, size in self.exports:
            if c == cls and n.lower() == name.lower():
                return self.data[offset:offset + size], offset
        raise KeyError(f"{cls} {name} not found")


def _bone(pkg: Package, d: bytes, base: int, p: int):
    try:
        name, p = pkg_name(pkg, d, p)
        p += 4
        q = struct.unpack_from("<4f", d, p)
        pos = struct.unpack_from("<3f", d, p + 16)
        length = struct.unpack_from("<f", d, p + 28)[0]
        size = struct.unpack_from("<3f", d, p + 32)
        children, parent = struct.unpack_from("<2i", d, p + 44)
    except (IndexError, struct.error):
        return None
    if not 0.95 < sum(x * x for x in q) < 1.05 or not 0 <= children < 512 or not -1 <= parent < 1024:
        return None
    return Bone(name, parent, q, pos, length, size, children), p + 52


def pkg_name(pkg: Package, d: bytes, p: int) -> Tuple[str, int]:
    i, p = compact(d, p)
    _, p = compact(d, p)
    if not 0 <= i < len(pkg.names):
        raise IndexError(i)
    return pkg.names[i], p


def read_merc(install: Path, mesh: str = "DEF_01") -> MercMesh:
    pkg = Package(Path(install) / "Packages" / "Animations" / "SPerso.ukx")
    d, base = pkg.export("SkeletalMesh", mesh)
    name, p = pkg_name(pkg, d, 0)
    if name != "None":
        raise ValueError(f"{mesh}: unexpected tagged properties")
    p += 25 + 16
    _version, vertex_count = struct.unpack_from("<2i", d, p)
    p += 8
    n, p = compact(d, p)
    p += 4 * n
    n, p = compact(d, p)
    for _ in range(n):
        _, p = compact(d, p)
    p += 24
    rot_origin = struct.unpack_from("<3i", d, p)
    p += 12
    # Skeleton: the first bone count followed by a chain of plausible bones.
    bones = None
    while p < len(d):
        count, q = compact(d, p)
        if 1 <= count <= 512:
            chain = []
            while len(chain) < count:
                b = _bone(pkg, d, base, q)
                if not b:
                    break
                chain.append(b[0])
                q = b[1]
            if len(chain) == count:
                bones, skeleton_end = chain, q
                break
        p += 1
    if bones is None:
        raise ValueError("skeleton not found")
    # Geometry: VertexCount compact, float3 points, then 0.
    enc = encode_compact(vertex_count)
    p = d.find(enc, skeleton_end)
    while p >= 0:
        q = p + len(enc)
        pts = np.frombuffer(d, np.float32, vertex_count * 3, q).reshape(-1, 3)
        tail = q + vertex_count * 12
        if np.all(np.isfinite(pts)) and np.abs(pts).max() < 1e5 and struct.unpack_from("<i", d, tail)[0] == 0:
            try:
                return _geometry(d, tail + 4, pts.copy(), bones, rot_origin)
            except (ValueError, struct.error):
                pass
        p = d.find(enc, p + 1)
    raise ValueError("geometry not found")


def _geometry(d, p, points, bones, rot_origin) -> MercMesh:
    nw, p = compact(d, p)
    w = np.frombuffer(d, np.dtype([("v", "<u2"), ("u", "<f4"), ("t", "<f4")]), nw, p)
    p += nw * 10
    if struct.unpack_from("<i", d, p)[0] != 0:
        raise ValueError("wedges")
    nf, p = compact(d, p + 4)
    f = np.frombuffer(d, np.dtype([("w", "<u2", 3), ("m", "u1"), ("fl", "u1"), ("s", "<i4")]), nf, p)
    p += nf * 12
    if struct.unpack_from("<i", d, p)[0] != 0:
        raise ValueError("faces")
    ni, p = compact(d, p + 4)
    inf = np.frombuffer(d, np.dtype([("w", "<f4"), ("p", "<u2"), ("b", "<u2")]), ni, p)
    if w["v"].max() >= len(points) or f["w"].max() >= nw or inf["b"].max() >= len(bones):
        raise ValueError("indices")
    return MercMesh(bones, points, w["v"].astype(np.int64), np.stack([w["u"], w["t"]], 1).astype(np.float64),
                    f["w"].astype(np.int64), f["m"].astype(np.int64),
                    np.stack([inf["w"], inf["p"], inf["b"]], 1).astype(np.float64), rot_origin)


# ---- skeleton maths (UE2: non-root bones store the inverse rotation) ----

def quat_matrix(q) -> np.ndarray:
    x, y, z, w = np.asarray(q, np.float64) / np.linalg.norm(q)
    return np.array([
        [1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
        [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
        [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)],
    ])


def bone_globals(bones: List[Bone]) -> List[np.ndarray]:
    out = []
    for i, b in enumerate(bones):
        m = np.eye(4)
        r = quat_matrix(b.rotation)
        m[:3, :3] = r if i == 0 else r.T
        m[:3, 3] = b.position
        out.append(m if i == 0 or b.parent == i else out[b.parent] @ m)
    return out


# ---- the merc's animation set (SPerso.Def), for posed previews ----

@dataclass
class Track:
    quats: np.ndarray
    positions: np.ndarray
    times: np.ndarray


@dataclass
class Sequence:
    name: str
    start: int
    frames: int
    rate: float


@dataclass
class Animation:
    bone_names: List[str]
    moves: List[Tuple[List[int], List[Track]]]
    sequences: List[Sequence]


def _track(d, p):
    p += 4
    nq, p = compact(d, p)
    q = np.frombuffer(d, np.int16, nq * 4, p).reshape(-1, 4) / 32767.0
    p += nq * 8
    npos, p = compact(d, p)
    pos = np.frombuffer(d, np.int16, npos * 3, p).reshape(-1, 3) / 32.0
    p += npos * 6
    nk, p = compact(d, p)
    t = np.frombuffer(d, np.uint16, nk, p).astype(int)
    return Track(q, pos, t), p + nk * 2


def read_animation(install: Path, name: str = "Def") -> Animation:
    """MeshAnimation layout (scctpkg/meshanim.py in the UE5 bridge): version, named bones,
    motion chunks {12 bytes, frames, start bone, flags, bone indices, tracks, root track},
    then FMeshAnimSeq {f32, name, groups, start, frames, notifies, rate}."""
    pkg = Package(Path(install) / "Packages" / "Animations" / "SPerso.ukx")
    d, _ = pkg.export("MeshAnimation", name)
    n, p = pkg_name(pkg, d, 0)
    if n != "None":
        raise ValueError("unexpected tagged properties")
    p += 4
    nb, p = compact(d, p)
    names = []
    for _ in range(nb):
        bn, p = pkg_name(pkg, d, p)
        names.append(bn)
        p += 8
    nm, p = compact(d, p)
    moves = []
    for _ in range(nm):
        p += 12 + 4 + 4 + 4
        nbi, p = compact(d, p)
        idx = list(struct.unpack_from(f"<{nbi}i", d, p))
        p += 4 * nbi
        nt, p = compact(d, p)
        tracks = []
        for _ in range(nt):
            t, p = _track(d, p)
            tracks.append(t)
        _, p = _track(d, p)
        moves.append((idx, tracks))
    ns, p = compact(d, p)
    seqs = []
    for _ in range(ns):
        p += 4
        sn, p = pkg_name(pkg, d, p)
        ng, p = compact(d, p)
        for _ in range(ng):
            _, p = pkg_name(pkg, d, p)
        start, frames = struct.unpack_from("<2i", d, p)
        p += 8
        nn, p = compact(d, p)
        for _ in range(nn):
            p += 6
            _, p = compact(d, p)
        rate = struct.unpack_from("<f", d, p)[0]
        p += 4
        seqs.append(Sequence(sn, start, frames, rate))
    return Animation(names, moves, seqs)


def pose(bones: List[Bone], anim: Animation, sequence: str, frame: float) -> List[np.ndarray]:
    """Global bone matrices of the mesh skeleton at a frame of a sequence."""
    k = [s.name.lower() for s in anim.sequences].index(sequence.lower())
    idx, tracks = anim.moves[k]
    by_name = {b.name.lower(): i for i, b in enumerate(bones)}
    local_r = [quat_matrix(b.rotation) if i == 0 else quat_matrix(b.rotation).T for i, b in enumerate(bones)]
    local_t = [np.array(b.position, float) for b in bones]
    for ti, track in enumerate(tracks):
        bi = by_name.get(anim.bone_names[idx[ti]].lower()) if ti < len(idx) else None
        if bi is None:
            continue
        def key(n):
            if n == 0:
                return None
            if len(track.times) >= n:
                times = track.times[:n]
                return int(np.clip(np.searchsorted(times, frame, side="right") - 1, 0, n - 1))
            return int(np.clip(round(frame), 0, n - 1))
        kq = key(len(track.quats))
        if kq is not None:
            r = quat_matrix(track.quats[kq])
            local_r[bi] = r if bi == 0 else r.T
        kp = key(len(track.positions))
        if kp is not None:
            local_t[bi] = track.positions[kp]
    out = []
    for i, b in enumerate(bones):
        m = np.eye(4)
        m[:3, :3] = local_r[i]
        m[:3, 3] = local_t[i]
        out.append(m if i == 0 or b.parent == i else out[b.parent] @ m)
    return out


if __name__ == "__main__":
    import sys
    m = read_merc(Path(sys.argv[1]))
    g = bone_globals(m.bones)
    print(len(m.points), "points", len(m.faces), "faces", len(m.bones), "bones")
    for b, x in zip(m.bones, g):
        print(f"{b.name:16s} {np.round(x[:3, 3], 1)}")

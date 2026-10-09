#!/usr/bin/env python3
"""
Generates the Paladin gear kit for TimeKnight as .glb files (one mesh per file).

Authoring convention (glTF space, units authored in centimetres, exported as metres):
    +Y = up        +Z = character forward        X = character left/right
Every piece is left/right mirror-symmetric on purpose, so it does not matter which way
the importer maps glTF's X axis onto Unreal's Y axis.

Each file contains ONE mesh with several primitives; each primitive has a named material
(Steel / Gold / Leather / Cloth / Hair).  The C++ component swaps those for its own
colour-parameterised materials by matching the slot name.
"""
import json
import struct
import sys
from pathlib import Path

import numpy as np

OUT = Path(sys.argv[1] if len(sys.argv) > 1 else "out")
OUT.mkdir(parents=True, exist_ok=True)

# name -> (rgb 0..1 linear-ish, metallic, roughness)
MATERIALS = {
    "Steel":   ((0.72, 0.74, 0.78), 0.90, 0.35),
    "Gold":    ((0.95, 0.70, 0.22), 0.90, 0.30),
    "Leather": ((0.20, 0.11, 0.06), 0.00, 0.80),
    "Cloth":   ((0.07, 0.14, 0.45), 0.00, 0.85),
    "Hair":    ((0.02, 0.02, 0.025), 0.00, 0.45),
}


# ----------------------------------------------------------------------------- geometry
class Part:
    def __init__(self, V, F, material, smooth=False):
        self.V = np.asarray(V, dtype=np.float64)
        self.F = np.asarray(F, dtype=np.int64)
        self.material = material
        self.smooth = smooth


def _face_normals(V, F):
    a, b, c = V[F[:, 0]], V[F[:, 1]], V[F[:, 2]]
    return np.cross(b - a, c - a)


def orient(V, F, refs):
    """Flip triangles so their normal points away from `refs` (per-face reference points or one point)."""
    V = np.asarray(V)
    F = np.asarray(F).copy()
    n = _face_normals(V, F)
    cen = V[F].mean(axis=1)
    refs = np.broadcast_to(np.asarray(refs, dtype=np.float64), cen.shape)
    flip = np.einsum("ij,ij->i", n, cen - refs) < 0
    F[flip] = F[flip][:, ::-1]
    return F


def orient_convex(V, F):
    return orient(V, F, np.asarray(V).mean(axis=0))


def icosphere(subdiv=2):
    t = (1.0 + 5 ** 0.5) / 2.0
    V = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t),
         (0, -1, -t), (0, 1, -t), (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    F = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4),
         (11, 10, 2), (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8),
         (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    V = [np.array(v, dtype=np.float64) / np.linalg.norm(v) for v in V]
    for _ in range(subdiv):
        cache, newF = {}, []

        def mid(i, j):
            k = (min(i, j), max(i, j))
            if k not in cache:
                m = V[i] + V[j]
                V.append(m / np.linalg.norm(m))
                cache[k] = len(V) - 1
            return cache[k]

        for a, b, c in F:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            newF += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        F = newF
    return np.array(V), np.array(F)


def rot_x(deg):
    a = np.radians(deg)
    c, s = np.cos(a), np.sin(a)
    return np.array([[1, 0, 0], [0, c, -s], [0, s, c]])


def rot_z(deg):
    a = np.radians(deg)
    c, s = np.cos(a), np.sin(a)
    return np.array([[c, -s, 0], [s, c, 0], [0, 0, 1]])


def ellipsoid(radii, center=(0, 0, 0), material="Steel", subdiv=3, rot=None, smooth=True):
    V, F = icosphere(subdiv)
    V = V * np.asarray(radii)
    if rot is not None:
        V = V @ rot.T
    V = V + np.asarray(center)
    return Part(V, F, material, smooth)


def box(size, center=(0, 0, 0), material="Steel"):
    sx, sy, sz = (s / 2 for s in size)
    V = np.array([(x, y, z) for x in (-sx, sx) for y in (-sy, sy) for z in (-sz, sz)], dtype=np.float64)
    F = []
    for axis in range(3):
        for sign in (0, 1):
            idx = [i for i in range(8) if ((i >> (2 - axis)) & 1) == sign]
            a, b, c, d = idx
            F += [(a, b, c), (b, d, c)]
    V = V + np.asarray(center)
    return Part(V, orient_convex(V, F), material, False)


def cylinder_y(radius, y0, y1, center_xz=(0, 0), material="Steel", sections=20, smooth=True):
    ang = np.linspace(0, 2 * np.pi, sections, endpoint=False)
    ring = np.stack([radius * np.cos(ang) + center_xz[0], np.zeros(sections), radius * np.sin(ang) + center_xz[1]], 1)
    V = np.vstack([ring + [0, y0, 0], ring + [0, y1, 0], [[center_xz[0], y0, center_xz[1]]], [[center_xz[0], y1, center_xz[1]]]])
    F = []
    n = sections
    for i in range(n):
        j = (i + 1) % n
        F += [(i, j, n + i), (j, n + j, n + i), (2 * n, j, i), (2 * n + 1, n + i, n + j)]
    return Part(V, orient_convex(V, F), material, smooth)


def torus_y(rx, rz, minor, y=0.0, material="Gold", major_sections=36, minor_sections=8):
    U = np.linspace(0, 2 * np.pi, major_sections, endpoint=False)
    W = np.linspace(0, 2 * np.pi, minor_sections, endpoint=False)
    V, refs_v = [], []
    for u in U:
        for w in W:
            V.append(((rx + minor * np.cos(w)) * np.cos(u), y + minor * np.sin(w), (rz + minor * np.cos(w)) * np.sin(u)))
            refs_v.append((rx * np.cos(u), y, rz * np.sin(u)))
    V, refs_v = np.array(V), np.array(refs_v)
    F = []
    for i in range(major_sections):
        for k in range(minor_sections):
            a = i * minor_sections + k
            b = i * minor_sections + (k + 1) % minor_sections
            c = ((i + 1) % major_sections) * minor_sections + k
            d = ((i + 1) % major_sections) * minor_sections + (k + 1) % minor_sections
            F += [(a, b, c), (b, d, c)]
    F = np.array(F)
    refs = refs_v[F].mean(axis=1)
    return Part(V, orient(V, F, refs), material, True)


def prism_x(poly_zy, x0, x1, material="Cloth"):
    """Extrude a CONVEX polygon given in (z, y) along X from x0 to x1."""
    P = np.asarray(poly_zy, dtype=np.float64)
    n = len(P)
    c = P.mean(axis=0)
    front = [(x1, p[1], p[0]) for p in P] + [(x1, c[1], c[0])]
    back = [(x0, p[1], p[0]) for p in P] + [(x0, c[1], c[0])]
    V = np.array(front + back)
    F = []
    for i in range(n):
        j = (i + 1) % n
        F += [(i, j, n), (n + 1 + i, n + 1 + j, 2 * n + 1)]
        F += [(i, j, n + 1 + i), (j, n + 1 + j, n + 1 + i)]
    return Part(V, orient_convex(V, F), material, False)


def loft_shell(rings, material="Steel", sections=28, front_bulge=1.0, back_scale=1.0):
    """rings: list of (y, half_width_x, half_depth_z). Open tube (no caps), outward normals."""
    t = np.linspace(0, 2 * np.pi, sections, endpoint=False)
    V = []
    for (y, rx, rz) in rings:
        for a in t:
            z = np.sin(a) * rz
            z *= front_bulge if z > 0 else back_scale
            V.append((np.cos(a) * rx, y, z))
    V = np.array(V)
    F = []
    for r in range(len(rings) - 1):
        for i in range(sections):
            j = (i + 1) % sections
            a, b = r * sections + i, r * sections + j
            c, d = (r + 1) * sections + i, (r + 1) * sections + j
            F += [(a, b, c), (b, d, c)]
    F = np.array(F)
    refs = V[F].mean(axis=1) * np.array([0, 0, 0]) + np.array([0, 0, 0])
    cen = V[F].mean(axis=1)
    refs = np.stack([np.zeros(len(F)), cen[:, 1], np.zeros(len(F))], 1)  # axis through (0, y, 0)
    return Part(V, orient(V, F, refs), material, True)


def cut(part, keep_face_fn):
    """Keep faces of `part` where keep_face_fn(centroids)->bool mask, and drop unused vertices."""
    cen = part.V[part.F].mean(axis=1)
    mask = keep_face_fn(cen)
    F = part.F[mask]
    used = np.unique(F)
    remap = -np.ones(len(part.V), dtype=np.int64)
    remap[used] = np.arange(len(used))
    return Part(part.V[used], remap[F], part.material, part.smooth)


# ----------------------------------------------------------------------------- items
def make_sword():
    parts = []
    # pivot = centre of the grip, blade along +Y, blade flat faces look left/right, cutting edges front/back
    parts.append(cylinder_y(1.45, -6.0, 6.0, material="Leather", sections=14))
    for y in (-3.8, -1.3, 1.2, 3.7):  # grip wrap rings
        parts.append(cylinder_y(1.75, y - 0.35, y + 0.35, material="Gold", sections=14))
    parts.append(ellipsoid((2.6, 2.3, 2.6), (0, -8.4, 0), "Gold", 2))               # pommel
    parts.append(box((3.2, 2.4, 4.0), (0, 6.9, 0), "Gold"))                          # guard collar
    parts.append(box((2.2, 2.0, 27.0), (0, 7.2, 0), "Gold"))                         # cross-guard, runs front/back
    for s in (-1, 1):
        parts.append(ellipsoid((1.5, 1.5, 1.7), (0, 7.2, s * 13.5), "Gold", 2))      # guard end caps
    # blade: diamond cross-section, tapering to a point
    rings = [(8.2, 0.60, 2.9), (58.0, 0.50, 2.5), (68.0, 0.42, 2.1)]
    tip_y = 84.0
    V, F = [], []
    for (y, tx, wz) in rings:
        V += [(0, y, wz), (tx, y, 0), (0, y, -wz), (-tx, y, 0)]
    V.append((0, tip_y, 0))
    for r in range(len(rings) - 1):
        for i in range(4):
            j = (i + 1) % 4
            a, b, c, d = r * 4 + i, r * 4 + j, (r + 1) * 4 + i, (r + 1) * 4 + j
            F += [(a, b, c), (b, d, c)]
    last = (len(rings) - 1) * 4
    for i in range(4):
        F.append((last + i, last + (i + 1) % 4, len(V) - 1))
    F += [(0, 2, 1), (0, 3, 2)]  # base cap (hidden inside guard)
    V = np.array(V)
    parts.append(Part(V, orient_convex(V, F), "Steel", False))
    return parts


def make_shield():
    # pivot = centre of the shield face; face looks along +-X (identical on both sides), top is +Y, width along Z.
    outline = [(-21, 25), (21, 25), (22, 9), (15, -12), (0, -32), (-15, -12), (-22, 9)]  # (z, y) heater shape
    outline = np.array(outline, dtype=np.float64)
    c = outline.mean(axis=0)
    gold = c + (outline - c) * 1.00
    cloth = c + (outline - c) * 0.90
    parts = [prism_x(gold, -1.5, 1.5, "Gold"),            # rim
             prism_x(cloth, -1.8, 1.8, "Cloth")]          # painted face, proud of the rim on both sides
    parts.append(box((4.2, 46.0, 6.0), (0, 2.0, 0), "Gold"))     # cross - vertical
    parts.append(box((4.2, 6.0, 30.0), (0, 10.0, 0), "Gold"))    # cross - horizontal
    parts.append(ellipsoid((2.6, 4.6, 4.6), (0, 10.0, 0), "Steel", 2))  # boss, both sides
    return parts


def make_hair():
    # pivot = approx. centre of the head; front = +Z, top = +Y
    base = ellipsoid((9.0, 11.6, 10.4), (0, 0, 0), "Hair", 3)

    def keep(c):
        below = c[:, 1] < -3.2
        face_opening = (c[:, 2] > 2.0) & (c[:, 1] < 5.0) & (np.abs(c[:, 0]) < 6.4)
        ear_cut = (np.abs(c[:, 0]) > 6.0) & (c[:, 1] < 0.5) & (c[:, 2] > -2.0)
        return ~(below | face_opening | ear_cut)

    parts = [cut(base, keep)]
    # swept-back quiff and top volume
    for (cx, cy, cz, rx, ry, rz, tilt) in [
        (0.0, 10.2, 3.2, 4.6, 3.0, 7.0, -20),
        (3.8, 9.6, 0.5, 3.4, 2.6, 6.2, -12),
        (-3.8, 9.6, 0.5, 3.4, 2.6, 6.2, -12),
        (0.0, 9.0, -4.5, 5.0, 3.2, 5.5, 10),
        (0.0, 4.0, -9.0, 6.5, 6.0, 3.2, 0),
    ]:
        parts.append(ellipsoid((rx, ry, rz), (cx, cy, cz), "Hair", 3, rot=rot_x(tilt)))
    # short sideburns
    for s in (-1, 1):
        parts.append(ellipsoid((1.2, 3.2, 1.8), (s * 8.0, 0.2, 3.8), "Hair", 2))
    return parts


def make_pauldron():
    # pivot = shoulder joint; dome is up (+Y); symmetric so one mesh serves both shoulders
    parts = []
    dome = ellipsoid((9.6, 7.6, 10.4), (0, 0, 0), "Steel", 3)
    parts.append(cut(dome, lambda c: c[:, 1] > -0.3))
    top = ellipsoid((7.6, 6.2, 8.2), (0, 1.8, 0), "Steel", 3)
    parts.append(cut(top, lambda c: c[:, 1] > 1.7))
    parts.append(torus_y(9.4, 10.2, 0.65, y=-0.1, material="Gold"))
    parts.append(torus_y(7.2, 7.9, 0.5, y=2.4, material="Gold"))
    return parts


def make_chest():
    # pivot = middle of the torso; spans y -16 (waist) to +18.5 (collar)
    rings = [(-16.0, 14.0, 10.2), (-7.0, 15.0, 11.2), (4.0, 17.6, 12.8), (13.0, 18.2, 12.2), (18.5, 13.0, 9.2)]
    parts = [loft_shell(rings, "Steel", sections=32, front_bulge=1.10, back_scale=0.95)]
    parts.append(torus_y(14.0, 10.2 * 1.02, 0.9, y=-16.0, material="Gold", major_sections=32))   # belt line
    parts.append(torus_y(13.0, 9.2, 0.8, y=18.5, material="Gold", major_sections=32))           # collar
    parts.append(box((3.0, 17.0, 2.6), (0, 5.0, 14.2), "Gold"))      # cross on the chest
    parts.append(box((11.0, 3.0, 2.6), (0, 9.0, 14.2), "Gold"))
    parts.append(box((3.0, 15.0, 2.0), (0, 4.0, -12.2), "Gold"))     # small cross on the back
    parts.append(box((10.0, 3.0, 2.0), (0, 8.0, -12.2), "Gold"))
    return parts


def make_tabard():
    # pivot = top centre of the cloth; plane at z=0 (the component offset pushes it in front of the hips)
    outline = [(-10.5, 0.0), (10.5, 0.0), (10.5, -25.0), (0.0, -31.0), (-10.5, -25.0)]  # (x, y)
    outline = np.array([(p[0], p[1]) for p in outline])
    # prism_x extrudes along X using (z, y); here we want to extrude along Z using (x, y) -> swap by hand
    P = outline
    n = len(P)
    c = P.mean(axis=0)
    t = 0.45
    V = np.array([(p[0], p[1], t) for p in P] + [(c[0], c[1], t)] + [(p[0], p[1], -t) for p in P] + [(c[0], c[1], -t)])
    F = []
    for i in range(n):
        j = (i + 1) % n
        F += [(i, j, n), (n + 1 + i, n + 1 + j, 2 * n + 1), (i, j, n + 1 + i), (j, n + 1 + j, n + 1 + i)]
    parts = [Part(V, orient_convex(V, F), "Cloth", False)]
    parts.append(box((21.0, 1.6, 1.3), (0, -0.8, 0), "Gold"))                 # gold waist hem
    parts.append(box((3.0, 17.0, 1.5), (0, -11.0, 0), "Gold"))                # cross down the front
    parts.append(box((10.0, 3.0, 1.5), (0, -8.0, 0), "Gold"))
    return parts


ITEMS = {
    "SM_Paladin_Sword": make_sword,
    "SM_Paladin_Shield": make_shield,
    "SM_Paladin_Hair": make_hair,
    "SM_Paladin_Pauldron": make_pauldron,
    "SM_Paladin_Chest": make_chest,
    "SM_Paladin_Tabard": make_tabard,
}


# ----------------------------------------------------------------------------- GLB writer
def vertex_normals(V, F):
    fn = _face_normals(V, F)
    N = np.zeros_like(V)
    for k in range(3):
        np.add.at(N, F[:, k], fn)
    ln = np.linalg.norm(N, axis=1, keepdims=True)
    ln[ln == 0] = 1
    return N / ln


def finalize(part):
    """Return (V, N, F) ready for export; flat parts get per-face vertices."""
    V, F = part.V, part.F
    if part.smooth:
        return V, vertex_normals(V, F), F
    nv = V[F].reshape(-1, 3)
    fn = _face_normals(V, F)
    ln = np.linalg.norm(fn, axis=1, keepdims=True)
    ln[ln == 0] = 1
    fn = fn / ln
    N = np.repeat(fn, 3, axis=0)
    return nv, N, np.arange(len(nv)).reshape(-1, 3)


def write_glb(path, name, parts):
    by_mat = {}
    for p in parts:
        by_mat.setdefault(p.material, []).append(finalize(p))

    bin_chunks, buffer_views, accessors, primitives, materials = [], [], [], [], []
    offset = 0

    def add_view(data, target):
        nonlocal offset
        pad = (-len(data)) % 4
        buffer_views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(data), "target": target})
        bin_chunks.append(data + b"\0" * pad)
        offset += len(data) + pad
        return len(buffer_views) - 1

    for mat_name, items in by_mat.items():
        Vs, Ns, Fs, base = [], [], [], 0
        for (V, N, F) in items:
            Vs.append(V / 100.0)  # cm -> m
            Ns.append(N)
            Fs.append(F + base)
            base += len(V)
        V = np.vstack(Vs).astype("<f4")
        N = np.vstack(Ns).astype("<f4")
        F = np.vstack(Fs).astype("<u4")
        rgb, metal, rough = MATERIALS[mat_name]
        materials.append({"name": mat_name, "doubleSided": True,
                          "pbrMetallicRoughness": {"baseColorFactor": [*rgb, 1.0], "metallicFactor": metal, "roughnessFactor": rough}})
        accessors.append({"bufferView": add_view(V.tobytes(), 34962), "componentType": 5126, "count": len(V), "type": "VEC3",
                          "min": V.min(axis=0).tolist(), "max": V.max(axis=0).tolist()})
        a_pos = len(accessors) - 1
        accessors.append({"bufferView": add_view(N.tobytes(), 34962), "componentType": 5126, "count": len(N), "type": "VEC3"})
        a_nrm = len(accessors) - 1
        accessors.append({"bufferView": add_view(F.tobytes(), 34963), "componentType": 5125, "count": int(F.size), "type": "SCALAR"})
        a_idx = len(accessors) - 1
        primitives.append({"attributes": {"POSITION": a_pos, "NORMAL": a_nrm}, "indices": a_idx, "material": len(materials) - 1, "mode": 4})

    gltf = {
        "asset": {"version": "2.0", "generator": "TimeKnight paladin kit generator"},
        "scene": 0, "scenes": [{"nodes": [0]}],
        "nodes": [{"name": name, "mesh": 0}],
        "meshes": [{"name": name, "primitives": primitives}],
        "materials": materials, "accessors": accessors, "bufferViews": buffer_views,
        "buffers": [{"byteLength": offset}],
    }
    js = json.dumps(gltf, separators=(",", ":")).encode()
    js += b" " * ((-len(js)) % 4)
    binary = b"".join(bin_chunks)
    total = 12 + 8 + len(js) + 8 + len(binary)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", 0x46546C67, 2, total))
        f.write(struct.pack("<II", len(js), 0x4E4F534A) + js)
        f.write(struct.pack("<II", len(binary), 0x004E4942) + binary)
    return sum(len(i[2]) for v in by_mat.values() for i in v)


if __name__ == "__main__":
    for name, fn in ITEMS.items():
        parts = fn()
        tris = write_glb(OUT / f"{name}.glb", name, parts)
        allv = np.vstack([p.V for p in parts])
        lo, hi = allv.min(0), allv.max(0)
        print(f"{name:22s} {tris:5d} tris   bounds(cm) X[{lo[0]:6.1f},{hi[0]:6.1f}] Y[{lo[1]:6.1f},{hi[1]:6.1f}] Z[{lo[2]:6.1f},{hi[2]:6.1f}]")

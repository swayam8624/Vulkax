#!/usr/bin/env python3
"""Vulkax presentation-only surface proxy renderer (stdlib only).

Stable visualisation layer for Vulkax captured-world runs.

Priority:
1. If --particles-csv is supplied and forms a regular 3-D lattice, render the
   outer shell of the physical MPM particle body. This is the preferred view
   for sparse-appearance captures such as the canonical 5-Gaussian/64-particle
   example.
2. Otherwise render a surface inferred from the appearance Gaussians:
   a square row-major lattice when available, or a conservative convex-hull
   proxy for sparse non-lattice clouds.

The renderer never mutates evidence. It may transfer an actual Gaussian
before->after displacement field onto a physical proxy for presentation, but
when there is no geometric displacement it keeps the geometry unchanged and
uses the verified rewrite region as an explanatory material highlight.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import struct
import tempfile
import zlib
from pathlib import Path

SH0 = 0.28209479177387814
EPS = 1.0e-12


def vadd(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def vsub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def vmul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0],
)
def norm(a): return math.sqrt(max(dot(a, a), 0.0))
def unit(a):
    n = norm(a)
    return (0.0, 0.0, 1.0) if n < EPS else vmul(a, 1.0 / n)
def clamp01(x): return max(0.0, min(1.0, x))
def mix(a, b, t): return tuple(a[i] * (1.0 - t) + b[i] * t for i in range(3))


def read_ply(path: Path):
    props = []
    count = None
    in_vertex = False
    with path.open("r", encoding="utf-8") as f:
        for raw in f:
            s = raw.strip()
            if s.startswith("element vertex "):
                count = int(s.split()[-1])
                in_vertex = True
                continue
            if s.startswith("element ") and not s.startswith("element vertex "):
                in_vertex = False
            if in_vertex and s.startswith("property "):
                props.append(s.split()[-1])
            if s == "end_header":
                break
        rows = [r.split() for r in f if r.strip()]
    if count is None or len(rows) < count:
        raise ValueError(f"bad PLY: {path}")
    ix = {p: i for i, p in enumerate(props)}
    if not all(k in ix for k in ("x", "y", "z")):
        raise ValueError(f"PLY lacks xyz positions: {path}")
    out = []
    for n, row in enumerate(rows[:count], 1):
        pos = (float(row[ix["x"]]), float(row[ix["y"]]), float(row[ix["z"]]))
        if all(f"f_dc_{c}" in ix for c in range(3)):
            rgb = tuple(clamp01(0.5 + SH0 * float(row[ix[f"f_dc_{c}"]])) for c in range(3))
        else:
            rgb = (0.64, 0.70, 0.82)
        lid = int(row[ix["vulkax_id_local"]]) if "vulkax_id_local" in ix else n
        out.append({"id": lid, "p": pos, "rgb": rgb})
    return sorted(out, key=lambda v: v["id"])


def read_particles_csv(path: Path):
    out = []
    with path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        need = {"particle_id", "rest_x", "rest_y", "rest_z"}
        if not reader.fieldnames or not need.issubset(set(reader.fieldnames)):
            raise ValueError(f"particle CSV missing required columns: {path}")
        for row in reader:
            out.append({
                "id": int(row["particle_id"]),
                "p": (float(row["rest_x"]), float(row["rest_y"]), float(row["rest_z"])),
                "rgb": (0.44, 0.58, 0.86),
            })
    if len(out) < 4:
        raise ValueError(f"particle CSV needs at least 4 particles: {path}")
    return out


def read_rewrite_region(run_dir: Path):
    path = run_dir / "influence" / "selected_rewrite_region.csv"
    if not path.exists():
        return set()
    ids = set()
    with path.open("r", encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            if row.get("particle_id"):
                ids.add(int(row["particle_id"]))
    return ids


def read_transaction(run_dir: Path):
    path = run_dir / "rewrite" / "transaction_summary.csv"
    if not path.exists():
        return {}
    with path.open("r", encoding="utf-8", newline="") as f:
        rows = list(csv.DictReader(f))
    return rows[0] if rows else {}


def displacement(before, after):
    if len(before) != len(after):
        return float("inf")
    by_id = {v["id"]: v for v in after}
    maximum = 0.0
    for a in before:
        b = by_id.get(a["id"])
        if b is None:
            return float("inf")
        maximum = max(maximum, norm(vsub(b["p"], a["p"])))
    return maximum


def gaussian_displacements(before, after):
    bmap = {v["id"]: v for v in after}
    out = []
    for v in before:
        w = bmap.get(v["id"])
        if w is not None:
            out.append((v["p"], vsub(w["p"], v["p"])))
    return out


def transfer_displacement(p, samples):
    if not samples:
        return (0.0, 0.0, 0.0)
    weighted = (0.0, 0.0, 0.0)
    total = 0.0
    for source, delta in samples:
        d = norm(vsub(p, source))
        if d < 1.0e-10:
            return delta
        w = 1.0 / (d * d + 1.0e-8)
        weighted = vadd(weighted, vmul(delta, w))
        total += w
    return vmul(weighted, 1.0 / max(total, EPS))


def unique_axis(values):
    vals = sorted(values)
    out = []
    for v in vals:
        if not out or abs(v - out[-1]) > 1.0e-9:
            out.append(v)
    return out


def nearest_index(axis, value):
    return min(range(len(axis)), key=lambda i: abs(axis[i] - value))


def regular_grid_shell(vertices):
    xs = unique_axis(v["p"][0] for v in vertices)
    ys = unique_axis(v["p"][1] for v in vertices)
    zs = unique_axis(v["p"][2] for v in vertices)
    if len(xs) * len(ys) * len(zs) != len(vertices):
        return None
    lookup = {}
    for i, v in enumerate(vertices):
        key = (
            nearest_index(xs, v["p"][0]),
            nearest_index(ys, v["p"][1]),
            nearest_index(zs, v["p"][2]),
        )
        if key in lookup:
            return None
        lookup[key] = i
    if len(lookup) != len(vertices):
        return None

    faces = []

    def add_quad(a, b, c, d, outward):
        for tri in ((a, b, c), (a, c, d)):
            p0, p1, p2 = (vertices[k]["p"] for k in tri)
            n = cross(vsub(p1, p0), vsub(p2, p0))
            if dot(n, outward) < 0.0:
                faces.append((tri[0], tri[2], tri[1]))
            else:
                faces.append(tri)

    nx, ny, nz = len(xs), len(ys), len(zs)
    for iy in range(ny - 1):
        for iz in range(nz - 1):
            add_quad(lookup[(0, iy, iz)], lookup[(0, iy, iz + 1)], lookup[(0, iy + 1, iz + 1)], lookup[(0, iy + 1, iz)], (-1, 0, 0))
            add_quad(lookup[(nx - 1, iy, iz)], lookup[(nx - 1, iy + 1, iz)], lookup[(nx - 1, iy + 1, iz + 1)], lookup[(nx - 1, iy, iz + 1)], (1, 0, 0))
    for ix in range(nx - 1):
        for iz in range(nz - 1):
            add_quad(lookup[(ix, 0, iz)], lookup[(ix + 1, 0, iz)], lookup[(ix + 1, 0, iz + 1)], lookup[(ix, 0, iz + 1)], (0, -1, 0))
            add_quad(lookup[(ix, ny - 1, iz)], lookup[(ix, ny - 1, iz + 1)], lookup[(ix + 1, ny - 1, iz + 1)], lookup[(ix + 1, ny - 1, iz)], (0, 1, 0))
    for ix in range(nx - 1):
        for iy in range(ny - 1):
            add_quad(lookup[(ix, iy, 0)], lookup[(ix, iy + 1, 0)], lookup[(ix + 1, iy + 1, 0)], lookup[(ix + 1, iy, 0)], (0, 0, -1))
            add_quad(lookup[(ix, iy, nz - 1)], lookup[(ix + 1, iy, nz - 1)], lookup[(ix + 1, iy + 1, nz - 1)], lookup[(ix, iy + 1, nz - 1)], (0, 0, 1))
    return faces, f"physical_lattice_{nx}x{ny}x{nz}"


def square_lattice_faces(vertices):
    n = len(vertices)
    s = int(round(math.sqrt(n)))
    if s < 2 or s * s != n:
        return None
    faces = []
    for y in range(s - 1):
        for x in range(s - 1):
            a = y * s + x
            b = a + 1
            d = (y + 1) * s + x
            c = d + 1
            faces.extend(((a, b, c), (a, c, d)))
    return faces, f"appearance_lattice_{s}x{s}"


def convex_hull_faces(vertices):
    n = len(vertices)
    if n < 3:
        return []
    if n == 3:
        return [(0, 1, 2)]
    pts = [v["p"] for v in vertices]
    centroid = tuple(sum(p[k] for p in pts) / n for k in range(3))
    scale = max((norm(vsub(a, b)) for a in pts for b in pts), default=1.0)
    eps = max(1.0e-10, scale * 1.0e-8)
    faces = set()
    for i in range(n - 2):
        for j in range(i + 1, n - 1):
            for k in range(j + 1, n):
                normal = cross(vsub(pts[j], pts[i]), vsub(pts[k], pts[i]))
                if norm(normal) <= eps:
                    continue
                pos = neg = False
                for m in range(n):
                    if m in (i, j, k):
                        continue
                    side = dot(normal, vsub(pts[m], pts[i]))
                    if side > eps: pos = True
                    elif side < -eps: neg = True
                    if pos and neg:
                        break
                if pos and neg:
                    continue
                tri = (i, j, k)
                if dot(normal, vsub(centroid, pts[i])) > 0.0:
                    tri = (i, k, j)
                faces.add(tri)
    if faces:
        return sorted(faces)

    ranges = [max(p[a] for p in pts) - min(p[a] for p in pts) for a in range(3)]
    drop = min(range(3), key=lambda a: ranges[a])
    axes = [a for a in range(3) if a != drop]
    ordered = sorted(
        range(n),
        key=lambda i: math.atan2(pts[i][axes[1]] - centroid[axes[1]], pts[i][axes[0]] - centroid[axes[0]]),
    )
    return [(ordered[0], ordered[i], ordered[i + 1]) for i in range(1, len(ordered) - 1)]


def appearance_mesh(vertices):
    sq = square_lattice_faces(vertices)
    if sq and len(vertices) >= 9:
        return sq
    return convex_hull_faces(vertices), "appearance_convex_hull"


def rotate_point(p, yaw, pitch):
    y = math.radians(yaw)
    x = math.radians(pitch)
    cy, sy = math.cos(y), math.sin(y)
    cx, sx = math.cos(x), math.sin(x)
    q = (cy * p[0] + sy * p[2], p[1], -sy * p[0] + cy * p[2])
    return (q[0], cx * q[1] - sx * q[2], sx * q[1] + cx * q[2])


def prepare(vertices, yaw, pitch=-10.0):
    pts = [v["p"] for v in vertices]
    mn = tuple(min(p[i] for p in pts) for i in range(3))
    mx = tuple(max(p[i] for p in pts) for i in range(3))
    c = tuple((mn[i] + mx[i]) * 0.5 for i in range(3))
    out = []
    for v in vertices:
        w = dict(v)
        w["p"] = rotate_point(vsub(v["p"], c), yaw, pitch)
        out.append(w)
    return out


def vertex_normals(vertices, faces):
    accum = [(0.0, 0.0, 0.0) for _ in vertices]
    for a, b, c in faces:
        p0, p1, p2 = vertices[a]["p"], vertices[b]["p"], vertices[c]["p"]
        n = cross(vsub(p1, p0), vsub(p2, p0))
        area = norm(n)
        if area < EPS:
            continue
        n = unit(n)
        accum[a] = vadd(accum[a], vmul(n, area))
        accum[b] = vadd(accum[b], vmul(n, area))
        accum[c] = vadd(accum[c], vmul(n, area))
    return [unit(n) for n in accum]


class Image:
    def __init__(self, width, height):
        self.w = width
        self.h = height
        self.rgb = bytearray(width * height * 3)
        self.z = [1.0e99] * (width * height)

    def background(self):
        for y in range(self.h):
            v = y / max(1, self.h - 1)
            for x in range(self.w):
                u = x / max(1, self.w - 1)
                d = math.hypot(u - 0.52, v - 0.42)
                glow = max(0.0, 1.0 - d / 0.78)
                c = (
                    0.018 + 0.030 * glow + 0.008 * (1 - v),
                    0.028 + 0.042 * glow + 0.010 * (1 - v),
                    0.060 + 0.080 * glow + 0.015 * (1 - v),
                )
                i = (y * self.w + x) * 3
                self.rgb[i:i + 3] = bytes(int(clamp01(q) * 255) for q in c)

    def put(self, x, y, z, color):
        if x < 0 or y < 0 or x >= self.w or y >= self.h:
            return
        k = y * self.w + x
        if z >= self.z[k]:
            return
        self.z[k] = z
        i = k * 3
        self.rgb[i:i + 3] = bytes(int(clamp01(q) * 255) for q in color)

    def blend(self, x, y, color, alpha):
        if x < 0 or y < 0 or x >= self.w or y >= self.h:
            return
        i = (y * self.w + x) * 3
        for j in range(3):
            old = self.rgb[i + j] / 255.0
            self.rgb[i + j] = int(clamp01(old * (1.0 - alpha) + color[j] * alpha) * 255)


def camera_basis(cam, target=(0.0, 0.0, 0.0)):
    f = unit(vsub(target, cam))
    r = unit(cross(f, (0.0, 1.0, 0.0)))
    u = unit(cross(r, f))
    return r, u, f


def project(p, cam, width, height, fov=38.0):
    r, u, f = camera_basis(cam)
    q = vsub(p, cam)
    z = dot(q, f)
    if z <= 1.0e-6:
        return None
    k = 1.0 / math.tan(math.radians(fov) / 2.0)
    aspect = width / height
    sx = (dot(q, r) / z) * k / aspect
    sy = (dot(q, u) / z) * k
    return ((sx * 0.5 + 0.5) * (width - 1), (0.5 - sy * 0.5) * (height - 1), z)


def shade(base, normal, point, highlight=0.0):
    key = unit((-0.58, 0.68, 0.94))
    fill = unit((0.72, 0.18, 0.60))
    rim = unit((0.15, -0.88, 0.46))
    view = (0.0, 0.0, 1.0)
    halfv = unit(vadd(key, view))
    kd = max(0.0, dot(normal, key))
    fd = max(0.0, dot(normal, fill))
    rd = max(0.0, dot(normal, rim))
    spec = max(0.0, dot(normal, halfv)) ** 38
    cool = (0.33, 0.48, 0.82)
    warm = (1.00, 0.48, 0.16)
    color = mix(mix(base, cool, 0.42), warm, clamp01(highlight) * 0.78)
    light = 0.30 + 0.72 * kd + 0.20 * fd + 0.14 * rd
    depth_tint = 1.0 + 0.05 * math.tanh(point[2] * 12.0)
    return tuple(clamp01(c * light * depth_tint + (0.12 + 0.05 * highlight) * spec) for c in color)


def edge(a, b, p):
    return (p[0] - a[0]) * (b[1] - a[1]) - (p[1] - a[1]) * (b[0] - a[0])


def raster_triangle(image, A, B, C):
    area = edge(A[:2], B[:2], C[:2])
    if abs(area) < 1.0e-9:
        return
    x0 = max(0, int(math.floor(min(A[0], B[0], C[0]))))
    x1 = min(image.w - 1, int(math.ceil(max(A[0], B[0], C[0]))))
    y0 = max(0, int(math.floor(min(A[1], B[1], C[1]))))
    y1 = min(image.h - 1, int(math.ceil(max(A[1], B[1], C[1]))))
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            P = (x + 0.5, y + 0.5)
            wa = edge(B[:2], C[:2], P) / area
            wb = edge(C[:2], A[:2], P) / area
            wc = 1.0 - wa - wb
            if min(wa, wb, wc) < -1.0e-6:
                continue
            z = wa * A[2] + wb * B[2] + wc * C[2]
            p = tuple(wa * A[3][i] + wb * B[3][i] + wc * C[3][i] for i in range(3))
            n = unit(tuple(wa * A[4][i] + wb * B[4][i] + wc * C[4][i] for i in range(3)))
            rgb = tuple(wa * A[5][i] + wb * B[5][i] + wc * C[5][i] for i in range(3))
            hi = wa * A[6] + wb * B[6] + wc * C[6]
            image.put(x, y, z, shade(rgb, n, p, hi))


def draw_line(image, a, b, color, alpha=0.30):
    dx, dy = b[0] - a[0], b[1] - a[1]
    steps = max(1, int(max(abs(dx), abs(dy))))
    for s in range(steps + 1):
        t = s / steps
        x = int(round(a[0] + dx * t))
        y = int(round(a[1] + dy * t))
        image.blend(x, y, color, alpha)


def soft_shadow(image, cx, cy, rx, ry):
    if rx <= 0 or ry <= 0:
        return
    for y in range(max(0, int(cy - ry)), min(image.h, int(cy + ry) + 1)):
        for x in range(max(0, int(cx - rx)), min(image.w, int(cx + rx) + 1)):
            q = ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2
            if q < 1.0:
                image.blend(x, y, (0.0, 0.0, 0.0), 0.25 * (1.0 - q) ** 2)


def marker(image, cx, cy, radius, color):
    rr = max(2, int(radius))
    for y in range(int(cy - rr), int(cy + rr) + 1):
        for x in range(int(cx - rr), int(cx + rr) + 1):
            d = math.hypot(x - cx, y - cy)
            if d <= rr:
                image.blend(x, y, color, 0.35 + 0.65 * clamp01((rr - d) / max(1.0, rr * 0.45)))


def render_mesh(vertices, faces, width, height, yaw, highlighted_ids=frozenset()):
    verts = prepare(vertices, yaw)
    normals = vertex_normals(verts, faces)
    pts = [v["p"] for v in verts]
    extent = [max(p[i] for p in pts) - min(p[i] for p in pts) for i in range(3)]
    span = max(max(extent), 1.0e-4)
    cam = (0.0, 0.035 * span, 1.85 * span)
    image = Image(width, height)
    image.background()

    projected = []
    for i, v in enumerate(verts):
        q = project(v["p"], cam, width, height)
        if q is None:
            raise ValueError("surface proxy projected behind camera")
        hi = 1.0 if v["id"] in highlighted_ids else 0.0
        projected.append((q[0], q[1], q[2], v["p"], normals[i], v["rgb"], hi))

    xs = [p[0] for p in projected]
    ys = [p[1] for p in projected]
    object_w = max(xs) - min(xs)
    object_h = max(ys) - min(ys)
    soft_shadow(image, (min(xs) + max(xs)) * 0.5 + object_w * 0.025, max(ys) + object_h * 0.08, object_w * 0.42, max(8.0, object_h * 0.12))

    for a, b, c in faces:
        raster_triangle(image, projected[a], projected[b], projected[c])

    edges = set()
    for a, b, c in faces:
        for u, v in ((a, b), (b, c), (c, a)):
            edges.add(tuple(sorted((u, v))))
    edge_color = (0.63, 0.76, 1.00)
    for a, b in edges:
        draw_line(image, projected[a], projected[b], edge_color, 0.12)

    for i, v in enumerate(verts):
        if v["id"] in highlighted_ids:
            marker(image, projected[i][0], projected[i][1], min(width, height) * 0.004, (1.0, 0.62, 0.22))
    return image


def png_chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)


def write_png(image, path: Path):
    raw = bytearray()
    stride = image.w * 3
    for y in range(image.h):
        raw += b"\0" + image.rgb[y * stride:(y + 1) * stride]
    data = (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", struct.pack(">IIBBBBB", image.w, image.h, 8, 2, 0, 0, 0))
        + png_chunk(b"IDAT", zlib.compress(bytes(raw), 6))
        + png_chunk(b"IEND", b"")
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def proxy_from_inputs(run_dir: Path, particles_csv: Path | None):
    before_g = read_ply(run_dir / "appearance" / "before.ply")
    after_g = read_ply(run_dir / "appearance" / "rewritten.ply")
    max_disp = displacement(before_g, after_g)
    source = "appearance"

    if particles_csv is not None:
        physical = read_particles_csv(particles_csv)
        shell = regular_grid_shell(physical)
        if shell is not None:
            faces, topology = shell
            before = [dict(v) for v in physical]
            after = [dict(v) for v in physical]
            samples = gaussian_displacements(before_g, after_g)
            if math.isfinite(max_disp) and max_disp > 1.0e-12:
                for v in after:
                    v["p"] = vadd(v["p"], transfer_displacement(v["p"], samples))
            source = "physical"
            return before, after, faces, faces, topology, source, max_disp

    before = [dict(v) for v in before_g]
    after = [dict(v) for v in after_g]
    faces_a, topology_a = appearance_mesh(before)
    faces_b, topology_b = appearance_mesh(after)
    topology = topology_a if topology_a == topology_b else f"{topology_a}->{topology_b}"
    return before, after, faces_a, faces_b, topology, source, max_disp


def gallery(path: Path, frames, max_disp, topology, source, rewrite_ids, tx):
    geometry_changed = math.isfinite(max_disp) and max_disp > 1.0e-12
    status = "Geometric rewrite visible" if geometry_changed else "Verified material rewrite · geometry unchanged"
    if not tx:
        status = "Surface proxy generated" if geometry_changed else "No committed geometric change detected"
    y0 = tx.get("young_modulus_before", "")
    y1 = tx.get("young_modulus_after", "")
    rewrite_status = tx.get("status", "")
    rollback = tx.get("rollback_performed", "")
    material = ""
    if y0 and y1:
        material = f"Young's modulus {y0} → {y1}. "
    if rewrite_status:
        material += f"Transaction status: {rewrite_status}. "
    if rollback:
        material += f"Rollback: {'yes' if rollback not in ('0','false','False','no') else 'no'}."
    if not material:
        material = "Rewrite metadata was not available to the presentation layer."
    displacement_note = (
        f"Maximum authoritative Gaussian-center displacement: {max_disp:.6g} world units."
        if math.isfinite(max_disp) else "Gaussian correspondence changed between states."
    )
    if not geometry_changed:
        displacement_note = "Authoritative before/rewritten Gaussian centers are unchanged; the after view highlights the verified rewrite region instead of inventing deformation."

    frames_json = json.dumps(frames)
    path.write_text(f'''<!doctype html>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Vulkax Surface Proxy</title>
<style>
*{{box-sizing:border-box}}body{{margin:0;background:radial-gradient(circle at 48% -10%,#1b3264,#070b16 56%);color:#eef5ff;font:15px/1.5 system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}}main{{max-width:1360px;margin:auto;padding:54px 24px 84px}}.eyebrow{{color:#8db4ff;text-transform:uppercase;letter-spacing:.16em;font-size:12px}}h1{{font-size:clamp(42px,6.5vw,78px);line-height:.96;letter-spacing:-.05em;margin:10px 0 20px}}p{{color:#a7b6cd;max-width:920px}}.pill{{display:inline-flex;gap:9px;align-items:center;border:1px solid #38517f;background:#0d1830cc;border-radius:999px;padding:8px 13px;margin:16px 0 32px}}.dot{{width:8px;height:8px;border-radius:50%;background:#79a7ff;box-shadow:0 0 18px #79a7ff}}.grid{{display:grid;grid-template-columns:1fr 1fr;gap:18px}}.card{{background:linear-gradient(180deg,#111b31,#0c1425);border:1px solid #263957;border-radius:22px;padding:14px;box-shadow:0 24px 70px #0006}}img{{display:block;width:100%;border-radius:14px;background:#050813}}b{{display:block;font-size:18px;margin:13px 4px 3px}}small,span{{color:#9fb0ca}}.meta{{display:grid;grid-template-columns:repeat(3,1fr);gap:12px;margin:0 0 20px}}.meta div{{padding:13px 15px;border:1px solid #233653;background:#0b1425;border-radius:15px}}.meta strong{{display:block;color:#e9f2ff;font-size:14px}}.turn{{margin-top:18px}}input{{width:100%;accent-color:#79a7ff}}.warn{{margin-top:18px;border-left:3px solid #ff9c45;padding:10px 14px;background:#19140f;color:#d9c3ad;border-radius:0 12px 12px 0}}@media(max-width:820px){{.grid,.meta{{grid-template-columns:1fr}}}}</style>
<main>
<div class="eyebrow">Vulkax 1.0.x · verified surface presentation</div>
<h1>Readable physical form.<br>Raw evidence preserved.</h1>
<p>This view is derived for presentation only. The Gaussian PLYs, scientific PPMs, rewrite evidence and certificate remain authoritative and unchanged.</p>
<div class="pill"><i class="dot"></i>{status}</div>
<div class="meta"><div><span>Proxy source</span><strong>{source}</strong></div><div><span>Topology</span><strong>{topology}</strong></div><div><span>Rewrite particles</span><strong>{len(rewrite_ids)}</strong></div></div>
<div class="grid">
  <div class="card"><img src="surface_before.png"><b>Before · physical surface</b><small>Continuous triangle surface, estimated normals, depth-tested studio shading.</small></div>
  <div class="card"><img src="surface_after.png"><b>After · verified rewrite view</b><small>{displacement_note}</small></div>
</div>
<div class="card turn"><b>Turntable</b><img id="turn" src="{frames[0] if frames else 'surface_after.png'}"><input id="slider" type="range" min="0" max="{max(0,len(frames)-1)}" value="0"><span id="label"></span></div>
<div class="warn">{material} Orange accents identify physical particles in the selected rewrite region; they do not claim geometric motion.</div>
<script>const f={frames_json},im=document.getElementById('turn'),s=document.getElementById('slider'),l=document.getElementById('label');function u(){{const i=+s.value||0;if(f.length)im.src=f[i];l.textContent=f.length?`frame ${{i+1}} / ${{f.length}}`:''}}s.oninput=u;u()</script>
</main>''', encoding="utf-8")


def run(run_dir: Path, width: int, height: int, turntable: int, particles_csv: Path | None):
    if width < 64 or height < 64:
        raise ValueError("surface proxy dimensions must be at least 64x64")
    before, after, faces_before, faces_after, topology, source, max_disp = proxy_from_inputs(run_dir, particles_csv)
    if not faces_before or not faces_after:
        raise ValueError("surface proxy could not infer a renderable surface")
    out = run_dir / "render" / "surface_proxy"
    out.mkdir(parents=True, exist_ok=True)
    rewrite_ids = read_rewrite_region(run_dir)
    tx = read_transaction(run_dir)

    write_png(render_mesh(before, faces_before, width, height, -26.0), out / "surface_before.png")
    write_png(render_mesh(after, faces_after, width, height, 20.0, rewrite_ids), out / "surface_after.png")

    frames = []
    if turntable > 0:
        td = out / "turntable"
        td.mkdir(exist_ok=True)
        for i in range(turntable):
            name = f"frame_{i:03d}.png"
            yaw = -58.0 + 116.0 * i / max(1, turntable - 1)
            write_png(render_mesh(after, faces_after, width, height, yaw, rewrite_ids), td / name)
            frames.append("turntable/" + name)

    gallery(out / "surface_gallery.html", frames, max_disp, topology, source, rewrite_ids, tx)
    manifest = {
        "schema": 2,
        "presentation_only": True,
        "source": source,
        "topology": topology,
        "width": width,
        "height": height,
        "turntable_frames": turntable,
        "gaussian_max_displacement": max_disp,
        "rewrite_particle_count": len(rewrite_ids),
        "raw_evidence_modified": False,
    }
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("surface_proxy_status: completed")
    print(f"surface_proxy_source: {source}")
    print(f"surface_proxy_topology: {topology}")
    print(f"surface_proxy_gallery: {out / 'surface_gallery.html'}")
    print(f"surface_proxy_max_displacement: {max_disp:.12g}")
    print(f"surface_proxy_rewrite_particles: {len(rewrite_ids)}")


def write_test_ply(path: Path, points):
    with path.open("w", encoding="utf-8") as f:
        f.write("ply\nformat ascii 1.0\n")
        f.write(f"element vertex {len(points)}\n")
        f.write("property double x\nproperty double y\nproperty double z\n")
        f.write("property double f_dc_0\nproperty double f_dc_1\nproperty double f_dc_2\n")
        f.write("property uint vulkax_id_local\nend_header\n")
        for i, p in enumerate(points, 1):
            f.write(f"{p[0]} {p[1]} {p[2]} 0 0 0 {i}\n")


def self_test():
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        (root / "appearance").mkdir(parents=True)
        points = [(-.10,-.05,-.02),(.11,-.04,.03),(-.03,.10,-.06),(.04,.06,.09),(0,0,0)]
        write_test_ply(root / "appearance" / "before.ply", points)
        moved = list(points)
        moved[-1] = (0.002, 0.0, 0.0)
        write_test_ply(root / "appearance" / "rewritten.ply", moved)
        run(root, 240, 160, 2, None)
        assert (root / "render" / "surface_proxy" / "surface_gallery.html").exists()

        particles = root / "particles.csv"
        with particles.open("w", encoding="utf-8", newline="") as f:
            w = csv.writer(f)
            w.writerow(["particle_id","rest_x","rest_y","rest_z","mass","rest_volume"])
            pid = 1
            for iz in range(4):
                for iy in range(4):
                    for ix in range(4):
                        w.writerow([pid,(ix-1.5)*.12,(iy-1.5)*.12,(iz-1.5)*.12,1,.001])
                        pid += 1
        run(root, 240, 160, 2, particles)
        manifest = json.loads((root / "render" / "surface_proxy" / "manifest.json").read_text())
        assert manifest["source"] == "physical"
        assert manifest["topology"] == "physical_lattice_4x4x4"
    print("surface_proxy_self_test: passed")


def main():
    parser = argparse.ArgumentParser(description="Render a stable presentation-only surface proxy for a Vulkax captured-world run.")
    parser.add_argument("run_dir", nargs="?", type=Path)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--turntable", type=int, default=12)
    parser.add_argument("--particles-csv", type=Path, help="Optional captured physical particle CSV. Preferred for sparse appearance clouds.")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.run_dir is None:
        parser.error("run_dir is required unless --self-test is used")
    run(args.run_dir, args.width, args.height, args.turntable, args.particles_csv)


if __name__ == "__main__":
    main()

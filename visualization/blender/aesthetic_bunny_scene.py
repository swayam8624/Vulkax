#!/usr/bin/env python3
"""SIGGRAPH-style VULKAX hero scene using the Stanford Bunny as a visualization carrier.

Scientific boundary:
- the frozen benchmark/result is NOT changed;
- the bunny is NOT claimed to be the benchmark object;
- bunny surface deformation is computed by trilinearly interpolating the raw
  4x4x4 VULKAX MPM displacement field exported by the frozen hero replay;
- display motion magnification is explicit in-scene.

The scene is intended for paper teaser / supplemental video presentation.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import sys
from collections import defaultdict
from pathlib import Path

import bpy
from bpy_extras import anim_utils
from mathutils import Vector

BG = (0.004, 0.010, 0.025, 1.0)
CYAN = (0.03, 0.66, 1.0, 1.0)
CYAN_SOFT = (0.22, 0.82, 1.0, 1.0)
ORANGE = (1.0, 0.23, 0.035, 1.0)
AMBER = (1.0, 0.56, 0.08, 1.0)
WHITE = (0.82, 0.88, 0.96, 1.0)
PEARL = (0.72, 0.78, 0.86, 1.0)
DARK_METAL = (0.014, 0.027, 0.055, 1.0)
GREEN = (0.05, 0.92, 0.55, 1.0)
RED = (1.0, 0.12, 0.20, 1.0)

STATIONS = (-5.5, 0.0, 5.5)
GRID_N = 4


def parse_args():
    argv = sys.argv
    args = argv[argv.index("--") + 1:] if "--" in argv else []
    p = argparse.ArgumentParser()
    p.add_argument("--trajectory-dir", type=Path, required=True)
    p.add_argument("--bunny", type=Path, required=True)
    p.add_argument("--direction", choices=["px", "nx", "py", "pz"], default="px")
    p.add_argument("--motion-scale", type=float, default=400.0)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--render-still", action="store_true")
    p.add_argument("--render-animation", action="store_true")
    p.add_argument("--engine", choices=["eevee", "cycles"], default="eevee")
    p.add_argument("--cycles-samples", type=int, default=96)
    return p.parse_args(args)


def set_eevee(scene):
    # Blender 5.2 exposes BLENDER_EEVEE; older 4.x builds may expose
    # BLENDER_EEVEE_NEXT. Try the current stable enum first.
    for engine in ("BLENDER_EEVEE", "BLENDER_EEVEE_NEXT"):
        try:
            scene.render.engine = engine
            return
        except Exception:
            pass
    raise RuntimeError("No supported Eevee render engine enum found")


def enable_cycles_metal(scene, samples):
    scene.render.engine = "CYCLES"
    scene.cycles.samples = max(16, int(samples))
    scene.cycles.use_denoising = True
    try:
        prefs = bpy.context.preferences.addons["cycles"].preferences
        prefs.compute_device_type = "METAL"
        prefs.get_devices()
        for device in prefs.devices:
            device.use = True
        scene.cycles.device = "GPU"
        print("Cycles: requested Metal GPU rendering")
    except Exception as exc:
        print(f"Cycles Metal setup unavailable; using Blender default device: {exc}")


def configure_scene(engine, samples):
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    scene = bpy.context.scene
    if engine == "cycles":
        enable_cycles_metal(scene, samples)
    else:
        set_eevee(scene)

    scene.render.resolution_x = 2560
    scene.render.resolution_y = 1440
    scene.render.resolution_percentage = 100
    scene.render.fps = 30
    scene.world.color = BG[:3]
    scene.frame_start = 1
    scene.frame_end = 181
    scene.render.image_settings.media_type = "IMAGE"
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGB"
    scene.render.image_settings.color_depth = "8"
    try:
        scene.view_settings.look = "AgX - Medium High Contrast"
    except Exception:
        try:
            scene.view_settings.look = "Medium High Contrast"
        except Exception:
            pass
    return scene


def material(name, color, metallic=0.0, roughness=0.35, emission=0.0, alpha=1.0):
    m = bpy.data.materials.new(name)
    m.diffuse_color = (color[0], color[1], color[2], alpha)
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (color[0], color[1], color[2], alpha)
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = roughness
    if "Coat Weight" in bsdf.inputs:
        bsdf.inputs["Coat Weight"].default_value = 0.35
        bsdf.inputs["Coat Roughness"].default_value = 0.16
    if emission > 0.0:
        if "Emission Color" in bsdf.inputs:
            bsdf.inputs["Emission Color"].default_value = (color[0], color[1], color[2], 1.0)
        elif "Emission" in bsdf.inputs:
            bsdf.inputs["Emission"].default_value = (color[0], color[1], color[2], 1.0)
        if "Emission Strength" in bsdf.inputs:
            bsdf.inputs["Emission Strength"].default_value = emission
    if alpha < 1.0:
        bsdf.inputs["Alpha"].default_value = alpha
        try:
            m.surface_render_method = "DITHERED"
        except Exception:
            pass
    return m


def add_box(name, location, scale, mat, bevel=0.08):
    bpy.ops.mesh.primitive_cube_add(location=location)
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    mod = obj.modifiers.new("micro_bevel", "BEVEL")
    mod.width = bevel
    mod.segments = 4
    obj.data.materials.append(mat)
    return obj


def add_cylinder(name, location, radius, depth, mat, vertices=96):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth, location=location)
    obj = bpy.context.object
    obj.name = name
    obj.data.materials.append(mat)
    bevel = obj.modifiers.new("edge_bevel", "BEVEL")
    bevel.width = min(radius * 0.06, 0.06)
    bevel.segments = 3
    return obj


def add_torus(name, location, major, minor, mat, rotation=(0.0, 0.0, 0.0)):
    bpy.ops.mesh.primitive_torus_add(
        major_radius=major,
        minor_radius=minor,
        major_segments=128,
        minor_segments=16,
        location=location,
        rotation=rotation,
    )
    obj = bpy.context.object
    obj.name = name
    obj.data.materials.append(mat)
    return obj


def cylinder_between(name, a, b, radius, mat, vertices=48):
    a = Vector(a)
    b = Vector(b)
    d = b - a
    if d.length <= 1e-12:
        return None
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=vertices,
        radius=radius,
        depth=d.length,
        location=(a + b) * 0.5,
    )
    obj = bpy.context.object
    obj.name = name
    obj.rotation_mode = "QUATERNION"
    obj.rotation_quaternion = d.to_track_quat("Z", "Y")
    obj.data.materials.append(mat)
    return obj


def add_arrow(name, start, end, radius, mat):
    a = Vector(start)
    b = Vector(end)
    d = b - a
    unit = d.normalized()
    cylinder_between(name + "_shaft", a, b - unit * 0.30, radius, mat)
    bpy.ops.mesh.primitive_cone_add(
        vertices=48,
        radius1=radius * 3.5,
        radius2=0.0,
        depth=0.42,
        location=b - unit * 0.19,
    )
    cone = bpy.context.object
    cone.name = name + "_head"
    cone.rotation_mode = "QUATERNION"
    cone.rotation_quaternion = d.to_track_quat("Z", "Y")
    cone.data.materials.append(mat)


def add_text(value, location, size, mat, align="CENTER", extrude=0.012):
    bpy.ops.object.text_add(location=location, rotation=(math.radians(90), 0.0, 0.0))
    obj = bpy.context.object
    obj.data.body = value
    obj.data.align_x = align
    obj.data.align_y = "CENTER"
    obj.data.size = size
    obj.data.extrude = extrude
    obj.data.bevel_depth = 0.003
    obj.data.materials.append(mat)
    return obj


def load_states(csv_path, direction):
    groups = defaultdict(dict)
    max_frame = 0
    with csv_path.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            if row["direction"] != direction:
                continue
            frame = int(row["frame"])
            pid = int(row["particle_id"])
            groups[(row["model"], frame)][pid] = {
                "rest": Vector((float(row["rest_x"]), float(row["rest_y"]), float(row["rest_z"]))),
                "pos": Vector((float(row["x"]), float(row["y"]), float(row["z"]))),
                "top": row["is_top"] == "1",
                "bottom": row["is_bottom"] == "1",
            }
            max_frame = max(max_frame, frame)
    return groups, max_frame


def import_ply(path):
    before = set(bpy.data.objects)
    try:
        bpy.ops.wm.ply_import(filepath=str(path))
    except Exception:
        bpy.ops.import_mesh.ply(filepath=str(path))
    created = [obj for obj in bpy.data.objects if obj not in before and obj.type == "MESH"]
    if not created:
        raise RuntimeError(f"PLY import created no mesh: {path}")
    return created[-1]


def orient_and_normalize_vertices(mesh):
    coords = [v.co.copy() for v in mesh.vertices]
    min_x = min(v.x for v in coords); max_x = max(v.x for v in coords)
    min_y = min(v.y for v in coords); max_y = max(v.y for v in coords)
    min_z = min(v.z for v in coords); max_z = max(v.z for v in coords)
    dx = max(max_x - min_x, 1e-12)
    dy = max(max_y - min_y, 1e-12)
    dz = max(max_z - min_z, 1e-12)

    params = []
    display = []
    for p in coords:
        u = (p.x - min_x) / dx
        # Stanford bunny is conventionally Y-up; map its Z depth to Blender Y
        # and its Y height to Blender Z.
        v = (p.z - min_z) / dz
        w = (p.y - min_y) / dy
        params.append((u, v, w))
        display.append(Vector(((u - 0.5) * 2.25, (v - 0.5) * 1.65, w * 2.75)))
    return params, display


def particle_id(ix, iy, iz):
    # Matches body() in frozen_hero_trajectory_probe.cpp: x advances fastest,
    # then y, then z, IDs are one-based.
    return iz * 16 + iy * 4 + ix + 1


def field_displacement(group, uvw):
    u, v, w = uvw
    # Display axes: solver X -> horizontal, solver Z -> depth,
    # solver Y -> vertical. Param w (bunny height) therefore samples solver Y.
    gx = min(max(u, 0.0), 1.0) * 3.0
    gy = min(max(w, 0.0), 1.0) * 3.0
    gz = min(max(v, 0.0), 1.0) * 3.0
    ix = min(int(math.floor(gx)), 2); tx = gx - ix
    iy = min(int(math.floor(gy)), 2); ty = gy - iy
    iz = min(int(math.floor(gz)), 2); tz = gz - iz

    disp = Vector((0.0, 0.0, 0.0))
    for dz in (0, 1):
        wz = (1.0 - tz) if dz == 0 else tz
        for dy in (0, 1):
            wy = (1.0 - ty) if dy == 0 else ty
            for dx in (0, 1):
                wx = (1.0 - tx) if dx == 0 else tx
                state = group[particle_id(ix + dx, iy + dy, iz + dz)]
                d = state["pos"] - state["rest"]
                # solver (x,y,z) -> Blender display (x,z,y)
                mapped = Vector((d.x, d.z, d.y))
                disp += mapped * (wx * wy * wz)
    return disp


def create_deformed_bunny(
    source_obj,
    name,
    params,
    rest_display,
    groups,
    model,
    last,
    offset_x,
    motion_scale,
    mat,
    animate=True,
    wireframe=False,
    ghost=False,
):
    mesh = source_obj.data.copy()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    obj.location.x = offset_x
    for poly in mesh.polygons:
        poly.use_smooth = True
    mesh.materials.append(mat)

    basis = obj.shape_key_add(name="Basis")
    for i, co in enumerate(rest_display):
        basis.data[i].co = co

    frame_stride = 3
    frames = range(last + 1) if animate else [last]
    keys = []
    for f in frames:
        key = obj.shape_key_add(name=f"solver_{f:03d}")
        group = groups[(model, f)]
        for i, uvw in enumerate(params):
            key.data[i].co = rest_display[i] + field_displacement(group, uvw) * motion_scale
        keys.append((f, key))

    if animate:
        for f, key in keys:
            t = 1 + f * frame_stride
            key.value = 0.0
            key.keyframe_insert(data_path="value", frame=max(1, t - frame_stride))
            key.value = 1.0
            key.keyframe_insert(data_path="value", frame=t)
            key.value = 0.0
            key.keyframe_insert(data_path="value", frame=min(1 + last * frame_stride, t + frame_stride))
        if obj.data.shape_keys and obj.data.shape_keys.animation_data:
            channelbag = anim_utils.animdata_get_channelbag_for_assigned_slot(
                obj.data.shape_keys.animation_data
            )
            if channelbag is not None:
                for fc in channelbag.fcurves:
                    for kp in fc.keyframe_points:
                        kp.interpolation = "LINEAR"
    else:
        keys[0][1].value = 1.0

    if wireframe:
        wf = obj.modifiers.new("scientific_wire", "WIREFRAME")
        wf.thickness = 0.012
        wf.use_replace = True
    else:
        subd = obj.modifiers.new("surface_polish", "SUBSURF")
        subd.subdivision_type = "CATMULL_CLARK"
        subd.levels = 1
        subd.render_levels = 1

    if ghost:
        obj.display_type = "WIRE"
    return obj


def add_solver_lattice(groups, model, frame, offset_x, motion_scale, point_mat, edge_mat, alpha=1.0):
    group = groups[(model, frame)]
    positions = {}
    for pid, state in group.items():
        d = state["pos"] - state["rest"]
        mapped_rest = Vector((state["rest"].x, state["rest"].z, state["rest"].y))
        mapped_disp = Vector((d.x, d.z, d.y))
        # Lattice scaled to roughly match bunny body volume and raised onto pedestal.
        p = Vector((mapped_rest.x * 5.2, mapped_rest.y * 4.1, mapped_rest.z * 6.5 + 1.38))
        p += mapped_disp * motion_scale
        p.x += offset_x
        positions[pid] = p
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=0.035, location=p)
        s = bpy.context.object
        s.data.materials.append(point_mat)

    for iz in range(4):
        for iy in range(4):
            for ix in range(4):
                a = positions[particle_id(ix, iy, iz)]
                if ix < 3:
                    cylinder_between("grid_x", a, positions[particle_id(ix + 1, iy, iz)], 0.006, edge_mat, 12)
                if iy < 3:
                    cylinder_between("grid_y", a, positions[particle_id(ix, iy + 1, iz)], 0.006, edge_mat, 12)
                if iz < 3:
                    cylinder_between("grid_z", a, positions[particle_id(ix, iy, iz + 1)], 0.006, edge_mat, 12)


def add_residual_vectors(groups, frame, offset_x, scale, truth_mat, repair_mat):
    truth = groups[("truth", frame)]
    repair = groups[("repair_pic", frame)]
    for pid in sorted(truth):
        tr = truth[pid]
        # Keep the X-ray legible: show the physically important top layer plus
        # a deterministic sparse interior sample instead of all 64 endpoints.
        if not (tr["top"] or pid % 6 == 0):
            continue
        rr = repair[pid]
        tr_rest = Vector((tr["rest"].x, tr["rest"].z, tr["rest"].y))
        tr_disp = Vector(((tr["pos"] - tr["rest"]).x, (tr["pos"] - tr["rest"]).z, (tr["pos"] - tr["rest"]).y))
        rr_disp = Vector(((rr["pos"] - rr["rest"]).x, (rr["pos"] - rr["rest"]).z, (rr["pos"] - rr["rest"]).y))
        base = Vector((tr_rest.x * 5.2 + offset_x, tr_rest.y * 4.1, tr_rest.z * 6.5 + 1.38))
        a = base + tr_disp * scale
        b = base + rr_disp * scale
        delta = b - a
        if delta.length <= 1e-6:
            continue
        cylinder_between(f"residual_{pid}", a, b, 0.009, repair_mat, 16)
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=0.025, location=a)
        bpy.context.object.data.materials.append(truth_mat)
        # A small arrowhead makes each residual read as a vector, not confetti.
        unit = delta.normalized()
        bpy.ops.mesh.primitive_cone_add(
            vertices=16,
            radius1=0.034,
            radius2=0.0,
            depth=0.085,
            location=b - unit * 0.03,
        )
        cone = bpy.context.object
        cone.rotation_mode = "QUATERNION"
        cone.rotation_quaternion = delta.to_track_quat("Z", "Y")
        cone.data.materials.append(repair_mat)


def add_pedestal(x, metal, glow):
    add_cylinder("pedestal_base", (x, 0, -0.30), 2.05, 0.34, metal)
    add_cylinder("pedestal_top", (x, 0, -0.08), 1.82, 0.16, metal)
    add_torus("pedestal_glow_outer", (x, 0, 0.02), 1.72, 0.035, glow)
    add_torus("pedestal_glow_inner", (x, 0, 0.04), 1.47, 0.012, glow)


def add_probe(center_x, direction, metal, glow):
    force = {
        "px": Vector((1, 0, 0)),
        "nx": Vector((-1, 0, 0)),
        "py": Vector((0, 0, 1)),
        "pz": Vector((0, 1, 0)),
    }[direction]
    # Contact the surface from outside instead of running a giant cylinder
    # through the subject. This remains aligned with the frozen force direction.
    center = Vector((center_x, -0.30, 1.72))
    tip = center - force * 0.92
    start = tip - force * 1.60
    cylinder_between("probe_body", start, tip - force * 0.22, 0.115, metal)
    for k in (0.28, 0.62, 0.96):
        collar = start + force * k
        cylinder_between(f"probe_ring_{k}", collar - force * 0.03, collar + force * 0.03, 0.145, glow)
    cylinder_between("probe_tip", tip - force * 0.34, tip, 0.040, glow)
    add_arrow("force_vector", tip - force * 0.05, tip + force * 1.15, 0.022, glow)


def add_light_bar(location, scale, mat):
    add_box("light_bar", location, scale, mat, bevel=0.025)


def set_camera(scene):
    bpy.ops.object.camera_add(location=(0.0, -21.8, 5.85))
    cam = bpy.context.object
    cam.data.lens = 49
    cam.data.sensor_width = 36
    target = Vector((0.0, 0.0, 1.35))
    cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam

    # Very subtle cinematic drift.
    cam.keyframe_insert(data_path="location", frame=1)
    cam.location = (0.35, -21.0, 5.70)
    cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()
    cam.keyframe_insert(data_path="location", frame=181)
    cam.keyframe_insert(data_path="rotation_euler", frame=181)


def setup_lighting(mats):
    bpy.ops.object.light_add(type="AREA", location=(0, -3.5, 9.5))
    key = bpy.context.object
    key.data.energy = 2400
    key.data.shape = "RECTANGLE"
    key.data.size = 12
    key.data.size_y = 5

    for loc, color, energy in [
        ((-7, -1, 5.0), (0.05, 0.45, 1.0), 1300),
        ((7, -1, 5.0), (1.0, 0.12, 0.02), 1500),
        ((0, 4.5, 5.5), (0.12, 0.55, 1.0), 900),
    ]:
        bpy.ops.object.light_add(type="AREA", location=loc)
        lamp = bpy.context.object
        lamp.data.energy = energy
        lamp.data.color = color
        lamp.data.size = 5.0
        lamp.rotation_euler = (math.radians(65), 0, math.radians(180))

    # Architectural strips frame each bay without crossing the title/subtitle.
    for x in (-8.0, -3.0, 3.0, 8.0):
        add_light_bar((x, 3.18, 2.15), (0.018, 0.025, 1.55), mats["cyan_glow"])
    add_light_bar((0.0, 3.18, 0.02), (8.8, 0.025, 0.012), mats["cyan_glow"])


def render_animation_52(scene, output):
    settings = scene.render.image_settings
    # Blender 5.2 split "Media Type" from image file_format.
    if hasattr(settings, "media_type"):
        settings.media_type = "VIDEO"
        scene.render.ffmpeg.format = "MPEG4"
        scene.render.ffmpeg.codec = "H264"
        try:
            scene.render.ffmpeg.constant_rate_factor = "HIGH"
        except Exception:
            pass
    else:
        settings.file_format = "FFMPEG"
        scene.render.ffmpeg.format = "MPEG4"
        scene.render.ffmpeg.codec = "H264"
    scene.render.filepath = str(output.with_suffix(".mp4"))
    bpy.ops.render.render(animation=True)


def main():
    args = parse_args()
    if not args.bunny.is_file():
        raise SystemExit(f"Stanford Bunny mesh not found: {args.bunny}")
    if not math.isfinite(args.motion_scale) or args.motion_scale <= 0:
        raise SystemExit("motion scale must be positive")

    manifest = json.loads((args.trajectory_dir / "trajectory_manifest.json").read_text(encoding="utf-8"))
    hero = json.loads((Path(__file__).resolve().parents[1] / "data/hero_case_ofc_2026-09-21.json").read_text(encoding="utf-8"))
    groups, last = load_states(args.trajectory_dir / "particle_trajectories.csv", args.direction)
    if last + 1 != manifest["frames_per_direction"]:
        raise SystemExit("trajectory frame count mismatch")

    scene = configure_scene(args.engine, args.cycles_samples)

    mats = {
        "stage": material("stage", DARK_METAL, metallic=0.88, roughness=0.18),
        "stage2": material("stage2", (0.025, 0.045, 0.085, 1), metallic=0.7, roughness=0.25),
        "cyan": material("cyan", CYAN, metallic=0.15, roughness=0.22, emission=0.6),
        "cyan_glow": material("cyan_glow", CYAN_SOFT, metallic=0.1, roughness=0.18, emission=5.0),
        "orange": material("orange", ORANGE, metallic=0.15, roughness=0.22, emission=0.7),
        "orange_glow": material("orange_glow", AMBER, metallic=0.1, roughness=0.18, emission=5.0),
        "pearl": material("pearl", (0.78, 0.84, 0.92, 1), metallic=0.34, roughness=0.12, emission=0.04),
        "white": material("white", WHITE, metallic=0.2, roughness=0.22),
        "green": material("green", GREEN, metallic=0.1, roughness=0.18, emission=2.0),
        "red": material("red", RED, metallic=0.1, roughness=0.18, emission=3.0),
        "ghost": material("ghost", CYAN_SOFT, metallic=0.0, roughness=0.10, emission=1.0, alpha=0.18),
        "repair_trans": material("repair_trans", ORANGE, metallic=0.0, roughness=0.15, emission=0.7, alpha=0.34),
    }

    # Cinematic stage.
    add_box("floor", (0, 0, -0.56), (10.2, 4.2, 0.08), mats["stage"], bevel=0.04)
    add_box("back_wall", (0, 3.35, 2.3), (10.2, 0.06, 3.1), mats["stage2"], bevel=0.03)
    for x in STATIONS:
        add_pedestal(x, mats["stage"], mats["cyan_glow"] if x < 3 else mats["orange_glow"])

    # Vertical "portal" rings give every stage a strong silhouette and keep the
    # composition from reading as three isolated objects on a blank wall.
    portal_rotation = (math.radians(90), 0.0, 0.0)
    add_torus("observe_portal", (STATIONS[0], 0.65, 1.48), 1.88, 0.018, mats["cyan_glow"], portal_rotation)
    add_torus("repair_portal", (STATIONS[1], 0.65, 1.48), 1.88, 0.018, mats["green"], portal_rotation)
    add_torus("interrogate_portal", (STATIONS[2], 0.65, 1.48), 1.88, 0.018, mats["orange_glow"], portal_rotation)

    # Import only once, copy its mesh for all visual states.
    source = import_ply(args.bunny)
    source.name = "Stanford_Bunny_Source_Hidden"
    params, rest_display = orient_and_normalize_vertices(source.data)
    source.hide_render = True
    source.hide_viewport = True

    # OBSERVE: baseline shown as scanner/wireframe plus actual solver cage.
    create_deformed_bunny(
        source, "Observe_Baseline_Wire", params, rest_display, groups,
        "baseline_apic", last, STATIONS[0], args.motion_scale, mats["cyan"],
        animate=True, wireframe=True,
    )
    create_deformed_bunny(
        source, "Observe_Baseline_Ghost", params, rest_display, groups,
        "baseline_apic", last, STATIONS[0], args.motion_scale, mats["ghost"],
        animate=True,
    )
    add_solver_lattice(groups, "baseline_apic", last, STATIONS[0], args.motion_scale,
                       mats["cyan_glow"], mats["cyan"], 0.8)

    # REPAIR: visually gorgeous solid PIC candidate.
    create_deformed_bunny(
        source, "Repair_PIC_Polished", params, rest_display, groups,
        "repair_pic", last, STATIONS[1], args.motion_scale, mats["pearl"],
        animate=True,
    )
    # No horizontal ring through the animal; the rear portal supplies the halo.

    # INTERROGATE: exact same repair overlaid with truth and residual field.
    create_deformed_bunny(
        source, "Interrogate_Repair", params, rest_display, groups,
        "repair_pic", last, STATIONS[2], args.motion_scale, mats["repair_trans"],
        animate=True,
    )
    create_deformed_bunny(
        source, "Interrogate_Truth", params, rest_display, groups,
        "truth", last, STATIONS[2], args.motion_scale, mats["ghost"],
        animate=True,
        wireframe=True,
        ghost=True,
    )
    add_residual_vectors(groups, last, STATIONS[2], args.motion_scale, mats["cyan_glow"], mats["orange_glow"])
    add_probe(STATIONS[2], args.direction, mats["stage"], mats["orange_glow"])

    row = hero["row"]
    ordinary_improvement = 100.0 * (row["baseline_holdout_m"] - row["repair_holdout_m"]) / row["baseline_holdout_m"]
    hidden_worsening = 100.0 * (row["repair_target_m"] - row["baseline_target_m"]) / row["baseline_target_m"]

    # Headline and stage labels — intentionally split so the scientific conflict
    # reads instantly, and lowered enough to stay inside the 16:9 safe frame.
    add_text("LOOKS RIGHT.", (-2.55, 3.16, 4.72), 0.40, mats["white"])
    add_text("PHYSICS SAYS NO.", (2.45, 3.16, 4.72), 0.40, mats["orange_glow"])
    add_text("VULKAX  //  MECHANISM-SELECTIVE PHYSICAL VERIFICATION", (0, 3.15, 4.24), 0.16, mats["cyan_glow"])

    add_text("01  OBSERVE", (STATIONS[0], 2.55, 3.55), 0.25, mats["cyan_glow"])
    add_text("CAPTURED / FITTED STATE", (STATIONS[0], 2.55, 3.23), 0.13, mats["white"])

    add_text("02  REPAIR", (STATIONS[1], 2.55, 3.55), 0.25, mats["green"])
    add_text(f"LOOKS BETTER   -{ordinary_improvement:.2f}% ORDINARY ERROR", (STATIONS[1], 2.55, 3.23), 0.13, mats["green"])

    add_text("03  INTERROGATE", (STATIONS[2], 2.55, 3.55), 0.25, mats["orange_glow"])
    add_text(f"PHYSICS WORSE   +{hidden_worsening:.2f}% HIDDEN TARGET", (STATIONS[2], 2.55, 3.23), 0.13, mats["red"])
    add_text(f"DCS z={row['dcs_progress_z']:.4f}  //  FORCE z={row['force_progress_z']:.4f}", (STATIONS[2], 2.55, 0.48), 0.12, mats["orange_glow"])
    add_text("REFUSE  //  INSUFFICIENT PHYSICAL INFORMATION", (STATIONS[2], 2.55, 0.20), 0.13, mats["red"])

    add_text(
        f"DISPLAY x{args.motion_scale:g}  //  VALUES UNSCALED",
        (-6.60, 3.14, -0.40), 0.10, mats["orange_glow"], align="LEFT"
    )
    add_text(
        "STANFORD BUNNY VISUALIZATION CARRIER  //  STANFORD COMPUTER GRAPHICS LAB",
        (2.10, 3.14, -0.40), 0.085, mats["white"], align="LEFT"
    )

    setup_lighting(mats)
    set_camera(scene)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    blend_path = args.output.with_suffix(".blend")
    bpy.ops.wm.save_as_mainfile(filepath=str(blend_path))

    if args.render_still:
        scene.frame_set(scene.frame_end)
        scene.render.image_settings.media_type = "IMAGE"
        scene.render.image_settings.file_format = "PNG"
        scene.render.filepath = str(args.output.with_suffix(".png"))
        bpy.ops.render.render(write_still=True)
        print(f"rendered {args.output.with_suffix('.png')}")

    if args.render_animation:
        render_animation_52(scene, args.output)

    print(f"saved {blend_path}")
    print("AESTHETIC HERO PASS")
    print("Stanford Bunny is a visualization carrier; deformation derives from VULKAX solver states.")


if __name__ == "__main__":
    main()

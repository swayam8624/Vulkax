#!/usr/bin/env python3
"""Procedural VULKAX "Reality Inspector" storyboard scene.

This scene is presentation-only. Geometry motion is schematic unless replaced with
exported solver-state trajectories. Numeric labels are read from the frozen VULKAX
result ledger.
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector

DARK = (0.012, 0.025, 0.05, 1.0)
NAVY = (0.03, 0.08, 0.15, 1.0)
CYAN = (0.05, 0.75, 0.95, 1.0)
GREEN = (0.05, 0.82, 0.48, 1.0)
RED = (1.0, 0.17, 0.30, 1.0)
AMBER = (1.0, 0.52, 0.05, 1.0)
WHITE = (0.92, 0.96, 1.0, 1.0)
GHOST = (0.45, 0.56, 0.72, 0.35)


def parse_args():
    argv = sys.argv
    args = argv[argv.index("--") + 1:] if "--" in argv else []
    p = argparse.ArgumentParser()
    p.add_argument("--repo-root", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--render", action="store_true")
    return p.parse_args(args)


def material(name, color, metallic=0.0, roughness=0.45, emission=None):
    m = bpy.data.materials.new(name)
    m.diffuse_color = color
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = color
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = roughness
    if emission is not None:
        bsdf.inputs["Emission Color"].default_value = emission
        bsdf.inputs["Emission Strength"].default_value = 3.0
    if color[3] < 1.0:
        bsdf.inputs["Alpha"].default_value = color[3]
        m.surface_render_method = "DITHERED"
    return m


def add_box(name, location, scale, mat, bevel=0.08):
    bpy.ops.mesh.primitive_cube_add(location=location)
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    bevel_mod = obj.modifiers.new("Bevel", "BEVEL")
    bevel_mod.width = bevel
    bevel_mod.segments = 4
    obj.data.materials.append(mat)
    return obj


def cylinder_between(name, a, b, radius, mat):
    a, b = Vector(a), Vector(b)
    mid = (a + b) * 0.5
    direction = b - a
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=24, radius=radius, depth=direction.length, location=mid
    )
    obj = bpy.context.object
    obj.name = name
    obj.rotation_mode = "QUATERNION"
    obj.rotation_quaternion = direction.to_track_quat("Z", "Y")
    obj.data.materials.append(mat)
    return obj


def beam_points(origin, length, drop, samples=11):
    ox, oy, oz = origin
    points = []
    for i in range(samples):
        t = i / (samples - 1)
        x = ox + length * t
        z = oz - drop * (t * t * (3.0 - 2.0 * t))
        points.append((x, oy, z))
    return points


def add_beam(name, origin, length, drop, radius, mat):
    pts = beam_points(origin, length, drop)
    objs = []
    for i, (a, b) in enumerate(zip(pts[:-1], pts[1:])):
        objs.append(cylinder_between(f"{name}_{i:02d}", a, b, radius, mat))
    return objs, pts[-1]


def add_text(body, location, size, mat, align="LEFT", extrude=0.01):
    bpy.ops.object.text_add(location=location, rotation=(math.radians(90), 0, 0))
    obj = bpy.context.object
    obj.data.body = body
    obj.data.align_x = align
    obj.data.size = size
    obj.data.extrude = extrude
    obj.data.bevel_depth = 0.004
    obj.data.materials.append(mat)
    return obj


def add_arrow(name, start, end, shaft_r, mat):
    shaft_end = Vector(end) + (Vector(start) - Vector(end)).normalized() * 0.35
    cylinder_between(name + "_shaft", start, shaft_end, shaft_r, mat)
    direction = Vector(end) - Vector(start)
    bpy.ops.mesh.primitive_cone_add(
        vertices=32,
        radius1=shaft_r * 3.0,
        radius2=0.0,
        depth=0.45,
        location=Vector(end) - direction.normalized() * 0.20,
    )
    cone = bpy.context.object
    cone.name = name + "_head"
    cone.rotation_mode = "QUATERNION"
    cone.rotation_quaternion = direction.to_track_quat("Z", "Y")
    cone.data.materials.append(mat)


def setup_world():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    scene = bpy.context.scene
    scene.render.engine = "BLENDER_EEVEE_NEXT"
    scene.render.resolution_x = 1920
    scene.render.resolution_y = 1080
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.world.color = DARK
    scene.frame_start = 1
    scene.frame_end = 240
    return scene


def main():
    args = parse_args()
    ledger_path = args.repo_root / "research/results/VULKAX_FINAL_RESULTS_2026-09-21.json"
    data = json.loads(ledger_path.read_text(encoding="utf-8"))
    ofc = data["orthogonal_force_compliance"]
    d1 = data["dcs"]["stages"]["D1"]

    scene = setup_world()
    mats = {
        "navy": material("navy", NAVY, metallic=0.35, roughness=0.25),
        "cyan": material("cyan", CYAN, metallic=0.2, roughness=0.25, emission=CYAN),
        "green": material("green", GREEN, metallic=0.15, roughness=0.3, emission=GREEN),
        "red": material("red", RED, metallic=0.15, roughness=0.3, emission=RED),
        "amber": material("amber", AMBER, metallic=0.2, roughness=0.25, emission=AMBER),
        "white": material("white", WHITE, metallic=0.1, roughness=0.4),
        "ghost": material("ghost", GHOST, metallic=0.0, roughness=0.55),
    }

    add_box("floor", (0, 0, -1.1), (9.5, 4.2, 0.08), mats["navy"], bevel=0.03)
    stations = [-5.7, 0.0, 5.7]
    for idx, x in enumerate(stations):
        add_box(f"pedestal_{idx}", (x, 0, -0.25), (2.2, 1.85, 0.15), mats["navy"], bevel=0.15)
        add_box(f"fixture_{idx}", (x-1.45, 0, 0.9), (0.18, 0.50, 1.10), mats["white"], bevel=0.10)

    add_beam("observed", (-7.1, 0, 1.25), 3.0, 1.0, 0.09, mats["white"])

    add_beam("repair_ghost", (-1.4, 0.03, 1.25), 3.0, 0.92, 0.11, mats["ghost"])
    add_beam("repair", (-1.4, -0.03, 1.25), 3.0, 1.0, 0.075, mats["green"])

    add_beam("probe_reference", (4.3, 0.05, 1.25), 3.0, 0.60, 0.075, mats["ghost"])
    add_beam("probe_deceptive", (4.3, -0.05, 1.25), 3.0, 1.45, 0.085, mats["red"])
    add_arrow("known_force", (6.35, 0.0, 3.1), (6.35, 0.0, 1.65), 0.045, mats["amber"])

    add_text("VULKAX  |  REALITY INSPECTOR", (-8.6, 2.0, 4.35), 0.48, mats["white"])
    add_text("SCHEMATIC STORYBOARD — NUMBERS FROM FROZEN LEDGER", (-8.6, 2.0, 3.75), 0.21, mats["ghost"])

    add_text("01  OBSERVE", (-7.7, 1.55, 2.95), 0.30, mats["cyan"])
    add_text("02  REPAIR", (-2.0, 1.55, 2.95), 0.30, mats["cyan"])
    add_text("03  INTERROGATE", (3.65, 1.55, 2.95), 0.30, mats["cyan"])

    add_text(
        f"D1: {d1['cases']}/{d1['cases']} constructed deceptive repairs rejected",
        (-2.0, 1.55, -0.72), 0.19, mats["white"]
    )
    add_text(
        f"median observation improvement: {100*d1['median_observation_improvement']:.2f}%",
        (-2.0, 1.55, -1.02), 0.17, mats["green"]
    )
    add_text(
        f"|z|  {ofc['dcs']['median_abs_z']:.4f}  ->  {ofc['force_compliance']['median_abs_z']:.4f}",
        (3.65, 1.55, -0.62), 0.20, mats["white"]
    )
    add_text(
        f"{ofc['force_to_dcs_median_abs_z_ratio']:.2f}x stronger evidence",
        (3.65, 1.55, -0.92), 0.23, mats["amber"]
    )
    add_text(
        f"max |z| = {ofc['force_compliance']['max_abs_z']:.4f} < 2  ->  REFUSE",
        (3.65, 1.55, -1.22), 0.20, mats["red"]
    )

    scan = add_box("scanner", (-8.0, 0, 1.3), (0.02, 2.2, 2.6), mats["cyan"], bevel=0.01)
    scan.display_type = "WIRE"
    scan.keyframe_insert(data_path="location", frame=1)
    scan.location.x = 8.0
    scan.keyframe_insert(data_path="location", frame=190)

    bpy.ops.object.camera_add(location=(0, -17.5, 6.8))
    cam = bpy.context.object
    cam.data.lens = 46
    target = Vector((0, 0, 1.15))
    cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam
    cam.keyframe_insert(data_path="location", frame=1)
    cam.location = (0.8, -16.2, 6.2)
    cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()
    cam.keyframe_insert(data_path="location", frame=240)
    cam.keyframe_insert(data_path="rotation_euler", frame=240)

    bpy.ops.object.light_add(type="AREA", location=(0, -2.5, 8.5))
    key = bpy.context.object
    key.data.energy = 1700
    key.data.shape = "RECTANGLE"
    key.data.size = 10
    key.rotation_euler = (math.radians(20), 0, 0)

    bpy.ops.object.light_add(type="AREA", location=(-7, 2.0, 4.0))
    fill = bpy.context.object
    fill.data.energy = 900
    fill.data.color = (0.1, 0.5, 1.0)
    fill.data.size = 5

    bpy.ops.object.light_add(type="AREA", location=(7, 1.5, 4.5))
    rim = bpy.context.object
    rim.data.energy = 1000
    rim.data.color = (1.0, 0.25, 0.08)
    rim.data.size = 5

    args.output.parent.mkdir(parents=True, exist_ok=True)
    blend_path = args.output.with_suffix(".blend")
    bpy.ops.wm.save_as_mainfile(filepath=str(blend_path))

    if args.render:
        scene.render.filepath = str(args.output) + "_"
        scene.render.image_settings.file_format = "PNG"
        bpy.ops.render.render(animation=True)

    print(f"saved {blend_path}")
    print("SCHEMATIC: mechanism geometry is explanatory; numeric labels are frozen evidence.")


if __name__ == "__main__":
    main()

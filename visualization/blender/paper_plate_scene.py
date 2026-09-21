#!/usr/bin/env python3
"""Render isolated, typography-free VULKAX plates for Illustrator assembly."""
from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))
import aesthetic_bunny_scene as hero  # noqa: E402


def parse_args():
    argv = sys.argv
    args = argv[argv.index("--") + 1:] if "--" in argv else []
    p = argparse.ArgumentParser()
    p.add_argument("--trajectory-dir", type=Path, required=True)
    p.add_argument("--bunny", type=Path, required=True)
    p.add_argument("--panel", choices=["observe", "repair", "interrogate", "xray", "wireframe"], required=True)
    p.add_argument("--direction", choices=["px", "nx", "py", "pz"], default="px")
    p.add_argument("--motion-scale", type=float, default=400.0)
    p.add_argument("--engine", choices=["eevee", "cycles"], default="eevee")
    p.add_argument("--cycles-samples", type=int, default=128)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--render-still", action="store_true")
    return p.parse_args(args)


def make_materials():
    return {
        "stage": hero.material("plate_stage", hero.DARK_METAL, metallic=0.90, roughness=0.16),
        "cyan": hero.material("plate_cyan", hero.CYAN, metallic=0.10, roughness=0.18, emission=0.8),
        "cyan_glow": hero.material("plate_cyan_glow", hero.CYAN_SOFT, metallic=0.0, roughness=0.12, emission=5.0),
        "green": hero.material("plate_green", hero.GREEN, metallic=0.08, roughness=0.15, emission=1.8),
        "orange_glow": hero.material("plate_orange_glow", hero.AMBER, metallic=0.0, roughness=0.12, emission=5.0),
        "pearl": hero.material("plate_pearl", (0.80, 0.86, 0.94, 1.0), metallic=0.30, roughness=0.10, emission=0.035),
        "ghost": hero.material("plate_ghost", hero.CYAN_SOFT, metallic=0.0, roughness=0.08, emission=1.2, alpha=0.16),
        "repair_trans": hero.material("plate_repair_trans", hero.ORANGE, metallic=0.0, roughness=0.12, emission=0.6, alpha=0.28),
    }


def plate_camera(scene, panel):
    if panel in {"interrogate", "xray"}:
        loc, lens, target = (-0.15, -9.6, 3.65), 56, Vector((-0.15, 0.0, 1.38))
    else:
        loc, lens, target = (0.0, -8.8, 3.55), 58, Vector((0.0, 0.0, 1.35))
    bpy.ops.object.camera_add(location=loc)
    cam = bpy.context.object
    cam.data.lens = lens
    cam.data.sensor_width = 36
    cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam


def plate_lighting(panel):
    bpy.ops.object.light_add(type="AREA", location=(0.0, -2.8, 6.8))
    key = bpy.context.object
    key.data.energy = 1750 if panel == "repair" else 1450
    key.data.shape = "DISK"
    key.data.size = 5.5

    bpy.ops.object.light_add(type="AREA", location=(-3.8, -0.8, 4.2))
    left = bpy.context.object
    left.data.energy = 820
    left.data.color = (0.05, 0.38, 1.0)
    left.data.size = 3.8
    left.rotation_euler = (math.radians(55), 0.0, math.radians(-28))

    bpy.ops.object.light_add(type="AREA", location=(3.8, 0.6, 4.5))
    rim = bpy.context.object
    rim.data.energy = 1050 if panel in {"interrogate", "xray"} else 720
    rim.data.color = (1.0, 0.18, 0.035) if panel in {"interrogate", "xray"} else (0.18, 0.75, 1.0)
    rim.data.size = 3.4
    rim.rotation_euler = (math.radians(62), 0.0, math.radians(28))


def setup_scene(args):
    scene = hero.configure_scene(args.engine, args.cycles_samples)
    scene.render.resolution_x = 1800
    scene.render.resolution_y = 1800
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True
    scene.render.image_settings.media_type = "IMAGE"
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.frame_start = 1
    scene.frame_end = 1
    return scene


def main():
    args = parse_args()
    if not args.bunny.is_file():
        raise SystemExit(f"bunny missing: {args.bunny}")
    if not math.isfinite(args.motion_scale) or args.motion_scale <= 0:
        raise SystemExit("motion scale must be finite and positive")

    groups, last = hero.load_states(args.trajectory_dir / "particle_trajectories.csv", args.direction)
    scene = setup_scene(args)
    mats = make_materials()

    source = hero.import_ply(args.bunny)
    source.name = "Stanford_Bunny_Source_Hidden"
    params, rest_display = hero.orient_and_normalize_vertices(source.data)
    source.hide_render = True
    source.hide_viewport = True

    panel = args.panel
    accent = mats["orange_glow"] if panel in {"interrogate", "xray"} else mats["cyan_glow"]
    if panel == "repair":
        accent = mats["green"]

    if panel != "xray":
        hero.add_pedestal(0.0, mats["stage"], accent)
        hero.add_torus(f"{panel}_portal", (0.0, 0.55, 1.48), 1.86, 0.018, accent,
                       rotation=(math.radians(90), 0.0, 0.0))

    if panel == "observe":
        hero.create_deformed_bunny(source, "Observe_Wire", params, rest_display, groups,
                                   "baseline_apic", last, 0.0, args.motion_scale, mats["cyan"],
                                   animate=False, wireframe=True)
        hero.create_deformed_bunny(source, "Observe_Ghost", params, rest_display, groups,
                                   "baseline_apic", last, 0.0, args.motion_scale, mats["ghost"],
                                   animate=False)
        hero.add_solver_lattice(groups, "baseline_apic", last, 0.0, args.motion_scale,
                                mats["cyan_glow"], mats["cyan"])

    elif panel == "wireframe":
        hero.create_deformed_bunny(source, "Wireframe_Bunny", params, rest_display, groups,
                                   "baseline_apic", last, 0.0, args.motion_scale, mats["cyan"],
                                   animate=False, wireframe=True)
        hero.add_solver_lattice(groups, "baseline_apic", last, 0.0, args.motion_scale,
                                mats["cyan_glow"], mats["cyan"])

    elif panel == "repair":
        hero.create_deformed_bunny(source, "Repair_PIC", params, rest_display, groups,
                                   "repair_pic", last, 0.0, args.motion_scale, mats["pearl"],
                                   animate=False)

    elif panel == "interrogate":
        hero.create_deformed_bunny(source, "Interrogate_Repair", params, rest_display, groups,
                                   "repair_pic", last, 0.0, args.motion_scale, mats["repair_trans"],
                                   animate=False)
        hero.create_deformed_bunny(source, "Interrogate_Truth", params, rest_display, groups,
                                   "truth", last, 0.0, args.motion_scale, mats["ghost"],
                                   animate=False, wireframe=True, ghost=True)
        hero.add_residual_vectors(groups, last, 0.0, args.motion_scale,
                                  mats["cyan_glow"], mats["orange_glow"])
        hero.add_probe(0.0, args.direction, mats["stage"], mats["orange_glow"])

    elif panel == "xray":
        hero.create_deformed_bunny(source, "XRay_Repair", params, rest_display, groups,
                                   "repair_pic", last, 0.0, args.motion_scale, mats["repair_trans"],
                                   animate=False)
        hero.create_deformed_bunny(source, "XRay_Truth", params, rest_display, groups,
                                   "truth", last, 0.0, args.motion_scale, mats["ghost"],
                                   animate=False, wireframe=True, ghost=True)
        hero.add_residual_vectors(groups, last, 0.0, args.motion_scale,
                                  mats["cyan_glow"], mats["orange_glow"])
        force = {"px": Vector((1, 0, 0)), "nx": Vector((-1, 0, 0)),
                 "py": Vector((0, 0, 1)), "pz": Vector((0, 1, 0))}[args.direction]
        origin = Vector((0, -0.18, 1.55))
        hero.add_arrow("xray_force", origin - force * 1.8, origin + force * 0.2,
                       0.025, mats["orange_glow"])

    plate_lighting(panel)
    plate_camera(scene, panel)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    blend_path = args.output.with_suffix(".blend")
    bpy.ops.wm.save_as_mainfile(filepath=str(blend_path))

    if args.render_still:
        scene.render.filepath = str(args.output.with_suffix(".png"))
        bpy.ops.render.render(write_still=True)
        print(f"rendered {args.output.with_suffix('.png')}")

    print(f"saved {blend_path}")
    print(f"PAPER_PLATE PASS panel={panel}")


if __name__ == "__main__":
    main()

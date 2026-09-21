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
    p.add_argument("--panel", choices=["observe", "repair", "interrogate", "xray", "wireframe", "brightfield", "texture_truth", "texture_repair", "darkfield"], required=True)
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
        "cyan": hero.material("plate_cyan", hero.CYAN, metallic=0.08, roughness=0.22, emission=0.34, alpha=0.52),
        "cyan_glow": hero.material("plate_cyan_glow", hero.CYAN_SOFT, metallic=0.0, roughness=0.16, emission=2.2),
        "lattice": hero.material("plate_lattice", (0.10, 0.58, 0.82, 1.0), metallic=0.0, roughness=0.25, emission=0.42, alpha=0.72),
        "lattice_point": hero.material("plate_lattice_point", (0.48, 0.88, 1.0, 1.0), metallic=0.0, roughness=0.18, emission=1.35),
        "green": hero.material("plate_green", hero.GREEN, metallic=0.08, roughness=0.17, emission=1.35),
        "orange_glow": hero.material("plate_orange_glow", hero.AMBER, metallic=0.0, roughness=0.14, emission=3.4),
        "pearl": hero.material("plate_pearl", (0.72, 0.80, 0.91, 1.0), metallic=0.24, roughness=0.15, emission=0.018),
        "observe_surface": hero.material("plate_observe_surface", (0.16, 0.62, 0.84, 1.0), metallic=0.0, roughness=0.18, emission=0.32, alpha=0.13),
        "truth_wire": hero.material("plate_truth_wire", (0.18, 0.78, 1.0, 1.0), metallic=0.0, roughness=0.12, emission=0.9, alpha=0.62),
        "repair_trans": hero.material("plate_repair_trans", (1.0, 0.23, 0.05, 1.0), metallic=0.0, roughness=0.18, emission=0.24, alpha=0.22),
        "residual": hero.material("plate_residual", (1.0, 0.54, 0.12, 1.0), metallic=0.0, roughness=0.12, emission=3.2),
        "diag_truth_base": hero.material("diag_truth_base", (0.80, 0.88, 0.93, 1.0), metallic=0.12, roughness=0.22),
        "diag_truth_line": hero.material("diag_truth_line", (0.02, 0.34, 0.48, 1.0), metallic=0.0, roughness=0.26, emission=0.18),
        "diag_repair_base": hero.material("diag_repair_base", (0.91, 0.84, 0.76, 1.0), metallic=0.10, roughness=0.22),
        "diag_repair_line": hero.material("diag_repair_line", (0.66, 0.20, 0.05, 1.0), metallic=0.0, roughness=0.26, emission=0.14),
    }


def plate_camera(scene, panel):
    # Keep all hero plates on the same optical scale so Illustrator assembly
    # does not have to hide framing differences.
    loc, lens, target = (0.0, -8.95, 3.55), 58, Vector((0.0, 0.0, 1.35))
    bpy.ops.object.camera_add(location=loc)
    cam = bpy.context.object
    cam.data.lens = lens
    cam.data.sensor_width = 36
    cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam


def plate_lighting(panel):
    bpy.ops.object.light_add(type="AREA", location=(0.0, -2.8, 6.8))
    key = bpy.context.object
    key.data.energy = 1580 if panel == "repair" else 1420
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



def solver_display_position(state, motion_scale):
    d = state["pos"] - state["rest"]
    rest = Vector((state["rest"].x, state["rest"].z, state["rest"].y))
    disp = Vector((d.x, d.z, d.y))
    return Vector((rest.x * 5.2, rest.y * 4.1, rest.z * 6.5 + 1.38)) + disp * motion_scale


def add_forensic_residuals(groups, frame, motion_scale, truth_mat, residual_mat):
    truth = groups[("truth", frame)]
    repair = groups[("repair_pic", frame)]
    # Use the top layer plus four deterministic interior witnesses. This keeps
    # the X-ray sparse enough to read while preserving solver-derived vectors.
    interior_ids = {22, 27, 38, 43}
    for pid in sorted(truth):
        if not (truth[pid]["top"] or pid in interior_ids):
            continue
        a = solver_display_position(truth[pid], motion_scale)
        b = solver_display_position(repair[pid], motion_scale)
        if (b - a).length <= 1e-7:
            continue
        hero.cylinder_between(f"forensic_residual_{pid}", a, b, 0.010, residual_mat, 18)
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=0.022, location=a)
        bpy.context.object.data.materials.append(truth_mat)
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=0.026, location=b)
        bpy.context.object.data.materials.append(residual_mat)


def add_compact_probe(direction, stage_mat, glow_mat):
    force = {
        "px": Vector((1, 0, 0)),
        "nx": Vector((-1, 0, 0)),
        "py": Vector((0, 0, 1)),
        "pz": Vector((0, 1, 0)),
    }[direction]
    # Compact actuator enters from the plate edge and contacts the subject
    # without visually bisecting it.
    if direction == "px":
        tip = Vector((-1.06, -0.30, 1.58))
    elif direction == "nx":
        tip = Vector((1.06, -0.30, 1.58))
    else:
        tip = Vector((0.0, -0.30, 1.58))
    start = tip - force * 1.15
    hero.cylinder_between("compact_probe_body", start, tip - force * 0.20, 0.082, stage_mat, 40)
    hero.cylinder_between("compact_probe_tip", tip - force * 0.28, tip, 0.034, glow_mat, 28)
    for k in (0.28, 0.60):
        p = start + force * k
        hero.cylinder_between(f"compact_probe_ring_{k}", p - force * 0.025, p + force * 0.025, 0.104, glow_mat, 32)
    hero.add_arrow("compact_force", tip - force * 0.02, tip + force * 0.72, 0.016, glow_mat)


def apply_diagnostic_grid(obj, params, line_mat, bands=11, width=0.085):
    """Assign a rest-space grid texture that deforms with the bunny surface."""
    obj.data.materials.append(line_mat)
    for poly in obj.data.polygons:
        us = [params[i][0] for i in poly.vertices]
        ws = [params[i][2] for i in poly.vertices]
        u = sum(us) / len(us)
        w = sum(ws) / len(ws)
        pu = (u * bands) % 1.0
        pw = (w * bands) % 1.0
        line = pu < width or pu > 1.0 - width or pw < width or pw > 1.0 - width
        poly.material_index = 1 if line else 0

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
                                   "baseline_apic", last, 0.0, args.motion_scale, mats["observe_surface"],
                                   animate=False)
        hero.add_solver_lattice(groups, "baseline_apic", last, 0.0, args.motion_scale,
                                mats["lattice_point"], mats["lattice"])

    elif panel == "wireframe":
        hero.create_deformed_bunny(source, "Wireframe_Bunny", params, rest_display, groups,
                                   "baseline_apic", last, 0.0, args.motion_scale, mats["cyan"],
                                   animate=False, wireframe=True)
        hero.add_solver_lattice(groups, "baseline_apic", last, 0.0, args.motion_scale,
                                mats["lattice_point"], mats["lattice"])

    elif panel == "repair":
        hero.create_deformed_bunny(source, "Repair_PIC", params, rest_display, groups,
                                   "repair_pic", last, 0.0, args.motion_scale, mats["pearl"],
                                   animate=False)

    elif panel == "interrogate":
        hero.create_deformed_bunny(source, "Interrogate_Repair", params, rest_display, groups,
                                   "repair_pic", last, 0.0, args.motion_scale, mats["repair_trans"],
                                   animate=False)
        hero.create_deformed_bunny(source, "Interrogate_Truth", params, rest_display, groups,
                                   "truth", last, 0.0, args.motion_scale, mats["truth_wire"],
                                   animate=False, wireframe=True, ghost=True)
        add_forensic_residuals(groups, last, args.motion_scale, mats["cyan_glow"], mats["residual"])
        add_compact_probe(args.direction, mats["stage"], mats["orange_glow"])

    elif panel == "brightfield":
        hero.create_deformed_bunny(source, "Brightfield_Repair", params, rest_display, groups,
                                   "repair_pic", last, 0.0, args.motion_scale, mats["pearl"],
                                   animate=False)

    elif panel == "texture_truth":
        obj = hero.create_deformed_bunny(source, "Diagnostic_Truth", params, rest_display, groups,
                                         "truth", last, 0.0, args.motion_scale, mats["diag_truth_base"],
                                         animate=False)
        apply_diagnostic_grid(obj, params, mats["diag_truth_line"])

    elif panel == "texture_repair":
        obj = hero.create_deformed_bunny(source, "Diagnostic_Repair", params, rest_display, groups,
                                         "repair_pic", last, 0.0, args.motion_scale, mats["diag_repair_base"],
                                         animate=False)
        apply_diagnostic_grid(obj, params, mats["diag_repair_line"])

    elif panel == "darkfield":
        hero.create_deformed_bunny(source, "Darkfield_Repair", params, rest_display, groups,
                                   "repair_pic", last, 0.0, args.motion_scale, mats["repair_trans"],
                                   animate=False)
        hero.create_deformed_bunny(source, "Darkfield_Truth", params, rest_display, groups,
                                   "truth", last, 0.0, args.motion_scale, mats["truth_wire"],
                                   animate=False, wireframe=True, ghost=True)
        add_forensic_residuals(groups, last, args.motion_scale, mats["cyan_glow"], mats["residual"])
        force = {"px": Vector((1, 0, 0)), "nx": Vector((-1, 0, 0)),
                 "py": Vector((0, 0, 1)), "pz": Vector((0, 1, 0))}[args.direction]
        origin = Vector((0, -0.18, 1.55))
        hero.add_arrow("darkfield_force", origin - force * 1.55, origin + force * 0.15,
                       0.022, mats["orange_glow"])

    elif panel == "xray":
        hero.create_deformed_bunny(source, "XRay_Repair", params, rest_display, groups,
                                   "repair_pic", last, 0.0, args.motion_scale, mats["repair_trans"],
                                   animate=False)
        hero.create_deformed_bunny(source, "XRay_Truth", params, rest_display, groups,
                                   "truth", last, 0.0, args.motion_scale, mats["truth_wire"],
                                   animate=False, wireframe=True, ghost=True)
        add_forensic_residuals(groups, last, args.motion_scale, mats["cyan_glow"], mats["residual"])
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

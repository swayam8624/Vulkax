# LEGACY PRESENTATION EXPERIMENT — NOT THE CANONICAL REALITY PROBE PAPER VIDEO.
# Kept only for provenance. Use visualization/explainer/render.py via
# visualization/explainer/render_explainer.sh for the canonical explainer.
#!/usr/bin/env python3
"""Build the restrained Reality Probe paper/supplement video.

Four shots, no ornamental scanner choreography:
1. Appearance: the deceptive repair looks plausible.
2. Response comparison: truth and repair under the same diagnostic grid/probe.
3. Mechanism darkfield: continuous candidate-minus-truth response field.
4. Decision: frozen quantitative evidence and refusal.

Every 3D response is driven by the frozen exported solver trajectory.
"""
from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import aesthetic_bunny_scene as hero
import paper_plate_scene as paper


def parse_args():
    argv = sys.argv
    args = argv[argv.index("--") + 1:] if "--" in argv else []
    p = argparse.ArgumentParser()
    p.add_argument("--trajectory-dir", type=Path, required=True)
    p.add_argument("--bunny", type=Path, required=True)
    p.add_argument("--direction", choices=["px", "nx", "py", "pz"], default="px")
    p.add_argument("--motion-scale", type=float, default=400.0)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--render", action="store_true")
    return p.parse_args(args)


def created_since(before):
    return [o for o in bpy.data.objects if o not in before]


def set_visible(objects, frame, visible):
    for obj in objects:
        obj.hide_render = not visible
        obj.hide_viewport = not visible
        obj.keyframe_insert(data_path="hide_render", frame=frame)
        obj.keyframe_insert(data_path="hide_viewport", frame=frame)


def shot_visibility(objects, start, end):
    set_visible(objects, max(1, start - 1), False)
    set_visible(objects, start, True)
    set_visible(objects, end, True)
    if end < 390:
        set_visible(objects, end + 1, False)


def emission(name, color, strength=1.0):
    return hero.material(name, color, metallic=0.0, roughness=0.32, emission=strength)


def add_screen_text(body, x, z, size, mat, align="LEFT", extrude=0.002):
    return hero.add_text(body, (x, -1.60, z), size, mat, align=align, extrude=extrude)


def add_screen_box(name, x, z, width, height, mat, bevel=0.035):
    return hero.add_box(name, (x, -1.42, z), (width * 0.5, 0.018, height * 0.5), mat, bevel=bevel)


def load_metrics():
    hero_case = json.loads((REPO / "visualization/data/hero_case_ofc_2026-09-21.json").read_text())
    ledger = json.loads((REPO / "research/results/VULKAX_FINAL_RESULTS_2026-09-21.json").read_text())
    row = hero_case["row"]
    improve = 100.0 * (row["baseline_holdout_m"] - row["repair_holdout_m"]) / row["baseline_holdout_m"]
    worsen = 100.0 * (row["repair_target_m"] - row["baseline_target_m"]) / row["baseline_target_m"]
    return improve, worsen, abs(row["force_progress_z"]), 2.0, ledger["orthogonal_force_compliance"]["force_to_dcs_median_abs_z_ratio"]


def setup_camera(scene):
    bpy.ops.object.camera_add(location=(0.0, -9.15, 3.18))
    cam = bpy.context.object
    cam.data.lens = 50
    cam.data.sensor_width = 36
    target = Vector((0.0, 0.0, 1.22))
    cam.rotation_euler = (target - cam.location).to_track_quat("-Z", "Y").to_euler()
    scene.camera = cam
    return cam


def setup_lighting():
    bpy.ops.object.light_add(type="AREA", location=(0.0, -2.6, 6.2))
    key = bpy.context.object
    key.data.energy = 1550
    key.data.shape = "DISK"
    key.data.size = 5.5

    bpy.ops.object.light_add(type="AREA", location=(-3.8, -0.4, 4.2))
    fill = bpy.context.object
    fill.data.energy = 720
    fill.data.color = (0.05, 0.35, 1.0)
    fill.data.size = 3.8

    bpy.ops.object.light_add(type="AREA", location=(3.8, 0.4, 4.2))
    rim = bpy.context.object
    rim.data.energy = 760
    rim.data.color = (1.0, 0.20, 0.04)
    rim.data.size = 3.6


def add_header(step, title, subtitle, accent, white, muted):
    objs = []
    objs.append(add_screen_text(step, -2.92, 2.58, 0.15, accent))
    objs.append(add_screen_text(title, -2.52, 2.58, 0.15, white))
    objs.append(add_screen_text(subtitle, -2.92, 2.30, 0.072, muted))
    return objs


def add_color_scale(materials):
    objs = []
    n = 18
    for i in range(n):
        t = i / (n - 1)
        mat = emission(f"video_scale_{i}", paper.residual_color(t), 0.10)
        x = -1.45 + i * (2.90 / n)
        objs.append(add_screen_box(f"scale_{i}", x, -0.22, 2.90 / n + 0.01, 0.075, mat, bevel=0.005))
    objs.append(add_screen_text("low disagreement", -1.52, -0.40, 0.065, materials["muted"]))
    objs.append(add_screen_text("high disagreement", 0.55, -0.40, 0.065, materials["muted"]))
    return objs


def main():
    args = parse_args()
    groups, last = hero.load_states(args.trajectory_dir / "particle_trajectories.csv", args.direction)

    scene = hero.configure_scene("eevee", 64)
    scene.render.resolution_x = 1920
    scene.render.resolution_y = 1080
    scene.render.resolution_percentage = 100
    scene.render.fps = 30
    scene.frame_start = 1
    scene.frame_end = 390
    scene.world.color = (0.010, 0.014, 0.020)
    try:
        scene.view_settings.look = "AgX - Medium High Contrast"
    except Exception:
        pass

    white = emission("video_white", (0.88, 0.94, 0.98, 1.0), 0.12)
    muted = emission("video_muted", (0.46, 0.57, 0.66, 1.0), 0.06)
    cyan = emission("video_cyan", (0.12, 0.74, 1.0, 1.0), 0.30)
    orange = emission("video_orange", (1.0, 0.42, 0.10, 1.0), 0.30)
    red = emission("video_red", (1.0, 0.16, 0.19, 1.0), 0.35)
    panel = hero.material("video_panel", (0.018, 0.030, 0.045, 1.0), metallic=0.05, roughness=0.32)
    pearl = hero.material("video_pearl", (0.76, 0.84, 0.92, 1.0), metallic=0.22, roughness=0.16)

    source = hero.import_ply(args.bunny)
    params, rest_display = hero.orient_and_normalize_vertices(source.data)
    source.hide_render = True
    source.hide_viewport = True

    diag_truth = paper.diagnostic_grid_material(
        "video_diag_truth", (0.80, 0.88, 0.93, 1.0), (0.015, 0.30, 0.48, 1.0), bands=10.0, width=0.050
    )
    diag_repair = paper.diagnostic_grid_material(
        "video_diag_repair", (0.89, 0.85, 0.80, 1.0), (0.74, 0.24, 0.055, 1.0), bands=10.0, width=0.050
    )
    residual_mat = paper.residual_field_material("video_residual_field")
    common_scale = paper.four_direction_residual_scale(args.trajectory_dir / "particle_trajectories.csv")

    # Shot 1: appearance.
    before = set(bpy.data.objects)
    repair = hero.create_deformed_bunny(
        source, "Video_Appearance_Repair", params, rest_display, groups,
        "repair_pic", last, 0.0, args.motion_scale, pearl, animate=False
    )
    repair.scale = (1.02, 1.02, 1.02)
    shot1 = created_since(before) + add_header(
        "01", "APPEARANCE", "ordinary evidence prefers this repair", cyan, white, muted
    )
    metric = add_screen_text("ordinary held-out error  -14.93%", -2.92, -0.55, 0.078, cyan)
    shot1.append(metric)
    shot_visibility(shot1, 1, 75)

    # Shot 2: side-by-side diagnostic response.
    before = set(bpy.data.objects)
    truth_obj = hero.create_deformed_bunny(
        source, "Video_Truth_Response", params, rest_display, groups,
        "truth", last, 0.0, args.motion_scale, diag_truth, animate=False
    )
    paper.attach_rest_uv(truth_obj, params)
    truth_obj.scale = (0.72, 0.72, 0.72)
    truth_obj.location.x = -1.45

    repair_obj = hero.create_deformed_bunny(
        source, "Video_Repair_Response", params, rest_display, groups,
        "repair_pic", last, 0.0, args.motion_scale, diag_repair, animate=False
    )
    paper.attach_rest_uv(repair_obj, params)
    repair_obj.scale = (0.72, 0.72, 0.72)
    repair_obj.location.x = 1.45

    shot2 = created_since(before) + add_header(
        "02", "SAME PROBE, DIFFERENT RESPONSE", "rest-space diagnostic grid under the frozen +X force", cyan, white, muted
    )
    shot2 += [
        add_screen_text("TRUTH", -2.30, -0.10, 0.090, cyan),
        add_screen_text("REPAIR", 0.72, -0.10, 0.090, orange),
    ]
    hero.add_arrow("video_force_arrow", Vector((-0.42, -1.50, 0.20)), Vector((0.42, -1.50, 0.20)), 0.012, orange)
    shot2 += [o for o in bpy.data.objects if o.name.startswith("video_force_arrow")]
    shot_visibility(shot2, 76, 180)

    # Shot 3: mechanism darkfield.
    before = set(bpy.data.objects)
    residual_obj = paper.create_residual_field_bunny(
        source, params, rest_display, groups, last, args.motion_scale,
        residual_mat, common_scale, "Video_Mechanism_Darkfield"
    )
    paper.add_surface_residual_vectors(
        params, rest_display, groups, last, args.motion_scale, cyan, orange
    )
    paper.add_force_glyph(args.direction, orange)
    shot3 = created_since(before) + add_header(
        "03", "MECHANISM DARKFIELD", "|u_repair - u_truth| exposes the hidden response mismatch", orange, white, muted
    )
    shot3 += add_color_scale({"muted": muted})
    shot_visibility(shot3, 181, 300)

    # Shot 4: quantitative decision.
    improve, worsen, force_z, threshold, median_ratio = load_metrics()
    before = set(bpy.data.objects)
    card = add_screen_box("decision_card", 0.0, 1.05, 5.10, 3.00, panel, bevel=0.10)
    title = add_screen_text("04  DECISION", -2.10, 2.25, 0.165, white)
    sub = add_screen_text("stronger physical evidence, still below the frozen certification bar", -2.10, 1.98, 0.068, muted)
    l1 = add_screen_text("ordinary fit", -2.00, 1.38, 0.090, muted)
    v1 = add_screen_text(f"-{improve:.2f}%", 0.70, 1.38, 0.125, cyan)
    l2 = add_screen_text("hidden physical target", -2.00, 0.88, 0.090, muted)
    v2 = add_screen_text(f"+{worsen:.2f}%", 0.70, 0.88, 0.125, orange)
    l3 = add_screen_text("force/compliance |z|", -2.00, 0.38, 0.090, muted)
    v3 = add_screen_text(f"{force_z:.3f}  <  {threshold:.0f}", 0.70, 0.38, 0.125, orange)
    gain = add_screen_text(f"median signal gain  {median_ratio:.2f}x", -2.00, -0.10, 0.078, muted)
    refuse_box = add_screen_box("refuse_box", 0.0, -0.76, 4.25, 0.60, hero.material("refuse_bg", (0.12, 0.018, 0.025, 1.0), metallic=0.0, roughness=0.35), bevel=0.08)
    refuse = add_screen_text("REFUSE CERTIFICATION", 0.0, -0.79, 0.175, red, align="CENTER")
    foot = add_screen_text("insufficient information is itself a result", 0.0, -1.26, 0.064, muted, align="CENTER")
    shot4 = created_since(before)
    shot_visibility(shot4, 301, 390)

    setup_lighting()
    setup_camera(scene)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    blend_path = args.output.with_suffix(".blend")
    bpy.ops.wm.save_as_mainfile(filepath=str(blend_path))

    if args.render:
        settings = scene.render.image_settings
        if hasattr(settings, "media_type"):
            settings.media_type = "VIDEO"
        else:
            settings.file_format = "FFMPEG"
        scene.render.ffmpeg.format = "MPEG4"
        scene.render.ffmpeg.codec = "H264"
        scene.render.ffmpeg.constant_rate_factor = "HIGH"
        scene.render.filepath = str(args.output.with_suffix(".mp4"))
        bpy.ops.render.render(animation=True)

    print(f"saved {blend_path}")
    print("REALITY_PROBE_VIDEO PASS")


if __name__ == "__main__":
    main()

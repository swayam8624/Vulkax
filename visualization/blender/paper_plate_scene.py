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


def diagnostic_grid_material(name, base_color, line_color, bands=10.0, width=0.055):
    """Smooth UV-space diagnostic grid that deforms with the solver-driven mesh."""
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()

    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    tex = nt.nodes.new("ShaderNodeTexCoord")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")

    nt.links.new(tex.outputs["UV"], sep.inputs["Vector"])

    masks = []
    threshold = math.sin(math.pi * width)
    for axis_name in ("X", "Y"):
        mul = nt.nodes.new("ShaderNodeMath")
        mul.operation = "MULTIPLY"
        mul.inputs[1].default_value = bands * math.pi
        sine = nt.nodes.new("ShaderNodeMath")
        sine.operation = "SINE"
        absv = nt.nodes.new("ShaderNodeMath")
        absv.operation = "ABSOLUTE"
        less = nt.nodes.new("ShaderNodeMath")
        less.operation = "LESS_THAN"
        less.inputs[1].default_value = threshold
        nt.links.new(sep.outputs[axis_name], mul.inputs[0])
        nt.links.new(mul.outputs[0], sine.inputs[0])
        nt.links.new(sine.outputs[0], absv.inputs[0])
        nt.links.new(absv.outputs[0], less.inputs[0])
        masks.append(less)

    grid = nt.nodes.new("ShaderNodeMath")
    grid.operation = "MAXIMUM"
    nt.links.new(masks[0].outputs[0], grid.inputs[0])
    nt.links.new(masks[1].outputs[0], grid.inputs[1])

    mix = nt.nodes.new("ShaderNodeMixRGB")
    mix.blend_type = "MIX"
    mix.inputs[1].default_value = (*base_color[:3], 1.0)
    mix.inputs[2].default_value = (*line_color[:3], 1.0)
    nt.links.new(grid.outputs[0], mix.inputs[0])
    nt.links.new(mix.outputs[0], bsdf.inputs["Base Color"])
    bsdf.inputs["Metallic"].default_value = 0.08
    bsdf.inputs["Roughness"].default_value = 0.24
    if "Coat Weight" in bsdf.inputs:
        bsdf.inputs["Coat Weight"].default_value = 0.28
        bsdf.inputs["Coat Roughness"].default_value = 0.16
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    return mat


def attach_rest_uv(obj, params):
    """Store normalized rest-space (u,w) on the copied bunny topology."""
    uv = obj.data.uv_layers.new(name="DiagnosticUV")
    for poly in obj.data.polygons:
        for loop_index in poly.loop_indices:
            vid = obj.data.loops[loop_index].vertex_index
            u, _v, w = params[vid]
            uv.data[loop_index].uv = (u, w)
    obj.data.uv_layers.active = uv
    obj.data.uv_layers.active_render = uv


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


def residual_field_material(name):
    """Render a smooth per-vertex candidate-minus-truth response magnitude field."""
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    try:
        attr = nt.nodes.new("ShaderNodeVertexColor")
        attr.layer_name = "ResidualColor"
        color_out = attr.outputs["Color"]
    except Exception:
        attr = nt.nodes.new("ShaderNodeAttribute")
        attr.attribute_name = "ResidualColor"
        color_out = attr.outputs["Color"]
    nt.links.new(color_out, bsdf.inputs["Base Color"])
    if "Emission Color" in bsdf.inputs:
        nt.links.new(color_out, bsdf.inputs["Emission Color"])
    elif "Emission" in bsdf.inputs:
        nt.links.new(color_out, bsdf.inputs["Emission"])
    if "Emission Strength" in bsdf.inputs:
        bsdf.inputs["Emission Strength"].default_value = 0.12
    bsdf.inputs["Metallic"].default_value = 0.08
    bsdf.inputs["Roughness"].default_value = 0.28
    if "Coat Weight" in bsdf.inputs:
        bsdf.inputs["Coat Weight"].default_value = 0.20
        bsdf.inputs["Coat Roughness"].default_value = 0.18
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    return mat


def four_direction_residual_scale(csv_path):
    """Common raw-metre scale across +X/-X/+Y/+Z for directly comparable fields."""
    max_mag = 1e-12
    for direction in ("px", "nx", "py", "pz"):
        dg, last = hero.load_states(csv_path, direction)
        truth = dg[("truth", last)]
        repair = dg[("repair_pic", last)]
        for pid in truth:
            td = truth[pid]["pos"] - truth[pid]["rest"]
            rd = repair[pid]["pos"] - repair[pid]["rest"]
            delta = rd - td
            mapped = Vector((delta.x, delta.z, delta.y))
            max_mag = max(max_mag, mapped.length)
    return max_mag


def residual_color(t):
    """Perceptually ordered cool->light->warm response color."""
    t = min(max(float(t), 0.0), 1.0)
    # Avoid a white midpoint: the disagreement field must remain perceptually
    # visible even when most samples cluster near the middle of the common scale.
    if t < 0.55:
        a = t / 0.55
        lo = Vector((0.020, 0.090, 0.260))
        hi = Vector((0.020, 0.620, 0.760))
        rgb = lo.lerp(hi, a)
    else:
        a = (t - 0.55) / 0.45
        lo = Vector((0.020, 0.620, 0.760))
        hi = Vector((1.000, 0.180, 0.030))
        rgb = lo.lerp(hi, a)
    return (rgb.x, rgb.y, rgb.z, 1.0)


def apply_residual_vertex_colors(obj, params, groups, frame, common_scale):
    truth = groups[("truth", frame)]
    repair = groups[("repair_pic", frame)]
    attr = obj.data.color_attributes.get("ResidualColor")
    if attr is None:
        attr = obj.data.color_attributes.new(name="ResidualColor", type="FLOAT_COLOR", domain="POINT")
    mags = []
    for i, uvw in enumerate(params):
        td = hero.field_displacement(truth, uvw)
        rd = hero.field_displacement(repair, uvw)
        mag = (rd - td).length
        mags.append(mag)
        attr.data[i].color = residual_color(mag / common_scale)
    return max(mags) if mags else 0.0


def create_residual_field_bunny(source, params, rest_display, groups, frame, motion_scale, mat, common_scale, name):
    obj = hero.create_deformed_bunny(
        source, name, params, rest_display, groups,
        "repair_pic", frame, 0.0, motion_scale, mat,
        animate=False,
    )
    max_surface = apply_residual_vertex_colors(obj, params, groups, frame, common_scale)
    print(f"RESIDUAL_FIELD {name} max_surface_raw_m={max_surface:.9g} common_scale_raw_m={common_scale:.9g}")
    return obj


def add_surface_residual_vectors(params, rest_display, groups, frame, motion_scale, truth_mat, residual_mat):
    """Show candidate-minus-truth response at deterministic bunny-surface samples."""
    truth_group = groups[("truth", frame)]
    repair_group = groups[("repair_pic", frame)]
    targets = [
        (0.24, 0.42, 0.40), (0.40, 0.48, 0.34), (0.58, 0.48, 0.38),
        (0.74, 0.52, 0.43), (0.30, 0.50, 0.62), (0.50, 0.50, 0.67),
        (0.68, 0.50, 0.64), (0.42, 0.36, 0.82), (0.62, 0.42, 0.82),
    ]
    used = set()
    for target in targets:
        idx = min(
            (i for i in range(len(params)) if i not in used),
            key=lambda i: sum((params[i][k] - target[k]) ** 2 for k in range(3)),
        )
        used.add(idx)
        uvw = params[idx]
        a = rest_display[idx] + hero.field_displacement(truth_group, uvw) * motion_scale
        b = rest_display[idx] + hero.field_displacement(repair_group, uvw) * motion_scale
        delta = b - a
        if delta.length <= 1e-7:
            continue
        hero.cylinder_between(f"surface_residual_{idx}", a, b, 0.008, residual_mat, 16)
        bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=1, radius=0.014, location=a)
        bpy.context.object.data.materials.append(truth_mat)
        unit = delta.normalized()
        bpy.ops.mesh.primitive_cone_add(
            vertices=16, radius1=0.026, radius2=0.0, depth=0.065,
            location=b - unit * 0.022,
        )
        cone = bpy.context.object
        cone.rotation_mode = "QUATERNION"
        cone.rotation_quaternion = delta.to_track_quat("Z", "Y")
        cone.data.materials.append(residual_mat)


def add_force_glyph(direction, glow_mat):
    """Compact direction glyph kept outside the central response field."""
    if direction == "px":
        hero.add_arrow("force_px", Vector((-1.95, -0.24, 1.52)), Vector((-1.18, -0.24, 1.52)), 0.012, glow_mat)
    elif direction == "nx":
        hero.add_arrow("force_nx", Vector((1.95, -0.24, 1.52)), Vector((1.18, -0.24, 1.52)), 0.012, glow_mat)
    elif direction == "py":
        hero.add_arrow("force_py", Vector((-1.62, -0.24, 0.52)), Vector((-1.62, -0.24, 1.20)), 0.012, glow_mat)
    else:
        # +solver-Z maps into screen depth from this camera: use the standard
        # circled-cross symbol for a vector pointing away from the viewer.
        center = Vector((1.58, -0.70, 1.52))
        hero.add_torus("force_pz_ring", center, 0.16, 0.010, glow_mat, rotation=(math.radians(90), 0.0, 0.0))
        hero.cylinder_between("force_pz_x1", center + Vector((-0.09, 0.0, -0.09)), center + Vector((0.09, 0.0, 0.09)), 0.010, glow_mat, 12)
        hero.cylinder_between("force_pz_x2", center + Vector((-0.09, 0.0, 0.09)), center + Vector((0.09, 0.0, -0.09)), 0.010, glow_mat, 12)


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
    diag_truth = diagnostic_grid_material("diag_truth_smooth", (0.82, 0.88, 0.92, 1.0), (0.015, 0.30, 0.48, 1.0))
    diag_repair = diagnostic_grid_material("diag_repair_smooth", (0.88, 0.84, 0.79, 1.0), (0.74, 0.24, 0.055, 1.0))
    residual_field = residual_field_material("residual_field")
    common_residual_scale = four_direction_residual_scale(args.trajectory_dir / "particle_trajectories.csv")

    source = hero.import_ply(args.bunny)
    source.name = "Stanford_Bunny_Source_Hidden"
    params, rest_display = hero.orient_and_normalize_vertices(source.data)
    source.hide_render = True
    source.hide_viewport = True

    panel = args.panel
    accent = mats["orange_glow"] if panel in {"interrogate", "xray", "darkfield"} else mats["cyan_glow"]
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
        add_surface_residual_vectors(params, rest_display, groups, last, args.motion_scale, mats["cyan_glow"], mats["residual"])
        add_compact_probe(args.direction, mats["stage"], mats["orange_glow"])

    elif panel == "brightfield":
        hero.create_deformed_bunny(source, "Brightfield_Repair", params, rest_display, groups,
                                   "repair_pic", last, 0.0, args.motion_scale, mats["pearl"],
                                   animate=False)

    elif panel == "texture_truth":
        obj = hero.create_deformed_bunny(source, "Diagnostic_Truth", params, rest_display, groups,
                                         "truth", last, 0.0, args.motion_scale, diag_truth,
                                         animate=False)
        attach_rest_uv(obj, params)

    elif panel == "texture_repair":
        obj = hero.create_deformed_bunny(source, "Diagnostic_Repair", params, rest_display, groups,
                                         "repair_pic", last, 0.0, args.motion_scale, diag_repair,
                                         animate=False)
        attach_rest_uv(obj, params)

    elif panel == "darkfield":
        create_residual_field_bunny(
            source, params, rest_display, groups, last, args.motion_scale,
            residual_field, common_residual_scale, "Darkfield_Response"
        )
        add_surface_residual_vectors(params, rest_display, groups, last, args.motion_scale, mats["cyan_glow"], mats["residual"])
        add_force_glyph(args.direction, mats["orange_glow"])

    elif panel == "xray":
        create_residual_field_bunny(
            source, params, rest_display, groups, last, args.motion_scale,
            residual_field, common_residual_scale, "XRay_Response"
        )
        add_surface_residual_vectors(params, rest_display, groups, last, args.motion_scale, mats["cyan_glow"], mats["residual"])
        add_force_glyph(args.direction, mats["orange_glow"])

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

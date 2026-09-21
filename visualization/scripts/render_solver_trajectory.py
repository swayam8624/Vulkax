#!/usr/bin/env python3
"""Render actual synchronized VULKAX MPM particle trajectories as deterministic SVG.

No interpolated or invented physical state is used. The figure reads the raw solver
state exported by vulkax_visualization_trajectory_probe.
"""
from __future__ import annotations

import argparse
import csv
import html
import json
import math
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERO = ROOT / "visualization/data/hero_case_ofc_2026-09-21.json"

INK = "#111827"
MUTED = "#5b6472"
GRID = "#d8dee8"
TRUTH = "#111827"
BASELINE = "#2563eb"
REPAIR = "#ea580c"
REST = "#cbd5e1"
PANEL = "#f8fafc"


def esc(v):
    return html.escape(str(v), quote=True)


def text(x, y, value, size=20, *, fill=INK, weight=400, anchor="start"):
    return (
        f'<text x="{x:.1f}" y="{y:.1f}" font-family="Inter,Arial,sans-serif" '
        f'font-size="{size}" font-weight="{weight}" fill="{fill}" '
        f'text-anchor="{anchor}">{esc(value)}</text>'
    )


def rect(x, y, w, h, fill="none", stroke="none", rx=0, width=1):
    return (
        f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" '
        f'rx="{rx:.1f}" fill="{fill}" stroke="{stroke}" stroke-width="{width}"/>'
    )


def circle(x, y, r, fill, opacity=1.0, stroke="none", width=1):
    return (
        f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{r:.1f}" fill="{fill}" '
        f'fill-opacity="{opacity}" stroke="{stroke}" stroke-width="{width}"/>'
    )


def path(points, color, width=3, opacity=1.0):
    if not points:
        return ""
    d = "M " + " L ".join(f"{x:.1f} {y:.1f}" for x, y in points)
    return (
        f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{width}" '
        f'opacity="{opacity}" stroke-linecap="round" stroke-linejoin="round"/>'
    )


def project(x, y, z):
    # Fixed oblique projection so all four force directions remain visible.
    return (x - 0.62 * y, z + 0.34 * y)


def load_rows(path):
    rows = []
    with path.open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            rows.append({
                "direction": row["direction"],
                "model": row["model"],
                "frame": int(row["frame"]),
                "time_s": float(row["time_s"]),
                "particle_id": int(row["particle_id"]),
                "rest": (float(row["rest_x"]), float(row["rest_y"]), float(row["rest_z"])),
                "pos": (float(row["x"]), float(row["y"]), float(row["z"])),
                "is_top": int(row["is_top"]) == 1,
            })
    return rows


def centroid(rows):
    if not rows:
        raise ValueError("empty centroid")
    n = float(len(rows))
    return tuple(sum(r["pos"][i] for r in rows) / n for i in range(3))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--trajectory-dir", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    manifest = json.loads(
        (args.trajectory_dir / "trajectory_manifest.json").read_text(encoding="utf-8")
    )
    hero = json.loads(HERO.read_text(encoding="utf-8"))
    r = hero["row"]

    if manifest["hero_truth_id"] != r["truth_id"]:
        raise SystemExit("trajectory truth id does not match frozen hero row")
    checks = [
        ("baseline heldout", manifest["baseline"]["heldout_error_m"], r["baseline_holdout_m"]),
        ("repair heldout", manifest["repair"]["heldout_error_m"], r["repair_holdout_m"]),
        ("baseline target", manifest["baseline"]["target_error_m"], r["baseline_target_m"]),
        ("repair target", manifest["repair"]["target_error_m"], r["repair_target_m"]),
    ]
    for name, actual, expected in checks:
        if not math.isclose(actual, expected, rel_tol=2e-7, abs_tol=5e-13):
            raise SystemExit(f"{name} mismatch: {actual} != {expected}")

    rows = load_rows(args.trajectory_dir / "particle_trajectories.csv")
    grouped = defaultdict(list)
    for row in rows:
        grouped[(row["direction"], row["model"], row["frame"])].append(row)

    directions = ["px", "nx", "py", "pz"]
    models = ["truth", "baseline_apic", "repair_pic"]
    colors = {"truth": TRUTH, "baseline_apic": BASELINE, "repair_pic": REPAIR}
    labels = {"truth": "truth APIC", "baseline_apic": "baseline APIC", "repair_pic": "repair PIC"}
    force_labels = {"px": "+x", "nx": "−x", "py": "+y", "pz": "+z"}
    last_frame = manifest["frames_per_direction"] - 1

    # Use a single projection scale across all panels.
    projected = []
    for row in rows:
        projected.append(project(*row["rest"]))
        projected.append(project(*row["pos"]))
    umin = min(p[0] for p in projected)
    umax = max(p[0] for p in projected)
    vmin = min(p[1] for p in projected)
    vmax = max(p[1] for p in projected)
    span = max(umax - umin, vmax - vmin)
    if span <= 0:
        raise SystemExit("degenerate trajectory projection")

    summaries = {}
    with (args.trajectory_dir / "direction_summary.csv").open(newline="", encoding="utf-8") as f:
        for row in csv.DictReader(f):
            summaries[row["direction"]] = row

    W, H = 1800, 1080
    body = [
        rect(0, 0, W, H, "#ffffff"),
        text(70, 68, "VULKAX — raw solver-state Reality Inspector", 40, weight=800),
        text(
            70, 108,
            "Frozen truth world 5 • exact APIC→PIC deceptive proposal • synchronized 40 N force trajectories",
            22, fill=MUTED
        ),
        text(
            70, 140,
            "Particle positions are raw MPM solver states before synthetic observation noise; no physical motion is invented.",
            18, fill=MUTED
        ),
    ]

    panel_w, panel_h = 805, 375
    origins = [(70, 190), (925, 190), (70, 600), (925, 600)]
    for direction, (px, py) in zip(directions, origins):
        body += [
            rect(px, py, panel_w, panel_h, PANEL, GRID, 22, 2),
            text(px + 30, py + 42, f"40 N / top particle   {force_labels[direction]}", 22, weight=800),
        ]

        plot_left = px + 55
        plot_top = py + 72
        plot_w = 470
        plot_h = 245
        scale = min(plot_w, plot_h) / span * 0.82
        center_u = 0.5 * (umin + umax)
        center_v = 0.5 * (vmin + vmax)
        cx = plot_left + plot_w * 0.5
        cy = plot_top + plot_h * 0.5

        def screen(point):
            u, v = project(*point)
            return (cx + (u - center_u) * scale, cy - (v - center_v) * scale)

        # Initial rest lattice.
        initial = grouped[(direction, "truth", 0)]
        for row in initial:
            sx, sy = screen(row["rest"])
            body.append(circle(sx, sy, 3.2, REST, opacity=0.75))

        # Actual top-layer centroid trails.
        for model in models:
            trail = []
            for frame in range(last_frame + 1):
                top_rows = [q for q in grouped[(direction, model, frame)] if q["is_top"]]
                trail.append(screen(centroid(top_rows)))
            body.append(path(trail, colors[model], width=3, opacity=0.72))

        # Final actual particle state.
        for model in models:
            for row in grouped[(direction, model, last_frame)]:
                sx, sy = screen(row["pos"])
                radius = 4.6 if row["is_top"] else 3.5
                body.append(circle(sx, sy, radius, colors[model], opacity=0.72))

        summary = summaries[direction]
        eb = float(summary["baseline_raw_rms_to_truth_m"])
        er = float(summary["repair_raw_rms_to_truth_m"])
        tx = px + 555
        body += [
            text(tx, py + 102, "final raw compliance RMS", 18, fill=MUTED),
            text(tx, py + 140, f"baseline → truth   {eb:.3e} m", 19, fill=BASELINE, weight=700),
            text(tx, py + 173, f"repair → truth      {er:.3e} m", 19, fill=REPAIR, weight=700),
            text(tx, py + 223, "trajectory legend", 18, fill=MUTED),
            circle(tx + 9, py + 251, 6, TRUTH),
            text(tx + 25, py + 258, "truth APIC", 18),
            circle(tx + 9, py + 281, 6, BASELINE),
            text(tx + 25, py + 288, "baseline APIC", 18),
            circle(tx + 9, py + 311, 6, REPAIR),
            text(tx + 25, py + 318, "repair PIC", 18),
        ]

    body += [
        rect(200, 1000, 1400, 54, "#eef2ff", "#c7d2fe", 15, 1),
        text(
            900, 1034,
            "This surface visualizes replayed frozen solver dynamics; scientific decision values remain those in the immutable evidence ledger.",
            18, anchor="middle", weight=650
        ),
    ]

    svg = "\n".join([
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">',
        "<title>VULKAX raw solver-state force trajectories</title>",
        *body,
        "</svg>",
        "",
    ])
    args.out.mkdir(parents=True, exist_ok=True)
    out = args.out / "fig_solver_state_force_trajectories.svg"
    out.write_text(svg, encoding="utf-8")
    print(out)


if __name__ == "__main__":
    main()

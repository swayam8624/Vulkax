#!/usr/bin/env python3
"""Measure whether GAUGE foam-shearing terminal markers support Vulkax's clamp model.

This is a measured-data diagnostic only. It does not fit material parameters and it
does not alter the forward model. The two terminal marker edges are derived from the
released triangular marker topology and initial geometry. For each trial we test both
possible fixed/moving end assignments and select the assignment that best matches the
released fixed-base / moving-base semantics using measured trajectories only.
"""
import argparse
import csv
import json
import math
import pathlib
import statistics
from collections import Counter


MM_TO_M = 1.0e-3


def v3(series, i):
    return tuple(float(series[a][i]) * MM_TO_M for a in ("x", "y", "z"))


def sub(a, b):
    return tuple(a[k] - b[k] for k in range(3))


def add(a, b):
    return tuple(a[k] + b[k] for k in range(3))


def scale(a, s):
    return tuple(x * s for x in a)


def norm(a):
    return math.sqrt(sum(x * x for x in a))


def rms(values):
    values = list(values)
    return math.sqrt(statistics.fmean(v * v for v in values)) if values else 0.0


def dedup_faces(metadata):
    seen = set()
    out = []
    for face in metadata["markers"]["faces"]:
        key = tuple(int(x) for x in face)
        if key not in seen:
            seen.add(key)
            out.append(key)
    return out


def terminal_edges(metadata, trial):
    """Return two topology-derived terminal marker-id pairs and the long axis."""
    marker_ids = sorted(trial["foam"])
    initial = [v3(trial["foam"][mid], 0) for mid in marker_ids]
    lo = [min(p[a] for p in initial) for a in range(3)]
    hi = [max(p[a] for p in initial) for a in range(3)]
    long_axis = max(range(3), key=lambda a: hi[a] - lo[a])

    edge_counts = Counter()
    for i, j, k in dedup_faces(metadata):
        for a, b in ((i, j), (j, k), (k, i)):
            edge_counts[tuple(sorted((a, b)))] += 1
    boundary = [edge for edge, count in edge_counts.items() if count == 1]
    if not boundary:
        raise RuntimeError("marker topology contains no boundary edges")

    def edge_coordinate(edge):
        return statistics.fmean(initial[i][long_axis] for i in edge)

    low = min(boundary, key=edge_coordinate)
    high = max(boundary, key=edge_coordinate)
    if low == high:
        raise RuntimeError("failed to identify distinct terminal marker edges")
    return (
        tuple(marker_ids[i] for i in low),
        tuple(marker_ids[i] for i in high),
        long_axis,
    )


def mean_marker_displacement(trial, marker_ids, frame):
    disp = []
    for mid in marker_ids:
        p0 = v3(trial["foam"][mid], 0)
        pt = v3(trial["foam"][mid], frame)
        disp.append(sub(pt, p0))
    total = (0.0, 0.0, 0.0)
    for d in disp:
        total = add(total, d)
    return scale(total, 1.0 / len(disp)), disp


def trial_metrics(metadata, path, material, trial_number):
    trial = json.loads(path.read_text())
    if trial.get("translation unit") != "mm":
        raise RuntimeError(f"expected mm trajectories: {path}")
    if "base" not in trial:
        raise RuntimeError(f"GAUGE shearing trial has no base trajectory: {path}")

    low_ids, high_ids, long_axis = terminal_edges(metadata, trial)
    base = trial["base"]
    marker_frames = min(
        len(trial["foam"][mid][a])
        for mid in trial["foam"]
        for a in ("x", "y", "z")
    )
    base_frames = min(len(base[a]) for a in ("x", "y", "z"))
    frames = min(marker_frames, base_frames)
    b0 = v3(base, 0)
    driver = [sub(v3(base, i), b0) for i in range(frames)]
    driver_peak = max(norm(d) for d in driver)
    if not (driver_peak > 0.0):
        raise RuntimeError(f"zero driver motion: {path}")

    end = {}
    for name, ids in (("low", low_ids), ("high", high_ids)):
        means = []
        nonrigid = []
        for frame in range(frames):
            mean_disp, individual = mean_marker_displacement(trial, ids, frame)
            means.append(mean_disp)
            for d in individual:
                nonrigid.append(norm(sub(d, mean_disp)))
        end[name] = {
            "ids": list(ids),
            "mean_displacements": means,
            "within_edge_nonrigid_rms_m": rms(nonrigid),
        }

    assignments = []
    for fixed_name, moving_name in (("low", "high"), ("high", "low")):
        fixed = end[fixed_name]["mean_displacements"]
        moving = end[moving_name]["mean_displacements"]
        fixed_rms = rms(norm(d) for d in fixed)
        moving_residual = rms(norm(sub(moving[i], driver[i])) for i in range(frames))
        score = fixed_rms + moving_residual
        assignments.append((score, fixed_name, moving_name, fixed_rms, moving_residual))
    _, fixed_name, moving_name, fixed_rms, moving_residual = min(assignments)

    moving = end[moving_name]["mean_displacements"]
    moving_peak = max(norm(d) for d in moving)
    peak_frame = max(range(frames), key=lambda i: norm(driver[i]))
    d_peak = driver[peak_frame]
    d_norm = norm(d_peak)
    direction = scale(d_peak, 1.0 / d_norm)
    projected_driver = [sum(d[k] * direction[k] for k in range(3)) for d in driver]
    projected_moving = [sum(d[k] * direction[k] for k in range(3)) for d in moving]
    mean_x = statistics.fmean(projected_driver)
    mean_y = statistics.fmean(projected_moving)
    num = sum((x - mean_x) * (y - mean_y) for x, y in zip(projected_driver, projected_moving))
    denx = math.sqrt(sum((x - mean_x) ** 2 for x in projected_driver))
    deny = math.sqrt(sum((y - mean_y) ** 2 for y in projected_moving))
    corr = num / (denx * deny) if denx > 0.0 and deny > 0.0 else 0.0

    return {
        "material": material,
        "trial": trial_number,
        "frames": frames,
        "long_axis": "xyz"[long_axis],
        "fixed_end": fixed_name,
        "moving_end": moving_name,
        "fixed_marker_ids": end[fixed_name]["ids"],
        "moving_marker_ids": end[moving_name]["ids"],
        "driver_peak_m": driver_peak,
        "moving_end_peak_m": moving_peak,
        "moving_end_peak_gain": moving_peak / driver_peak,
        "fixed_end_rms_m": fixed_rms,
        "fixed_end_rms_fraction_of_driver_peak": fixed_rms / driver_peak,
        "moving_end_driver_residual_rms_m": moving_residual,
        "moving_end_driver_residual_fraction_of_driver_peak": moving_residual / driver_peak,
        "fixed_end_nonrigid_rms_m": end[fixed_name]["within_edge_nonrigid_rms_m"],
        "moving_end_nonrigid_rms_m": end[moving_name]["within_edge_nonrigid_rms_m"],
        "moving_driver_direction_correlation": corr,
    }


def summarize(rows):
    by_material = {}
    for material in ("soft", "hard"):
        group = [r for r in rows if r["material"] == material]
        by_material[material] = {
            "trials": len(group),
            "fixed_end_assignment_counts": dict(Counter(r["fixed_end"] for r in group)),
            "moving_end_assignment_counts": dict(Counter(r["moving_end"] for r in group)),
            "long_axis_counts": dict(Counter(r["long_axis"] for r in group)),
            "fixed_end_rms_fraction_mean": statistics.fmean(
                r["fixed_end_rms_fraction_of_driver_peak"] for r in group
            ),
            "fixed_end_rms_fraction_max": max(
                r["fixed_end_rms_fraction_of_driver_peak"] for r in group
            ),
            "moving_residual_fraction_mean": statistics.fmean(
                r["moving_end_driver_residual_fraction_of_driver_peak"] for r in group
            ),
            "moving_residual_fraction_max": max(
                r["moving_end_driver_residual_fraction_of_driver_peak"] for r in group
            ),
            "moving_peak_gain_mean": statistics.fmean(r["moving_end_peak_gain"] for r in group),
            "moving_driver_correlation_mean": statistics.fmean(
                r["moving_driver_direction_correlation"] for r in group
            ),
            "fixed_nonrigid_rms_mm_mean": 1000.0 * statistics.fmean(
                r["fixed_end_nonrigid_rms_m"] for r in group
            ),
            "moving_nonrigid_rms_mm_mean": 1000.0 * statistics.fmean(
                r["moving_end_nonrigid_rms_m"] for r in group
            ),
        }
    return by_material


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    root = pathlib.Path(args.root)
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    metadata = json.loads((root / "metadata" / "foam shearing.json").read_text())

    rows = []
    for material in ("soft", "hard"):
        for trial in range(1, 11):
            path = root / "data" / "foam shearing" / material / f"{trial}.json"
            rows.append(trial_metrics(metadata, path, material, trial))

    fieldnames = list(rows[0].keys())
    with (out / "trials.csv").open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            serial = {
                k: json.dumps(v) if isinstance(v, list) else v
                for k, v in row.items()
            }
            writer.writerow(serial)

    result = {
        "schema": "vulkax.gauge_shearing_boundary_attachment",
        "version": 1,
        "provenance": "measured-only",
        "fit_performed": False,
        "simulation_performed": False,
        "trials": len(rows),
        "method": {
            "terminal_edges": "two extreme boundary edges derived from released marker triangle topology",
            "assignment": "choose fixed/moving geometric end using measured fixed-stasis + moving-base residual only",
            "driver": "released GAUGE base translation relative to frame 0",
        },
        "materials": summarize(rows),
        "interpretation_guard": (
            "This diagnostic measures whether terminal marker motion is compatible with a hard translational "
            "clamp. It does not prove contact mechanics, identify material parameters, or authorize inverse fitting."
        ),
    }
    (out / "summary.json").write_text(json.dumps(result, indent=2) + "\n")
    print("VALID GAUGE boundary-attachment diagnostic")
    for material, stats in result["materials"].items():
        print(
            "ATTACHMENT",
            material,
            "fixed_frac_mean", stats["fixed_end_rms_fraction_mean"],
            "moving_resid_frac_mean", stats["moving_residual_fraction_mean"],
            "moving_gain_mean", stats["moving_peak_gain_mean"],
            "corr_mean", stats["moving_driver_correlation_mean"],
            "fixed_nonrigid_mm", stats["fixed_nonrigid_rms_mm_mean"],
            "moving_nonrigid_mm", stats["moving_nonrigid_rms_mm_mean"],
        )


if __name__ == "__main__":
    main()

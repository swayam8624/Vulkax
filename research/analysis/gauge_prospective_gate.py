#!/usr/bin/env python3
"""Prospective controlled Reality Probe gate on untouched GAUGE foam trials.

This runner deliberately excludes the already-analyzed GAUGE shearing task.
Stretching and compression are partitioned before execution:
  development: trials 1-4
  validation:  trials 5-7
  final_test:  trials 8-10

For each measured world, the experiment compares a baseline material model with a
proposed rewrite while keeping geometry, boundary motion, density, mass, Poisson
ratio, solver, timestep and observation channel fixed.

Controlled truth:
- SUPPORT: baseline E is wrong; proposal restores independently measured GAUGE E.
- VETO: baseline uses measured E; proposal changes E away from measured truth.
- UNRESOLVED/placebo: baseline and proposal are identical.

The Reality Probe score uses a lower-order-annihilating three-time response
(start/mid/end second difference). Two transparent baselines use the same marker
budget: raw three-frame residual and full-trajectory residual.

No parameter is fit to measured marker error.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
import shutil
import statistics
import subprocess
import sys
import tempfile
from typing import Iterable

from gauge_fixture_overlap_heldout import (
    measured_frames,
    read_pred,
    run_arm,
    write_inputs,
)
from gauge_dcs_retrospective import select_mid_frame

THRESHOLD = 2.0
TASKS = {
    "foam stretching": "foam stretching",
    "foam compression": "foam compressing",
}
SPLIT_TRIALS = {
    "development": (1, 2, 3, 4),
    "validation": (5, 6, 7),
    "final_test": (8, 9, 10),
}
METHODS = (
    "reality_probe_dcs",
    "reality_probe_rigid_invariant_strain",
    "same_cost_rigid_aligned_bundle",
    "same_cost_raw_bundle",
    "simple_residual_or_uncertainty_baseline",
)
RECORD_FIELDS = [
    "record_version", "trial_id", "paired_key", "dataset", "scene", "split",
    "evidence_class", "confirmatory", "trial_family", "ground_truth", "method",
    "score", "decision", "decision_threshold", "confidence", "physical_delta",
    "target_error_delta", "measurement_noise_sigma", "pose_noise_sigma",
    "missing_fraction", "channel_dependence", "negative_control", "seed",
    "source_artifact", "notes",
]


def vec_sub(a: list[float], b: list[float]) -> list[float]:
    return [a[i] - b[i] for i in range(3)]


def norm(a: list[float]) -> float:
    return math.sqrt(sum(x * x for x in a))


def second_difference(
    p0: list[float], pm: list[float], p1: list[float]
) -> list[float]:
    return [p1[i] - 2.0 * pm[i] + p0[i] for i in range(3)]


def decision(score: float) -> str:
    if score >= THRESHOLD:
        return "support"
    if score <= -THRESHOLD:
        return "veto"
    return "unresolved"


def self_normalized_score(deltas: list[float]) -> float:
    """Signed bounded evidence statistic.

    sqrt(n) * mean(delta) / RMS(delta) is bounded by sqrt(n), preserves sign,
    remains finite for equal effects, and is exactly zero for a placebo.
    Positive means the proposal reduced probe error.
    """
    if not deltas:
        raise ValueError("cannot score an empty evidence vector")
    m = statistics.fmean(deltas)
    rms = math.sqrt(statistics.fmean(x * x for x in deltas))
    if rms <= 1.0e-18:
        return 0.0
    score = math.sqrt(len(deltas)) * m / rms
    if not math.isfinite(score):
        raise ValueError("non-finite evidence score")
    return score


def mst_edges(
    rest: dict[str, list[float]], ids: list[str]
) -> list[tuple[str, str]]:
    """Deterministic Euclidean MST over rest markers.

    The MST limits the evidence vector to n-1 local geometric constraints rather
    than treating all O(n^2) pair distances as independent evidence.
    """
    ordered = sorted(ids)
    if len(ordered) < 2:
        raise ValueError("rigid-invariant strain probe needs at least two markers")
    used = {ordered[0]}
    edges: list[tuple[str, str]] = []
    while len(used) < len(ordered):
        best: tuple[float, str, str] | None = None
        for a in sorted(used):
            for b in ordered:
                if b in used:
                    continue
                d = norm(vec_sub(rest[a], rest[b]))
                if d <= 1.0e-12:
                    continue
                candidate = (d, a, b)
                if best is None or candidate < best:
                    best = candidate
        if best is None:
            raise ValueError("marker rest geometry cannot form a non-degenerate MST")
        _, a, b = best
        edges.append((a, b))
        used.add(b)
    return edges


def apply_rotation_translation(
    p: list[float], rotation: list[list[float]], translation: list[float]
) -> list[float]:
    return [
        sum(rotation[i][j] * p[j] for j in range(3)) + translation[i]
        for i in range(3)
    ]


def verification_rigid_nuisance(
    frames: dict[int, dict[str, list[float]]],
    rotation_degrees: float,
    translation_m: float,
) -> dict[int, dict[str, list[float]]]:
    if abs(rotation_degrees) <= 1.0e-15 and abs(translation_m) <= 1.0e-15:
        return frames
    axis = [1.0, 2.0, 3.0]
    axis_norm = math.sqrt(sum(x * x for x in axis))
    x, y, z = [v / axis_norm for v in axis]
    a = math.radians(rotation_degrees)
    co, si, om = math.cos(a), math.sin(a), 1.0 - math.cos(a)
    rotation = [
        [co + x*x*om, x*y*om - z*si, x*z*om + y*si],
        [y*x*om + z*si, co + y*y*om, y*z*om - x*si],
        [z*x*om - y*si, z*y*om + x*si, co + z*z*om],
    ]
    # Fixed non-axis-aligned direction avoids a special relationship with any one task.
    td = [0.37, -0.61, 0.70]
    td_norm = math.sqrt(sum(v * v for v in td))
    translation = [translation_m * v / td_norm for v in td]
    return {
        frame: {
            marker: apply_rotation_translation(p, rotation, translation)
            for marker, p in markers.items()
        }
        for frame, markers in frames.items()
    }


def horn_rigid_alignment(
    source: list[list[float]], target: list[list[float]]
) -> tuple[list[list[float]], list[float]]:
    """Least-squares proper rigid transform source -> target (Horn quaternion)."""
    if len(source) != len(target) or len(source) < 3:
        raise ValueError("rigid alignment needs >=3 paired points")
    n = len(source)
    cs = [statistics.fmean(p[i] for p in source) for i in range(3)]
    ct = [statistics.fmean(p[i] for p in target) for i in range(3)]
    s = [[0.0] * 3 for _ in range(3)]
    for p, q in zip(source, target):
        px = [p[i] - cs[i] for i in range(3)]
        qx = [q[i] - ct[i] for i in range(3)]
        for i in range(3):
            for j in range(3):
                s[i][j] += px[i] * qx[j]
    sxx, sxy, sxz = s[0]
    syx, syy, syz = s[1]
    szx, szy, szz = s[2]
    nmat = [
        [sxx+syy+szz, syz-szy, szx-sxz, sxy-syx],
        [syz-szy, sxx-syy-szz, sxy+syx, szx+sxz],
        [szx-sxz, sxy+syx, -sxx+syy-szz, syz+szy],
        [sxy-syx, szx+sxz, syz+szy, -sxx-syy+szz],
    ]
    quat = [1.0, 0.2, 0.3, 0.4]
    for _ in range(128):
        nxt = [sum(nmat[i][j] * quat[j] for j in range(4)) for i in range(4)]
        qn = math.sqrt(sum(v * v for v in nxt))
        if qn <= 1.0e-18:
            raise ValueError("degenerate rigid alignment")
        nxt = [v / qn for v in nxt]
        if sum((nxt[i]-quat[i])**2 for i in range(4)) < 1.0e-28:
            quat = nxt
            break
        quat = nxt
    qw, qx, qy, qz = quat
    rotation = [
        [1-2*(qy*qy+qz*qz), 2*(qx*qy-qw*qz), 2*(qx*qz+qw*qy)],
        [2*(qx*qy+qw*qz), 1-2*(qx*qx+qz*qz), 2*(qy*qz-qw*qx)],
        [2*(qx*qz-qw*qy), 2*(qy*qz+qw*qx), 1-2*(qx*qx+qy*qy)],
    ]
    translation = [
        ct[i] - sum(rotation[i][j] * cs[j] for j in range(3))
        for i in range(3)
    ]
    return rotation, translation


def rigid_aligned_three_frame_errors(
    measured: dict[int, dict[str, list[float]]],
    predicted: dict[int, dict[str, list[float]]],
    ids: list[str],
    mid: int,
    n: int,
) -> list[float]:
    common = min(n, len(predicted), len(measured))
    last = common - 1
    mid = min(max(1, mid), last - 1)
    per_marker: dict[str, list[float]] = {m: [] for m in ids}
    for frame in (0, mid, last):
        src = [predicted[frame][m] for m in ids]
        dst = [measured[frame][m] for m in ids]
        rotation, translation = horn_rigid_alignment(src, dst)
        for marker in ids:
            aligned = apply_rotation_translation(
                predicted[frame][marker], rotation, translation
            )
            per_marker[marker].append(
                norm(vec_sub(aligned, measured[frame][marker]))
            )
    return [
        math.sqrt(statistics.fmean(e * e for e in per_marker[m]))
        for m in ids
    ]


def rigid_invariant_strain_errors(
    measured: dict[int, dict[str, list[float]]],
    predicted: dict[int, dict[str, list[float]]],
    ids: list[str],
    mid: int,
    n: int,
) -> list[float]:
    """Per-MST-edge strain mismatch at frozen mid/final probe times.

    Relative pair length is invariant to global translation and rotation, so this
    selectively cancels rigid nuisance modes without annihilating stretch.
    """
    common = min(n, len(predicted), len(measured))
    if common < 3:
        raise RuntimeError("too few common frames for invariant strain probe")
    last = common - 1
    mid = min(max(1, mid), last - 1)
    edges = mst_edges(measured[0], ids)
    out: list[float] = []
    for a, b in edges:
        d0 = norm(vec_sub(measured[0][a], measured[0][b]))
        if d0 <= 1.0e-12:
            raise RuntimeError("degenerate rest edge in invariant strain probe")
        residuals = []
        for frame in (mid, last):
            dm = norm(vec_sub(measured[frame][a], measured[frame][b]))
            dp = norm(vec_sub(predicted[frame][a], predicted[frame][b]))
            measured_strain = (dm - d0) / d0
            predicted_strain = (dp - d0) / d0
            residuals.append(predicted_strain - measured_strain)
        out.append(math.sqrt(statistics.fmean(x * x for x in residuals)))
    return out


def marker_error_vectors(
    measured: dict[int, dict[str, list[float]]],
    predicted: dict[int, dict[str, list[float]]],
    ids: list[str],
    mid: int,
    n: int,
) -> dict[str, list[float]]:
    common = min(n, len(predicted), len(measured))
    if common < 3:
        raise RuntimeError("too few common frames")
    last = common - 1
    mid = min(max(1, mid), last - 1)

    dcs: list[float] = []
    raw3: list[float] = []
    full: list[float] = []
    for marker in ids:
        for f in (0, mid, last):
            if marker not in measured[f] or marker not in predicted[f]:
                raise RuntimeError(f"marker mismatch at frame {f}: {marker}")

        md = second_difference(
            measured[0][marker], measured[mid][marker], measured[last][marker]
        )
        pd = second_difference(
            predicted[0][marker], predicted[mid][marker], predicted[last][marker]
        )
        dcs.append(norm(vec_sub(pd, md)))

        raw_e = [
            norm(vec_sub(predicted[f][marker], measured[f][marker]))
            for f in (0, mid, last)
        ]
        raw3.append(math.sqrt(statistics.fmean(x * x for x in raw_e)))

        full_e = []
        for f in range(common):
            if marker not in measured[f] or marker not in predicted[f]:
                raise RuntimeError(f"marker mismatch at frame {f}: {marker}")
            full_e.append(norm(vec_sub(predicted[f][marker], measured[f][marker])))
        full.append(math.sqrt(statistics.fmean(x * x for x in full_e)))

    return {
        "reality_probe_dcs": dcs,
        "reality_probe_rigid_invariant_strain": rigid_invariant_strain_errors(
            measured, predicted, ids, mid, n
        ),
        "same_cost_rigid_aligned_bundle": rigid_aligned_three_frame_errors(
            measured, predicted, ids, mid, n
        ),
        "same_cost_raw_bundle": raw3,
        "simple_residual_or_uncertainty_baseline": full,
    }


def factor_key(factor: float) -> str:
    return ("%.6f" % factor).rstrip("0").rstrip(".").replace(".", "p")


def load_material(root: Path, task: str, material: str) -> tuple[Path, dict]:
    # fetch_gauge_foam_subset.py preserves the data-task spelling locally:
    # upstream metadata "foam compressing.json" -> local "foam compression.json".
    metadata_path = root / "metadata" / f"{task}.json"
    md = json.loads(metadata_path.read_text(encoding="utf-8"))
    mat = dict(md["assets"]["foam"]["material"][material])
    return metadata_path, mat


def material_with_factor(mat: dict, factor: float) -> dict:
    out = dict(mat)
    out["young"] = float(mat["young"]) * factor
    return out


def run_candidate(
    *,
    exe: Path,
    marker_path: Path,
    driver_path: Path,
    cache_dir: Path,
    mat: dict,
    factor: float,
    dt: float,
    n_cross: int,
    n_long: int,
    label: str,
) -> tuple[Path, dict]:
    cache_dir.mkdir(parents=True, exist_ok=True)
    pred = cache_dir / f"pred_Efactor_{factor_key(factor)}.csv"
    summary = pred.parent / (pred.stem + "_summary.json")
    if pred.is_file() and summary.is_file():
        return pred, json.loads(summary.read_text(encoding="utf-8"))

    candidate = material_with_factor(mat, factor)
    evidence = run_arm(
        str(exe),
        marker_path,
        driver_path,
        pred,
        candidate,
        dt,
        n_cross,
        n_long,
        label,
    )
    return pred, evidence


def pair_records(
    *,
    scene: str,
    split: str,
    family: str,
    truth: str,
    baseline_factor: float,
    proposal_factor: float,
    baseline_errors: dict[str, list[float]],
    proposal_errors: dict[str, list[float]],
    true_young: float,
    source_artifact: str,
    notes: str,
) -> list[dict[str, object]]:
    pair = (
        f"gauge:{scene}:{family}:"
        f"{factor_key(baseline_factor)}->{factor_key(proposal_factor)}"
    )
    records = []
    full_delta = (
        statistics.fmean(proposal_errors["simple_residual_or_uncertainty_baseline"])
        - statistics.fmean(baseline_errors["simple_residual_or_uncertainty_baseline"])
    )
    physical_delta = proposal_factor - baseline_factor
    for method in METHODS:
        deltas = [
            b - p for b, p in zip(baseline_errors[method], proposal_errors[method])
        ]
        score = self_normalized_score(deltas)
        records.append({
            "record_version": 1,
            "trial_id": f"{pair}:{method}",
            "paired_key": pair,
            "dataset": "gauge_prospective",
            "scene": scene,
            "split": split,
            "evidence_class": "prospective_measured_gauge_controlled_rewrite",
            "confirmatory": split == "final_test",
            "trial_family": family,
            "ground_truth": truth,
            "method": method,
            "score": f"{score:.17g}",
            "decision": decision(score),
            "decision_threshold": THRESHOLD,
            "confidence": "",
            "physical_delta": f"{physical_delta:.17g}",
            "target_error_delta": f"{full_delta:.17g}",
            "measurement_noise_sigma": "",
            "pose_noise_sigma": "",
            "missing_fraction": "",
            "channel_dependence": "0",
            "negative_control": family == "placebo",
            "seed": 0,
            "source_artifact": source_artifact,
            "notes": (
                f"{notes}; true_E_pa={true_young:.17g}; "
                f"baseline_E_factor={baseline_factor:.17g}; "
                f"proposal_E_factor={proposal_factor:.17g}"
            ),
        })
    return records


def selected_factors(profile: str) -> list[float]:
    if profile == "truth":
        return [2.0]
    if profile == "dose":
        return [1.025, 1.05, 1.10, 1.20, 2.0]
    raise ValueError(profile)


def write_records(path: Path, rows: list[dict[str, object]]) -> None:
    ids = [str(r["trial_id"]) for r in rows]
    if len(ids) != len(set(ids)):
        raise RuntimeError("duplicate validation record id")
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=RECORD_FIELDS)
        w.writeheader()
        for row in rows:
            w.writerow({k: row.get(k, "") for k in RECORD_FIELDS})


def summarize(rows: list[dict[str, object]], worlds: list[dict]) -> dict:
    by_method: dict[str, dict[str, int]] = {}
    for method in METHODS:
        subset = [r for r in rows if r["method"] == method]
        by_method[method] = {
            "records": len(subset),
            "correct_three_way": sum(r["decision"] == r["ground_truth"] for r in subset),
            "support_truth": sum(r["ground_truth"] == "support" for r in subset),
            "support_called": sum(
                r["ground_truth"] == "support" and r["decision"] == "support"
                for r in subset
            ),
            "veto_truth": sum(r["ground_truth"] == "veto" for r in subset),
            "veto_called": sum(
                r["ground_truth"] == "veto" and r["decision"] == "veto"
                for r in subset
            ),
            "unresolved_truth": sum(r["ground_truth"] == "unresolved" for r in subset),
            "unresolved_called": sum(
                r["ground_truth"] == "unresolved" and r["decision"] == "unresolved"
                for r in subset
            ),
        }
    return {
        "schema": "vulkax.gauge_prospective_gate",
        "version": 1,
        "provenance": "prospective-controlled-rewrite-on-measured-GAUGE-stretch-compression",
        "confirmatory_evidence": any(bool(r["confirmatory"]) for r in rows),
        "world_count": len(worlds),
        "record_count": len(rows),
        "methods": by_method,
        "worlds": worlds,
        "guardrails": [
            "GAUGE shearing is excluded because it was already inspected retrospectively.",
            "No material parameter is fit to marker trajectory error.",
            "Young's modulus truth comes from independently released GAUGE metadata.",
            "Proposal and verification channels are separated by construction: controlled rewrite vs measured marker response.",
            "Final-test trials 8-10 require an explicit final-test lock.",
        ],
    }


def self_test() -> None:
    ids = ["m0", "m1", "m2", "m3"]
    rest = {
        "m0": [0.0, 0.0, 0.0],
        "m1": [0.0, 1.0, 0.0],
        "m2": [0.8, 0.2, 0.3],
        "m3": [0.2, 0.3, 0.9],
    }

    def good_frame(dx: float) -> dict[str, list[float]]:
        return {m: [p[0] + dx, p[1], p[2]] for m, p in rest.items()}

    measured = {
        0: good_frame(0.0),
        1: good_frame(0.5),
        2: good_frame(1.2),
    }
    good = {
        f: {m: list(p) for m, p in frame.items()}
        for f, frame in measured.items()
    }
    bad = {
        0: good_frame(0.0),
        1: {
            "m0": [0.2, 0.0, 0.0],
            "m1": [0.45, 1.15, 0.0],
            "m2": [1.05, 0.28, 0.38],
            "m3": [0.55, 0.38, 1.02],
        },
        2: {
            "m0": [0.7, 0.0, 0.0],
            "m1": [1.10, 1.35, 0.0],
            "m2": [1.70, 0.38, 0.52],
            "m3": [1.18, 0.48, 1.18],
        },
    }
    g = marker_error_vectors(measured, good, ids, 1, 3)
    b = marker_error_vectors(measured, bad, ids, 1, 3)
    for method in METHODS:
        support = self_normalized_score([x - y for x, y in zip(b[method], g[method])])
        veto = self_normalized_score([x - y for x, y in zip(g[method], b[method])])
        placebo = self_normalized_score([x - x for x in g[method]])
        assert support > 0.0, method
        assert veto < 0.0, method
        assert placebo == 0.0, method

    assert SPLIT_TRIALS["final_test"] == (8, 9, 10)

    # Horn alignment must remove a pure, non-trivial global rigid transform.
    src = [[0,0,0],[1,0,0],[0,1,0],[0,0,1],[1,1,0.2]]
    src_frames = {
        0:{str(i):list(p) for i,p in enumerate(src)},
        1:{str(i):list(p) for i,p in enumerate(src)},
        2:{str(i):list(p) for i,p in enumerate(src)},
    }
    dst_frames = verification_rigid_nuisance(src_frames, 17.0, 0.023)
    e = rigid_aligned_three_frame_errors(
        dst_frames, src_frames, [str(i) for i in range(len(src))], 1, 3
    )
    assert max(e) < 1.0e-9

    # Rigid-invariant strain must also be unchanged by that global transform.
    invariant = rigid_invariant_strain_errors(
        dst_frames, src_frames, [str(i) for i in range(len(src))], 1, 3
    )
    assert max(invariant) < 1.0e-12

    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        (root / "metadata").mkdir()
        payload = {"assets": {"foam": {"material": {"soft": {"young": 123.0}}}}}
        (root / "metadata" / "foam compression.json").write_text(
            json.dumps(payload), encoding="utf-8"
        )
        path, mat = load_material(root, "foam compression", "soft")
        assert path.name == "foam compression.json"
        assert float(mat["young"]) == 123.0
    print("VALID GAUGE prospective-gate self-test")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=Path("build/gauge-download"))
    ap.add_argument("--exe", type=Path, default=Path("build/vulkax_gauge_shearing_forward_probe"))
    ap.add_argument("--out", type=Path, default=Path("build/gauge-prospective-gate"))
    ap.add_argument("--split", choices=tuple(SPLIT_TRIALS), default="development")
    ap.add_argument("--profile", choices=("truth", "dose"), default="truth")
    ap.add_argument("--mode", choices=("pilot", "definitive"), default="pilot")
    ap.add_argument("--final-lock", type=Path)
    ap.add_argument("--max-worlds", type=int)
    ap.add_argument("--shard-index", type=int, default=0)
    ap.add_argument("--shard-count", type=int, default=1)
    ap.add_argument("--verification-rotation-deg", type=float, default=0.0)
    ap.add_argument("--verification-translation-m", type=float, default=0.0)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        self_test()
        return 0

    if args.shard_count < 1:
        raise SystemExit("--shard-count must be >= 1")
    if not (0 <= args.shard_index < args.shard_count):
        raise SystemExit("--shard-index must satisfy 0 <= index < count")

    if args.split == "final_test":
        if args.final_lock is None:
            raise SystemExit("final_test requires --final-lock")
        subprocess.run(
            [
                sys.executable,
                "research/analysis/freeze_publication_validation.py",
                "--check",
                str(args.final_lock),
            ],
            check=True,
        )

    if not args.exe.is_file():
        raise SystemExit(f"missing forward executable: {args.exe}")
    if args.mode == "definitive":
        n_cross, n_long, dt = 7, 49, 1.0 / 48000.0
    else:
        n_cross, n_long, dt = 5, 25, 1.0 / 24000.0

    factors = selected_factors(args.profile)
    worlds: list[dict] = []
    rows: list[dict[str, object]] = []
    processed = 0
    world_ordinal = 0

    with tempfile.TemporaryDirectory(prefix="vulkax-gauge-prospective-") as td:
        tmp = Path(td)
        for task in TASKS:
            for material in ("soft", "hard"):
                metadata_path, mat = load_material(args.root, task, material)
                true_young = float(mat["young"])
                for trial in SPLIT_TRIALS[args.split]:
                    ordinal = world_ordinal
                    world_ordinal += 1
                    if ordinal % args.shard_count != args.shard_index:
                        continue
                    if args.max_worlds is not None and processed >= args.max_worlds:
                        break
                    raw_path = args.root / "data" / task / material / f"{trial}.json"
                    if not raw_path.is_file():
                        raise FileNotFoundError(raw_path)
                    data = json.loads(raw_path.read_text(encoding="utf-8"))
                    marker = tmp / f"{task}_{material}_{trial}_markers.csv"
                    driver = tmp / f"{task}_{material}_{trial}_driver.csv"
                    ids, n = write_inputs(data, marker, driver)
                    mid, mid_progress, max_orth = select_mid_frame(data, n)
                    measured = measured_frames(data, ids, n)
                    measured_eval = verification_rigid_nuisance(
                        measured,
                        args.verification_rotation_deg,
                        args.verification_translation_m,
                    )
                    scene = f"{task}/{material}/{trial:02d}"
                    cache = args.out / "simulations" / task.replace(" ", "_") / material / f"{trial:02d}"

                    prediction_errors: dict[float, dict[str, list[float]]] = {}
                    solver_evidence: dict[str, dict] = {}
                    needed = sorted(set([1.0, *factors]))
                    for factor in needed:
                        pred_path, evidence = run_candidate(
                            exe=args.exe,
                            marker_path=marker,
                            driver_path=driver,
                            cache_dir=cache,
                            mat=mat,
                            factor=factor,
                            dt=dt,
                            n_cross=n_cross,
                            n_long=n_long,
                            label=f"prospective-{task}-{material}-{trial}-E{factor_key(factor)}",
                        )
                        predicted = read_pred(pred_path)
                        prediction_errors[factor] = marker_error_vectors(
                            measured_eval, predicted, ids, mid, n
                        )
                        solver_evidence[factor_key(factor)] = {
                            "minimum_J": evidence.get("minimum_J"),
                            "maximum_J": evidence.get("maximum_J"),
                            "maximum_momentum_accounting_error": evidence.get(
                                "maximum_momentum_accounting_error"
                            ),
                            "prediction": str(pred_path),
                        }

                    base_notes = (
                        f"task={task}; material={material}; trial={trial}; "
                        f"metadata={metadata_path}; mid_frame={mid}; "
                        f"mid_progress={mid_progress:.9g}; driver_max_orthogonal_fraction={max_orth:.9g}; "
                        f"numerics={n_cross}x{n_cross}x{n_long},dt={dt:.17g}; "
                        f"verification_rotation_deg={args.verification_rotation_deg:.17g}; "
                        f"verification_translation_m={args.verification_translation_m:.17g}"
                    )

                    # Placebo: identical physical hypothesis, so asserting support/veto is a failure.
                    rows += pair_records(
                        scene=scene,
                        split=args.split,
                        family="placebo",
                        truth="unresolved",
                        baseline_factor=1.0,
                        proposal_factor=1.0,
                        baseline_errors=prediction_errors[1.0],
                        proposal_errors=prediction_errors[1.0],
                        true_young=true_young,
                        source_artifact=str(cache),
                        notes=base_notes,
                    )

                    # Strong controlled truth pair.
                    strong = 2.0
                    if strong in prediction_errors:
                        rows += pair_records(
                            scene=scene,
                            split=args.split,
                            family="truth_control",
                            truth="support",
                            baseline_factor=strong,
                            proposal_factor=1.0,
                            baseline_errors=prediction_errors[strong],
                            proposal_errors=prediction_errors[1.0],
                            true_young=true_young,
                            source_artifact=str(cache),
                            notes=base_notes,
                        )
                        rows += pair_records(
                            scene=scene,
                            split=args.split,
                            family="truth_control",
                            truth="veto",
                            baseline_factor=1.0,
                            proposal_factor=strong,
                            baseline_errors=prediction_errors[1.0],
                            proposal_errors=prediction_errors[strong],
                            true_young=true_young,
                            source_artifact=str(cache),
                            notes=base_notes,
                        )

                    if args.profile == "dose":
                        for factor in factors:
                            if abs(factor - strong) < 1.0e-12:
                                continue
                            rows += pair_records(
                                scene=scene,
                                split=args.split,
                                family="dose_response",
                                truth="support",
                                baseline_factor=factor,
                                proposal_factor=1.0,
                                baseline_errors=prediction_errors[factor],
                                proposal_errors=prediction_errors[1.0],
                                true_young=true_young,
                                source_artifact=str(cache),
                                notes=base_notes,
                            )
                            rows += pair_records(
                                scene=scene,
                                split=args.split,
                                family="dose_response",
                                truth="veto",
                                baseline_factor=1.0,
                                proposal_factor=factor,
                                baseline_errors=prediction_errors[1.0],
                                proposal_errors=prediction_errors[factor],
                                true_young=true_young,
                                source_artifact=str(cache),
                                notes=base_notes,
                            )

                    worlds.append({
                        "scene": scene,
                        "split": args.split,
                        "task": task,
                        "material": material,
                        "trial": trial,
                        "true_young_pa": true_young,
                        "mid_frame": mid,
                        "mid_progress": mid_progress,
                        "driver_max_orthogonal_fraction": max_orth,
                        "solver_evidence": solver_evidence,
                    })
                    processed += 1
                if args.max_worlds is not None and processed >= args.max_worlds:
                    break
            if args.max_worlds is not None and processed >= args.max_worlds:
                break

    args.out.mkdir(parents=True, exist_ok=True)
    records_path = args.out / f"records_{args.split}_{args.profile}_{args.mode}.csv"
    write_records(records_path, rows)
    summary = summarize(rows, worlds)
    summary.update({
        "split": args.split,
        "profile": args.profile,
        "mode": args.mode,
        "numerics": {"n_cross": n_cross, "n_long": n_long, "dt_s": dt},
        "records_path": str(records_path),
        "shard": {"index": args.shard_index, "count": args.shard_count},
        "verification_nuisance": {
            "rotation_degrees": args.verification_rotation_deg,
            "translation_m": args.verification_translation_m,
            "axis": [1.0, 2.0, 3.0],
        },
    })
    summary_path = args.out / f"summary_{args.split}_{args.profile}_{args.mode}.json"
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    print("VALID GAUGE prospective controlled gate")
    print("SPLIT", args.split)
    print("PROFILE", args.profile)
    print("MODE", args.mode)
    print("SHARD", args.shard_index, "/", args.shard_count)
    print("WORLDS", len(worlds))
    print("RECORDS", len(rows))
    for method in METHODS:
        m = summary["methods"][method]
        print(
            "METHOD", method,
            "correct", m["correct_three_way"], "/", m["records"],
            "support", m["support_called"], "/", m["support_truth"],
            "veto", m["veto_called"], "/", m["veto_truth"],
            "unresolved", m["unresolved_called"], "/", m["unresolved_truth"],
        )
    print("OUT", args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

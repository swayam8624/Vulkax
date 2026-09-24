#!/usr/bin/env python3
"""One-shot retrospective IRIS free-fall V6.7 campaign.

This campaign intentionally operates only on already-spent evidence:
  - development: drop_50/06..10
  - failed validation: drop_100/06..10

It does NOT touch drop_150.

The campaign separates four possible V6.6 failure modes in one run:
  1. analysis resolution (640 vs 1280 px),
  2. temporal association horizon (stock tracker vs long-gap tracker),
  3. event ranking semantics after identical causal V6.6 eligibility,
  4. coverage / controlled rejection.

All candidate generation and ranking variants are target-free: they never use
gravity, expected flight duration, or drop height.  Ground-truth g/height enter
only after a candidate is selected, for retrospective scoring.

Outputs:
  methods.csv  - aggregate method comparison across spent splits
  videos.csv   - per-video selection/evaluation rows
  decision.json - compact diagnosis and best retrospective method(s)

No result from this script is confirmatory evidence for V6.7.
"""
from __future__ import annotations

import argparse
import csv
import importlib.util
import json
import math
from pathlib import Path
from collections import Counter, defaultdict

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
ANALYZER = ROOT / "research/analysis/run_iris_freefall_validation.py"
G = 9.80665


def load_analyzer():
    spec = importlib.util.spec_from_file_location("iris_ff_v67_campaign", ANALYZER)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


def extract_frame_candidates(mod, video: Path, cfg: dict, width: int):
    cv2 = mod.import_cv()
    max_seconds = float(cfg.get("max_seconds", 5.0))

    cap = cv2.VideoCapture(str(video))
    if not cap.isOpened():
        raise RuntimeError(f"cannot open {video}")
    fps = float(cap.get(cv2.CAP_PROP_FPS))
    frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    n = min(frames, max(120, int(fps * max_seconds)))
    cap.release()

    cap = cv2.VideoCapture(str(video))
    ids = np.linspace(0, n - 1, min(31, n), dtype=int)
    samples = []
    for i in ids:
        cap.set(cv2.CAP_PROP_POS_FRAMES, int(i))
        ok, fr = cap.read()
        if ok:
            samples.append(mod.resize_gray(fr, width, cv2))
    cap.release()
    if len(samples) < 8:
        raise RuntimeError("too few background samples")

    stack = np.stack(samples)
    bg = np.median(stack, axis=0).astype(np.uint8)
    energy = np.mean(
        np.abs(stack.astype(np.float32) - bg.astype(np.float32)), axis=0
    )
    cut = max(4.0, float(np.percentile(energy, 99.0)))
    mask = (energy >= cut).astype(np.uint8) * 255
    mask = cv2.dilate(
        mask,
        cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (9, 9)),
        iterations=2,
    )
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    if contours:
        pts = np.vstack([cc.reshape(-1, 2) for cc in contours])
        x, y0, w, h = cv2.boundingRect(pts)
        mx = max(25, int(0.25 * w))
        my = max(25, int(0.15 * h))
        x0 = max(0, x - mx)
        yy0 = max(0, y0 - my)
        x1 = min(bg.shape[1], x + w + mx)
        y1 = min(bg.shape[0], y0 + h + my)
        if (x1 - x0) * (y1 - yy0) < 0.03 * bg.size:
            x0 = yy0 = 0
            y1, x1 = bg.shape
    else:
        x0 = yy0 = 0
        y1, x1 = bg.shape

    frame_candidates = []
    cap = cv2.VideoCapture(str(video))
    i = 0
    while i < n:
        ok, fr = cap.read()
        if not ok:
            break
        gray = mod.resize_gray(fr, width, cv2)
        diff = cv2.absdiff(gray, bg)
        diff = cv2.GaussianBlur(diff, (5, 5), 0)
        crop = diff[yy0:y1, x0:x1]
        q = max(7.0, float(np.percentile(crop, 99.5)) * 0.55)
        frame_candidates.append(mod.extract_components(crop, q, cv2, x0, yy0))
        i += 1
    cap.release()
    return fps, frame_candidates


def build_tracks_longgap(frame_candidates, fps, width, max_gap=12):
    active = []
    finished = []
    next_id = 0
    px_scale = float(width) / 640.0

    def predict(tr, frame_idx):
        pts = tr["pts"]
        last = pts[-1]
        gap = max(1, frame_idx - int(last["frame"]))
        if len(pts) >= 3:
            q = pts[-3:]
            tt = np.asarray([float(p["frame"]) for p in q])
            tx = np.asarray([float(p["x"]) for p in q])
            ty = np.asarray([float(p["y"]) for p in q])
            t0 = tt[-1]
            u = tt - t0
            try:
                cx = np.polyfit(u, tx, min(2, len(q) - 1))
                cy = np.polyfit(u, ty, min(2, len(q) - 1))
                return float(np.polyval(cx, gap)), float(np.polyval(cy, gap)), gap
            except Exception:
                pass
        if len(pts) >= 2:
            p0, p1 = pts[-2], pts[-1]
            df = max(1, int(p1["frame"]) - int(p0["frame"]))
            vx = (float(p1["x"]) - float(p0["x"])) / df
            vy = (float(p1["y"]) - float(p0["y"])) / df
        else:
            vx = vy = 0.0
        return float(last["x"]) + vx * gap, float(last["y"]) + vy * gap, gap

    for frame_idx, cands in enumerate(frame_candidates):
        unmatched = set(range(len(cands)))
        proposals = []
        for ti, tr in enumerate(active):
            predx, predy, gap = predict(tr, frame_idx)
            last = tr["pts"][-1]
            if len(tr["pts"]) >= 2:
                p0, p1 = tr["pts"][-2], last
                df = max(1, int(p1["frame"]) - int(p0["frame"]))
                speed = math.hypot(
                    (float(p1["x"]) - float(p0["x"])) / df,
                    (float(p1["y"]) - float(p0["y"])) / df,
                )
            else:
                speed = 0.0
            for ci, cc in enumerate(cands):
                dist = math.hypot(float(cc["x"]) - predx, float(cc["y"]) - predy)
                area_ratio = max(float(cc["area"]), float(last["area"])) / max(
                    1.0, min(float(cc["area"]), float(last["area"]))
                )
                radius_ratio = max(
                    float(cc.get("radius", 1.0)),
                    float(last.get("radius", 1.0)),
                ) / max(
                    1e-6,
                    min(float(cc.get("radius", 1.0)), float(last.get("radius", 1.0))),
                )
                gate = max(26.0 * px_scale, (10.0 * px_scale) * gap + 3.2 * speed * gap)
                if dist > gate or area_ratio > 5.0 or radius_ratio > 3.2:
                    continue
                horiz = abs(float(cc["x"]) - float(last["x"]))
                morphology = (
                    abs(float(cc.get("circularity", 0.0)) - float(last.get("circularity", 0.0)))
                    + abs(float(cc.get("circle_fill", 0.0)) - float(last.get("circle_fill", 0.0)))
                    + abs(float(cc.get("axis_ratio", 0.0)) - float(last.get("axis_ratio", 0.0)))
                )
                cost = (
                    dist / gap
                    + 0.20 * horiz / gap
                    + 2.5 * abs(math.log(area_ratio))
                    + 1.5 * abs(math.log(radius_ratio))
                    + 4.0 * morphology
                    - 0.002 * float(cc.get("appearance_score", 0.0))
                )
                proposals.append((cost, ti, ci))

        assigned_tracks = set()
        assigned_cands = set()
        for cost, ti, ci in sorted(proposals):
            if ti in assigned_tracks or ci in assigned_cands:
                continue
            tr = active[ti]
            cc = dict(cands[ci])
            cc["frame"] = frame_idx
            tr["pts"].append(cc)
            tr["missed"] = 0
            assigned_tracks.add(ti)
            assigned_cands.add(ci)
            unmatched.discard(ci)

        survivors = []
        for ti, tr in enumerate(active):
            if ti not in assigned_tracks:
                tr["missed"] += 1
            if tr["missed"] > max_gap:
                if len(tr["pts"]) >= 6:
                    finished.append(tr)
            else:
                survivors.append(tr)
        active = survivors

        for ci in sorted(unmatched):
            cc = dict(cands[ci])
            cc["frame"] = frame_idx
            active.append({"id": next_id, "pts": [cc], "missed": 0})
            next_id += 1

    for tr in active:
        if len(tr["pts"]) >= 6:
            finished.append(tr)
    return finished


def generate_v66_candidates(mod, tracks, fps, cfg):
    minimum_interval_frames = int(cfg.get("minimum_active_frames", 12))
    chunks = mod._identity_track_chunks(tracks, fps, cfg)
    candidates = []
    for chunk in chunks:
        sign = float(cfg.get("expected_image_gravity_sign", 1.0))
        z = sign * np.asarray(chunk["y"], float)
        coarse = mod._causal_median_v66(z, 3)
        scale = max(float(np.ptp(coarse)), 1.0)
        progress = (coarse - float(np.min(coarse))) / scale
        for run_id, (aa, bb) in enumerate(mod._causal_monotone_runs_v66(progress)):
            upper = min(len(z) - minimum_interval_frames, aa + 7)
            if upper <= max(0, aa - 3):
                continue
            starts = range(max(0, aa - 3), upper)
            end_cap = min(len(z) - 1, bb + 12)
            for s in starts:
                for e in range(s + minimum_interval_frames, end_cap + 1):
                    q = mod._evaluate_prefix_v66(chunk, s, e, fps, cfg)
                    if q is not None:
                        q = dict(q)
                        q["run_id"] = int(run_id)
                        candidates.append(q)
                        break
    return candidates


def impact_consensus(candidates):
    return Counter(
        (int(q["track_id"]), int(q.get("impact_frame", -1)))
        for q in candidates
    )


def rank_candidate(q, mode, consensus):
    shape = float(q["trajectory_shape_rms_fraction"])
    stab = float(q.get("acceleration_stability", 0.0))
    release = abs(float(q.get("release_phase", 0.0)))
    xdrift = float(q["x_drift_fraction"])
    persistent = float(q.get("impact_persistent_samples", 0))
    identity = float(q["identity_score"])
    detected = float(q["detected_window_fraction"])
    span = float(q.get("local_span_px", 0.0))
    duration = float(q["full_fall_time_s"])
    t0 = float(q.get("t0_s", 0.0))
    support = consensus[(int(q["track_id"]), int(q.get("impact_frame", -1)))]

    if mode == "shape":
        return shape, stab, release, xdrift, -persistent, -identity, -detected, -span
    if mode == "semantic_long":
        return -duration, release, xdrift, shape, stab, -persistent, -identity, -detected
    if mode == "earliest_release":
        return t0, release, shape, stab, xdrift, -persistent, -identity, -duration
    if mode == "consensus":
        return -support, release, shape, stab, xdrift, -duration, -identity, -detected
    raise ValueError(mode)


def choose_variant(candidates, mode):
    if not candidates:
        return None
    consensus = impact_consensus(candidates)
    return min(candidates, key=lambda q: rank_candidate(q, mode, consensus))


def evaluate(chosen, height):
    if chosen is None:
        return {}
    T = float(chosen["full_fall_time_s"])
    truth_T = math.sqrt(2.0 * height / G)
    g = 2.0 * height / (T * T)
    return {
        "T_s": T,
        "truth_T_s": truth_T,
        "T_fraction_truth": T / truth_T,
        "g_eval_m_s2": g,
        "g_relative_error": abs(g - G) / G,
        "track_id": int(chosen["track_id"]),
        "impact_frame": int(chosen.get("impact_frame", -1)),
        "shape": float(chosen["trajectory_shape_rms_fraction"]),
        "acc_stability": float(chosen.get("acceleration_stability", 0.0)),
        "release_phase": float(chosen.get("release_phase", 0.0)),
        "identity": float(chosen["identity_score"]),
        "detected_fraction": float(chosen["detected_window_fraction"]),
        "local_span_px": float(chosen.get("local_span_px", 0.0)),
    }


def median_or_nan(xs):
    xs = [float(x) for x in xs if math.isfinite(float(x))]
    return float(np.median(xs)) if xs else float("nan")


def max_or_nan(xs):
    xs = [float(x) for x in xs if math.isfinite(float(x))]
    return float(np.max(xs)) if xs else float("nan")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--config",
        type=Path,
        default=ROOT / "research/validation/iris_freefall_rescue_v6.json",
    )
    ap.add_argument("--data-root", type=Path, required=True)
    ap.add_argument(
        "--out-dir",
        type=Path,
        default=ROOT / "build/v67-retrospective-campaign",
    )
    ap.add_argument("--widths", nargs="+", type=int, default=[640, 1280])
    args = ap.parse_args()

    mod = load_analyzer()
    config = json.loads(args.config.read_text())
    base_cfg = dict(config["tracker"])
    modes = ("shape", "semantic_long", "earliest_release", "consensus")
    tracker_modes = ("stock", "longgap12")
    splits = ("development", "validation")
    args.out_dir.mkdir(parents=True, exist_ok=True)

    video_rows = []
    extraction_failures = []

    print(
        "V6.7 RETROSPECTIVE ONE-SHOT CAMPAIGN -- "
        "drop_50 + failed drop_100 only; drop_150 is untouched"
    )
    print("METHODS widths=", args.widths, "trackers=", tracker_modes, "ranks=", modes)

    for split in splits:
        spec = config["dataset"][split]
        setting = spec["setting"]
        height = float(config["physics"]["drop_heights_m"][setting])
        for take in spec["takes"]:
            video = args.data_root / "iris" / "Dropping_ball" / setting / f"{take}.mp4"
            for width in args.widths:
                print(f"[extract] {split} {setting}/{take} width={width}")
                cfg = dict(base_cfg)
                cfg["analysis_width"] = int(width)
                try:
                    fps, frame_candidates = extract_frame_candidates(mod, video, cfg, int(width))
                except Exception as exc:
                    extraction_failures.append({
                        "split": split, "setting": setting, "take": take,
                        "width": width, "error": f"{type(exc).__name__}: {exc}",
                    })
                    print("  EXTRACT_FAIL", type(exc).__name__, exc)
                    continue

                tracks_by_mode = {
                    "stock": mod.build_temporal_tracks(frame_candidates, fps),
                    "longgap12": build_tracks_longgap(frame_candidates, fps, int(width), max_gap=12),
                }
                for tracker_mode, tracks in tracks_by_mode.items():
                    try:
                        cands = generate_v66_candidates(mod, tracks, fps, cfg)
                    except Exception as exc:
                        cands = []
                        print(f"  CANDIDATE_FAIL tracker={tracker_mode}", type(exc).__name__, exc)
                    for mode in modes:
                        chosen = choose_variant(cands, mode)
                        row = {
                            "method": f"w{width}_{tracker_mode}_{mode}",
                            "width": int(width),
                            "tracker_mode": tracker_mode,
                            "rank_mode": mode,
                            "split": split,
                            "setting": setting,
                            "take": take,
                            "candidate_count": len(cands),
                            "selected": chosen is not None,
                        }
                        row.update(evaluate(chosen, height))
                        video_rows.append(row)

    method_groups = defaultdict(list)
    for r in video_rows:
        method_groups[r["method"]].append(r)

    method_rows = []
    expected_per_split = {split: len(config["dataset"][split]["takes"]) for split in splits}
    for method, rows in sorted(method_groups.items()):
        out = {"method": method}
        for split in splits:
            rr = [r for r in rows if r["split"] == split]
            ok = [r for r in rr if r["selected"]]
            out[f"{split}_selected"] = len(ok)
            out[f"{split}_expected"] = expected_per_split[split]
            out[f"{split}_coverage"] = len(ok) / expected_per_split[split]
            out[f"{split}_median_g_relative_error"] = median_or_nan(
                [r["g_relative_error"] for r in ok]
            )
            out[f"{split}_max_g_relative_error"] = max_or_nan(
                [r["g_relative_error"] for r in ok]
            )
            out[f"{split}_median_T_fraction_truth"] = median_or_nan(
                [r["T_fraction_truth"] for r in ok]
            )

        dev = float(out["development_median_g_relative_error"])
        val = float(out["validation_median_g_relative_error"])
        cov = min(float(out["development_coverage"]), float(out["validation_coverage"]))
        out["retrospective_worst_split_median_error"] = max(dev, val)
        out["retrospective_coverage_penalized_score"] = max(dev, val) + 2.0 * (1.0 - cov)
        out["generalization_gap_abs"] = abs(val - dev)
        method_rows.append(out)

    ranked = sorted(
        method_rows,
        key=lambda r: (
            float(r["retrospective_coverage_penalized_score"]),
            float(r["retrospective_worst_split_median_error"]),
            float(r["generalization_gap_abs"]),
            r["method"],
        ),
    )

    video_fields = sorted({k for r in video_rows for k in r})
    with (args.out_dir / "videos.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=video_fields)
        w.writeheader()
        w.writerows(video_rows)

    method_fields = sorted({k for r in method_rows for k in r})
    with (args.out_dir / "methods.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=method_fields)
        w.writeheader()
        w.writerows(ranked)

    baseline_name = "w640_stock_shape"
    baseline = next((r for r in method_rows if r["method"] == baseline_name), None)
    best = ranked[0] if ranked else None

    diagnosis = {
        "schema": "vulkax.iris_freefall_v67_retrospective_campaign",
        "version": 1,
        "confirmatory": False,
        "data_status": {
            "development": "spent development evidence",
            "validation": "failed V6.6 validation; permanently retrospective",
            "drop_150": "NOT TOUCHED",
        },
        "method_count": len(method_rows),
        "video_method_rows": len(video_rows),
        "extraction_failures": extraction_failures,
        "baseline": baseline,
        "best_retrospective_method": best,
        "top_methods": ranked[:8],
        "interpretation_guard": (
            "Ranking is retrospective over already-opened drop_50/drop_100 data. "
            "It may select a V6.7 design candidate but cannot validate it."
        ),
    }

    if baseline and best:
        diagnosis["mechanistic_flags"] = {
            "resolution_helped": (
                best["method"].startswith("w1280_")
                and float(best["validation_median_g_relative_error"])
                < 0.75 * float(baseline["validation_median_g_relative_error"])
            ),
            "association_helped": (
                "_longgap12_" in best["method"]
                and float(best["validation_median_g_relative_error"])
                < 0.75 * float(baseline["validation_median_g_relative_error"])
            ),
            "ranking_helped": (
                not best["method"].endswith("_shape")
                and float(best["validation_median_g_relative_error"])
                < 0.75 * float(baseline["validation_median_g_relative_error"])
            ),
            "retrospective_candidate_under_20pct_both_splits": (
                float(best["development_median_g_relative_error"]) <= 0.20
                and float(best["validation_median_g_relative_error"]) <= 0.20
                and int(best["development_selected"]) >= 4
                and int(best["validation_selected"]) >= 4
            ),
        }

    (args.out_dir / "decision.json").write_text(
        json.dumps(diagnosis, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    print("\n=== V6.7 RETROSPECTIVE CAMPAIGN RANKING ===")
    for i, r in enumerate(ranked[:10], 1):
        print(
            f"{i:2d}. {r['method']:<34} "
            f"dev={r['development_median_g_relative_error']:.3f} "
            f"({r['development_selected']}/{r['development_expected']}) "
            f"val={r['validation_median_g_relative_error']:.3f} "
            f"({r['validation_selected']}/{r['validation_expected']}) "
            f"worst={r['retrospective_worst_split_median_error']:.3f} "
            f"gap={r['generalization_gap_abs']:.3f}"
        )

    if baseline:
        print(
            "\nBASELINE", baseline_name,
            "dev", baseline["development_median_g_relative_error"],
            "val", baseline["validation_median_g_relative_error"],
        )
    if best:
        print(
            "BEST_RETROSPECTIVE", best["method"],
            "dev", best["development_median_g_relative_error"],
            "val", best["validation_median_g_relative_error"],
            "coverage",
            f"{best['development_selected']}/{best['development_expected']}",
            f"{best['validation_selected']}/{best['validation_expected']}",
        )
    print("DECISION", args.out_dir / "decision.json")
    print("METHODS", args.out_dir / "methods.csv")
    print("VIDEOS", args.out_dir / "videos.csv")
    print("FINAL_TEST_STATUS drop_150 NOT TOUCHED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

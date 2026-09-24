#!/usr/bin/env python3
"""Retrospective scale diagnosis for the failed IRIS free-fall V6.6 validation.

This script does NOT define or tune V6.7.  It replays the frozen V6.6 selector on
already-opened evidence and measures the spatial/temporal mismatch that caused the
prospective failure.

It may use ground-truth gravity and drop height for *diagnosis only*.  Those
quantities are never fed back into V6.6 candidate generation or ranking.

Primary questions:
  1. What fraction of the physical drop does the V6.6 selected interval imply?
  2. What fraction of each ball-like track/global image-space envelope did it span?
  3. If curvature is converted with a scene-scale estimate, does the catastrophic
     2h/T^2 overestimate disappear or merely move elsewhere?

drop_100/06..10 is permanently retrospective after the V6.6 validation failure.
"""
from __future__ import annotations

import argparse
import csv
import importlib.util
import json
import math
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
ANALYZER = ROOT / "research/analysis/run_iris_freefall_validation.py"
TRACK_DIAG = ROOT / "research/scripts/diagnose_iris_freefall_v66.py"
G = 9.80665


def _load(path: Path, name: str):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


def _find_chunk(chunks, chunk_id: int):
    for q in chunks:
        if int(q["chunk_id"]) == int(chunk_id):
            return q
    raise RuntimeError(f"selected chunk {chunk_id} not found")


def diagnose_take(analyzer, diag, video: Path, tracker: dict, height: float) -> dict:
    fps, tracks = diag.build_tracks(analyzer, video, tracker)
    chunks = analyzer._identity_track_chunks(tracks, fps, tracker)
    chosen, _audit = analyzer.choose_ballistic_track_v66(
        tracks,
        fps,
        minimum_interval_frames=int(tracker.get("minimum_active_frames", 12)),
        identity_cfg=tracker,
    )
    chunk = _find_chunk(chunks, int(chosen["chunk_id"]))

    sign = float(tracker.get("expected_image_gravity_sign", 1.0))
    frames = np.asarray(chunk["frames"], int)
    z = sign * np.asarray(chunk["y"], float)
    ids = np.asarray(chosen["window_indices"], int)
    fr = frames[ids]
    zz = z[ids]

    t, a, b, c, pred = analyzer._quad_fit_v66(fr, zz, fps)
    residual = zz - pred
    event_dy_px = float(zz[-1] - zz[0])
    event_range_px = float(np.ptp(zz))
    chunk_span_px = float(np.ptp(z))

    try:
        top, bottom, global_span_px, env_track, env_chunk = (
            analyzer._global_envelope_from_chunks(chunks)
        )
    except Exception:
        top = bottom = global_span_px = float("nan")
        env_track = env_chunk = -1

    T = float(chosen["full_fall_time_s"])
    g_v66 = 2.0 * height / (T * T)
    expected_T = math.sqrt(2.0 * height / G)
    expected_frames = expected_T * fps

    # Retrospective truth diagnostic: distance a true free-fall body would cover
    # during the selected V6.6 interval.
    truth_distance_for_selected_T = 0.5 * G * T * T
    truth_height_fraction = truth_distance_for_selected_T / height

    # Curvature + independent image scale diagnostics.  These are NOT candidate
    # selectors.  They test whether the failure is primarily temporal semantics,
    # spatial scale, or both.
    g_from_global_scale = (
        float(c) * height / global_span_px
        if math.isfinite(global_span_px) and global_span_px > 0
        else float("nan")
    )
    g_from_chunk_scale = (
        float(c) * height / chunk_span_px if chunk_span_px > 0 else float("nan")
    )

    return {
        "fps": float(fps),
        "track_id": int(chosen["track_id"]),
        "chunk_id": int(chosen["chunk_id"]),
        "event_frame_start": int(fr[0]),
        "event_frame_end": int(fr[-1]),
        "event_frames": int(fr[-1] - fr[0] + 1),
        "selected_T_s": T,
        "expected_truth_T_s": expected_T,
        "selected_T_fraction_of_truth": T / expected_T,
        "selected_inferred_frames": float(chosen["inferred_full_fall_frames"]),
        "expected_truth_frames": expected_frames,
        "event_dy_px": event_dy_px,
        "event_range_px": event_range_px,
        "chunk_span_px": chunk_span_px,
        "global_envelope_px": float(global_span_px),
        "event_fraction_of_chunk_span": (
            event_range_px / chunk_span_px if chunk_span_px > 0 else float("nan")
        ),
        "event_fraction_of_global_envelope": (
            event_range_px / global_span_px
            if math.isfinite(global_span_px) and global_span_px > 0
            else float("nan")
        ),
        "global_envelope_source_track": int(env_track),
        "global_envelope_source_chunk": int(env_chunk),
        "curvature_px_s2": float(c),
        "fit_rms_px": float(np.sqrt(np.mean(residual * residual))),
        "v6_6_g_m_s2": g_v66,
        "v6_6_g_relative_error": abs(g_v66 - G) / G,
        "truth_distance_for_selected_T_m": truth_distance_for_selected_T,
        "truth_height_fraction_for_selected_T": truth_height_fraction,
        "g_from_global_envelope_scale_m_s2": g_from_global_scale,
        "g_from_global_envelope_relative_error": (
            abs(g_from_global_scale - G) / G
            if math.isfinite(g_from_global_scale) else float("nan")
        ),
        "g_from_chunk_scale_m_s2": g_from_chunk_scale,
        "g_from_chunk_scale_relative_error": (
            abs(g_from_chunk_scale - G) / G
            if math.isfinite(g_from_chunk_scale) else float("nan")
        ),
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--config",
        type=Path,
        default=ROOT / "research/validation/iris_freefall_rescue_v6.json",
    )
    ap.add_argument(
        "--data-root",
        type=Path,
        required=True,
        help="Path to the already-materialized V6.6 public-datasets directory.",
    )
    ap.add_argument(
        "--split",
        choices=("development", "validation"),
        default="validation",
    )
    ap.add_argument("--take", action="append")
    ap.add_argument("--out", type=Path)
    args = ap.parse_args()

    analyzer = _load(ANALYZER, "iris_ff_v67_scale_diag")
    diag = _load(TRACK_DIAG, "iris_ff_v66_track_diag")
    cfg = json.loads(args.config.read_text())
    tracker = cfg["tracker"]
    spec = cfg["dataset"][args.split]
    setting = spec["setting"]
    height = float(cfg["physics"]["drop_heights_m"][setting])
    takes = args.take or list(spec["takes"])

    rows = []
    for take in takes:
        video = (
            args.data_root / "iris" / "Dropping_ball" / setting / f"{take}.mp4"
        )
        row = {
            "split": args.split,
            "setting": setting,
            "take": take,
            "height_m": height,
            "video": str(video),
        }
        try:
            row.update(diagnose_take(analyzer, diag, video, tracker, height))
            row["status"] = "selected"
        except Exception as exc:
            row["status"] = "no_valid_candidate"
            row["error"] = f"{type(exc).__name__}: {exc}"
        rows.append(row)

    print(
        "RETROSPECTIVE_ONLY V6.7 SCALE DIAGNOSIS -- "
        "ground truth is used for failure analysis, never selection"
    )
    for r in rows:
        if r["status"] != "selected":
            print(f"{r['setting']}/{r['take']} FAIL {r.get('error','')}")
            continue
        print(
            f"{r['setting']}/{r['take']} "
            f"T={r['selected_T_s']:.4f}s "
            f"T/Ttruth={r['selected_T_fraction_of_truth']:.3f} "
            f"truth_h_frac={r['truth_height_fraction_for_selected_T']:.3f} "
            f"event/global={r['event_fraction_of_global_envelope']:.3f} "
            f"event/chunk={r['event_fraction_of_chunk_span']:.3f} "
            f"c={r['curvature_px_s2']:.2f}px/s2 "
            f"g_v66={r['v6_6_g_m_s2']:.3f} "
            f"g_global={r['g_from_global_envelope_scale_m_s2']:.3f} "
            f"g_chunk={r['g_from_chunk_scale_m_s2']:.3f}"
        )

    selected = [r for r in rows if r["status"] == "selected"]
    if selected:
        def med(key):
            vals = [float(r[key]) for r in selected if math.isfinite(float(r[key]))]
            return float(np.median(vals)) if vals else float("nan")
        summary = {
            "selected_takes": len(selected),
            "median_T_fraction_of_truth": med("selected_T_fraction_of_truth"),
            "median_truth_height_fraction_for_selected_T": med(
                "truth_height_fraction_for_selected_T"
            ),
            "median_event_fraction_of_global_envelope": med(
                "event_fraction_of_global_envelope"
            ),
            "median_v6_6_g_relative_error": med("v6_6_g_relative_error"),
            "median_global_scale_g_relative_error": med(
                "g_from_global_envelope_relative_error"
            ),
            "median_chunk_scale_g_relative_error": med(
                "g_from_chunk_scale_relative_error"
            ),
        }
        print("SUMMARY", json.dumps(summary, sort_keys=True))

    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        fields = sorted({k for r in rows for k in r})
        with args.out.open("w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=fields)
            w.writeheader()
            w.writerows(rows)
        print("OUT", args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Development-only IRIS V6.6 diagnostic.

Inspect raw ball-identity temporal chunks before the V6.5 global-envelope
calibration.  This intentionally does not use target gravity or validation data.
It reports pixel-space constant-acceleration diagnostics for downward monotone
runs so the release->impact selector can be designed from the development split
without fitting to g.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import math
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[2]
ANALYZER = ROOT / "research/analysis/run_iris_freefall_validation.py"


def load_analyzer():
    spec = importlib.util.spec_from_file_location("iris_ff", ANALYZER)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


def build_tracks(mod, video: Path, cfg: dict):
    cv2 = mod.import_cv()
    width = int(cfg.get("analysis_width", 640))
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
    energy = np.mean(np.abs(stack.astype(np.float32) - bg.astype(np.float32)), axis=0)
    cut = max(4.0, float(np.percentile(energy, 99.0)))
    mask = (energy >= cut).astype(np.uint8) * 255
    mask = cv2.dilate(
        mask, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (9, 9)), iterations=2
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
        g = mod.resize_gray(fr, width, cv2)
        diff = cv2.absdiff(g, bg)
        diff = cv2.GaussianBlur(diff, (5, 5), 0)
        crop = diff[yy0:y1, x0:x1]
        q = max(7.0, float(np.percentile(crop, 99.5)) * 0.55)
        frame_candidates.append(mod.extract_components(crop, q, cv2, x0, yy0))
        i += 1
    cap.release()

    return fps, mod.build_temporal_tracks(frame_candidates, fps)


def fit_run(mod, chunk, aa, bb, fps):
    frames = np.asarray(chunk["frames"][aa:bb], float)
    y = mod.smooth1(np.asarray(chunk["y"][aa:bb], float), 5)
    x = mod.smooth1(np.asarray(chunk["x"][aa:bb], float), 5)
    if len(frames) < 6:
        return None

    t = (frames - frames[0]) / fps
    X = np.column_stack([np.ones(len(t)), t, 0.5 * t * t])
    coef = np.linalg.lstsq(X, y, rcond=None)[0]
    a, b, c = map(float, coef)
    if not all(math.isfinite(v) for v in (a, b, c)) or c <= 0:
        return None

    pred = X @ coef
    span = max(float(np.ptp(y)), 1e-9)
    shape = float(np.sqrt(np.mean((y - pred) ** 2)) / span)
    deriv = b + c * t

    release_tau = -b / c
    release_extrap_frames = max(0.0, -release_tau * fps)
    release_inside = 0.0 <= release_tau <= float(t[-1])

    # Compare curvature in first/last overlapping halves. This is dimensionless
    # and does not use the known value of gravity.
    half = max(4, len(t) // 2)
    parts = []
    for lo, hi in ((0, half), (max(0, len(t) - half), len(t))):
        if hi - lo < 4:
            continue
        tt = t[lo:hi] - t[lo]
        yy = y[lo:hi]
        XX = np.column_stack([np.ones(len(tt)), tt, 0.5 * tt * tt])
        cc = float(np.linalg.lstsq(XX, yy, rcond=None)[0][2])
        if math.isfinite(cc):
            parts.append(cc)
    stability = (
        abs(parts[0] - parts[-1]) / max(abs(c), 1e-9)
        if len(parts) >= 2
        else float("inf")
    )

    speed0 = float(deriv[0])
    speed1 = float(deriv[-1])
    speed_growth = speed1 / max(abs(speed0), 1e-6)
    xdrift = float(np.ptp(x)) / span

    return {
        "frames": int(frames[-1] - frames[0] + 1),
        "frame_start": int(frames[0]),
        "frame_end": int(frames[-1]),
        "duration_s": float(t[-1]),
        "dy_px": float(y[-1] - y[0]),
        "curvature_px_s2": c,
        "shape_rms_frac": shape,
        "acc_stability": stability,
        "speed_start_px_s": speed0,
        "speed_end_px_s": speed1,
        "speed_growth": speed_growth,
        "release_tau_s": release_tau,
        "release_extrap_frames": release_extrap_frames,
        "release_inside_run": release_inside,
        "x_drift_frac": xdrift,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--config",
        type=Path,
        default=ROOT / "research/validation/iris_freefall_rescue_v6.json",
    )
    ap.add_argument(
        "--data-root",
        type=Path,
        default=ROOT / "build/public-datasets",
    )
    ap.add_argument("--take", action="append", help="Optional development take, e.g. 06")
    args = ap.parse_args()

    mod = load_analyzer()
    cfg = json.loads(args.config.read_text())
    tracker = cfg["tracker"]
    dev = cfg["dataset"]["development"]
    takes = args.take or list(dev["takes"])
    setting = dev["setting"]

    for take in takes:
        video = args.data_root / "iris" / "Dropping_ball" / setting / f"{take}.mp4"
        print("\n" + "=" * 120)
        print(f"TAKE {take}  VIDEO {video}")
        print("=" * 120)

        fps, tracks = build_tracks(mod, video, tracker)
        chunks = mod._identity_track_chunks(tracks, fps, tracker)
        print(f"fps={fps:.3f} tracks={len(tracks)} identity_chunks={len(chunks)}")

        rows = []
        for chunk in chunks:
            y = np.asarray(chunk["y"], float)
            # Use direction only. No global spatial scale and no target g.
            progress = y if float(tracker.get("expected_image_gravity_sign", 1)) > 0 else -y
            scale = max(float(np.ptp(progress)), 1.0)
            runs = mod._monotone_runs((progress - float(np.min(progress))) / scale)
            for rid, (aa, bb) in enumerate(runs):
                fit = fit_run(mod, chunk, aa, bb, fps)
                if fit is None:
                    continue
                fit.update({
                    "track": int(chunk["track_id"]),
                    "chunk": int(chunk["chunk_id"]),
                    "run": int(rid),
                    "identity": float(chunk["identity_score"]),
                    "detected_fraction": float(chunk["detected_fraction_chunk"]),
                })
                rows.append(fit)

        rows.sort(
            key=lambda r: (
                r["acc_stability"],
                r["shape_rms_frac"],
                r["release_extrap_frames"],
                -r["dy_px"],
            )
        )

        for r in rows[:25]:
            print(
                "track={track:3d} chunk={chunk:3d} run={run:2d} "
                "fr={frame_start:3d}-{frame_end:3d} n={frames:3d} "
                "Tobs={duration_s:.4f}s dy={dy_px:8.2f}px "
                "c={curvature_px_s2:9.2f}px/s2 "
                "shape={shape_rms_frac:.4f} stab={acc_stability:8.3f} "
                "v0={speed_start_px_s:8.2f} v1={speed_end_px_s:8.2f} "
                "grow={speed_growth:8.2f} "
                "release_tau={release_tau_s:8.4f}s "
                "pre={release_extrap_frames:6.2f}fr "
                "xin={x_drift_frac:.3f} id={identity:.3f} det={detected_fraction:.3f}"
                .format(**r)
            )


if __name__ == "__main__":
    main()

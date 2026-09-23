#!/usr/bin/env python3
"""Prospective IRIS single-pendulum validation for Reality Probe.

The implementation intentionally uses a deterministic classical motion signal and
pendulum period physics rather than a learned video model. Development and
validation are separated by IRIS initial-angle setting. Final-test processing is
refused unless a publication-validation lock is supplied.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
import statistics
import subprocess
from typing import Iterable

G = 9.80665
TAU = 2.0
DOSES = (0.025, 0.05, 0.10, 0.20)


def decision(z: float) -> str:
    if z >= TAU:
        return "support"
    if z <= -TAU:
        return "veto"
    return "unresolved"


def safe_scene(scene: str) -> str:
    return scene.replace("/", "__").replace(" ", "_")


def complete_elliptic_k(k: float) -> float:
    if not (0.0 <= k < 1.0):
        raise ValueError("elliptic modulus must be in [0,1)")
    a = 1.0
    b = math.sqrt(max(0.0, 1.0 - k * k))
    for _ in range(80):
        an = 0.5 * (a + b)
        bn = math.sqrt(a * b)
        a, b = an, bn
        if abs(a - b) <= 1.0e-15 * max(1.0, abs(a)):
            break
    return math.pi / (2.0 * a)


def predicted_period(length_m: float, angle_deg: float, finite_amplitude: bool) -> float:
    if not length_m > 0.0:
        raise ValueError("pendulum length must be positive")
    base = 2.0 * math.pi * math.sqrt(length_m / G)
    if not finite_amplitude:
        return base
    theta = math.radians(abs(angle_deg))
    k = math.sin(0.5 * theta)
    K = complete_elliptic_k(k)
    return 4.0 * math.sqrt(length_m / G) * K


def inferred_length(period_s: float, angle_deg: float) -> float:
    theta = math.radians(abs(angle_deg))
    K = complete_elliptic_k(math.sin(0.5 * theta))
    return G * (period_s / (4.0 * K)) ** 2


def robust_scale(values: list[float]) -> float:
    if not values:
        return 0.0
    med = statistics.median(values)
    mad = statistics.median(abs(x - med) for x in values)
    return 1.4826 * mad


def score_candidate(
    periods: list[float],
    fps: float,
    angle_deg: float,
    base_length: float,
    candidate_length: float,
    *,
    finite_amplitude: bool,
) -> tuple[float, float, float, float]:
    tb = predicted_period(base_length, angle_deg, finite_amplitude)
    tc = predicted_period(candidate_length, angle_deg, finite_amplitude)
    improvements = [abs(t - tb) - abs(t - tc) for t in periods]
    mean_improvement = statistics.fmean(improvements)
    sigma = max(robust_scale(improvements), 1.0 / fps)
    return mean_improvement / sigma, mean_improvement, sigma, tb - tc


def import_cv():
    try:
        import cv2  # type: ignore
        import numpy as np  # type: ignore
    except ImportError as exc:
        raise SystemExit(
            "IRIS validation requires numpy and opencv-python-headless. "
            "Run research/scripts/run_iris_pendulum_validation.sh."
        ) from exc
    return cv2, np


def resize_gray(frame, width: int, cv2):
    h, w = frame.shape[:2]
    if w <= 0 or h <= 0:
        raise ValueError("invalid video frame dimensions")
    if w == width:
        resized = frame
    else:
        scale = width / float(w)
        resized = cv2.resize(
            frame,
            (width, max(1, int(round(h * scale)))),
            interpolation=cv2.INTER_AREA,
        )
    return cv2.cvtColor(resized, cv2.COLOR_BGR2GRAY)


def build_background(video: Path, width: int, max_frames: int, sample_count: int):
    cv2, np = import_cv()
    cap = cv2.VideoCapture(str(video))
    if not cap.isOpened():
        raise RuntimeError(f"cannot open video: {video}")
    indices = np.linspace(0, max_frames - 1, num=min(sample_count, max_frames), dtype=int)
    frames = []
    for idx in indices:
        cap.set(cv2.CAP_PROP_POS_FRAMES, int(idx))
        ok, frame = cap.read()
        if ok:
            frames.append(resize_gray(frame, width, cv2))
    cap.release()
    if len(frames) < 8:
        raise RuntimeError(f"too few background samples: {video}")
    stack = np.stack(frames, axis=0)
    bg = np.median(stack, axis=0).astype(np.uint8)

    energy = np.mean(np.abs(stack.astype(np.float32) - bg.astype(np.float32)), axis=0)
    cutoff = max(5.0, float(np.percentile(energy, 98.0)))
    mask = (energy >= cutoff).astype(np.uint8) * 255
    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (9, 9))
    mask = cv2.dilate(mask, kernel, iterations=2)
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    if not contours:
        roi = (0, 0, bg.shape[1], bg.shape[0])
    else:
        contour = max(contours, key=cv2.contourArea)
        x, y, w, h = cv2.boundingRect(contour)
        mx = max(20, int(0.25 * w))
        my = max(20, int(0.25 * h))
        x0 = max(0, x - mx)
        y0 = max(0, y - my)
        x1 = min(bg.shape[1], x + w + mx)
        y1 = min(bg.shape[0], y + h + my)
        if (x1 - x0) * (y1 - y0) < 0.0025 * bg.size:
            roi = (0, 0, bg.shape[1], bg.shape[0])
        else:
            roi = (x0, y0, x1 - x0, y1 - y0)
    return bg, roi


def extract_motion_signal(
    video: Path,
    *,
    width: int = 640,
    max_seconds: float = 14.0,
    sample_count: int = 31,
) -> dict:
    cv2, np = import_cv()
    cap0 = cv2.VideoCapture(str(video))
    if not cap0.isOpened():
        raise RuntimeError(f"cannot open video: {video}")
    fps = float(cap0.get(cv2.CAP_PROP_FPS))
    frame_count = int(cap0.get(cv2.CAP_PROP_FRAME_COUNT))
    cap0.release()
    if not (20.0 <= fps <= 240.0):
        raise RuntimeError(f"unexpected video fps={fps}: {video}")
    max_frames = min(frame_count, max(180, int(round(fps * max_seconds))))
    if max_frames < int(fps * 3.0):
        raise RuntimeError(f"video too short for pendulum period analysis: {video}")

    background, roi = build_background(video, width, max_frames, sample_count)
    x0, y0, rw, rh = roi

    cap = cv2.VideoCapture(str(video))
    xs = []
    ys = []
    valid = []
    times = []
    frame_idx = 0
    while frame_idx < max_frames:
        ok, frame = cap.read()
        if not ok:
            break
        gray = resize_gray(frame, width, cv2)
        diff = cv2.absdiff(gray, background)
        diff = cv2.GaussianBlur(diff, (5, 5), 0)
        crop = diff[y0 : y0 + rh, x0 : x0 + rw]
        q = float(np.percentile(crop, 99.0))
        threshold = max(10.0, 0.70 * q)
        mask = (crop >= threshold).astype(np.uint8)
        kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3))
        mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel, iterations=1)
        yy, xx = np.nonzero(mask)
        if len(xx) >= 12:
            ww = crop[yy, xx].astype(np.float64)
            ww = np.maximum(ww - threshold + 1.0, 1.0)
            sx = float(np.sum((xx + x0) * ww) / np.sum(ww))
            sy = float(np.sum((yy + y0) * ww) / np.sum(ww))
            xs.append(sx)
            ys.append(sy)
            valid.append(True)
        else:
            xs.append(float("nan"))
            ys.append(float("nan"))
            valid.append(False)
        times.append(frame_idx / fps)
        frame_idx += 1
    cap.release()

    if frame_idx < int(0.8 * max_frames):
        raise RuntimeError(f"video decode stopped early: {video}")

    xarr = np.asarray(xs, dtype=float)
    yarr = np.asarray(ys, dtype=float)
    valid_arr = np.asarray(valid, dtype=bool)
    valid_fraction = float(np.mean(valid_arr))
    if np.sum(valid_arr) < 30:
        raise RuntimeError(f"insufficient tracked motion samples: {video}")

    idx = np.arange(len(xarr), dtype=float)
    good = np.flatnonzero(valid_arr)
    xarr = np.interp(idx, good, xarr[good])
    yarr = np.interp(idx, good, yarr[good])

    # Short median-like smoothing without scipy.
    kernel_len = 5
    kernel = np.ones(kernel_len, dtype=float) / kernel_len
    xsm = np.convolve(xarr, kernel, mode="same")
    edge = kernel_len // 2
    xsm[:edge] = xarr[:edge]
    xsm[-edge:] = xarr[-edge:]

    # Remove only a linear drift; the physical oscillation remains.
    tarr = np.asarray(times, dtype=float)
    coef = np.polyfit(tarr, xsm, 1)
    signal = xsm - (coef[0] * tarr + coef[1])
    amplitude_px = float(np.percentile(signal, 95) - np.percentile(signal, 5))

    # FFT period.
    centered = signal - np.mean(signal)
    window = np.hanning(len(centered))
    spec = np.abs(np.fft.rfft(centered * window)) ** 2
    freqs = np.fft.rfftfreq(len(centered), d=1.0 / fps)
    band = (freqs >= 0.25) & (freqs <= 2.0)
    if not np.any(band):
        raise RuntimeError("empty pendulum frequency band")
    band_idx = np.flatnonzero(band)
    peak_idx = int(band_idx[np.argmax(spec[band])])
    peak_freq = float(freqs[peak_idx])
    if not peak_freq > 0.0:
        raise RuntimeError("invalid FFT pendulum frequency")
    period_fft = 1.0 / peak_freq
    band_power = spec[band]
    sorted_power = np.sort(band_power)
    noise_floor = float(np.median(sorted_power)) + 1.0e-12
    spectral_peak_ratio = float(spec[peak_idx] / noise_floor)

    # Autocorrelation period, searched around the FFT fundamental to avoid a harmonic.
    ac = np.correlate(centered, centered, mode="full")[len(centered) - 1 :]
    target_lag = fps * period_fft
    lag_lo = max(2, int(math.floor(0.70 * target_lag)))
    lag_hi = min(len(ac) - 1, int(math.ceil(1.30 * target_lag)))
    if lag_hi <= lag_lo:
        raise RuntimeError("invalid autocorrelation lag search")
    lag = lag_lo + int(np.argmax(ac[lag_lo : lag_hi + 1]))
    period_ac = lag / fps
    estimator_agreement = abs(period_ac - period_fft) / max(period_fft, period_ac)

    # Same-direction zero crossings supply cycle-wise measured periods.
    def crossing_times(upward: bool) -> list[float]:
        out = []
        for i in range(1, len(signal)):
            a = signal[i - 1]
            b = signal[i]
            hit = (a <= 0.0 < b) if upward else (a >= 0.0 > b)
            if not hit:
                continue
            denom = b - a
            frac = (-a / denom) if abs(denom) > 1.0e-12 else 0.0
            out.append((i - 1 + frac) / fps)
        return out

    period_candidates = []
    for upward in (True, False):
        crossings = crossing_times(upward)
        for a, b in zip(crossings, crossings[1:]):
            p = b - a
            if 0.70 * period_fft <= p <= 1.30 * period_fft:
                period_candidates.append(p)
    period_candidates.sort()

    if len(period_candidates) < 3:
        raise RuntimeError(
            f"too few consistent pendulum cycles ({len(period_candidates)}): {video}"
        )
    observed_period = statistics.median(period_candidates)
    period_sigma = max(robust_scale(period_candidates), 1.0 / fps)

    return {
        "fps": fps,
        "frames_analyzed": len(signal),
        "valid_fraction": valid_fraction,
        "amplitude_px": amplitude_px,
        "period_fft_s": period_fft,
        "period_autocorr_s": period_ac,
        "period_estimator_relative_disagreement": estimator_agreement,
        "spectral_peak_ratio": spectral_peak_ratio,
        "observed_period_s": observed_period,
        "period_sigma_s": period_sigma,
        "cycle_periods_s": period_candidates,
        "roi": [int(x0), int(y0), int(rw), int(rh)],
        "times_s": [float(x) for x in tarr],
        "motion_x_px": [float(x) for x in xarr],
        "motion_y_px": [float(y) for y in yarr],
        "tracking_valid": [bool(x) for x in valid_arr],
    }


def load_pendulum_manifests(adapted_root: Path, split: str) -> list[tuple[Path, dict]]:
    out = []
    for p in sorted((adapted_root / "iris").glob("*/manifest.json")):
        m = json.loads(p.read_text(encoding="utf-8"))
        if not str(m.get("scene", "")).startswith("pendulum/"):
            continue
        if m.get("split") != split:
            continue
        out.append((p.parent, m))
    return out


def parameter_mean(manifest: dict, key: str) -> float:
    params = manifest.get("ground_truth", {}).get("parameters")
    if not isinstance(params, dict) or key not in params:
        raise ValueError(f"missing IRIS ground-truth parameter {key!r}")
    entry = params[key]
    if not isinstance(entry, dict) or "mean" not in entry:
        raise ValueError(f"malformed IRIS parameter {key!r}")
    return float(entry["mean"])


def candidate_cases() -> list[dict]:
    out = [
        {
            "name": "truth_support",
            "factor": 1.0,
            "truth": "support",
            "trial_family": "truth_control",
            "negative_control": False,
            "dose": None,
        },
        {
            "name": "truth_veto",
            "factor": 1.60,
            "truth": "veto",
            "trial_family": "truth_control",
            "negative_control": False,
            "dose": None,
        },
        {
            "name": "placebo",
            "factor": 1.25,
            "truth": "unresolved",
            "trial_family": "placebo",
            "negative_control": True,
            "dose": 0.0,
        },
    ]
    for dose in DOSES:
        out.append(
            {
                "name": f"support_dose_{dose:.3f}",
                "factor": 1.25 - dose,
                "truth": "support",
                "trial_family": "dose_response",
                "negative_control": False,
                "dose": dose,
            }
        )
        out.append(
            {
                "name": f"veto_dose_{dose:.3f}",
                "factor": 1.25 + dose,
                "truth": "veto",
                "trial_family": "dose_response",
                "negative_control": False,
                "dose": dose,
            }
        )
    return out


RECORD_FIELDS = [
    "record_version",
    "trial_id",
    "paired_key",
    "dataset",
    "scene",
    "split",
    "evidence_class",
    "confirmatory",
    "trial_family",
    "ground_truth",
    "method",
    "score",
    "decision",
    "decision_threshold",
    "confidence",
    "physical_delta",
    "target_error_delta",
    "measurement_noise_sigma",
    "pose_noise_sigma",
    "missing_fraction",
    "channel_dependence",
    "negative_control",
    "seed",
    "source_artifact",
    "notes",
]


def write_csv(path: Path, fields: list[str], rows: Iterable[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    rows = list(rows)
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)


def analyze_take(
    package: Path,
    manifest: dict,
    out_dir: Path,
    *,
    width: int,
    max_seconds: float,
) -> tuple[list[dict], dict]:
    scene = str(manifest["scene"])
    video = Path(manifest["video"]["path"])
    if not video.is_file():
        raise FileNotFoundError(video)
    angle_deg = parameter_mean(manifest, "angle")
    true_length = parameter_mean(manifest, "rope_length")
    tracking = extract_motion_signal(
        video,
        width=width,
        max_seconds=max_seconds,
    )
    length_est = inferred_length(tracking["observed_period_s"], angle_deg)
    length_rel_error = abs(length_est - true_length) / true_length

    take_out = out_dir / safe_scene(scene)
    take_out.mkdir(parents=True, exist_ok=True)
    tracking_summary = {
        k: v
        for k, v in tracking.items()
        if k not in {"times_s", "motion_x_px", "motion_y_px", "tracking_valid"}
    }
    tracking_summary.update(
        {
            "scene": scene,
            "video": str(video),
            "angle_deg": angle_deg,
            "true_length_m": true_length,
            "period_inferred_length_m": length_est,
            "period_inferred_length_relative_error": length_rel_error,
        }
    )
    (take_out / "tracking_summary.json").write_text(
        json.dumps(tracking_summary, indent=2) + "\n",
        encoding="utf-8",
    )
    write_csv(
        take_out / "motion_signal.csv",
        ["time_s", "motion_x_px", "motion_y_px", "tracking_valid"],
        [
            {
                "time_s": t,
                "motion_x_px": x,
                "motion_y_px": y,
                "tracking_valid": str(v).lower(),
            }
            for t, x, y, v in zip(
                tracking["times_s"],
                tracking["motion_x_px"],
                tracking["motion_y_px"],
                tracking["tracking_valid"],
            )
        ],
    )

    quality_ok = (
        tracking["valid_fraction"] >= 0.75
        and tracking["amplitude_px"] >= 5.0
        and len(tracking["cycle_periods_s"]) >= 3
        and tracking["period_estimator_relative_disagreement"] <= 0.15
        and tracking["spectral_peak_ratio"] >= 5.0
    )

    base_length = 1.25 * true_length
    rows = []
    if quality_ok:
        for case in candidate_cases():
            candidate_length = float(case["factor"]) * true_length
            target_delta = (
                abs(math.log(candidate_length / true_length))
                - abs(math.log(base_length / true_length))
            )
            for method, finite in (
                ("finite_amplitude_period_probe", True),
                ("small_angle_period_baseline", False),
            ):
                z, signal, sigma, model_period_delta = score_candidate(
                    list(tracking["cycle_periods_s"]),
                    float(tracking["fps"]),
                    angle_deg,
                    base_length,
                    candidate_length,
                    finite_amplitude=finite,
                )
                paired_key = f"iris:{scene}:{case['name']}"
                rows.append(
                    {
                        "record_version": 1,
                        "trial_id": f"{paired_key}:{method}",
                        "paired_key": paired_key,
                        "dataset": "iris_real_pendulum",
                        "scene": scene,
                        "split": manifest["split"],
                        "evidence_class": "prospective_real_video_period_probe",
                        "confirmatory": str(manifest["split"] == "final_test").lower(),
                        "trial_family": case["trial_family"],
                        "ground_truth": case["truth"],
                        "method": method,
                        "score": z,
                        "decision": decision(z),
                        "decision_threshold": TAU,
                        "confidence": "",
                        "physical_delta": math.log(candidate_length / base_length),
                        "target_error_delta": target_delta,
                        "measurement_noise_sigma": sigma,
                        "pose_noise_sigma": "",
                        "missing_fraction": 1.0 - float(tracking["valid_fraction"]),
                        "channel_dependence": 1.0,
                        "negative_control": str(bool(case["negative_control"])).lower(),
                        "seed": 0,
                        "source_artifact": str(take_out),
                        "notes": (
                            f"IRIS real video; angle={angle_deg}; "
                            f"candidate_factor={case['factor']}; "
                            f"period_signal_s={signal}; "
                            f"model_period_delta_s={model_period_delta}; "
                            "same-video independent observable; no learned tracker"
                        ),
                    }
                )

    report = {
        "scene": scene,
        "split": manifest["split"],
        "quality_ok": quality_ok,
        "tracking_valid_fraction": tracking["valid_fraction"],
        "motion_amplitude_px": tracking["amplitude_px"],
        "cycle_count": len(tracking["cycle_periods_s"]),
        "period_fft_s": tracking["period_fft_s"],
        "period_autocorr_s": tracking["period_autocorr_s"],
        "period_disagreement": tracking["period_estimator_relative_disagreement"],
        "spectral_peak_ratio": tracking["spectral_peak_ratio"],
        "observed_period_s": tracking["observed_period_s"],
        "period_sigma_s": tracking["period_sigma_s"],
        "angle_deg": angle_deg,
        "true_length_m": true_length,
        "period_inferred_length_m": length_est,
        "period_inferred_length_relative_error": length_rel_error,
        "record_count": len(rows),
    }
    return rows, report


def self_test() -> None:
    small = predicted_period(0.5, 45.0, False)
    finite = predicted_period(0.5, 45.0, True)
    assert finite > small
    recovered = inferred_length(finite, 45.0)
    assert abs(recovered - 0.5) < 1.0e-10

    periods = [finite - 0.01, finite, finite + 0.01, finite + 0.005]
    z0, *_ = score_candidate(periods, 60.0, 45.0, 0.625, 0.625, finite_amplitude=True)
    assert abs(z0) < 1.0e-12
    zs, *_ = score_candidate(periods, 60.0, 45.0, 0.625, 0.5, finite_amplitude=True)
    zv, *_ = score_candidate(periods, 60.0, 45.0, 0.625, 0.8, finite_amplitude=True)
    assert zs > 0.0
    assert zv < 0.0
    cases = candidate_cases()
    assert len(cases) == 11
    assert sum(c["truth"] == "support" for c in cases) == 5
    assert sum(c["truth"] == "veto" for c in cases) == 5
    assert sum(c["truth"] == "unresolved" for c in cases) == 1
    print("VALID IRIS pendulum validation self-test")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--adapted-root",
        type=Path,
        default=Path("build/publication-validation/public-data/adapted"),
    )
    ap.add_argument(
        "--out",
        type=Path,
        default=Path("build/publication-validation/iris-pendulum"),
    )
    ap.add_argument(
        "--split",
        choices=("development", "validation", "final_test"),
        default="validation",
    )
    ap.add_argument("--require-development-summary", type=Path)
    ap.add_argument("--lock", type=Path)
    ap.add_argument("--width", type=int, default=640)
    ap.add_argument("--max-seconds", type=float, default=14.0)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        self_test()
        return 0

    if args.split == "validation":
        if args.require_development_summary is None:
            raise SystemExit("validation requires --require-development-summary")
        dev = json.loads(args.require_development_summary.read_text(encoding="utf-8"))
        if not dev.get("development_gate_pass", False):
            raise SystemExit("development gate did not pass; validation remains unopened")
    elif args.split == "final_test":
        if args.lock is None:
            raise SystemExit("final_test requires --lock")
        subprocess.run(
            [
                "python3",
                "research/analysis/freeze_publication_validation.py",
                "--check",
                str(args.lock),
            ],
            check=True,
        )

    manifests = load_pendulum_manifests(args.adapted_root, args.split)
    if not manifests:
        raise SystemExit(f"no adapted IRIS pendulum manifests for split={args.split}")

    args.out.mkdir(parents=True, exist_ok=True)
    all_rows = []
    reports = []
    failures = []
    for package, manifest in manifests:
        try:
            rows, report = analyze_take(
                package,
                manifest,
                args.out,
                width=args.width,
                max_seconds=args.max_seconds,
            )
            all_rows.extend(rows)
            reports.append(report)
            if not report["quality_ok"]:
                failures.append(
                    {"scene": report["scene"], "reason": "tracking_quality_gate"}
                )
        except Exception as exc:
            failures.append(
                {
                    "scene": manifest.get("scene", str(package)),
                    "reason": f"{type(exc).__name__}: {exc}",
                }
            )

    expected = len(manifests)
    quality_count = sum(bool(r["quality_ok"]) for r in reports)
    length_errors = [
        float(r["period_inferred_length_relative_error"])
        for r in reports
        if r["quality_ok"]
    ]
    median_length_error = statistics.median(length_errors) if length_errors else None

    development_gate_pass = None
    if args.split == "development":
        development_gate_pass = (
            expected >= 10
            and quality_count >= 8
            and median_length_error is not None
            and median_length_error <= 0.20
        )

    summary = {
        "schema": "vulkax.iris_pendulum_validation",
        "version": 1,
        "split": args.split,
        "expected_take_count": expected,
        "quality_pass_take_count": quality_count,
        "failure_count": len(failures),
        "record_count": len(all_rows),
        "median_period_inferred_length_relative_error": median_length_error,
        "development_gate_pass": development_gate_pass,
        "tracker_config": {
            "analysis_width": args.width,
            "max_seconds": args.max_seconds,
            "minimum_valid_fraction": 0.75,
            "minimum_motion_amplitude_px": 5.0,
            "minimum_cycle_count": 3,
            "maximum_fft_autocorr_relative_disagreement": 0.15,
            "minimum_spectral_peak_ratio": 5.0,
        },
        "candidate_schedule": {
            "baseline_factor": 1.25,
            "truth_support_factor": 1.0,
            "truth_veto_factor": 1.60,
            "dose_levels": list(DOSES),
        },
        "failures": failures,
        "takes": reports,
        "claim_guard": (
            "Development may tune implementation; validation cannot tune the global "
            "|z|=2 threshold; final_test requires a checked lock."
        ),
    }
    (args.out / "summary.json").write_text(
        json.dumps(summary, indent=2) + "\n",
        encoding="utf-8",
    )
    write_csv(args.out / "validation_records.csv", RECORD_FIELDS, all_rows)
    if reports:
        write_csv(
            args.out / "take_summary.csv",
            list(reports[0].keys()),
            reports,
        )

    print("VALID IRIS pendulum analysis")
    print("SPLIT", args.split)
    print("TAKES", expected, "QUALITY_PASS", quality_count, "FAILURES", len(failures))
    print("MEDIAN_LENGTH_REL_ERROR", median_length_error)
    if args.split == "development":
        print("DEVELOPMENT_GATE_PASS", bool(development_gate_pass))
    print("RECORDS", len(all_rows))
    print("OUT", args.out)

    if args.split == "development" and not development_gate_pass:
        raise SystemExit("IRIS pendulum development gate failed; validation remains unopened")
    if args.split in {"validation", "final_test"} and failures:
        raise SystemExit(
            f"IRIS pendulum {args.split} incomplete: {len(failures)} take(s) failed"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Post-final corruption robustness for the locked IRIS pendulum method.

On macOS, backend=auto uses a native AVFoundation + Metal preprocessing path:
  hardware video decode where available -> Metal corruption/resize/grayscale ->
  raw 640px frames -> unchanged CPU motion centroid/FFT/scoring.

The Metal path deliberately avoids temporary full-resolution MJPEG re-encoding.
Before corrupted conditions are accepted, every clean Metal video must reproduce
the frozen primary decision vector exactly and agree on observed period.

No robustness run replaces the locked final result.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import os
import pathlib
import platform
import subprocess
import tempfile
import time
import sys

import numpy as np

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "research" / "analysis"))
from run_iris_pendulum_validation import (  # noqa:E402
    extract_motion_signal,
    score_candidate,
    candidate_cases,
    decision,
)

PRIMARY = "finite_amplitude_period_probe"


def read_csv(p):
    with p.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def safe(s):
    return s.replace("/", "__").replace(" ", "_")


def corrupt_video(src, dst, kind, value, seed):
    """Legacy CPU/OpenCV corruption path retained for portability."""
    import cv2

    rng = np.random.default_rng(seed)
    cap = cv2.VideoCapture(str(src))
    if not cap.isOpened():
        raise RuntimeError(f"cannot open {src}")
    fps = float(cap.get(cv2.CAP_PROP_FPS))
    w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    step = 1
    outfps = fps
    if kind == "fps":
        step = max(1, int(round(fps / float(value))))
        outfps = fps / step
    writer = cv2.VideoWriter(
        str(dst), cv2.VideoWriter_fourcc(*"MJPG"), outfps, (w, h)
    )
    if not writer.isOpened():
        raise RuntimeError("video writer unavailable")
    i = 0
    last = None
    while True:
        ok, fr = cap.read()
        if not ok:
            break
        if kind == "fps" and i % step != 0:
            i += 1
            continue
        q = fr.copy()
        if kind == "noise":
            q = np.clip(
                q.astype(np.float32) + rng.normal(0, float(value), q.shape),
                0,
                255,
            ).astype(np.uint8)
        elif kind == "blur":
            k = int(value)
            q = cv2.GaussianBlur(q, (k, k), 0)
        elif kind == "frame_drop":
            period = max(2, int(round(1 / max(1e-6, float(value)))))
            if i % period == 0 and last is not None:
                q = last.copy()
        elif kind == "occlusion":
            frac = float(value)
            side = math.sqrt(frac)
            ww = max(1, int(w * side))
            hh = max(1, int(h * side))
            x = (w - ww) // 2
            y = (h - hh) // 2
            q[y : y + hh, x : x + ww] = 0
        elif kind == "crop":
            frac = float(value)
            dx = int(w * frac)
            dy = int(h * frac)
            x0 = min(dx, w // 4)
            y0 = min(dy, h // 4)
            crop = q[y0 : h - y0, x0 : w - x0]
            if crop.size:
                q = cv2.resize(crop, (w, h), interpolation=cv2.INTER_LINEAR)
        writer.write(q)
        last = q
        i += 1
    cap.release()
    writer.release()


def ensure_metal_helper(path: pathlib.Path) -> pathlib.Path:
    if platform.system() != "Darwin":
        raise RuntimeError("Metal backend requested on non-macOS host")
    source = ROOT / "src" / "tools" / "iris_metal_preprocess.mm"
    rebuild = not path.is_file()
    if path.is_file():
        rebuild = path.stat().st_mtime < source.stat().st_mtime
    if rebuild:
        print(f"[robustness] building Metal helper -> {path}", flush=True)
        subprocess.run(
            [
                "bash",
                "research/scripts/build_iris_metal_preprocessor.sh",
                str(path),
            ],
            cwd=ROOT,
            check=True,
        )
    if not os.access(path, os.X_OK):
        raise RuntimeError(f"Metal helper is not executable: {path}")
    return path


def metal_preprocess(
    helper: pathlib.Path,
    src: pathlib.Path,
    raw: pathlib.Path,
    meta: pathlib.Path,
    kind: str,
    value: float,
    seed: int,
    *,
    width: int = 640,
    max_seconds: float = 14.0,
):
    subprocess.run(
        [
            str(helper),
            "--input",
            str(src),
            "--output",
            str(raw),
            "--meta",
            str(meta),
            "--kind",
            str(kind),
            "--value",
            str(value),
            "--seed",
            str(seed),
            "--width",
            str(width),
            "--max-seconds",
            str(max_seconds),
        ],
        check=True,
        stdout=subprocess.DEVNULL,
    )


def extract_motion_signal_gray_frames(raw: pathlib.Path, meta_path: pathlib.Path) -> dict:
    """Same motion-analysis logic as the frozen tracker after resize/grayscale."""
    import cv2

    meta = json.loads(meta_path.read_text(encoding="utf-8"))
    fps = float(meta["fps"])
    width = int(meta["width"])
    height = int(meta["height"])
    frame_count = int(meta["frame_count"])
    if frame_count < int(fps * 3.0):
        raise RuntimeError(f"Metal frame stream too short: {frame_count} frames")
    expected_bytes = frame_count * width * height
    if raw.stat().st_size != expected_bytes:
        raise RuntimeError(
            f"Metal raw size mismatch: got {raw.stat().st_size}, expected {expected_bytes}"
        )

    frames = np.memmap(
        raw,
        dtype=np.uint8,
        mode="r",
        shape=(frame_count, height, width),
    )

    sample_count = min(31, frame_count)
    indices = np.linspace(0, frame_count - 1, num=sample_count, dtype=int)
    stack = np.stack([np.asarray(frames[i]) for i in indices], axis=0)
    bg = np.median(stack, axis=0).astype(np.uint8)

    energy = np.mean(
        np.abs(stack.astype(np.float32) - bg.astype(np.float32)), axis=0
    )
    cutoff = max(5.0, float(np.percentile(energy, 98.0)))
    mask = (energy >= cutoff).astype(np.uint8) * 255
    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (9, 9))
    mask = cv2.dilate(mask, kernel, iterations=2)
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    if not contours:
        roi = (0, 0, width, height)
    else:
        contour = max(contours, key=cv2.contourArea)
        x, y, w, h = cv2.boundingRect(contour)
        mx = max(20, int(0.25 * w))
        my = max(20, int(0.25 * h))
        x0 = max(0, x - mx)
        y0 = max(0, y - my)
        x1 = min(width, x + w + mx)
        y1 = min(height, y + h + my)
        if (x1 - x0) * (y1 - y0) < 0.0025 * bg.size:
            roi = (0, 0, width, height)
        else:
            roi = (x0, y0, x1 - x0, y1 - y0)

    x0, y0, rw, rh = roi
    xs = []
    ys = []
    valid = []
    times = []
    for i in range(frame_count):
        gray = np.asarray(frames[i])
        diff = cv2.absdiff(gray, bg)
        diff = cv2.GaussianBlur(diff, (5, 5), 0)
        crop = diff[y0 : y0 + rh, x0 : x0 + rw]
        q = float(np.percentile(crop, 99.0))
        threshold = max(10.0, 0.70 * q)
        mm = (crop >= threshold).astype(np.uint8)
        k3 = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3))
        mm = cv2.morphologyEx(mm, cv2.MORPH_OPEN, k3, iterations=1)
        yy, xx = np.nonzero(mm)
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
        times.append(i / fps)

    xarr = np.asarray(xs, dtype=float)
    yarr = np.asarray(ys, dtype=float)
    valid_arr = np.asarray(valid, dtype=bool)
    valid_fraction = float(np.mean(valid_arr))
    if np.sum(valid_arr) < 30:
        raise RuntimeError("insufficient Metal tracked motion samples")

    idx = np.arange(len(xarr), dtype=float)
    good = np.flatnonzero(valid_arr)
    xarr = np.interp(idx, good, xarr[good])
    yarr = np.interp(idx, good, yarr[good])

    kernel_len = 5
    smooth_kernel = np.ones(kernel_len, dtype=float) / kernel_len
    xsm = np.convolve(xarr, smooth_kernel, mode="same")
    edge = kernel_len // 2
    xsm[:edge] = xarr[:edge]
    xsm[-edge:] = xarr[-edge:]

    tarr = np.asarray(times, dtype=float)
    coef = np.polyfit(tarr, xsm, 1)
    signal = xsm - (coef[0] * tarr + coef[1])
    amplitude_px = float(np.percentile(signal, 95) - np.percentile(signal, 5))

    centered = signal - np.mean(signal)
    window = np.hanning(len(centered))
    spec = np.abs(np.fft.rfft(centered * window)) ** 2
    freqs = np.fft.rfftfreq(len(centered), d=1.0 / fps)
    band = (freqs >= 0.25) & (freqs <= 2.0)
    band_idx = np.flatnonzero(band)
    if len(band_idx) == 0:
        raise RuntimeError("empty Metal pendulum frequency band")
    peak_idx = int(band_idx[np.argmax(spec[band])])
    peak_freq = float(freqs[peak_idx])
    if not peak_freq > 0.0:
        raise RuntimeError("invalid Metal FFT pendulum frequency")
    period_fft = 1.0 / peak_freq
    noise_floor = float(np.median(np.sort(spec[band]))) + 1.0e-12
    spectral_peak_ratio = float(spec[peak_idx] / noise_floor)

    ac = np.correlate(centered, centered, mode="full")[len(centered) - 1 :]
    target_lag = fps * period_fft
    lag_lo = max(2, int(math.floor(0.70 * target_lag)))
    lag_hi = min(len(ac) - 1, int(math.ceil(1.30 * target_lag)))
    if lag_hi <= lag_lo:
        raise RuntimeError("invalid Metal autocorrelation lag search")
    lag = lag_lo + int(np.argmax(ac[lag_lo : lag_hi + 1]))
    period_ac = lag / fps
    estimator_agreement = abs(period_ac - period_fft) / max(period_fft, period_ac)

    def crossing_times(upward: bool):
        out = []
        for i in range(1, len(signal)):
            aa = signal[i - 1]
            bb = signal[i]
            hit = (aa <= 0.0 < bb) if upward else (aa >= 0.0 > bb)
            if not hit:
                continue
            denom = bb - aa
            frac = (-aa / denom) if abs(denom) > 1.0e-12 else 0.0
            out.append((i - 1 + frac) / fps)
        return out

    period_candidates = []
    for upward in (True, False):
        crossings = crossing_times(upward)
        for aa, bb in zip(crossings, crossings[1:]):
            p = bb - aa
            if 0.70 * period_fft <= p <= 1.30 * period_fft:
                period_candidates.append(p)
    period_candidates.sort()
    if len(period_candidates) < 3:
        raise RuntimeError(
            f"too few consistent Metal pendulum cycles ({len(period_candidates)})"
        )

    observed_period = float(np.median(period_candidates))
    med = float(np.median(period_candidates))
    mad = float(np.median(np.abs(np.asarray(period_candidates) - med)))
    period_sigma = max(1.4826 * mad, 1.0 / fps)

    return {
        "fps": fps,
        "frames_analyzed": frame_count,
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
        "metal_device": meta.get("device"),
        "metal_source": {
            "width": meta.get("source_width"),
            "height": meta.get("source_height"),
            "fps": meta.get("source_fps"),
        },
    }


def locked_primary_decisions(campaign: pathlib.Path):
    out = {}
    for r in read_csv(campaign / "validation_records.csv"):
        if r["method"] != PRIMARY:
            continue
        scene = r["scene"]
        prefix = f"iris:{scene}:"
        paired = r["paired_key"]
        if not paired.startswith(prefix):
            continue
        case = paired[len(prefix) :]
        out[(scene, case)] = r["decision"]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--campaign",
        type=pathlib.Path,
        default=pathlib.Path(
            "build/publication-validation/iris-pendulum-final-test"
        ),
    )
    ap.add_argument(
        "--out",
        type=pathlib.Path,
        default=pathlib.Path(
            "build/publication-validation/iris-pendulum-postfinal-robustness"
        ),
    )
    ap.add_argument("--profile", choices=("quick", "full"), default="full")
    ap.add_argument("--backend", choices=("auto", "cpu", "metal"), default="auto")
    ap.add_argument(
        "--metal-helper",
        type=pathlib.Path,
        default=pathlib.Path("build/tools/vulkax_iris_metal_preprocess"),
    )
    ap.add_argument("--self-test", action="store_true")
    a = ap.parse_args()

    if a.self_test:
        assert safe("pendulum/a b/01") == "pendulum__a_b__01"
        assert a.backend in {"auto", "cpu", "metal"}
        print("VALID IRIS robustness self-test")
        return

    backend = a.backend
    if backend == "auto":
        backend = "metal" if platform.system() == "Darwin" else "cpu"
    helper = None
    if backend == "metal":
        helper = ensure_metal_helper(a.metal_helper)

    takes = read_csv(a.campaign / "take_summary.csv")
    locked = locked_primary_decisions(a.campaign)
    conditions = [("clean", 0)]
    conditions += [
        ("noise", x) for x in ([15] if a.profile == "quick" else [5, 15, 30])
    ]
    conditions += [
        ("blur", x) for x in ([7] if a.profile == "quick" else [3, 7, 15])
    ]
    conditions += [
        ("frame_drop", x)
        for x in ([0.25] if a.profile == "quick" else [0.10, 0.25, 0.50])
    ]
    conditions += [
        ("fps", x) for x in ([30] if a.profile == "quick" else [30, 15])
    ]
    conditions += [
        ("occlusion", x)
        for x in ([0.15] if a.profile == "quick" else [0.05, 0.15, 0.30])
    ]
    conditions += [
        ("crop", x)
        for x in ([0.05] if a.profile == "quick" else [0.02, 0.05, 0.10])
    ]

    total = len(conditions) * len(takes)
    rows = []
    fail = []
    clean_gate = []
    start_all = time.perf_counter()

    for ci, (kind, val) in enumerate(conditions):
        start_condition = time.perf_counter()
        print(
            f"[robustness] condition {ci + 1}/{len(conditions)}: "
            f"{kind}={val} backend={backend}",
            flush=True,
        )
        for ti, take in enumerate(takes):
            ordinal = ci * len(takes) + ti + 1
            scene = take["scene"]
            stored = json.loads(
                (a.campaign / safe(scene) / "tracking_summary.json").read_text()
            )
            src = pathlib.Path(stored["video"])
            angle = float(take["angle_deg"])
            Ltrue = float(take["true_length_m"])
            t0 = time.perf_counter()
            print(
                f"  [{ordinal:03d}/{total:03d}] {scene}",
                end="",
                flush=True,
            )
            try:
                if backend == "metal":
                    with tempfile.TemporaryDirectory() as td:
                        td = pathlib.Path(td)
                        raw = td / "frames.raw"
                        meta = td / "frames.json"
                        metal_preprocess(
                            helper,
                            src,
                            raw,
                            meta,
                            kind,
                            float(val),
                            20260923 + ci * 100 + ti,
                        )
                        tr = extract_motion_signal_gray_frames(raw, meta)
                else:
                    if kind == "clean":
                        tr = extract_motion_signal(src, width=640, max_seconds=14.0)
                    else:
                        with tempfile.TemporaryDirectory() as td:
                            dst = pathlib.Path(td) / "corrupt.avi"
                            corrupt_video(
                                src,
                                dst,
                                kind,
                                val,
                                20260923 + ci * 100 + ti,
                            )
                            tr = extract_motion_signal(
                                dst, width=640, max_seconds=14.0
                            )

                case_decisions = {}
                for case in candidate_cases():
                    L0 = 1.25 * Ltrue
                    L1 = float(case["factor"]) * Ltrue
                    score, _, _, _ = score_candidate(
                        list(tr["cycle_periods_s"]),
                        float(tr["fps"]),
                        angle,
                        L0,
                        L1,
                        finite_amplitude=True,
                    )
                    d = decision(score)
                    case_decisions[case["name"]] = d
                    rows.append(
                        {
                            "condition": kind,
                            "value": val,
                            "scene": scene,
                            "truth": case["truth"],
                            "case": case["name"],
                            "score": score,
                            "decision": d,
                            "correct": str(d == case["truth"]).lower(),
                            "valid_fraction": tr["valid_fraction"],
                            "cycle_count": len(tr["cycle_periods_s"]),
                            "backend": backend,
                            "metal_device": tr.get("metal_device", ""),
                        }
                    )

                if kind == "clean" and backend == "metal":
                    expected_period = float(stored["observed_period_s"])
                    period_rel = abs(
                        float(tr["observed_period_s"]) - expected_period
                    ) / max(expected_period, 1.0e-12)
                    mismatches = []
                    for case in candidate_cases():
                        expected = locked.get((scene, case["name"]))
                        actual = case_decisions[case["name"]]
                        if expected is None:
                            mismatches.append(
                                f"{case['name']}:missing_locked_decision"
                            )
                        elif expected != actual:
                            mismatches.append(
                                f"{case['name']}:{expected}->{actual}"
                            )
                    gate_ok = period_rel <= 0.02 and not mismatches
                    clean_gate.append(
                        {
                            "scene": scene,
                            "period_relative_difference": period_rel,
                            "decision_mismatches": ";".join(mismatches),
                            "gate_ok": gate_ok,
                            "metal_device": tr.get("metal_device"),
                        }
                    )
                    if not gate_ok:
                        raise RuntimeError(
                            "Metal clean-equivalence gate failed: "
                            f"period_rel={period_rel}; mismatches={mismatches}"
                        )
                elapsed = time.perf_counter() - t0
                print(f"  ok  {elapsed:.1f}s", flush=True)
            except Exception as e:
                elapsed = time.perf_counter() - t0
                print(f"  FAIL  {elapsed:.1f}s  {type(e).__name__}: {e}", flush=True)
                fail.append(
                    {
                        "condition": kind,
                        "value": val,
                        "scene": scene,
                        "error": f"{type(e).__name__}: {e}",
                        "backend": backend,
                    }
                )
                if kind == "clean" and backend == "metal":
                    raise

        print(
            f"[robustness] completed {kind}={val} in "
            f"{time.perf_counter() - start_condition:.1f}s",
            flush=True,
        )

    a.out.mkdir(parents=True, exist_ok=True)
    if rows:
        with (a.out / "records.csv").open("w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0]))
            w.writeheader()
            w.writerows(rows)
    if fail:
        with (a.out / "failures.csv").open("w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=list(fail[0]))
            w.writeheader()
            w.writerows(fail)
    if clean_gate:
        with (a.out / "metal_clean_equivalence.csv").open(
            "w", newline="", encoding="utf-8"
        ) as f:
            w = csv.DictWriter(f, fieldnames=list(clean_gate[0]))
            w.writeheader()
            w.writerows(clean_gate)

    summaries = []
    for kind, val in conditions:
        q = [
            r
            for r in rows
            if r["condition"] == kind and str(r["value"]) == str(val)
        ]
        if not q:
            continue
        summaries.append(
            {
                "condition": kind,
                "value": val,
                "records": len(q),
                "strict_accuracy": sum(r["correct"] == "true" for r in q) / len(q),
                "support_accuracy": sum(
                    r["correct"] == "true"
                    for r in q
                    if r["truth"] == "support"
                )
                / max(1, sum(r["truth"] == "support" for r in q)),
                "veto_accuracy": sum(
                    r["correct"] == "true"
                    for r in q
                    if r["truth"] == "veto"
                )
                / max(1, sum(r["truth"] == "veto" for r in q)),
                "placebo_accuracy": sum(
                    r["correct"] == "true"
                    for r in q
                    if r["truth"] == "unresolved"
                )
                / max(1, sum(r["truth"] == "unresolved" for r in q)),
                "failed_videos": sum(
                    1
                    for x in fail
                    if x["condition"] == kind and str(x["value"]) == str(val)
                ),
                "backend": backend,
            }
        )

    with (a.out / "summary.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(summaries[0]))
        w.writeheader()
        w.writerows(summaries)

    report = {
        "schema": "vulkax.iris_postfinal_robustness",
        "version": 2,
        "profile": a.profile,
        "backend": backend,
        "physical_videos": len(takes),
        "conditions": summaries,
        "failures": len(fail),
        "elapsed_seconds": time.perf_counter() - start_all,
        "metal_clean_equivalence": {
            "required": backend == "metal",
            "videos_checked": len(clean_gate),
            "all_passed": all(x["gate_ok"] for x in clean_gate)
            if clean_gate
            else None,
            "period_relative_tolerance": 0.02,
            "decision_vector_must_match_locked_primary": True,
        },
        "claim_guard": (
            "Post-final corruption study. Metal is an execution backend optimization. "
            "The clean Metal path must reproduce every frozen primary decision before "
            "corrupted results are accepted. Never replace locked final numbers."
        ),
    }
    (a.out / "summary.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8"
    )

    print("VALID IRIS post-final robustness", flush=True)
    print("BACKEND", backend, flush=True)
    print("ELAPSED_SECONDS", report["elapsed_seconds"], flush=True)
    for r in summaries:
        print(r, flush=True)
    print("FAILURES", len(fail), flush=True)
    if clean_gate:
        print(
            "METAL_CLEAN_EQUIVALENCE",
            sum(x["gate_ok"] for x in clean_gate),
            "/",
            len(clean_gate),
            flush=True,
        )
    print("OUT", a.out, flush=True)


if __name__ == "__main__":
    main()

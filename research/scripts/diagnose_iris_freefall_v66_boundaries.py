#!/usr/bin/env python3
"""Development-only IRIS free-fall V6.6 boundary diagnostic.

V6.6d detects the *onset* of impact/contact as a causal regime change.

Important safeguards:
- development split only;
- no validation/final clips;
- no target gravity or expected fall duration in filtering/ranking;
- no centered smoothing in boundary evidence;
- the first persistent out-of-model innovation is selected, not the largest
  later innovation.

The known 0.5 m drop height is used only for EVAL_ONLY columns printed after
selection so development behavior can be inspected without circular fitting.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import math
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
RAW_DIAG = ROOT / "research/scripts/diagnose_iris_freefall_v66.py"


def load_raw_diag():
    spec = importlib.util.spec_from_file_location("iris_v66_diag", RAW_DIAG)
    mod = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


def causal_median(x, width=3):
    """Past-only median filter; never looks at future samples."""
    q = np.asarray(x, float)
    out = np.empty_like(q)
    for i in range(len(q)):
        lo = max(0, i - width + 1)
        out[i] = float(np.median(q[lo : i + 1]))
    return out


def causal_monotone_runs(progress, minimum_points=6):
    """Sustained positive-motion phases using past-only smoothing."""
    p = np.asarray(progress, float)
    if len(p) < minimum_points:
        return []
    ps = causal_median(p, 3)
    dp = np.diff(ps)
    eps = max(0.0015, 0.03 / max(len(p), 1))
    active = dp > eps

    # Bridge at most two inactive derivative samples. This is only a coarse
    # candidate generator; final impact placement is handled by causal
    # out-of-sample innovations below.
    bridged = active.copy()
    for i in range(1, len(active) - 1):
        if (
            not active[i]
            and active[max(0, i - 2) : i].any()
            and active[i + 1 : min(len(active), i + 3)].any()
        ):
            bridged[i] = True

    runs = []
    i = 0
    while i < len(bridged):
        if not bridged[i]:
            i += 1
            continue
        start = i
        j = i
        gap = 0
        while j + 1 < len(bridged):
            j += 1
            if bridged[j]:
                gap = 0
            else:
                gap += 1
                if gap > 2:
                    j -= gap
                    break
        a = max(0, start - 1)
        b = min(len(p), j + 2)
        if b - a >= minimum_points and float(p[b - 1] - p[a]) > 0.0:
            runs.append((a, b))
        i = max(i + 1, j + 1)
    return runs


def quad_fit(frames, z, fps):
    frames = np.asarray(frames, float)
    z = np.asarray(z, float)
    t = (frames - frames[0]) / fps
    X = np.column_stack([np.ones(len(t)), t, 0.5 * t * t])
    coef = np.linalg.lstsq(X, z, rcond=None)[0]
    a, b, c = map(float, coef)
    pred = X @ coef
    return t, a, b, c, pred


def stability_score(frames, z, fps, c_full):
    n = len(frames)
    if n < 10:
        return float("inf")
    half = max(5, int(math.ceil(0.60 * n)))
    cs = []
    for lo, hi in ((0, half), (n - half, n)):
        _, _, _, cc, _ = quad_fit(frames[lo:hi], z[lo:hi], fps)
        if math.isfinite(cc):
            cs.append(cc)
    if len(cs) != 2:
        return float("inf")
    return abs(cs[0] - cs[1]) / max(abs(c_full), 1e-9)


def robust_noise_scale(residual):
    r = np.asarray(residual, float)
    if len(r) == 0:
        return 0.75
    med = float(np.median(r))
    mad = float(np.median(np.abs(r - med)))
    sigma = 1.4826 * mad
    rms = float(np.sqrt(np.mean(r * r)))
    # Pixel floor prevents tiny residuals from creating enormous significance.
    return max(sigma, rms, 0.75)


def evaluate_prefix(frames, z, x, s, e, fps, max_shape, max_xdrift):
    """Fit frames[s:e] and test future samples without refitting."""
    fr = np.asarray(frames[s:e], int)
    zz = np.asarray(z[s:e], float)
    xx = np.asarray(x[s:e], float)
    if len(fr) < 12:
        return None

    t, a, b, c, pred = quad_fit(fr, zz, fps)
    if not math.isfinite(c) or c <= 0:
        return None

    span = max(float(np.ptp(zz)), 1e-9)
    dy = float(zz[-1] - zz[0])
    if dy <= 2.0:
        return None

    residual = zz - pred
    fit_rms_px = float(np.sqrt(np.mean(residual * residual)))
    shape = fit_rms_px / span
    if not math.isfinite(shape) or shape > max_shape:
        return None

    duration = float(t[-1])
    release_tau = -b / c
    release_phase = release_tau / max(duration, 1e-9)
    if release_phase < -0.20 or release_phase > 0.30:
        return None

    deriv = b + c * t
    if float(np.mean(deriv >= -1e-6)) < 0.85:
        return None

    stab = stability_score(fr, zz, fps, c)
    xdrift = float(np.ptp(xx)) / span
    release_good = -0.08 <= release_phase <= 0.20
    x_good = xdrift <= max_xdrift

    noise = robust_noise_scale(residual)

    # Predict up to the next three observations using the *same* pre-change
    # model. No future sample is allowed into the fit.
    innov = []
    velocity_departures = []
    gaps = []
    for j in range(e, min(len(frames), e + 3)):
        gap = int(frames[j] - fr[-1])
        gaps.append(gap)
        tj = float(frames[j] - fr[0]) / fps
        pred_j = a + b * tj + 0.5 * c * tj * tj
        err = abs(float(z[j]) - float(pred_j))
        innov.append(err / noise)

        dt_step = float(frames[j] - fr[-1]) / fps
        if dt_step > 0:
            observed_v = (float(z[j]) - float(zz[-1])) / dt_step
            predicted_v = b + c * tj
            velocity_departures.append(
                abs(observed_v - predicted_v) / max(abs(predicted_v), 1.0)
            )
        else:
            velocity_departures.append(float("inf"))

    first_innov = float(innov[0]) if innov else 0.0
    first_vdep = float(velocity_departures[0]) if velocity_departures else 0.0
    contiguous = bool(gaps and gaps[0] <= 2)

    # Four-sigma causal innovation, supported by either persistence into a
    # second future sample or a large instantaneous velocity-regime departure.
    # These are generic change-point criteria, not tuned against gravity.
    persistent = sum(
        1
        for q, gap in zip(innov, gaps)
        if gap <= 3 and math.isfinite(q) and q >= 4.0
    )
    onset = (
        contiguous
        and first_innov >= 4.0
        and (persistent >= 2 or first_vdep >= 0.50)
    )

    release_abs = float(fr[0]) / fps + release_tau
    impact_abs = float(fr[-1]) / fps
    T_eval = impact_abs - release_abs
    g_eval = 2.0 * 0.5 / (T_eval * T_eval) if T_eval > 0 else float("nan")

    return {
        "start": int(fr[0]),
        "end": int(fr[-1]),
        "next_frame": int(frames[e]) if e < len(frames) else -1,
        "n": len(fr),
        "duration": duration,
        "dy": dy,
        "shape": shape,
        "stability": stab,
        "release_tau": release_tau,
        "release_phase": release_phase,
        "v0": float(deriv[0]),
        "v1": float(deriv[-1]),
        "xdrift": xdrift,
        "release_good": release_good,
        "x_good": x_good,
        "noise": noise,
        "first_innov": first_innov,
        "first_vdep": first_vdep,
        "persistent": int(persistent),
        "gaps": gaps,
        "onset": bool(onset),
        "T_eval": T_eval,
        "g_eval": g_eval,
    }


def candidate_onsets(analyzer, chunk, fps, tracker):
    sign = float(tracker.get("expected_image_gravity_sign", 1.0))

    # IMPORTANT: raw centroid samples for boundary detection. The analyzer's
    # smooth1() is centered and would leak future impact samples backward.
    frames = np.asarray(chunk["frames"], int)
    z = sign * np.asarray(chunk["y"], float)
    x = np.asarray(chunk["x"], float)
    if len(frames) < 13:
        return []

    max_shape = float(tracker.get("maximum_trajectory_shape_rms_fraction", 0.10))
    max_xdrift = float(tracker.get("maximum_x_drift_fraction", 0.45))

    coarse = causal_median(z, 3)
    scale = max(float(np.ptp(coarse)), 1.0)
    progress = (coarse - float(np.min(coarse))) / scale
    runs = causal_monotone_runs(progress)

    rows = []
    for run_id, (aa, bb) in enumerate(runs):
        starts = range(max(0, aa - 3), min(len(frames) - 12, aa + 7))
        end_cap = min(len(frames) - 1, bb + 12)

        for s in starts:
            fallback = None
            # Scan FORWARD in time and stop at the first persistent change point.
            for e in range(s + 12, end_cap + 1):
                q = evaluate_prefix(
                    frames, z, x, s, e, fps, max_shape, max_xdrift
                )
                if q is None:
                    continue
                q.update({
                    "run": run_id,
                    "track": int(chunk["track_id"]),
                    "chunk": int(chunk["chunk_id"]),
                    "identity": float(chunk["identity_score"]),
                    "detected": float(chunk["detected_fraction_chunk"]),
                })

                if q["release_good"] and q["x_good"]:
                    fallback = q
                if q["onset"] and q["release_good"] and q["x_good"]:
                    rows.append(q)
                    break
            else:
                # Keep one best-effort non-onset row for audit visibility only.
                if fallback is not None:
                    fallback = dict(fallback)
                    fallback["onset"] = False
                    rows.append(fallback)

    for q in rows:
        # The endpoint itself is determined by the *first* causal onset above.
        # Ranking only chooses among independent track/start hypotheses.
        q["rank"] = (
            0 if q["onset"] else 1,
            q["shape"],
            q["stability"],
            abs(q["release_phase"]),
            q["xdrift"],
            -q["persistent"],
            -q["identity"],
            -q["detected"],
            -q["dy"],
        )

    rows.sort(key=lambda r: r["rank"])
    return rows


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
    ap.add_argument("--take", action="append")
    ap.add_argument("--top", type=int, default=15)
    args = ap.parse_args()

    diag = load_raw_diag()
    analyzer = diag.load_analyzer()

    cfg = json.loads(args.config.read_text())
    tracker = cfg["tracker"]
    dev = cfg["dataset"]["development"]
    takes = args.take or list(dev["takes"])
    setting = dev["setting"]

    for take in takes:
        video = args.data_root / "iris" / "Dropping_ball" / setting / f"{take}.mp4"
        fps, tracks = diag.build_tracks(analyzer, video, tracker)
        chunks = analyzer._identity_track_chunks(tracks, fps, tracker)

        rows = []
        for chunk in chunks:
            rows.extend(candidate_onsets(analyzer, chunk, fps, tracker))
        rows.sort(key=lambda r: r["rank"])

        print("\n" + "=" * 180)
        print(
            f"TAKE {take} fps={fps:.3f} tracks={len(tracks)} "
            f"chunks={len(chunks)} onset_hypotheses={len(rows)}"
        )
        print(
            "V6.6d CAUSAL RANKING: first persistent 4-sigma innovation onset; "
            "raw boundary samples; g_eval is evaluation only"
        )
        print("=" * 180)
        for i, r in enumerate(rows[: args.top], 1):
            print(
                f"rank={i:2d} onset={int(r['onset'])} "
                f"tr={r['track']:3d} ch={r['chunk']:3d} run={r['run']:2d} "
                f"fr={r['start']:3d}-{r['end']:3d} -> next={r['next_frame']:3d} "
                f"n={r['n']:2d} dur={r['duration']:.4f}s dy={r['dy']:7.2f}px "
                f"shape={r['shape']:.4f} stab={r['stability']:7.3f} "
                f"rel={r['release_phase']:+.3f} "
                f"v0={r['v0']:8.2f} v1={r['v1']:8.2f} "
                f"innov={r['first_innov']:7.2f} persist={r['persistent']} "
                f"vdep={r['first_vdep']:6.2f} noise={r['noise']:.2f}px "
                f"x={r['xdrift']:.3f} id={r['identity']:.3f} det={r['detected']:.3f} "
                f"| EVAL_ONLY T={r['T_eval']:.4f}s g={r['g_eval']:.3f}"
            )


if __name__ == "__main__":
    main()

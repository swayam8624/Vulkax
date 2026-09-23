#!/usr/bin/env python3
"""Development-only boundary scan for IRIS free-fall V6.6.

This script searches release->impact change points on the already-opened
drop_50 development split. It NEVER uses target gravity, expected fall time, or
validation/final clips in candidate generation/ranking.

V6.6b fixes one diagnostic mistake exposed by the first all-development run:
ranking the smallest in-window quadratic residual first inevitably selects an
early clean PREFIX of a real free fall. Endpoint evidence must lead once a
window already satisfies the frozen kinematic/identity quality constraints.

The known 0.5 m height is used only for an evaluation column printed AFTER
ranking. Neither g nor expected flight duration enters filtering or ranking.
"""
from __future__ import annotations

import argparse
import importlib.util
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


def candidate_windows(diag, analyzer, chunk, fps, tracker):
    sign = float(tracker.get("expected_image_gravity_sign", 1.0))
    frames = np.asarray(chunk["frames"], int)
    z = sign * analyzer.smooth1(np.asarray(chunk["y"], float), 5)
    x = analyzer.smooth1(np.asarray(chunk["x"], float), 5)
    if len(frames) < 12:
        return []

    max_shape = float(tracker.get("maximum_trajectory_shape_rms_fraction", 0.10))
    max_xdrift = float(tracker.get("maximum_x_drift_fraction", 0.45))

    scale = max(float(np.ptp(z)), 1.0)
    runs = analyzer._monotone_runs((z - float(np.min(z))) / scale)
    out = []

    for run_id, (aa, bb) in enumerate(runs):
        starts = range(max(0, aa - 2), min(len(frames) - 11, aa + 7))
        end_cap = min(len(frames), bb + 10)

        for s in starts:
            for e in range(s + 12, end_cap + 1):
                fr = frames[s:e]
                zz = z[s:e]
                xx = x[s:e]
                if len(fr) < 12:
                    continue

                t, a, b, c, pred = quad_fit(fr, zz, fps)
                if not math.isfinite(c) or c <= 0:
                    continue

                dy = float(zz[-1] - zz[0])
                span = max(float(np.ptp(zz)), 1e-9)
                if dy <= 2.0:
                    continue

                fit_rms_px = float(np.sqrt(np.mean((zz - pred) ** 2)))
                shape = fit_rms_px / span
                if not math.isfinite(shape) or shape > max_shape:
                    continue

                release_tau = -b / c
                duration = float(t[-1])
                release_phase = release_tau / max(duration, 1e-9)
                if release_phase < -0.20 or release_phase > 0.30:
                    continue

                deriv = b + c * t
                if float(np.mean(deriv >= -1e-6)) < 0.85:
                    continue

                stab = stability_score(fr, zz, fps, c)
                xdrift = float(np.ptp(xx)) / span

                post_hi = min(len(frames), e + 8)
                post_n = post_hi - e
                if post_n:
                    post_t = (frames[e:post_hi] - fr[0]) / fps
                    post_pred = a + b * post_t + 0.5 * c * post_t * post_t
                    post_err = np.asarray(z[e:post_hi], float) - post_pred
                    post_rms = float(np.sqrt(np.mean(post_err * post_err)))
                    break_ratio = post_rms / max(fit_rms_px, 0.75)
                    edge = 0
                else:
                    post_rms = float("nan")
                    # Track termination is useful but weaker than an observed
                    # post-window model break, so do not fabricate a huge score.
                    break_ratio = 4.0
                    edge = 1

                release_good = -0.08 <= release_phase <= 0.20
                x_good = xdrift <= max_xdrift

                # V6.6b target-free endpoint ranking:
                #
                # 1. enforce the release-from-rest boundary condition qualitatively;
                # 2. use the already-frozen lateral-drift gate;
                # 3. among admissible quadratic windows, prefer the endpoint after
                #    which the pre-impact model fails most strongly;
                # 4. only then use stability/residual/span tie-breakers.
                #
                # This intentionally prevents a pristine 12-frame PREFIX from
                # outranking a slightly noisier but much stronger impact boundary.
                rank = (
                    0 if release_good else 1,
                    0 if x_good else 1,
                    -min(break_ratio, 50.0),
                    stab,
                    shape,
                    xdrift,
                    -span,
                    -duration,
                )

                # Development EVALUATION ONLY. These values never affect rank.
                release_abs = float(fr[0]) / fps + release_tau
                impact_abs = float(fr[-1]) / fps
                T_eval = impact_abs - release_abs
                g_eval = 2.0 * 0.5 / (T_eval * T_eval) if T_eval > 0 else float("nan")

                out.append({
                    "rank": rank,
                    "run": run_id,
                    "track": int(chunk["track_id"]),
                    "chunk": int(chunk["chunk_id"]),
                    "start": int(fr[0]),
                    "end": int(fr[-1]),
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
                    "post_n": post_n,
                    "post_rms": post_rms,
                    "break_ratio": break_ratio,
                    "edge": edge,
                    "identity": float(chunk["identity_score"]),
                    "detected": float(chunk["detected_fraction_chunk"]),
                    "T_eval": T_eval,
                    "g_eval": g_eval,
                })

    return out


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

    import json
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
            rows.extend(candidate_windows(diag, analyzer, chunk, fps, tracker))
        rows.sort(key=lambda r: r["rank"])

        print("\n" + "=" * 150)
        print(f"TAKE {take} fps={fps:.3f} tracks={len(tracks)} chunks={len(chunks)} candidates={len(rows)}")
        print("V6.6b RANKING: release/x gates -> endpoint break -> stability/shape; g_eval is evaluation only")
        print("=" * 150)
        for i, r in enumerate(rows[: args.top], 1):
            print(
                f"rank={i:2d} tr={r['track']:3d} ch={r['chunk']:3d} run={r['run']:2d} "
                f"fr={r['start']:3d}-{r['end']:3d} n={r['n']:2d} "
                f"dur={r['duration']:.4f}s dy={r['dy']:7.2f}px "
                f"shape={r['shape']:.4f} stab={r['stability']:7.3f} "
                f"rel_tau={r['release_tau']:+.4f}s rel_phase={r['release_phase']:+.3f} "
                f"v0={r['v0']:8.2f} v1={r['v1']:8.2f} "
                f"break={r['break_ratio']:7.2f} post={r['post_n']} edge={r['edge']} "
                f"x={r['xdrift']:.3f} id={r['identity']:.3f} det={r['detected']:.3f} "
                f"| EVAL_ONLY T={r['T_eval']:.4f}s g={r['g_eval']:.3f}"
            )


if __name__ == "__main__":
    main()

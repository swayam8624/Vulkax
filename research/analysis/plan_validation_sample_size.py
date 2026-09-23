#!/usr/bin/env python3
"""Precision-based sample-size planning for Reality Probe validation.

This is deliberately a planning utility, not a p-value generator. It answers:
1) how many independent truth trials are needed for a target Wilson half-width;
2) how many zero-event negative controls are needed to bound a false-assertion
   probability below a chosen limit at a chosen confidence level.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
from statistics import NormalDist


def wilson_interval(p: float, n: int, confidence: float) -> tuple[float, float]:
    if n <= 0:
        raise ValueError("n must be positive")
    z = NormalDist().inv_cdf(0.5 + confidence / 2.0)
    denom = 1.0 + z * z / n
    center = (p + z * z / (2.0 * n)) / denom
    half = z * math.sqrt(p * (1.0 - p) / n + z * z / (4.0 * n * n)) / denom
    return max(0.0, center - half), min(1.0, center + half)


def n_for_half_width(p: float, half_width: float, confidence: float, design_effect: float) -> int:
    if not 0.0 < half_width < 1.0:
        raise ValueError("half_width must be in (0,1)")
    if not 0.0 <= p <= 1.0:
        raise ValueError("expected rate must be in [0,1]")
    for n in range(2, 1_000_000):
        lo, hi = wilson_interval(p, n, confidence)
        if max(p - lo, hi - p) <= half_width:
            return math.ceil(n * design_effect)
    raise RuntimeError("sample size search did not converge")


def n_for_zero_event_upper_bound(max_rate: float, confidence: float, design_effect: float) -> int:
    if not 0.0 < max_rate < 1.0:
        raise ValueError("max_rate must be in (0,1)")
    alpha = 1.0 - confidence
    # Exact one-sided binomial upper bound after observing zero events:
    # upper = 1 - alpha^(1/n). Solve upper <= max_rate.
    n = math.ceil(math.log(alpha) / math.log(1.0 - max_rate))
    return math.ceil(n * design_effect)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--expected-recall", type=float, default=0.80)
    ap.add_argument("--half-width", type=float, default=0.10)
    ap.add_argument("--max-null-false-assertion", type=float, default=0.05)
    ap.add_argument("--confidence", type=float, default=0.95)
    ap.add_argument(
        "--design-effect",
        type=float,
        default=1.0,
        help="inflate for correlated trials/scenes; final analysis still resamples paired_key clusters",
    )
    ap.add_argument("--out", type=Path)
    args = ap.parse_args()

    recall_n = n_for_half_width(
        args.expected_recall, args.half_width, args.confidence, args.design_effect
    )
    null_n = n_for_zero_event_upper_bound(
        args.max_null_false_assertion, args.confidence, args.design_effect
    )
    result = {
        "schema": "vulkax.publication_validation_sample_size",
        "version": 1,
        "confidence": args.confidence,
        "design_effect": args.design_effect,
        "expected_support_or_veto_recall": args.expected_recall,
        "target_wilson_half_width": args.half_width,
        "minimum_independent_support_trials": recall_n,
        "minimum_independent_veto_trials": recall_n,
        "target_null_false_assertion_upper_bound": args.max_null_false_assertion,
        "minimum_zero-event_unresolved_or_placebo_trials": null_n,
        "warning": (
            "These are precision-based planning counts, not a substitute for a domain-specific "
            "power analysis. Increase design_effect when scenes/trials are correlated."
        ),
    }
    text = json.dumps(result, indent=2) + "\n"
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text, encoding="utf-8")
    print(text, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

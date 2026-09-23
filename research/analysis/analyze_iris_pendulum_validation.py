#!/usr/bin/env python3
"""Forensic analysis of an already executed IRIS pendulum validation campaign.

This script is descriptive only. It does not rerun video tracking, alter the
candidate schedule, change the global |z|=2 threshold, or inspect final-test data.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
import re
import statistics
from collections import defaultdict

PRIMARY = "finite_amplitude_period_probe"
BASELINE = "small_angle_period_baseline"
TAU = 2.0

FACTOR_RE = re.compile(r"candidate_factor=([0-9.]+)")


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def factor(row: dict[str, str]) -> float:
    m = FACTOR_RE.search(row.get("notes", ""))
    if not m:
        raise ValueError(f"candidate_factor missing from notes for {row.get('trial_id')}")
    return float(m.group(1))


def truth_sign(truth: str) -> int:
    if truth == "support":
        return 1
    if truth == "veto":
        return -1
    return 0


def median(xs):
    vals = [float(x) for x in xs if x is not None and math.isfinite(float(x))]
    return statistics.median(vals) if vals else None


def spearman(xs: list[float], ys: list[float]) -> float | None:
    if len(xs) != len(ys) or len(xs) < 2:
        return None

    def ranks(vals: list[float]) -> list[float]:
        order = sorted(range(len(vals)), key=vals.__getitem__)
        out = [0.0] * len(vals)
        i = 0
        while i < len(vals):
            j = i + 1
            while j < len(vals) and vals[order[j]] == vals[order[i]]:
                j += 1
            r = 0.5 * ((i + 1) + j)
            for k in range(i, j):
                out[order[k]] = r
            i = j
        return out

    rx, ry = ranks(xs), ranks(ys)
    mx, my = statistics.fmean(rx), statistics.fmean(ry)
    num = sum((a - mx) * (b - my) for a, b in zip(rx, ry))
    dx = math.sqrt(sum((a - mx) ** 2 for a in rx))
    dy = math.sqrt(sum((b - my) ** 2 for b in ry))
    if dx == 0.0 or dy == 0.0:
        return None
    return num / (dx * dy)


def summarize_group(rows: list[dict[str, str]]) -> dict:
    n = len(rows)
    if not n:
        return {}
    truth = rows[0]["ground_truth"]
    scores = [float(r["score"]) for r in rows]
    expected = truth_sign(truth)
    if expected:
        sign_correct = sum((s * expected) > 0 for s in scores)
    else:
        sign_correct = sum(abs(s) < TAU for s in scores)
    return {
        "n": n,
        "correct_n": sum(r["decision"] == truth for r in rows),
        "correct_rate": sum(r["decision"] == truth for r in rows) / n,
        "resolved_n": sum(r["decision"] != "unresolved" for r in rows),
        "coverage": sum(r["decision"] != "unresolved" for r in rows) / n,
        "sign_correct_n": sign_correct,
        "sign_correct_rate": sign_correct / n,
        "median_score": median(scores),
        "median_abs_score": median([abs(x) for x in scores]),
        "min_score": min(scores),
        "max_score": max(scores),
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--records",
        type=Path,
        default=Path(
            "build/publication-validation/iris-pendulum-validation/validation_records.csv"
        ),
    )
    ap.add_argument(
        "--take-summary",
        type=Path,
        default=Path(
            "build/publication-validation/iris-pendulum-validation/take_summary.csv"
        ),
    )
    ap.add_argument(
        "--out",
        type=Path,
        default=Path(
            "build/publication-validation/iris-pendulum-validation/forensics"
        ),
    )
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        fake = [
            {
                "trial_id": "a",
                "notes": "candidate_factor=1.05",
                "ground_truth": "support",
                "decision": "support",
                "score": "3.0",
            },
            {
                "trial_id": "b",
                "notes": "candidate_factor=1.05",
                "ground_truth": "support",
                "decision": "unresolved",
                "score": "1.0",
            },
        ]
        assert factor(fake[0]) == 1.05
        s = summarize_group(fake)
        assert s["n"] == 2 and s["correct_n"] == 1
        assert s["sign_correct_n"] == 2
        assert abs((spearman([1, 2, 3], [1, 2, 3]) or 0) - 1.0) < 1e-12
        print("VALID IRIS validation forensic self-test")
        return 0

    rows = read_rows(args.records)
    if not rows:
        raise SystemExit("empty IRIS validation record table")
    if any(r["split"] != "validation" for r in rows):
        raise SystemExit("forensic accepts validation-split rows only")

    takes = read_rows(args.take_summary) if args.take_summary.is_file() else []
    args.out.mkdir(parents=True, exist_ok=True)

    enriched = []
    for r in rows:
        q = dict(r)
        q["candidate_factor"] = factor(r)
        q["score_f"] = float(r["score"])
        q["expected_sign"] = truth_sign(r["ground_truth"])
        q["sign_correct"] = (
            (q["score_f"] * q["expected_sign"] > 0)
            if q["expected_sign"]
            else (abs(q["score_f"]) < TAU)
        )
        q["margin_to_threshold"] = abs(q["score_f"]) - TAU
        enriched.append(q)

    by = defaultdict(list)
    for r in enriched:
        by[(r["method"], r["ground_truth"], r["candidate_factor"])].append(r)

    per_factor = []
    for (method, truth, fac), group in sorted(by.items()):
        s = summarize_group(group)
        per_factor.append(
            {
                "method": method,
                "truth": truth,
                "candidate_factor": fac,
                **s,
            }
        )

    factor_fields = [
        "method",
        "truth",
        "candidate_factor",
        "n",
        "correct_n",
        "correct_rate",
        "resolved_n",
        "coverage",
        "sign_correct_n",
        "sign_correct_rate",
        "median_score",
        "median_abs_score",
        "min_score",
        "max_score",
    ]
    with (args.out / "per_factor.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=factor_fields)
        w.writeheader()
        w.writerows(per_factor)

    method_summary = []
    for method in sorted({r["method"] for r in enriched}):
        mr = [r for r in enriched if r["method"] == method]
        support = [r for r in mr if r["ground_truth"] == "support"]
        veto = [r for r in mr if r["ground_truth"] == "veto"]
        unresolved = [r for r in mr if r["ground_truth"] == "unresolved"]

        # Dose-response monotonicity is measured on validation data descriptively.
        # Support "dose" grows as candidate factor moves farther below baseline 1.25.
        # Veto "dose" grows as candidate factor moves farther above baseline.
        dose_rows = [r for r in mr if r["trial_family"] == "dose_response"]
        doses, directional_scores = [], []
        for r in dose_rows:
            fac = r["candidate_factor"]
            if r["ground_truth"] == "support":
                dose = 1.25 - fac
                directional = r["score_f"]
            elif r["ground_truth"] == "veto":
                dose = fac - 1.25
                directional = -r["score_f"]
            else:
                continue
            doses.append(dose)
            directional_scores.append(directional)

        truth_controls = [r for r in mr if r["trial_family"] == "truth_control"]
        placebo = [r for r in mr if r["trial_family"] == "placebo"]
        method_summary.append(
            {
                "method": method,
                "n": len(mr),
                "overall_correct_rate": sum(
                    r["decision"] == r["ground_truth"] for r in mr
                )
                / len(mr),
                "support_correct_rate": sum(
                    r["decision"] == "support" for r in support
                )
                / len(support),
                "veto_correct_rate": sum(
                    r["decision"] == "veto" for r in veto
                )
                / len(veto),
                "placebo_correct_rate": sum(
                    r["decision"] == "unresolved" for r in unresolved
                )
                / len(unresolved),
                "truth_control_correct_rate": sum(
                    r["decision"] == r["ground_truth"] for r in truth_controls
                )
                / len(truth_controls),
                "dose_direction_sign_rate": sum(
                    r["sign_correct"] for r in dose_rows
                )
                / len(dose_rows),
                "dose_monotonic_spearman": spearman(doses, directional_scores),
                "median_abs_score": median([abs(r["score_f"]) for r in mr]),
                "median_threshold_margin": median(
                    [abs(r["score_f"]) - TAU for r in mr]
                ),
                "placebo_max_abs_score": max(
                    [abs(r["score_f"]) for r in placebo], default=0.0
                ),
            }
        )

    with (args.out / "method_summary.csv").open(
        "w", newline="", encoding="utf-8"
    ) as f:
        w = csv.DictWriter(f, fieldnames=list(method_summary[0]))
        w.writeheader()
        w.writerows(method_summary)

    # Paired comparison on every identical proposal/take.
    paired = defaultdict(dict)
    for r in enriched:
        paired[r["paired_key"]][r["method"]] = r

    comparisons = []
    primary_wins = baseline_wins = ties = 0
    for key, methods in sorted(paired.items()):
        if PRIMARY not in methods or BASELINE not in methods:
            continue
        p, b = methods[PRIMARY], methods[BASELINE]
        truth = p["ground_truth"]
        p_correct = p["decision"] == truth
        b_correct = b["decision"] == truth
        if p_correct and not b_correct:
            primary_wins += 1
            outcome = "primary_only_correct"
        elif b_correct and not p_correct:
            baseline_wins += 1
            outcome = "baseline_only_correct"
        else:
            ties += 1
            outcome = "tie"
        comparisons.append(
            {
                "paired_key": key,
                "truth": truth,
                "candidate_factor": p["candidate_factor"],
                "primary_score": p["score_f"],
                "baseline_score": b["score_f"],
                "primary_decision": p["decision"],
                "baseline_decision": b["decision"],
                "outcome": outcome,
            }
        )

    with (args.out / "paired_comparison.csv").open(
        "w", newline="", encoding="utf-8"
    ) as f:
        fields = list(comparisons[0]) if comparisons else [
            "paired_key","truth","candidate_factor","primary_score","baseline_score",
            "primary_decision","baseline_decision","outcome"
        ]
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(comparisons)

    primary_summary = next(x for x in method_summary if x["method"] == PRIMARY)
    baseline_summary = next(x for x in method_summary if x["method"] == BASELINE)

    # This is deliberately a descriptive readiness assessment, not a preregistered
    # confirmatory gate. Validation data have already been observed.
    large_support = [
        r for r in enriched
        if r["method"] == PRIMARY
        and r["ground_truth"] == "support"
        and r["candidate_factor"] <= 1.15
    ]
    large_veto = [
        r for r in enriched
        if r["method"] == PRIMARY
        and r["ground_truth"] == "veto"
        and r["candidate_factor"] >= 1.35
    ]
    large_effect_correct = (
        sum(r["decision"] == r["ground_truth"] for r in large_support + large_veto)
        / max(1, len(large_support) + len(large_veto))
    )

    take_median_error = None
    if takes:
        errs = [
            float(r["period_inferred_length_relative_error"])
            for r in takes
            if str(r.get("quality_ok", "")).lower() in {"true", "1"}
        ]
        if errs:
            take_median_error = statistics.median(errs)

    readiness = {
        "classification": (
            "validation_supports_freeze_without_retuning"
            if (
                primary_summary["truth_control_correct_rate"] >= 0.90
                and primary_summary["placebo_correct_rate"] == 1.0
                and large_effect_correct >= 0.80
                and primary_summary["dose_direction_sign_rate"] >= 0.90
                and (
                    primary_summary["dose_monotonic_spearman"] is None
                    or primary_summary["dose_monotonic_spearman"] >= 0.60
                )
                and primary_wins > baseline_wins
            )
            else "validation_does_not_yet_support_final_freeze"
        ),
        "important_caveat": (
            "This readiness classification is post-hoc descriptive because validation "
            "outcomes were already observed. It must not be presented as a preregistered gate."
        ),
        "global_threshold_changed": False,
        "candidate_schedule_changed": False,
        "final_test_opened": False,
        "primary_truth_control_correct_rate": primary_summary[
            "truth_control_correct_rate"
        ],
        "primary_placebo_correct_rate": primary_summary["placebo_correct_rate"],
        "primary_large_effect_correct_rate": large_effect_correct,
        "primary_dose_direction_sign_rate": primary_summary[
            "dose_direction_sign_rate"
        ],
        "primary_dose_monotonic_spearman": primary_summary[
            "dose_monotonic_spearman"
        ],
        "primary_vs_baseline_only_correct_pairs": primary_wins,
        "baseline_vs_primary_only_correct_pairs": baseline_wins,
        "paired_ties": ties,
        "validation_median_period_inferred_length_relative_error": take_median_error,
    }

    report = {
        "schema": "vulkax.iris_pendulum_validation_forensics",
        "version": 1,
        "record_count": len(enriched),
        "take_count": len(takes),
        "methods": method_summary,
        "readiness": readiness,
    }
    (args.out / "summary.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8"
    )

    print("VALID IRIS pendulum validation forensics")
    print("PRIMARY", PRIMARY)
    print("TRUTH_CONTROL_CORRECT_RATE", readiness["primary_truth_control_correct_rate"])
    print("PLACEBO_CORRECT_RATE", readiness["primary_placebo_correct_rate"])
    print("LARGE_EFFECT_CORRECT_RATE", readiness["primary_large_effect_correct_rate"])
    print("DOSE_DIRECTION_SIGN_RATE", readiness["primary_dose_direction_sign_rate"])
    print("DOSE_MONOTONIC_SPEARMAN", readiness["primary_dose_monotonic_spearman"])
    print(
        "PAIRED_PRIMARY_WINS",
        primary_wins,
        "BASELINE_WINS",
        baseline_wins,
        "TIES",
        ties,
    )
    print(
        "VALIDATION_MEDIAN_LENGTH_REL_ERROR",
        readiness["validation_median_period_inferred_length_relative_error"],
    )
    print("READINESS", readiness["classification"])
    print("FINAL_TEST_OPENED", False)
    print("OUT", args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

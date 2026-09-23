#!/usr/bin/env python3
"""Descriptive summary for the locked IRIS pendulum final test.

This script never changes decisions, thresholds, candidates, or exclusions.
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
FACTOR_RE = re.compile(r"candidate_factor=([0-9.]+)")


def read_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def factor(row: dict[str, str]) -> float:
    m = FACTOR_RE.search(row.get("notes", ""))
    if not m:
        raise ValueError(f"candidate_factor missing for {row.get('trial_id')}")
    return float(m.group(1))


def median(values):
    vals = [float(x) for x in values if x is not None and math.isfinite(float(x))]
    return statistics.median(vals) if vals else None


def summarize_method(rows: list[dict[str, str]], method: str) -> dict:
    mr = [r for r in rows if r["method"] == method]
    if not mr:
        raise ValueError(f"method missing: {method}")
    truths = {}
    for truth in ("support", "veto", "unresolved"):
        q = [r for r in mr if r["ground_truth"] == truth]
        truths[truth] = {
            "n": len(q),
            "correct_n": sum(r["decision"] == truth for r in q),
            "correct_rate": (
                sum(r["decision"] == truth for r in q) / len(q) if q else None
            ),
            "coverage": (
                sum(r["decision"] != "unresolved" for r in q) / len(q) if q else None
            ),
        }
    return {
        "method": method,
        "n": len(mr),
        "strict_correct_n": sum(r["decision"] == r["ground_truth"] for r in mr),
        "strict_accuracy": sum(r["decision"] == r["ground_truth"] for r in mr)
        / len(mr),
        "coverage": sum(r["decision"] != "unresolved" for r in mr) / len(mr),
        "median_abs_score": median([abs(float(r["score"])) for r in mr]),
        "truth": truths,
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--records",
        type=Path,
        default=Path(
            "build/publication-validation/iris-pendulum-final-test/validation_records.csv"
        ),
    )
    ap.add_argument(
        "--take-summary",
        type=Path,
        default=Path(
            "build/publication-validation/iris-pendulum-final-test/take_summary.csv"
        ),
    )
    ap.add_argument(
        "--out",
        type=Path,
        default=Path(
            "build/publication-validation/iris-pendulum-final-test/final_summary.json"
        ),
    )
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        fake = {
            "notes": "candidate_factor=1.6",
            "trial_id": "x",
        }
        assert factor(fake) == 1.6
        print("VALID IRIS final-test summary self-test")
        return 0

    rows = read_rows(args.records)
    if not rows:
        raise SystemExit("empty IRIS final-test records")
    if any(r["split"] != "final_test" for r in rows):
        raise SystemExit("final summary accepts final_test rows only")
    if any(str(r["confirmatory"]).lower() not in {"true", "1"} for r in rows):
        raise SystemExit("final_test records must be marked confirmatory")

    takes = read_rows(args.take_summary)
    if len(takes) != 10:
        raise SystemExit(f"expected 10 IRIS final-test takes, got {len(takes)}")

    primary = summarize_method(rows, PRIMARY)
    baseline = summarize_method(rows, BASELINE)

    by_factor = defaultdict(list)
    for r in rows:
        by_factor[(r["method"], r["ground_truth"], factor(r))].append(r)

    per_factor = []
    for (method, truth, fac), group in sorted(by_factor.items()):
        per_factor.append(
            {
                "method": method,
                "truth": truth,
                "candidate_factor": fac,
                "n": len(group),
                "correct_n": sum(r["decision"] == truth for r in group),
                "correct_rate": sum(r["decision"] == truth for r in group)
                / len(group),
                "coverage": sum(r["decision"] != "unresolved" for r in group)
                / len(group),
                "median_score": median([float(r["score"]) for r in group]),
                "median_abs_score": median([abs(float(r["score"])) for r in group]),
            }
        )

    paired = defaultdict(dict)
    for r in rows:
        paired[r["paired_key"]][r["method"]] = r
    pwin = bwin = ties = 0
    for methods in paired.values():
        if PRIMARY not in methods or BASELINE not in methods:
            continue
        p = methods[PRIMARY]
        b = methods[BASELINE]
        pc = p["decision"] == p["ground_truth"]
        bc = b["decision"] == b["ground_truth"]
        if pc and not bc:
            pwin += 1
        elif bc and not pc:
            bwin += 1
        else:
            ties += 1

    quality_pass = sum(
        str(r.get("quality_ok", "")).lower() in {"true", "1"} for r in takes
    )
    length_errors = [
        float(r["period_inferred_length_relative_error"])
        for r in takes
        if str(r.get("quality_ok", "")).lower() in {"true", "1"}
    ]

    summary = {
        "schema": "vulkax.iris_pendulum_final_test_summary",
        "version": 1,
        "final_test": True,
        "confirmatory_record_count": len(rows),
        "take_count": len(takes),
        "quality_pass_take_count": quality_pass,
        "median_period_inferred_length_relative_error": median(length_errors),
        "primary": primary,
        "baseline": baseline,
        "paired_primary_only_correct": pwin,
        "paired_baseline_only_correct": bwin,
        "paired_ties": ties,
        "per_factor": per_factor,
        "claim_guard": (
            "Descriptive locked final-test summary only. No threshold, tracker, "
            "candidate schedule, or exclusions were changed after final-test opening."
        ),
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    per_factor_path = args.out.parent / "final_per_factor.csv"
    with per_factor_path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(per_factor[0]))
        w.writeheader()
        w.writerows(per_factor)

    print("VALID IRIS pendulum locked final-test summary")
    print("TAKES", len(takes), "QUALITY_PASS", quality_pass)
    print("MEDIAN_LENGTH_REL_ERROR", summary["median_period_inferred_length_relative_error"])
    for m in (primary, baseline):
        print(m["method"])
        print(" STRICT_ACCURACY", m["strict_accuracy"])
        for truth in ("support", "veto", "unresolved"):
            q = m["truth"][truth]
            print(" ", truth, q["correct_n"], "/", q["n"], "correct")
    print("PAIRED_PRIMARY_WINS", pwin, "BASELINE_WINS", bwin, "TIES", ties)
    print("OUT", args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

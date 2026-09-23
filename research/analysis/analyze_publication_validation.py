#!/usr/bin/env python3
"""Analyze standardized Reality Probe publication-validation records.

Only the Python standard library is used. The bootstrap resampling unit is the
paired_key, not the individual method row, so paired methods stay paired.
"""
from __future__ import annotations

import argparse
import csv
import itertools
import json
import math
from pathlib import Path
import random
import statistics
import tempfile
from typing import Callable

TRUTHS = {"support", "veto", "unresolved"}


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def parse_bool(value: str) -> bool:
    return str(value).strip().lower() in {"1", "true", "yes", "y"}


def opt_float(value: str | None) -> float | None:
    if value is None or str(value).strip() == "":
        return None
    x = float(value)
    if not math.isfinite(x):
        raise ValueError(f"non-finite numeric value: {value!r}")
    return x


def quantile(values: list[float], q: float) -> float:
    if not values:
        raise ValueError("empty quantile")
    xs = sorted(values)
    if len(xs) == 1:
        return xs[0]
    p = (len(xs) - 1) * q
    lo, hi = math.floor(p), math.ceil(p)
    if lo == hi:
        return xs[lo]
    w = p - lo
    return xs[lo] * (1.0 - w) + xs[hi] * w


def decision(score: float, threshold: float) -> str:
    if score >= threshold:
        return "support"
    if score <= -threshold:
        return "veto"
    return "unresolved"


def prepare(rows: list[dict[str, str]]) -> list[dict[str, object]]:
    out: list[dict[str, object]] = []
    seen: set[str] = set()
    for i, row in enumerate(rows, start=2):
        trial_id = row.get("trial_id", "")
        if not trial_id:
            raise ValueError(f"row {i}: missing trial_id")
        if trial_id in seen:
            raise ValueError(f"row {i}: duplicate trial_id {trial_id!r}")
        seen.add(trial_id)
        truth = row.get("ground_truth", "")
        if truth not in TRUTHS:
            raise ValueError(f"row {i}: invalid ground_truth {truth!r}")
        score = float(row["score"])
        if not math.isfinite(score):
            raise ValueError(f"row {i}: non-finite score")
        out.append(
            {
                **row,
                "score_f": score,
                "confirmatory_b": parse_bool(row.get("confirmatory", "")),
                "negative_control_b": parse_bool(row.get("negative_control", "")),
                "confidence_f": opt_float(row.get("confidence")),
                "physical_delta_f": opt_float(row.get("physical_delta")),
                "target_error_delta_f": opt_float(row.get("target_error_delta")),
                "measurement_noise_sigma_f": opt_float(row.get("measurement_noise_sigma")),
                "pose_noise_sigma_f": opt_float(row.get("pose_noise_sigma")),
                "missing_fraction_f": opt_float(row.get("missing_fraction")),
                "channel_dependence_f": opt_float(row.get("channel_dependence")),
            }
        )
    return out


def primary_metrics(rows: list[dict[str, object]], threshold: float) -> dict[str, object]:
    n = len(rows)
    if n == 0:
        return {"n": 0}
    decisions = [decision(float(r["score_f"]), threshold) for r in rows]
    truths = [str(r["ground_truth"]) for r in rows]
    resolved = [d != "unresolved" for d in decisions]
    resolved_n = sum(resolved)
    correct = [d == t for d, t in zip(decisions, truths)]
    selective_correct = [c for c, is_resolved in zip(correct, resolved) if is_resolved]

    support_n = sum(t == "support" for t in truths)
    veto_n = sum(t == "veto" for t in truths)
    unresolved_truth_n = sum(t == "unresolved" for t in truths)

    support_correct = sum(t == "support" and d == "support" for t, d in zip(truths, decisions))
    veto_correct = sum(t == "veto" and d == "veto" for t, d in zip(truths, decisions))
    false_support_on_veto = sum(t == "veto" and d == "support" for t, d in zip(truths, decisions))
    false_veto_on_support = sum(t == "support" and d == "veto" for t, d in zip(truths, decisions))
    false_assert_unresolved = sum(t == "unresolved" and d != "unresolved" for t, d in zip(truths, decisions))

    directional_rows = [r for r in rows if r["ground_truth"] in {"support", "veto"}]
    direction_ok = sum(
        (r["ground_truth"] == "support" and float(r["score_f"]) > 0)
        or (r["ground_truth"] == "veto" and float(r["score_f"]) < 0)
        for r in directional_rows
    )
    abs_scores = [abs(float(r["score_f"])) for r in rows]

    selective_accuracy = (
        sum(selective_correct) / len(selective_correct) if selective_correct else None
    )
    return {
        "n": n,
        "support_truth_n": support_n,
        "veto_truth_n": veto_n,
        "unresolved_truth_n": unresolved_truth_n,
        "resolved_n": resolved_n,
        "coverage": resolved_n / n,
        "strict_three_way_accuracy": sum(correct) / n,
        "selective_accuracy": selective_accuracy,
        "selective_risk": None if selective_accuracy is None else 1.0 - selective_accuracy,
        "support_recall_all": support_correct / support_n if support_n else None,
        "veto_recall_all": veto_correct / veto_n if veto_n else None,
        "false_support_rate_on_veto": false_support_on_veto / veto_n if veto_n else None,
        "false_veto_rate_on_support": false_veto_on_support / support_n if support_n else None,
        "false_assertion_rate_on_unresolved": (
            false_assert_unresolved / unresolved_truth_n if unresolved_truth_n else None
        ),
        "directional_sign_accuracy": direction_ok / len(directional_rows) if directional_rows else None,
        "median_abs_score": statistics.median(abs_scores),
        "p90_abs_score": quantile(abs_scores, 0.90),
        "max_abs_score": max(abs_scores),
    }


def bootstrap_ci(
    rows: list[dict[str, object]],
    metric: Callable[[list[dict[str, object]]], float | None],
    *,
    reps: int,
    level: float,
    seed: int,
) -> tuple[float | None, float | None]:
    if not rows or reps <= 0:
        return None, None
    groups: dict[str, list[dict[str, object]]] = {}
    for r in rows:
        groups.setdefault(str(r["paired_key"]), []).append(r)
    keys = sorted(groups)
    rng = random.Random(seed)
    samples: list[float] = []
    for _ in range(reps):
        sample: list[dict[str, object]] = []
        for _j in range(len(keys)):
            k = keys[rng.randrange(len(keys))]
            sample.extend(groups[k])
        value = metric(sample)
        if value is not None and math.isfinite(value):
            samples.append(value)
    if not samples:
        return None, None
    alpha = 1.0 - level
    return quantile(samples, alpha / 2.0), quantile(samples, 1.0 - alpha / 2.0)


def grouped(rows: list[dict[str, object]], keys: tuple[str, ...]):
    groups: dict[tuple[str, ...], list[dict[str, object]]] = {}
    for row in rows:
        key = tuple(str(row[k]) for k in keys)
        groups.setdefault(key, []).append(row)
    return groups


def method_summary(
    rows: list[dict[str, object]],
    threshold: float,
    reps: int,
    level: float,
) -> list[dict[str, object]]:
    out = []
    # Report both pooled-by-method and dataset-specific summaries.
    specs = [(("method",), "all")] + [(("dataset", "method"), "dataset")]
    seed_base = 918273
    for keys, scope in specs:
        for idx, (key, subset) in enumerate(sorted(grouped(rows, keys).items())):
            metrics = primary_metrics(subset, threshold)
            row: dict[str, object] = {"scope": scope}
            for k, v in zip(keys, key):
                row[k] = v
            for name, value in metrics.items():
                row[name] = value
            for metric_name in ("coverage", "strict_three_way_accuracy", "selective_accuracy"):
                lo, hi = bootstrap_ci(
                    subset,
                    lambda s, mn=metric_name: primary_metrics(s, threshold).get(mn),  # type: ignore[arg-type]
                    reps=reps,
                    level=level,
                    seed=seed_base + idx * 31 + len(out),
                )
                row[metric_name + "_ci_low"] = lo
                row[metric_name + "_ci_high"] = hi
            out.append(row)
    return out


def threshold_curves(
    rows: list[dict[str, object]], thresholds: list[float]
) -> list[dict[str, object]]:
    out = []
    for (dataset, method), subset in sorted(grouped(rows, ("dataset", "method")).items()):
        for tau in thresholds:
            m = primary_metrics(subset, tau)
            out.append(
                {
                    "dataset": dataset,
                    "method": method,
                    "threshold": tau,
                    "n": m["n"],
                    "coverage": m["coverage"],
                    "strict_three_way_accuracy": m["strict_three_way_accuracy"],
                    "selective_accuracy": m["selective_accuracy"],
                    "selective_risk": m["selective_risk"],
                    "false_assertion_rate_on_unresolved": m["false_assertion_rate_on_unresolved"],
                }
            )
    return out


def reliability(rows: list[dict[str, object]]) -> list[dict[str, object]]:
    bins = [(0.0, 0.5), (0.5, 1.0), (1.0, 1.5), (1.5, 2.0), (2.0, 3.0), (3.0, math.inf)]
    out = []
    for (dataset, method), subset in sorted(grouped(rows, ("dataset", "method")).items()):
        for lo, hi in bins:
            bucket = [r for r in subset if lo <= abs(float(r["score_f"])) < hi]
            if not bucket:
                continue
            directional = [r for r in bucket if r["ground_truth"] in {"support", "veto"}]
            direction_ok = sum(
                (r["ground_truth"] == "support" and float(r["score_f"]) > 0)
                or (r["ground_truth"] == "veto" and float(r["score_f"]) < 0)
                for r in directional
            )
            unresolved = [r for r in bucket if r["ground_truth"] == "unresolved"]
            false_assert = sum(abs(float(r["score_f"])) >= 2.0 for r in unresolved)
            out.append(
                {
                    "dataset": dataset,
                    "method": method,
                    "abs_score_bin_low": lo,
                    "abs_score_bin_high": "inf" if math.isinf(hi) else hi,
                    "n": len(bucket),
                    "directional_n": len(directional),
                    "directional_accuracy": direction_ok / len(directional) if directional else None,
                    "unresolved_truth_n": len(unresolved),
                    "unresolved_false_assertion_rate": (
                        false_assert / len(unresolved) if unresolved else None
                    ),
                }
            )
    return out


def paired_comparisons(rows: list[dict[str, object]], threshold: float) -> list[dict[str, object]]:
    by_pair: dict[tuple[str, str], dict[str, dict[str, object]]] = {}
    for r in rows:
        by_pair.setdefault((str(r["dataset"]), str(r["paired_key"])), {})[str(r["method"])] = r
    datasets = sorted({k[0] for k in by_pair})
    out = []
    for dataset in datasets:
        methods = sorted({
            method
            for (ds, _), entries in by_pair.items()
            if ds == dataset
            for method in entries
        })
        for a, b in itertools.combinations(methods, 2):
            cases = []
            for (ds, _), entries in by_pair.items():
                if ds == dataset and a in entries and b in entries:
                    cases.append((entries[a], entries[b]))
            if not cases:
                continue
            a_wins = b_wins = ties = 0
            abs_deltas = []
            for ra, rb in cases:
                truth = str(ra["ground_truth"])
                ca = decision(float(ra["score_f"]), threshold) == truth
                cb = decision(float(rb["score_f"]), threshold) == truth
                if ca and not cb:
                    a_wins += 1
                elif cb and not ca:
                    b_wins += 1
                else:
                    ties += 1
                abs_deltas.append(abs(float(ra["score_f"])) - abs(float(rb["score_f"])))
            out.append(
                {
                    "dataset": dataset,
                    "method_a": a,
                    "method_b": b,
                    "paired_n": len(cases),
                    "strict_accuracy_wins_a": a_wins,
                    "strict_accuracy_wins_b": b_wins,
                    "strict_accuracy_ties": ties,
                    "median_abs_score_delta_a_minus_b": statistics.median(abs_deltas),
                }
            )
    return out


def average_ranks(values: list[float]) -> list[float]:
    order = sorted(range(len(values)), key=values.__getitem__)
    ranks = [0.0] * len(values)
    i = 0
    while i < len(order):
        j = i + 1
        while j < len(order) and values[order[j]] == values[order[i]]:
            j += 1
        rank = (i + 1 + j) / 2.0
        for k in order[i:j]:
            ranks[k] = rank
        i = j
    return ranks


def pearson(a: list[float], b: list[float]) -> float | None:
    if len(a) < 2 or len(a) != len(b):
        return None
    ma, mb = statistics.mean(a), statistics.mean(b)
    va = sum((x - ma) ** 2 for x in a)
    vb = sum((x - mb) ** 2 for x in b)
    if va <= 0 or vb <= 0:
        return None
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / math.sqrt(va * vb)


def dose_response(rows: list[dict[str, object]]) -> list[dict[str, object]]:
    out = []
    dose = [r for r in rows if r.get("trial_family") == "dose_response" and r["physical_delta_f"] is not None]
    for (dataset, method), subset in sorted(grouped(dose, ("dataset", "method")).items()):
        x = [abs(float(r["physical_delta_f"])) for r in subset]
        y = [abs(float(r["score_f"])) for r in subset]
        rho = pearson(average_ranks(x), average_ranks(y))
        out.append({"dataset": dataset, "method": method, "n": len(subset), "spearman_abs_delta_abs_score": rho})
    return out


def robustness(rows: list[dict[str, object]], threshold: float) -> list[dict[str, object]]:
    axes = {
        "measurement_noise_sigma": "measurement_noise_sigma_f",
        "pose_noise_sigma": "pose_noise_sigma_f",
        "missing_fraction": "missing_fraction_f",
        "channel_dependence": "channel_dependence_f",
    }
    out = []
    for axis, parsed in axes.items():
        eligible = [r for r in rows if r[parsed] is not None]
        groups: dict[tuple[str, str, float], list[dict[str, object]]] = {}
        for r in eligible:
            groups.setdefault((str(r["dataset"]), str(r["method"]), float(r[parsed])), []).append(r)
        for (dataset, method, level), subset in sorted(groups.items()):
            m = primary_metrics(subset, threshold)
            out.append(
                {
                    "axis": axis,
                    "level": level,
                    "dataset": dataset,
                    "method": method,
                    "n": m["n"],
                    "coverage": m["coverage"],
                    "strict_three_way_accuracy": m["strict_three_way_accuracy"],
                    "selective_accuracy": m["selective_accuracy"],
                    "false_assertion_rate_on_unresolved": m["false_assertion_rate_on_unresolved"],
                    "median_abs_score": m["median_abs_score"],
                }
            )
    return out


def negative_controls(rows: list[dict[str, object]], threshold: float) -> list[dict[str, object]]:
    controls = [r for r in rows if bool(r["negative_control_b"])]
    out = []
    for (dataset, method), subset in sorted(grouped(controls, ("dataset", "method")).items()):
        asserted = sum(decision(float(r["score_f"]), threshold) != "unresolved" for r in subset)
        out.append(
            {
                "dataset": dataset,
                "method": method,
                "n": len(subset),
                "false_assertion_n": asserted,
                "false_assertion_rate": asserted / len(subset) if subset else None,
                "median_abs_score": statistics.median(abs(float(r["score_f"])) for r in subset) if subset else None,
            }
        )
    return out


def confidence_calibration(rows: list[dict[str, object]], threshold: float) -> list[dict[str, object]]:
    out = []
    eligible = [r for r in rows if r["confidence_f"] is not None]
    for (dataset, method), subset in sorted(grouped(eligible, ("dataset", "method")).items()):
        if not subset:
            continue
        brier = statistics.mean(
            (float(r["confidence_f"]) - float(decision(float(r["score_f"]), threshold) == r["ground_truth"])) ** 2
            for r in subset
        )
        bins = [[] for _ in range(10)]
        for r in subset:
            c = float(r["confidence_f"])
            bins[min(9, int(c * 10))].append(r)
        ece = 0.0
        for bucket in bins:
            if not bucket:
                continue
            conf = statistics.mean(float(r["confidence_f"]) for r in bucket)
            acc = statistics.mean(
                float(decision(float(r["score_f"]), threshold) == r["ground_truth"])
                for r in bucket
            )
            ece += len(bucket) / len(subset) * abs(conf - acc)
        out.append({"dataset": dataset, "method": method, "n": len(subset), "brier": brier, "ece_10": ece})
    return out


def failure_cases(rows: list[dict[str, object]], threshold: float) -> list[dict[str, object]]:
    out = []
    for r in rows:
        truth = str(r["ground_truth"])
        d = decision(float(r["score_f"]), threshold)
        failure = ""
        if truth == "support" and d == "veto":
            failure = "false_veto"
        elif truth == "veto" and d == "support":
            failure = "false_support"
        elif truth == "unresolved" and d != "unresolved":
            failure = "false_assertion_on_unresolved"
        elif truth == "support" and d == "unresolved":
            failure = "missed_support"
        elif truth == "veto" and d == "unresolved":
            failure = "missed_veto"
        if failure:
            out.append(
                {
                    "trial_id": r["trial_id"],
                    "paired_key": r["paired_key"],
                    "dataset": r["dataset"],
                    "scene": r["scene"],
                    "split": r["split"],
                    "trial_family": r["trial_family"],
                    "method": r["method"],
                    "ground_truth": truth,
                    "decision": d,
                    "score": r["score_f"],
                    "failure_class": failure,
                    "source_artifact": r["source_artifact"],
                }
            )
    return out


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    fields = list(rows[0])
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)


def self_test() -> None:
    rows: list[dict[str, str]] = []
    for i, (truth, score) in enumerate([
        ("support", 2.5), ("veto", -2.5), ("unresolved", 0.1), ("support", 0.5),
    ]):
        rows.append({
            "trial_id": f"t{i}", "paired_key": f"p{i}", "dataset": "toy", "scene": str(i),
            "split": "development", "evidence_class": "test", "confirmatory": "false",
            "trial_family": "truth_control", "ground_truth": truth, "method": "m",
            "score": str(score), "decision": decision(score, 2.0), "decision_threshold": "2",
            "confidence": "", "physical_delta": "", "target_error_delta": "",
            "measurement_noise_sigma": "", "pose_noise_sigma": "", "missing_fraction": "",
            "channel_dependence": "", "negative_control": "false", "seed": "0",
            "source_artifact": "self-test", "notes": "",
        })
    prepared = prepare(rows)
    m = primary_metrics(prepared, 2.0)
    assert m["n"] == 4
    assert m["coverage"] == 0.5
    assert m["strict_three_way_accuracy"] == 0.75
    assert len(failure_cases(prepared, 2.0)) == 1
    lo, hi = bootstrap_ci(
        prepared,
        lambda s: primary_metrics(s, 2.0)["strict_three_way_accuracy"],  # type: ignore[return-value]
        reps=100,
        level=0.95,
        seed=1,
    )
    assert lo is not None and hi is not None and lo <= hi
    print("VALID publication-validation analyzer self-test")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--records", type=Path, default=Path("build/publication-validation/records.csv"))
    ap.add_argument("--protocol", type=Path, default=Path("research/validation/protocol_v1.json"))
    ap.add_argument("--out", type=Path, default=Path("build/publication-validation/analysis"))
    ap.add_argument("--bootstrap-reps", type=int)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        self_test()
        return 0

    protocol = json.loads(args.protocol.read_text(encoding="utf-8"))
    threshold = float(protocol["primary_decision_threshold_abs_z"])
    thresholds = [float(x) for x in protocol["diagnostic_threshold_sweep"]]
    reps = args.bootstrap_reps or int(protocol["bootstrap"]["replicates"])
    level = float(protocol["bootstrap"]["confidence_level"])

    rows = prepare(read_csv(args.records))
    if not rows:
        raise SystemExit("record table is empty")
    args.out.mkdir(parents=True, exist_ok=True)

    summaries = method_summary(rows, threshold, reps, level)
    curves = threshold_curves(rows, thresholds)
    rel = reliability(rows)
    paired = paired_comparisons(rows, threshold)
    dose = dose_response(rows)
    robust = robustness(rows, threshold)
    controls = negative_controls(rows, threshold)
    calibration = confidence_calibration(rows, threshold)
    failures = failure_cases(rows, threshold)

    write_csv(args.out / "method_summary.csv", summaries)
    write_csv(args.out / "risk_coverage.csv", curves)
    write_csv(args.out / "reliability.csv", rel)
    write_csv(args.out / "paired_method_comparison.csv", paired)
    write_csv(args.out / "dose_response.csv", dose)
    write_csv(args.out / "robustness.csv", robust)
    write_csv(args.out / "negative_controls.csv", controls)
    write_csv(args.out / "calibration.csv", calibration)
    write_csv(args.out / "failure_cases.csv", failures)

    manifest = {
        "schema": "vulkax.publication_validation_analysis",
        "version": 1,
        "primary_threshold_abs_z": threshold,
        "record_count": len(rows),
        "dataset_count": len({str(r["dataset"]) for r in rows}),
        "method_count": len({str(r["method"]) for r in rows}),
        "confirmatory_record_count": sum(bool(r["confirmatory_b"]) for r in rows),
        "prospective_validation_record_count": sum(str(r["split"]) == "validation" for r in rows),
        "prospective_development_record_count": sum(str(r["split"]) == "development" for r in rows),
        "retrospective_or_followon_record_count": sum(
            str(r["split"]) in {"retrospective", "frozen_followon"} for r in rows
        ),
        "negative_control_record_count": sum(bool(r["negative_control_b"]) for r in rows),
        "bootstrap_replicates": reps,
        "confidence_level": level,
        "claim_guard": (
            "Summary tables mix no provenance silently: dataset/split/evidence_class remain in "
            "the source records. Existing D4V/OFC normalized rows are diagnostic historical evidence."
        ),
        "artifacts": [
            "method_summary.csv", "risk_coverage.csv", "reliability.csv",
            "paired_method_comparison.csv", "dose_response.csv", "robustness.csv",
            "negative_controls.csv", "calibration.csv", "failure_cases.csv",
        ],
    }
    (args.out / "summary.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("VALID publication validation analysis")
    print("RECORDS", len(rows))
    print("METHODS", manifest["method_count"])
    print("CONFIRMATORY", manifest["confirmatory_record_count"])
    print("OUT", args.out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

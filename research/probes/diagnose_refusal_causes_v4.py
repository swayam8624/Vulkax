#!/usr/bin/env python3
"""Fresh Validation-4 for a convergence-aware refusal-cause prototype.

Primary hypothesis was fixed before Validation-4 labels: replacing absolute
dt->dt/2 disagreement with the dt refinement contraction ratio should prevent
large-but-converging targets from masquerading as a numerical-failure cause.
Both prototype classifiers are trained on the original calibration domain only.
"""
import csv
import json
import math
import pathlib
import statistics
import sys

if len(sys.argv) < 4:
    raise SystemExit(
        "usage: diagnose_refusal_causes_v4.py calibration.csv validation4.csv out_dir"
    )

cal_path = pathlib.Path(sys.argv[1])
val_path = pathlib.Path(sys.argv[2])
out = pathlib.Path(sys.argv[3])
out.mkdir(parents=True, exist_ok=True)

variants = ("fine_apic", "coarse_apic", "pic", "constrained_nu")
feature_sets = {
    "legacy_absolute_numerics": (
        "evidence_noise_ratio",
        "heldout_noise_ratio",
        "numerical_fraction",
        "scheme_fraction",
    ),
    "primary_convergence_aware": (
        "evidence_noise_ratio",
        "heldout_noise_ratio",
        "numerical_convergence_ratio",
        "scheme_fraction",
    ),
}


def read(path):
    rows = list(csv.DictReader(path.open()))
    if not rows:
        raise SystemExit(f"empty dataset: {path}")
    for row in rows:
        if row["variant"] not in variants:
            raise SystemExit(f"unknown planted variant: {row['variant']}")
        for key in {
            key for features in feature_sets.values() for key in features
        } | {"post_target_relative_error"}:
            value = float(row[key])
            if not math.isfinite(value) or value < 0.0:
                raise SystemExit(f"invalid {path}/{key}: {row[key]}")
    return rows


cal = read(cal_path)
val = read(val_path)


def fit_model(features):
    def transform(row):
        return [math.log1p(float(row[k])) for k in features]

    xs = [transform(row) for row in cal]
    means = [statistics.fmean(x[j] for x in xs) for j in range(len(features))]
    scales = []
    for j in range(len(features)):
        sd = statistics.stdev(x[j] for x in xs)
        scales.append(max(sd, 1.0e-12))

    def z(row):
        x = transform(row)
        return [(x[j] - means[j]) / scales[j] for j in range(len(features))]

    centroids = {}
    for variant in variants:
        group = [z(row) for row in cal if row["variant"] == variant]
        if not group:
            raise SystemExit(f"missing calibration class {variant}")
        centroids[variant] = [
            statistics.fmean(x[j] for x in group)
            for j in range(len(features))
        ]

    def predict(row):
        q = z(row)
        return min(
            variants,
            key=lambda v: sum(
                (q[j] - centroids[v][j]) ** 2 for j in range(len(features))
            ),
        )

    return predict, {
        "features": list(features),
        "mean": dict(zip(features, means)),
        "scale": dict(zip(features, scales)),
        "centroids": centroids,
    }


def evaluate(rows, predict):
    confusion = {truth: {pred: 0 for pred in variants} for truth in variants}
    unsafe_confusion = {
        truth: {pred: 0 for pred in variants} for truth in variants
    }
    correct = 0
    unsafe_correct = 0
    unsafe_count = 0
    for row in rows:
        truth = row["variant"]
        pred = predict(row)
        confusion[truth][pred] += 1
        correct += pred == truth
        if float(row["post_target_relative_error"]) > 0.10:
            unsafe_count += 1
            unsafe_confusion[truth][pred] += 1
            unsafe_correct += pred == truth

    recall = {}
    for variant in variants:
        n = sum(confusion[variant].values())
        recall[variant] = confusion[variant][variant] / n if n else None

    return {
        "cases": len(rows),
        "cause_accuracy": correct / len(rows),
        "unsafe_cases": unsafe_count,
        "unsafe_cause_accuracy": (
            unsafe_correct / unsafe_count if unsafe_count else None
        ),
        "per_variant_recall": recall,
        "confusion": confusion,
        "unsafe_confusion": unsafe_confusion,
    }


models = {}
for name, features in feature_sets.items():
    predict, model = fit_model(features)
    models[name] = {
        "model": model,
        "calibration": evaluate(cal, predict),
        "validation4": evaluate(val, predict),
    }

primary = models["primary_convergence_aware"]["validation4"]
result = {
    "schema": "vulkax.refusal_cause_validation4",
    "version": 1,
    "provenance": "synthetic-validation4",
    "selection_data": "original calibration only",
    "primary_hypothesis": (
        "Use two-level numerical convergence ratio rather than absolute refinement "
        "magnitude as the numerical cause feature."
    ),
    "explicitly_unused_for_design_or_selection": [
        "Validation-4 labels",
        "Validation-4 target errors",
    ],
    "models": models,
    "primary_success_rule": (
        "descriptive development gate: cause accuracy >= 0.80 and unsafe-cause "
        "accuracy >= 0.80 on fresh Validation-4"
    ),
    "primary_success": (
        primary["cause_accuracy"] >= 0.80
        and primary["unsafe_cause_accuracy"] is not None
        and primary["unsafe_cause_accuracy"] >= 0.80
    ),
    "warning": (
        "Synthetic planted-cause validation only. Success would justify further "
        "mechanistic testing, not a real-world causal-diagnosis or novelty claim."
    ),
}
(out / "cause_validation4.json").write_text(json.dumps(result, indent=2) + "\n")

print("VALID refusal-cause Validation-4")
for name, data in models.items():
    v = data["validation4"]
    print(
        "CAUSE_V4",
        name,
        "accuracy", f'{v["cause_accuracy"]:.4f}',
        "unsafe_accuracy",
        ("none" if v["unsafe_cause_accuracy"] is None
         else f'{v["unsafe_cause_accuracy"]:.4f}'),
        "recall", v["per_variant_recall"],
    )
print("PRIMARY_SUCCESS", result["primary_success"])

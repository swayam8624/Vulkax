#!/usr/bin/env python3
"""Calibration-only abstaining reason classifier for synthetic refusal causes.

The base reason classifier can fail badly under feature-distribution shift. This
probe adds an explicit unknown/out-of-support outcome. The abstention threshold
is chosen from calibration distances only; Validation-1/2/3 labels are never
used to select it.
"""
import csv
import json
import math
import pathlib
import statistics
import sys

if len(sys.argv) < 6:
    raise SystemExit(
        "usage: diagnose_refusal_causes_abstain.py calibration.csv validation1.csv "
        "validation2.csv validation3.csv out_dir"
    )

paths = {
    "calibration": pathlib.Path(sys.argv[1]),
    "validation1": pathlib.Path(sys.argv[2]),
    "validation2": pathlib.Path(sys.argv[3]),
    "validation3": pathlib.Path(sys.argv[4]),
}
out = pathlib.Path(sys.argv[5])
out.mkdir(parents=True, exist_ok=True)

noise = {
    "calibration": 2.0e-5,
    "validation1": 2.0e-5,
    "validation2": 2.5e-5,
    "validation3": 2.2e-5,
}
variants = ("fine_apic", "coarse_apic", "pic", "constrained_nu")
features = (
    "evidence_noise_ratio",
    "heldout_noise_ratio",
    "numerical_fraction",
    "scheme_fraction",
)


def read_dataset(name, path):
    rows = list(csv.DictReader(path.open()))
    if not rows:
        raise SystemExit(f"empty dataset: {name}")
    for row in rows:
        if row["variant"] not in variants:
            raise SystemExit(f"unknown variant {row['variant']}")
        if not row.get("heldout_noise_ratio"):
            row["heldout_noise_ratio"] = str(
                float(row["repaired_heldout_rms_m"]) / noise[name]
            )
        for key in features + ("post_target_relative_error",):
            value = float(row[key])
            if not math.isfinite(value) or value < 0.0:
                raise SystemExit(f"invalid {name}/{key}: {row[key]}")
    return rows


datasets = {name: read_dataset(name, path) for name, path in paths.items()}
calibration = datasets["calibration"]


def transformed(row):
    return [math.log1p(float(row[key])) for key in features]


cal_x = [transformed(row) for row in calibration]
means = [statistics.fmean(x[j] for x in cal_x) for j in range(len(features))]
scales = [
    max(statistics.stdev(x[j] for x in cal_x), 1.0e-12)
    for j in range(len(features))
]


def z(row):
    x = transformed(row)
    return [(x[j] - means[j]) / scales[j] for j in range(len(features))]


def centroid(rows):
    zs = [z(row) for row in rows]
    return [statistics.fmean(x[j] for x in zs) for j in range(len(features))]


centroids = {
    variant: centroid([row for row in calibration if row["variant"] == variant])
    for variant in variants
}


def distance2(point, center):
    return sum((point[j] - center[j]) ** 2 for j in range(len(point)))


def nearest(row):
    q = z(row)
    distances = {variant: distance2(q, center) for variant, center in centroids.items()}
    pred = min(distances, key=distances.get)
    return pred, math.sqrt(distances[pred])


# Calibration-only threshold: estimate how far a correctly labeled calibration
# example may sit from its own class center. Leave-one-out class centers reduce
# self-inclusion optimism.
loo_distances = []
for i, row in enumerate(calibration):
    truth = row["variant"]
    group = [
        other for j, other in enumerate(calibration)
        if j != i and other["variant"] == truth
    ]
    if not group:
        raise SystemExit(f"cannot form leave-one-out center for {truth}")
    q = z(row)
    loo_distances.append(math.sqrt(distance2(q, centroid(group))))


def quantile(values, q):
    xs = sorted(values)
    position = (len(xs) - 1) * q
    lo = int(math.floor(position))
    hi = int(math.ceil(position))
    if lo == hi:
        return xs[lo]
    return xs[lo] * (hi - position) + xs[hi] * (position - lo)


# Fixed before any validation labels are inspected. 95% is a conventional
# calibration-support envelope, not a target-error-tuned hyperparameter.
threshold = quantile(loo_distances, 0.95)


def summarize(rows):
    assigned = []
    abstained = []
    confusion = {truth: {pred: 0 for pred in variants} for truth in variants}
    for row in rows:
        pred, dist = nearest(row)
        record = (row, pred, dist)
        if dist > threshold:
            abstained.append(record)
        else:
            assigned.append(record)
            confusion[row["variant"]][pred] += 1

    correct = sum(pred == row["variant"] for row, pred, _ in assigned)
    unsafe_assigned = [
        item for item in assigned
        if float(item[0]["post_target_relative_error"]) > 0.10
    ]
    unsafe_correct = sum(
        pred == row["variant"] for row, pred, _ in unsafe_assigned
    )
    unsafe_abstained = sum(
        float(row["post_target_relative_error"]) > 0.10
        for row, _, _ in abstained
    )

    return {
        "cases": len(rows),
        "assigned": len(assigned),
        "cause_coverage": len(assigned) / len(rows),
        "abstained": len(abstained),
        "abstention_rate": len(abstained) / len(rows),
        "assigned_cause_accuracy": correct / len(assigned) if assigned else None,
        "unsafe_assigned_cases": len(unsafe_assigned),
        "unsafe_assigned_cause_accuracy": (
            unsafe_correct / len(unsafe_assigned) if unsafe_assigned else None
        ),
        "unsafe_abstained_cases": unsafe_abstained,
        "unsafe_abstention_fraction_of_all_unsafe": (
            unsafe_abstained
            / sum(float(row["post_target_relative_error"]) > 0.10 for row in rows)
            if any(float(row["post_target_relative_error"]) > 0.10 for row in rows)
            else None
        ),
        "median_nearest_distance": statistics.median(nearest(row)[1] for row in rows),
        "confusion_assigned_only": confusion,
        "assigned_variants": {
            variant: sum(row["variant"] == variant for row, _, _ in assigned)
            for variant in variants
        },
        "abstained_variants": {
            variant: sum(row["variant"] == variant for row, _, _ in abstained)
            for variant in variants
        },
    }


result = {
    "schema": "vulkax.refusal_cause_abstention",
    "version": 1,
    "provenance": "synthetic planted-failure-family diagnostic",
    "classifier": "calibration-only nearest centroid in standardized log evidence space",
    "support_guard": "95th percentile leave-one-out within-class calibration distance",
    "distance_threshold": threshold,
    "selection_data": "original calibration only",
    "explicitly_unused_for_selection": [
        "Validation-1 labels",
        "Validation-2 labels",
        "Validation-3 labels",
        "target-error labels outside calibration",
    ],
    "datasets": {
        name: summarize(rows)
        for name, rows in datasets.items()
    },
    "warning": (
        "Synthetic cause labels are planted mismatch families. Abstention tests "
        "whether the evidence signature is inside calibration support; it does not "
        "establish real-world causal diagnosis."
    ),
}

(out / "refusal_cause_abstention.json").write_text(
    json.dumps(result, indent=2) + "\n"
)
print("VALID refusal-cause abstention diagnostic")
print("CAUSE_SUPPORT_THRESHOLD", threshold)
for name, data in result["datasets"].items():
    print(
        "CAUSE_ABSTAIN",
        name,
        "coverage", f'{data["cause_coverage"]:.4f}',
        "assigned_accuracy",
        ("none" if data["assigned_cause_accuracy"] is None
         else f'{data["assigned_cause_accuracy"]:.4f}'),
        "abstention", f'{data["abstention_rate"]:.4f}',
        "unsafe_abstention_fraction",
        ("none" if data["unsafe_abstention_fraction_of_all_unsafe"] is None
         else f'{data["unsafe_abstention_fraction_of_all_unsafe"]:.4f}'),
    )

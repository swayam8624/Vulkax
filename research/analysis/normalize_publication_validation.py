#!/usr/bin/env python3
"""Normalize Reality Probe evidence into one reviewer-facing record table.

This adapter deliberately preserves evidence provenance:
- D4V is frozen synthetic discovery evidence.
- OFC is a frozen fresh synthetic follow-on.
- --extra inputs must already use the publication-validation record contract.

The adapter never changes source labels or thresholds and never upgrades an old
result to confirmatory evidence.
"""
from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path
import tempfile

PRIMARY_THRESHOLD = 2.0

FIELDS = [
    "record_version", "trial_id", "paired_key", "dataset", "scene", "split",
    "evidence_class", "confirmatory", "trial_family", "ground_truth", "method",
    "score", "decision", "decision_threshold", "confidence", "physical_delta",
    "target_error_delta", "measurement_noise_sigma", "pose_noise_sigma",
    "missing_fraction", "channel_dependence", "negative_control", "seed",
    "source_artifact", "notes",
]

D4V_METHODS = {
    "reality_probe_dcs": ("dcs_progress_z", "dcs_decision"),
    "same_cost_raw_bundle": ("raw_bundle_progress_z", "raw_bundle_decision"),
    "raw_pairwise_intervention": ("raw_point_progress_z", "raw_point_decision"),
    "fisher_or_jacobian": ("fisher_progress_z", "fisher_decision"),
    "maximum_motion": ("maxmotion_progress_z", "maxmotion_decision"),
}

OFC_METHODS = {
    **D4V_METHODS,
    "orthogonal_force_compliance": ("force_progress_z", "force_decision"),
}


def decision_from_score(score: float, threshold: float = PRIMARY_THRESHOLD) -> str:
    if score >= threshold:
        return "support"
    if score <= -threshold:
        return "veto"
    return "unresolved"


def truth_from_legacy(label: str) -> str:
    if label == "beneficial":
        return "support"
    if label == "deceptive":
        return "veto"
    raise ValueError(f"unknown legacy truth label: {label!r}")


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def check_finite(value: str, field: str) -> float:
    x = float(value)
    if not math.isfinite(x):
        raise ValueError(f"{field} is not finite: {value}")
    return x


def normalize_legacy(
    path: Path,
    *,
    dataset: str,
    split: str,
    evidence_class: str,
    methods: dict[str, tuple[str, str]],
) -> list[dict[str, object]]:
    rows = read_csv(path)
    out: list[dict[str, object]] = []
    for row_idx, row in enumerate(rows):
        truth = truth_from_legacy(row["label"])
        scene = str(row.get("truth_id", row_idx))
        baseline = row.get("baseline", "")
        repair = row.get("repair", "")
        paired_key = f"{dataset}:{scene}:{baseline}:{repair}"
        target_delta = (
            check_finite(row["repair_target_m"], "repair_target_m")
            - check_finite(row["baseline_target_m"], "baseline_target_m")
        )
        for method, (score_key, decision_key) in methods.items():
            if score_key not in row:
                continue
            score = check_finite(row[score_key], score_key)
            derived = decision_from_score(score)
            source_decision = row.get(decision_key, derived)
            if source_decision != derived:
                raise ValueError(
                    f"{path}:{row_idx + 2}: source decision {source_decision!r} "
                    f"does not match frozen |z|={PRIMARY_THRESHOLD:g} rule ({derived!r})"
                )
            out.append(
                {
                    "record_version": 1,
                    "trial_id": f"{paired_key}:{method}",
                    "paired_key": paired_key,
                    "dataset": dataset,
                    "scene": scene,
                    "split": split,
                    "evidence_class": evidence_class,
                    "confirmatory": False,
                    "trial_family": "legacy_normalization",
                    "ground_truth": truth,
                    "method": method,
                    "score": f"{score:.17g}",
                    "decision": derived,
                    "decision_threshold": PRIMARY_THRESHOLD,
                    "confidence": "",
                    "physical_delta": "",
                    "target_error_delta": f"{target_delta:.17g}",
                    "measurement_noise_sigma": "",
                    "pose_noise_sigma": "",
                    "missing_fraction": "",
                    "channel_dependence": "",
                    "negative_control": False,
                    "seed": 0,
                    "source_artifact": str(path),
                    "notes": "normalized historical result; not prospective confirmation",
                }
            )
    return out


def normalize_extra(path: Path) -> list[dict[str, object]]:
    rows = read_csv(path)
    required = {
        "trial_id", "paired_key", "dataset", "scene", "split", "evidence_class",
        "confirmatory", "trial_family", "ground_truth", "method", "score",
        "decision", "decision_threshold", "negative_control", "seed",
        "source_artifact",
    }
    if not rows:
        return []
    missing = required - set(rows[0])
    if missing:
        raise ValueError(f"{path}: missing standardized fields: {sorted(missing)}")
    out: list[dict[str, object]] = []
    for i, row in enumerate(rows):
        score = check_finite(row["score"], "score")
        threshold = check_finite(row["decision_threshold"], "decision_threshold")
        if threshold <= 0:
            raise ValueError(f"{path}:{i+2}: threshold must be positive")
        truth = row["ground_truth"]
        if truth not in {"support", "veto", "unresolved"}:
            raise ValueError(f"{path}:{i+2}: bad ground_truth {truth!r}")
        if row["decision"] not in {"support", "veto", "unresolved"}:
            raise ValueError(f"{path}:{i+2}: bad decision {row['decision']!r}")
        # At the primary threshold, a standardized row's decision must be reproducible
        # from its signed score. This prevents opaque post-hoc decision overrides.
        derived = decision_from_score(score, threshold)
        if row["decision"] != derived:
            raise ValueError(
                f"{path}:{i+2}: decision {row['decision']!r} != score-derived {derived!r}"
            )
        merged = {key: row.get(key, "") for key in FIELDS}
        merged["record_version"] = int(row.get("record_version") or 1)
        out.append(merged)
    return out


def write_records(rows: list[dict[str, object]], out: Path) -> None:
    ids: set[str] = set()
    for row in rows:
        trial_id = str(row["trial_id"])
        if trial_id in ids:
            raise ValueError(f"duplicate trial_id: {trial_id}")
        ids.add(trial_id)
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=FIELDS, extrasaction="ignore")
        writer.writeheader()
        for row in rows:
            writer.writerow({k: row.get(k, "") for k in FIELDS})


def self_test() -> None:
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        d4v = root / "d4v.csv"
        d4v.write_text(
            "truth_id,baseline,repair,baseline_target_m,repair_target_m,label,"
            "dcs_progress_z,dcs_decision,raw_bundle_progress_z,raw_bundle_decision,"
            "raw_point_progress_z,raw_point_decision,fisher_progress_z,fisher_decision,"
            "maxmotion_progress_z,maxmotion_decision\n"
            "1,a,b,0.2,0.3,deceptive,-2.5,veto,-1.0,unresolved,"
            "-2.1,veto,-0.2,unresolved,-3.0,veto\n",
            encoding="utf-8",
        )
        rows = normalize_legacy(
            d4v,
            dataset="d4v_synthetic",
            split="retrospective",
            evidence_class="frozen_synthetic_discovery",
            methods=D4V_METHODS,
        )
        assert len(rows) == 5
        assert all(r["ground_truth"] == "veto" for r in rows)
        assert next(r for r in rows if r["method"] == "reality_probe_dcs")["decision"] == "veto"
        output = root / "records.csv"
        write_records(rows, output)
        assert len(read_csv(output)) == 5
    print("VALID publication-validation normalizer self-test")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--d4v", type=Path)
    ap.add_argument("--ofc", type=Path)
    ap.add_argument("--extra", type=Path, action="append", default=[])
    ap.add_argument("--out", type=Path, default=Path("build/publication-validation/records.csv"))
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        self_test()
        return 0

    rows: list[dict[str, object]] = []
    if args.d4v:
        rows += normalize_legacy(
            args.d4v,
            dataset="d4v_synthetic",
            split="retrospective",
            evidence_class="frozen_synthetic_discovery",
            methods=D4V_METHODS,
        )
    if args.ofc:
        rows += normalize_legacy(
            args.ofc,
            dataset="ofc_synthetic",
            split="frozen_followon",
            evidence_class="frozen_synthetic_followon",
            methods=OFC_METHODS,
        )
    for path in args.extra:
        rows += normalize_extra(path)

    if not rows:
        raise SystemExit("no input records supplied")
    write_records(rows, args.out)
    print(f"VALID normalized publication records: {len(rows)} -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Consolidate post-final IRIS reviewer-hardening outputs.

This script is deliberately descriptive. It never changes the locked final-test
result, thresholds, decisions, media, or proposal schedule. It packages the
secondary analyses with SHA-256 provenance so manuscript claims can point to one
machine-readable ledger.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

DEFAULT_BUILD = Path("build")
LOCKED = Path("research/results/IRIS_PENDULUM_FINAL_TEST_RESULT_2026-09-23.json")

SOURCES = {
    "replayed_final_summary": "publication-validation/iris-pendulum-final-test/final_summary.json",
    "clustered_statistics": "publication-validation/iris-pendulum-final-test/clustered_statistics/summary.json",
    "strong_baselines": "publication-validation/iris-pendulum-postfinal-baselines/summary.json",
    "gt_hidden_proposals": "publication-validation/iris-pendulum-postfinal-proposals/summary.json",
    "truth_uncertainty": "publication-validation/iris-pendulum-postfinal-truth-uncertainty/summary.json",
    "channel_dependence": "publication-validation/iris-pendulum-postfinal-dependence/summary.json",
    "corruption_robustness": "publication-validation/iris-pendulum-postfinal-robustness/summary.json",
    "official_iris_reference": "publication-validation/iris-official-reference/manifest.json",
    "hardening_manifest": "publication-validation/reviewer-hardening-manifest.json",
}

def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()

def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))

def check_locked_replay(locked: dict, replay: dict) -> None:
    assert replay["take_count"] == locked["dataset"]["takes"]
    assert replay["quality_pass_take_count"] == locked["dataset"]["quality_pass_takes"]
    assert replay["confirmatory_record_count"] == locked["evidence"]["confirmatory_records"]
    assert abs(
        replay["median_period_inferred_length_relative_error"]
        - locked["physical_recovery"]["median_period_inferred_length_relative_error"]
    ) < 1e-12
    for key in ("primary", "baseline"):
        r, q = replay[key], locked[key]
        assert r["method"] == q["method"]
        assert r["strict_correct_n"] == q["correct"]
        assert r["n"] == q["total"]
        assert abs(r["strict_accuracy"] - q["strict_accuracy"]) < 1e-12
        for truth in ("support", "veto", "unresolved"):
            assert r["truth"][truth]["correct_n"] == q[f"{truth}_correct"]
            assert r["truth"][truth]["n"] == q[f"{truth}_total"]
    assert replay["paired_primary_only_correct"] == locked["paired"]["primary_only_correct"]
    assert replay["paired_baseline_only_correct"] == locked["paired"]["baseline_only_correct"]
    assert replay["paired_ties"] == locked["paired"]["ties"]

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--build-dir", type=Path, default=DEFAULT_BUILD)
    ap.add_argument(
        "--out",
        type=Path,
        default=Path("build/publication-validation/iris-pendulum-postfinal-hardening-summary.json"),
    )
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        assert set(SOURCES) == {
            "replayed_final_summary",
            "clustered_statistics",
            "strong_baselines",
            "gt_hidden_proposals",
            "truth_uncertainty",
            "channel_dependence",
            "corruption_robustness",
            "official_iris_reference",
            "hardening_manifest",
        }
        print("VALID post-final hardening packer self-test")
        return 0

    if not LOCKED.is_file():
        raise SystemExit(f"missing locked result: {LOCKED}")
    locked = read_json(LOCKED)

    missing = []
    entries = {}
    for key, rel in SOURCES.items():
        path = args.build_dir / rel
        if not path.is_file():
            missing.append(str(path))
            continue
        entries[key] = {
            "path": str(path),
            "sha256": sha256(path),
            "bytes": path.stat().st_size,
            "payload": read_json(path),
        }
    if missing:
        raise SystemExit("missing post-final hardening outputs:\n" + "\n".join(missing))

    check_locked_replay(locked, entries["replayed_final_summary"]["payload"])

    report = {
        "schema": "vulkax.iris_pendulum_postfinal_hardening_summary",
        "version": 1,
        "locked_result": {
            "path": str(LOCKED),
            "sha256": sha256(LOCKED),
            "lock_commit": locked["lock_commit"],
            "dataset_revision": locked["dataset"]["revision"],
            "unchanged": True,
            "exact_replay_verified": True,
        },
        "physical_video_units": locked["evidence"]["physical_video_units"],
        "secondary_analysis_only": True,
        "analyses": entries,
        "claim_guard": (
            "Post-final reviewer-hardening evidence only. The locked IRIS result "
            "remains the confirmatory result; these analyses may qualify or contextualize "
            "it but must never replace, retune, or relabel it."
        ),
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print("VALID consolidated IRIS post-final hardening ledger")
    print("OUT", args.out)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())

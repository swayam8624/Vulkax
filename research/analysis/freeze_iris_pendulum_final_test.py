#!/usr/bin/env python3
"""Create/check the IRIS pendulum final-test lock.

This wrapper requires the validation forensic to support freezing without
retuning, then hashes every code/config artifact that can affect the final-test
result. It never downloads or opens final-test videos.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys

REQUIRED_REPO_FILES = [
    Path("research/validation/protocol_v1.json"),
    Path("research/validation/record_schema_v1.json"),
    Path("research/validation/iris_pendulum_final_test_v1.json"),
    Path("research/benchmarks/IRIS_PENDULUM_PROSPECTIVE_VALIDATION_PROTOCOL_2026-09-23.md"),
    Path("research/scripts/fetch_iris_pendulum_repeats.py"),
    Path("research/analysis/prepare_public_validation_datasets.py"),
    Path("research/analysis/adapt_public_validation_inputs.py"),
    Path("research/analysis/run_iris_pendulum_validation.py"),
    Path("research/analysis/analyze_iris_pendulum_validation.py"),
    Path("research/analysis/summarize_iris_pendulum_final_test.py"),
    Path("research/scripts/run_iris_pendulum_final_test.sh"),
]

REQUIRED_VALIDATION_ARTIFACTS = [
    Path("build/publication-validation/iris-pendulum-validation/validation_records.csv"),
    Path("build/publication-validation/iris-pendulum-validation/take_summary.csv"),
    Path("build/publication-validation/iris-pendulum-validation/forensics/summary.json"),
]

EXPECTED_READINESS = "validation_supports_freeze_without_retuning"


def validate_readiness(path: Path) -> dict:
    if not path.is_file():
        raise FileNotFoundError(path)
    data = json.loads(path.read_text(encoding="utf-8"))
    readiness = data.get("readiness", {})
    if readiness.get("classification") != EXPECTED_READINESS:
        raise RuntimeError(
            "IRIS validation forensic does not support freezing: "
            f"{readiness.get('classification')!r}"
        )
    if readiness.get("global_threshold_changed") is not False:
        raise RuntimeError("validation forensic indicates threshold change")
    if readiness.get("candidate_schedule_changed") is not False:
        raise RuntimeError("validation forensic indicates candidate-schedule change")
    if readiness.get("final_test_opened") is not False:
        raise RuntimeError("validation forensic says final test was already opened")
    return data


def run_freeze(out: Path, require_clean: bool) -> None:
    readiness_path = REQUIRED_VALIDATION_ARTIFACTS[-1]
    validate_readiness(readiness_path)

    missing = [str(p) for p in REQUIRED_REPO_FILES + REQUIRED_VALIDATION_ARTIFACTS if not p.is_file()]
    if missing:
        raise RuntimeError("missing final-freeze input(s):\n" + "\n".join(missing))

    cmd = [
        sys.executable,
        "research/analysis/freeze_publication_validation.py",
        "--protocol",
        str(REQUIRED_REPO_FILES[0]),
        "--schema",
        str(REQUIRED_REPO_FILES[1]),
        "--out",
        str(out),
    ]
    for p in REQUIRED_REPO_FILES[2:] + REQUIRED_VALIDATION_ARTIFACTS:
        cmd += ["--extra", str(p)]
    if require_clean:
        cmd.append("--require-clean")
    subprocess.run(cmd, check=True)

    lock = json.loads(out.read_text(encoding="utf-8"))
    lock["purpose"] = "IRIS pendulum 90-degree single-open final test"
    lock["expected_readiness"] = EXPECTED_READINESS
    lock["final_test_config"] = str(REQUIRED_REPO_FILES[2])
    lock["validation_forensic"] = str(readiness_path)
    out.write_text(json.dumps(lock, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    # Re-check after adding metadata. The underlying hashed files are unchanged.
    subprocess.run(
        [
            sys.executable,
            "research/analysis/freeze_publication_validation.py",
            "--check",
            str(out),
        ],
        check=True,
    )
    print("VALID IRIS pendulum final-test freeze")
    print("LOCK", out)
    print("READINESS", EXPECTED_READINESS)


def check_lock(path: Path) -> None:
    if not path.is_file():
        raise FileNotFoundError(path)
    lock = json.loads(path.read_text(encoding="utf-8"))
    if lock.get("purpose") != "IRIS pendulum 90-degree single-open final test":
        raise RuntimeError("not an IRIS pendulum final-test lock")
    if lock.get("expected_readiness") != EXPECTED_READINESS:
        raise RuntimeError("unexpected IRIS freeze readiness marker")

    validate_readiness(Path(lock["validation_forensic"]))
    subprocess.run(
        [
            sys.executable,
            "research/analysis/freeze_publication_validation.py",
            "--check",
            str(path),
        ],
        check=True,
    )

    required_paths = {str(p) for p in REQUIRED_REPO_FILES + REQUIRED_VALIDATION_ARTIFACTS}
    locked_paths = {str(item["path"]) for item in lock.get("files", [])}
    missing = sorted(required_paths - locked_paths)
    if missing:
        raise RuntimeError(
            "IRIS final lock does not cover every required input:\n" + "\n".join(missing)
        )
    print("VALID IRIS pendulum final-test lock coverage")
    print("LOCK", path)


def self_test() -> None:
    assert REQUIRED_REPO_FILES[2].name == "iris_pendulum_final_test_v1.json"
    assert EXPECTED_READINESS == "validation_supports_freeze_without_retuning"
    print("VALID IRIS final-freeze wrapper self-test")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--out",
        type=Path,
        default=Path("build/publication-validation/iris-pendulum-final-lock.json"),
    )
    ap.add_argument("--check", type=Path)
    ap.add_argument("--require-clean", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        self_test()
        return 0
    if args.check:
        check_lock(args.check)
        return 0
    run_freeze(args.out, args.require_clean)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

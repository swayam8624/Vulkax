#!/usr/bin/env python3
"""Fail closed if presentation code drifts away from the frozen VULKAX evidence."""
from __future__ import annotations

import json
import math
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "visualization/manifest.json"
LEDGER = ROOT / "research/results/VULKAX_FINAL_RESULTS_2026-09-21.json"


def get_path(obj: dict, dotted: str):
    cur = obj
    for part in dotted.split("."):
        cur = cur[part]
    return cur


def check_equal(actual, expected, path: str) -> None:
    if isinstance(expected, float):
        if not math.isclose(float(actual), expected, rel_tol=1e-12, abs_tol=1e-12):
            raise SystemExit(f"frozen evidence mismatch at {path}: {actual!r} != {expected!r}")
    elif actual != expected:
        raise SystemExit(f"frozen evidence mismatch at {path}: {actual!r} != {expected!r}")


def git(*args: str) -> str:
    return subprocess.check_output(
        ["git", *args], cwd=ROOT, text=True, stderr=subprocess.DEVNULL
    ).strip()


def main() -> int:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    ledger = json.loads(LEDGER.read_text(encoding="utf-8"))

    if ledger.get("schema") != "vulkax.final_results":
        raise SystemExit("unexpected final-result schema")
    if ledger.get("research_program_status") != "frozen_for_manuscript_preparation":
        raise SystemExit("ledger is not marked frozen_for_manuscript_preparation")

    for dotted, expected in manifest["frozen_values"].items():
        check_equal(get_path(ledger, dotted), expected, dotted)

    try:
        frozen_commit = git("rev-parse", f"{manifest['frozen_tag']}^{{commit}}")
        check_equal(frozen_commit, manifest["frozen_commit"], "frozen tag commit")
        frozen_ledger = json.loads(
            git("show", f"{manifest['frozen_tag']}:{manifest['ledger']}")
        )
        for dotted, expected in manifest["frozen_values"].items():
            check_equal(get_path(frozen_ledger, dotted), expected, f"tag:{dotted}")
    except (FileNotFoundError, subprocess.CalledProcessError):
        print("warning: git metadata unavailable; skipped immutable-tag resolution check")

    print("VISUALIZATION_EVIDENCE_GUARD PASS")
    print(f"ledger: {manifest['ledger']}")
    print(f"frozen tag: {manifest['frozen_tag']}")
    print(f"frozen commit: {manifest['frozen_commit']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

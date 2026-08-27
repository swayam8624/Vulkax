#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys


def fail(message: str) -> None:
    raise ValueError(message)


def require_contains(text: str, needle: str, label: str) -> None:
    if needle not in text:
        fail(f"{label} is missing required text: {needle!r}")


def require_absent(text: str, needle: str, label: str) -> None:
    if needle in text:
        fail(f"{label} contains stale or forbidden text: {needle!r}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Audit release-facing Vulkax claims against the 0.90 hardening contract.")
    parser.add_argument("repo_root", nargs="?", default=".")
    parser.add_argument(
        "--expected-project-version",
        default="0.80.0",
        help="Expected CMake version while 0.90 is a behavior candidate. Change to 0.90.0 only for the release-head recheck.",
    )
    args = parser.parse_args()

    root = Path(args.repo_root).resolve()
    readme = (root / "README.md").read_text(encoding="utf-8")
    roadmap = (root / "docs" / "ROADMAP_1_0.md").read_text(encoding="utf-8")
    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")

    match = re.search(r"project\(Vulkax VERSION ([0-9]+\.[0-9]+\.[0-9]+) LANGUAGES CXX\)", cmake)
    if not match:
        fail("CMakeLists.txt does not expose the expected Vulkax semantic version")
    if match.group(1) != args.expected_project_version:
        fail(f"expected project version {args.expected_project_version}, found {match.group(1)}")

    require_contains(readme, "## Current implementation — Vulkax 0.80", "README")
    require_contains(readme, "## One-command captured-world research + showcase — 0.80", "README")
    require_contains(readme, "docs/CAPTURED_WORLD_RUN_0_80.md", "README")
    require_contains(readme, "docs/INSTALL_0_90.md", "README")
    require_contains(readme, "0.90  release hardening and documentation/performance audit", "README")
    require_contains(readme, "1.0   stable verified-rewritable-reality baseline", "README")

    require_absent(readme, "## Current implementation — Vulkax 0.70", "README")
    require_absent(readme, "The 0.70 milestone does not change the language standard.", "README")
    require_absent(
        readme,
        "0.80  one-command captured-world research + visual showcase demo\n0.90",
        "README remaining-milestone list",
    )

    require_contains(roadmap, "### 0.90 — release hardening", "roadmap")
    require_contains(roadmap, "warning/error cleanup in code touched by the 1.0 path", "roadmap")
    require_contains(roadmap, "performance report for the principal path", "roadmap")
    require_contains(roadmap, "installation/build instructions for macOS, Linux and Windows", "roadmap")

    required_files = [
        "docs/CAPTURED_WORLD_RUN_0_80.md",
        "docs/INSTALL_0_90.md",
        "docs/RELEASE_HARDENING_0_90.md",
        "schemas/evidence_registry.json",
        "scripts/benchmark_captured_world_run.py",
        "scripts/test_release_cli_failures.py",
        "scripts/validate_evidence_registry.py",
    ]
    for relative in required_files:
        if not (root / relative).is_file():
            fail(f"required 0.90 release-hardening file is missing: {relative}")

    print(
        "PASS release claim audit: "
        f"project_version={args.expected_project_version} current_documented_release=0.80 hardening_target=0.90"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"release claim audit failed: {error}", file=sys.stderr)
        raise SystemExit(1)

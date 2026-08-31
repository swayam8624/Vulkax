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


def project_version(cmake: str) -> str:
    match = re.search(r"project\(Vulkax VERSION ([0-9]+\.[0-9]+\.[0-9]+) LANGUAGES CXX\)", cmake)
    if not match:
        fail("CMakeLists.txt does not expose the expected Vulkax semantic version")
    return match.group(1)


def main() -> int:
    parser = argparse.ArgumentParser(description="Audit release-facing Vulkax claims against the implemented release contract.")
    parser.add_argument("repo_root", nargs="?", default=".")
    parser.add_argument(
        "--expected-project-version",
        default=None,
        help="Optional MAJOR.MINOR.PATCH expectation. When omitted the version is read from CMakeLists.txt.",
    )
    args = parser.parse_args()

    if args.expected_project_version is not None and not re.fullmatch(
        r"[0-9]+\.[0-9]+\.[0-9]+", args.expected_project_version
    ):
        parser.error("--expected-project-version must be MAJOR.MINOR.PATCH")

    root = Path(args.repo_root).resolve()
    readme = (root / "README.md").read_text(encoding="utf-8")
    roadmap = (root / "docs" / "ROADMAP_1_0.md").read_text(encoding="utf-8")
    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")

    detected_version = project_version(cmake)
    expected_version = args.expected_project_version or detected_version
    if detected_version != expected_version:
        fail(f"expected project version {expected_version}, found {detected_version}")

    current_release = ".".join(expected_version.split(".")[:2])
    if current_release not in {"0.80", "0.90", "1.0"}:
        parser.error("release audit supports the 0.80 behavior candidate, 0.90 hardening release, and 1.0 baseline")

    require_contains(readme, f"## Current implementation — Vulkax {current_release}", "README")
    require_contains(readme, "## One-command captured-world research + showcase — 0.80", "README")
    require_contains(readme, "docs/CAPTURED_WORLD_RUN_0_80.md", "README")
    require_contains(readme, "docs/MEASURED_BENCHMARK_0_45.md", "README")
    require_contains(readme, "docs/INSTALL_0_90.md", "README")
    require_contains(readme, "docs/PERFORMANCE_0_90.md", "README")

    require_absent(readme, "## Current implementation — Vulkax 0.70", "README")
    require_absent(readme, "The 0.70 milestone does not change the language standard.", "README")
    require_absent(
        readme,
        "0.80  one-command captured-world research + visual showcase demo\n0.90",
        "README remaining-milestone list",
    )

    if current_release == "0.90":
        require_absent(readme, "## Current implementation — Vulkax 0.80", "README release-head current label")
        require_absent(
            readme,
            "0.90  release hardening and documentation/performance audit\n1.0",
            "README completed 0.90 milestone",
        )
        require_contains(readme, "Vulkax 0.90 is the release-hardened baseline", "README 0.90 release statement")

    require_contains(roadmap, "### 0.45 — measured deformable benchmark — implemented", "roadmap")
    require_contains(roadmap, "### 0.80 — one-command end-to-end research demo — implemented", "roadmap")
    require_contains(roadmap, "### 0.90 — release hardening — implemented", "roadmap")
    require_contains(roadmap, "warning/error cleanup on the principal path", "roadmap")
    require_contains(roadmap, "performance report", "roadmap")
    require_contains(roadmap, "installation instructions", "roadmap")

    required_files = [
        "docs/MEASURED_BENCHMARK_0_45.md",
        "docs/CAPTURED_WORLD_RUN_0_80.md",
        "docs/INSTALL_0_90.md",
        "docs/RELEASE_HARDENING_0_90.md",
        "docs/PERFORMANCE_0_90.md",
        "schemas/evidence_registry.json",
        "scripts/benchmark_captured_world_run.py",
        "scripts/test_release_cli_failures.py",
        "scripts/validate_evidence_registry.py",
        "scripts/validate_measured_dot_c2.py",
    ]

    if current_release == "1.0":
        require_contains(readme, "docs/RELEASE_1_0.md", "README 1.0 release document")
        require_contains(readme, "Vulkax 1.0 is the stable verified-rewritable-reality baseline", "README 1.0 release statement")
        require_contains(roadmap, "### 1.0 — stable verified-rewritable-reality baseline — implemented", "roadmap")
        require_absent(readme, "1.0   stable verified-rewritable-reality baseline", "README stale remaining milestone")
        required_files.extend(
            [
                "docs/RELEASE_1_0.md",
                ".github/workflows/release-smoke.yml",
            ]
        )

    for relative in required_files:
        if not (root / relative).is_file():
            fail(f"required release-facing file is missing: {relative}")

    print(
        "PASS release claim audit: "
        f"project_version={expected_version} current_documented_release={current_release}"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        print(f"release claim audit failed: {error}", file=sys.stderr)
        raise SystemExit(1)

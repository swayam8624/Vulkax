#!/usr/bin/env python3

from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import subprocess
import sys


def invoke(executable: Path, args: list[str], repo_root: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(executable), *args],
        cwd=repo_root,
        text=True,
        capture_output=True,
        check=False,
    )


def combined(result: subprocess.CompletedProcess[str]) -> str:
    return result.stdout + result.stderr


def require_success(result: subprocess.CompletedProcess[str], needle: str, label: str) -> None:
    text = combined(result)
    if result.returncode != 0:
        raise RuntimeError(f"{label}: expected success, got {result.returncode}\n{text}")
    if needle not in text:
        raise RuntimeError(f"{label}: missing expected text {needle!r}\n{text}")


def require_failure(result: subprocess.CompletedProcess[str], needle: str, label: str) -> None:
    text = combined(result)
    if result.returncode == 0:
        raise RuntimeError(f"{label}: expected non-zero exit status\n{text}")
    if needle not in text:
        raise RuntimeError(f"{label}: missing expected text {needle!r}\n{text}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Exercise release-facing captured-world CLI failure cases.")
    parser.add_argument("--repo-root", default=".")
    parser.add_argument("--executable", default="build/vulkax")
    parser.add_argument("--output", default="build/release-cli-failures")
    args = parser.parse_args()

    repo_root = Path(args.repo_root).resolve()
    executable = (repo_root / args.executable).resolve()
    output_root = (repo_root / args.output).resolve()
    if not executable.is_file():
        raise RuntimeError(f"Vulkax executable does not exist: {executable}")

    require_success(
        invoke(executable, ["captured-world-run", "--help"], repo_root),
        "usage: vulkax captured-world-run",
        "captured-world help",
    )
    require_failure(
        invoke(executable, ["captured-world-run"], repo_root),
        "usage: vulkax captured-world-run",
        "missing required arguments",
    )

    parser_prefix = ["missing.vkcap", "unused-output", "m4", "0.003", "1", "1", "1"]
    require_failure(
        invoke(executable, ["captured-world-run", *parser_prefix, "CUDA"], repo_root),
        "unknown captured-world-run render backend",
        "unknown backend",
    )
    require_failure(
        invoke(
            executable,
            ["captured-world-run", "missing.vkcap", "unused-output", "m4", "0.003", "0", "0", "0", "none"],
            repo_root,
        ),
        "objective direction must be finite and non-zero",
        "zero objective direction",
    )
    require_failure(
        invoke(
            executable,
            ["captured-world-run", *parser_prefix, "none", "--showcase", "invalid_scene"],
            repo_root,
        ),
        "showcase scene preset must be studio_pedestal or cloth_showcase",
        "unknown showcase preset",
    )
    require_failure(
        invoke(
            executable,
            ["captured-world-run", *parser_prefix, "none", "--showcase-resolution", "640x360"],
            repo_root,
        ),
        "showcase-specific options require --showcase <preset>",
        "orphan showcase option",
    )
    require_failure(
        invoke(
            executable,
            ["captured-world-run", *parser_prefix, "none", "--showcase", "cloth_showcase"],
            repo_root,
        ),
        "showcase output requires a native render backend",
        "showcase without renderer",
    )

    shutil.rmtree(output_root, ignore_errors=True)
    bundle_root = output_root / "bundle"
    result = invoke(executable, ["captured-deformable-generate-example", str(bundle_root)], repo_root)
    require_success(result, "Generated deterministic captured-deformable calibration example", "controlled bundle generation")

    run_root = output_root / "run"
    valid_args = [
        "captured-world-run",
        str(bundle_root / "capture.vkcap"),
        str(run_root),
        "m4",
        "0.003",
        "1",
        "1",
        "1",
        "none",
        "0.08",
        "0.01",
        "0.02",
        "12345",
    ]
    require_success(invoke(executable, valid_args, repo_root), "run_status: completed", "controlled no-render run")
    require_failure(
        invoke(executable, valid_args, repo_root),
        "captured-world-run output directory must be empty or absent",
        "non-empty output rejection",
    )

    print("PASS release CLI failure cases")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as error:
        print(f"release CLI failure regression failed: {error}", file=sys.stderr)
        raise SystemExit(1)

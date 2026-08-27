#!/usr/bin/env python3

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import time


def run(command: list[str], *, cwd: Path) -> subprocess.CompletedProcess[str]:
    result = subprocess.run(command, cwd=cwd, text=True, capture_output=True, check=False)
    if result.returncode != 0:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise RuntimeError(f"command failed with exit code {result.returncode}: {' '.join(command)}")
    return result


def load_certificate(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        certificate = json.load(stream)
    if certificate.get("schema") != "vulkax_captured_world_run" or certificate.get("schema_version") != 2:
        raise RuntimeError("benchmark received an unexpected captured-world certificate schema")
    if certificate.get("run_status") != "completed":
        raise RuntimeError("benchmark run did not complete")
    return certificate


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run the deterministic controlled captured-world path repeatedly and emit a wall-clock performance report."
    )
    parser.add_argument("--repo-root", default=".")
    parser.add_argument("--executable", default="build/vulkax")
    parser.add_argument("--output", default="build/release-hardening-performance")
    parser.add_argument("--iterations", type=int, default=3)
    parser.add_argument("--backend", choices=["none", "Vulkan", "Metal", "OpenGL"], default="none")
    parser.add_argument("--showcase", choices=["none", "studio_pedestal", "cloth_showcase"], default="none")
    parser.add_argument("--showcase-assets", default="build/demo-assets")
    parser.add_argument("--showcase-resolution", default="640x360")
    parser.add_argument("--turntable", type=int, default=4)
    args = parser.parse_args()

    if args.iterations < 1 or args.iterations > 50:
        parser.error("--iterations must lie in [1,50]")
    if args.showcase != "none" and args.backend == "none":
        parser.error("showcase benchmarking requires a native render backend")
    if args.turntable < 1 or args.turntable > 72:
        parser.error("--turntable must lie in [1,72]")

    repo_root = Path(args.repo_root).resolve()
    executable = (repo_root / args.executable).resolve()
    output_root = (repo_root / args.output).resolve()
    if not executable.is_file():
        raise RuntimeError(f"Vulkax executable does not exist: {executable}")

    shutil.rmtree(output_root, ignore_errors=True)
    output_root.mkdir(parents=True)
    bundle_root = output_root / "controlled-bundle"
    run([str(executable), "captured-deformable-generate-example", str(bundle_root)], cwd=repo_root)

    rows: list[dict[str, object]] = []
    for iteration in range(args.iterations):
        run_root = output_root / f"run-{iteration:02d}"
        command = [
            str(executable),
            "captured-world-run",
            str(bundle_root / "capture.vkcap"),
            str(run_root),
            "m4",
            "0.003",
            "1",
            "1",
            "1",
            args.backend,
            "0.08",
            "0.01",
            "0.02",
            str(12345 + iteration),
        ]
        if args.showcase != "none":
            command += [
                "--showcase",
                args.showcase,
                "--showcase-assets",
                str((repo_root / args.showcase_assets).resolve()),
                "--showcase-resolution",
                args.showcase_resolution,
                "--turntable",
                str(args.turntable),
            ]

        started = time.perf_counter()
        run(command, cwd=repo_root)
        elapsed = time.perf_counter() - started
        certificate = load_certificate(run_root / "certificate.json")
        rewrite = certificate["rewrite"]
        render = certificate["render"]
        showcase = certificate["showcase"]
        rows.append(
            {
                "iteration": iteration,
                "elapsed_seconds": f"{elapsed:.9f}",
                "backend_requested": args.backend,
                "render_produced": int(bool(render.get("produced"))),
                "render_backend": render.get("backend") or "none",
                "showcase": showcase.get("scene") if showcase.get("produced") else "none",
                "rewrite_status": rewrite.get("status"),
                "rollback_performed": int(bool(rewrite.get("rollback_performed"))),
                "artifact_count": len(certificate.get("artifacts", [])),
            }
        )

    report = output_root / "captured_world_performance.csv"
    fieldnames = list(rows[0].keys())
    with report.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    elapsed_values = [float(row["elapsed_seconds"]) for row in rows]
    summary = {
        "schema": "vulkax_captured_world_performance",
        "schema_version": 1,
        "platform": platform.platform(),
        "python": platform.python_version(),
        "iterations": len(rows),
        "backend_requested": args.backend,
        "showcase_requested": args.showcase,
        "minimum_seconds": min(elapsed_values),
        "maximum_seconds": max(elapsed_values),
        "mean_seconds": sum(elapsed_values) / len(elapsed_values),
        "measurement": "wall-clock orchestration time measured by Python time.perf_counter; evidence-only, not a correctness threshold",
    }
    with (output_root / "captured_world_performance_summary.json").open("w", encoding="utf-8") as stream:
        json.dump(summary, stream, indent=2, sort_keys=True)
        stream.write("\n")

    print(f"WROTE captured-world performance report: {report}")
    print(
        "PERFORMANCE evidence only: "
        f"iterations={len(rows)} mean={summary['mean_seconds']:.6f}s "
        f"min={summary['minimum_seconds']:.6f}s max={summary['maximum_seconds']:.6f}s"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, json.JSONDecodeError) as error:
        print(f"captured-world benchmark failed: {error}", file=sys.stderr)
        raise SystemExit(1)

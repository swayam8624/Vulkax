#!/usr/bin/env python3

import argparse
import hashlib
import json
import math
from pathlib import Path
import sys


def fail(message: str) -> None:
    raise ValueError(message)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def finite_number(value, label: str) -> float:
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        fail(f"{label} must be numeric")
    result = float(value)
    if not math.isfinite(result):
        fail(f"{label} must be finite")
    return result


def validate(root: Path, expected_backend: str | None) -> dict:
    certificate_path = root / "certificate.json"
    if not certificate_path.is_file():
        fail("certificate.json is missing")
    with certificate_path.open("r", encoding="utf-8") as stream:
        certificate = json.load(stream)

    if certificate.get("schema") != "vulkax_captured_world_run":
        fail("unexpected certificate schema")
    if certificate.get("schema_version") != 1:
        fail("unexpected certificate schema version")
    if certificate.get("status") != "verified":
        fail("captured-world-run rewrite is not verified")
    if certificate.get("source_kind") not in {"synthetic", "measured", "derived"}:
        fail("unexpected source kind")

    calibration = certificate.get("calibration")
    if not isinstance(calibration, dict):
        fail("calibration object is missing")
    finite_number(calibration.get("young_modulus"), "calibration young_modulus")
    finite_number(calibration.get("poisson_ratio"), "calibration poisson_ratio")
    finite_number(calibration.get("fit_dynamic_rms"), "calibration fit_dynamic_rms")
    finite_number(calibration.get("validation_dynamic_rms"), "calibration validation_dynamic_rms")

    adaptive = certificate.get("adaptive_proposal")
    if not isinstance(adaptive, dict):
        fail("adaptive_proposal object is missing")
    if not isinstance(adaptive.get("region_count"), int) or adaptive["region_count"] <= 0:
        fail("adaptive proposal has no regions")
    if not isinstance(adaptive.get("particle_count"), int) or adaptive["particle_count"] <= 0:
        fail("adaptive proposal has no particles")
    fraction = finite_number(adaptive.get("absolute_gradient_fraction"), "adaptive gradient fraction")
    if not (0.0 < fraction <= 1.0 + 1.0e-12):
        fail("adaptive gradient fraction is outside (0,1]")

    rewrite = certificate.get("rewrite")
    if not isinstance(rewrite, dict):
        fail("rewrite object is missing")
    if rewrite.get("rollback_performed") is not False:
        fail("verified rewrite unexpectedly rolled back")
    error = finite_number(rewrite.get("physical_error"), "rewrite physical_error")
    tolerance = finite_number(rewrite.get("physical_tolerance"), "rewrite physical_tolerance")
    if error > tolerance:
        fail("rewrite physical error exceeds tolerance")

    render = certificate.get("render")
    if not isinstance(render, dict) or not isinstance(render.get("produced"), bool):
        fail("render evidence object is malformed")
    if expected_backend == "none":
        if render["produced"]:
            fail("render was produced but validator expected none")
    elif expected_backend is not None:
        if not render["produced"]:
            fail("render evidence was not produced")
        if render.get("backend") != expected_backend:
            fail(f"expected render backend {expected_backend}, got {render.get('backend')}")
    if render["produced"]:
        finite_number(render.get("max_channel_difference"), "render max_channel_difference")
        finite_number(render.get("rmse"), "render rmse")
        finite_number(render.get("changed_pixel_fraction"), "render changed_pixel_fraction")

    artifacts = certificate.get("artifacts")
    if not isinstance(artifacts, list) or not artifacts:
        fail("certificate artifact index is empty")
    paths: set[str] = set()
    for index, artifact in enumerate(artifacts):
        if not isinstance(artifact, dict):
            fail(f"artifact {index} is not an object")
        relative = artifact.get("path")
        if not isinstance(relative, str) or not relative:
            fail(f"artifact {index} path is invalid")
        candidate = Path(relative)
        if candidate.is_absolute() or ".." in candidate.parts:
            fail(f"artifact path escapes output root: {relative}")
        if relative == "certificate.json":
            fail("certificate must not index itself")
        if relative in paths:
            fail(f"duplicate artifact path: {relative}")
        paths.add(relative)
        absolute = root / candidate
        if not absolute.is_file():
            fail(f"indexed artifact is missing: {relative}")
        size = artifact.get("bytes")
        if not isinstance(size, int) or size < 0 or size != absolute.stat().st_size:
            fail(f"artifact byte count mismatch: {relative}")
        digest = artifact.get("sha256")
        if not isinstance(digest, str) or digest != sha256_file(absolute):
            fail(f"artifact SHA-256 mismatch: {relative}")

    required = {
        "input/validated_manifest.txt",
        "calibration/material_grid.csv",
        "calibration/selected_summary.csv",
        "robustness/robustness.csv",
        "robustness/scenarios.csv",
        "influence/particle_adjoint.csv",
        "influence/adaptive_proposal_summary.csv",
        "influence/selected_rewrite_region.csv",
        "rewrite/transaction_evidence.csv",
        "rewrite/transaction_summary.csv",
        "rewrite/provenance.csv",
        "rewrite/physical_evidence/reference.csv",
        "rewrite/physical_evidence/counterfactual.csv",
        "rewrite/physical_evidence/adjoint.csv",
        "rewrite/physical_evidence/derivative_comparison.csv",
        "appearance/before.ply",
        "appearance/rewritten.ply",
        "run_summary.csv",
    }
    if render["produced"]:
        required.update({"render/before.ppm", "render/after.ppm", "render/comparison.csv"})
    missing = sorted(required - paths)
    if missing:
        fail("certificate does not index required artifacts: " + ", ".join(missing))

    return {
        "bundle_id": certificate.get("bundle_id"),
        "source_kind": certificate.get("source_kind"),
        "artifact_count": len(artifacts),
        "render_backend": render.get("backend", "none"),
        "adaptive_particle_count": adaptive["particle_count"],
        "adaptive_gradient_fraction": fraction,
        "physical_error": error,
        "physical_tolerance": tolerance,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate Vulkax captured-world-run evidence bundle")
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("expected_backend", nargs="?", choices=["Vulkan", "Metal", "OpenGL", "none"])
    args = parser.parse_args()
    try:
        result = validate(args.output_dir, args.expected_backend)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"INVALID captured-world-run evidence: {error}", file=sys.stderr)
        return 1
    print(
        "VALID captured-world-run evidence: "
        f"bundle={result['bundle_id']} source={result['source_kind']} "
        f"artifacts={result['artifact_count']} render={result['render_backend']} "
        f"adaptive_particles={result['adaptive_particle_count']} "
        f"adaptive_gradient_fraction={result['adaptive_gradient_fraction']:.12g} "
        f"physical_error={result['physical_error']:.12g} "
        f"physical_tolerance={result['physical_tolerance']:.12g}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

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


def validate(
    root: Path,
    expected_backend: str | None,
    expected_rewrite: str | None,
    expected_showcase: str | None,
) -> dict:
    certificate_path = root / "certificate.json"
    if not certificate_path.is_file():
        fail("certificate.json is missing")
    with certificate_path.open("r", encoding="utf-8") as stream:
        certificate = json.load(stream)

    if certificate.get("schema") != "vulkax_captured_world_run":
        fail("unexpected certificate schema")
    if certificate.get("schema_version") != 2:
        fail("unexpected certificate schema version")
    if certificate.get("run_status") != "completed":
        fail("captured-world-run did not complete")
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
    rewrite_status = rewrite.get("status")
    if rewrite_status not in {"verified", "rejected"}:
        fail("unexpected rewrite status")
    rollback = rewrite.get("rollback_performed")
    if not isinstance(rollback, bool):
        fail("rewrite rollback_performed must be boolean")
    if rewrite_status == "verified" and rollback:
        fail("verified rewrite unexpectedly rolled back")
    if rewrite_status == "rejected" and not rollback:
        fail("rejected rewrite must be rolled back")
    if expected_rewrite is not None and rewrite_status != expected_rewrite:
        fail(f"expected rewrite status {expected_rewrite}, got {rewrite_status}")

    error = finite_number(rewrite.get("physical_error"), "rewrite physical_error")
    tolerance = finite_number(rewrite.get("physical_tolerance"), "rewrite physical_tolerance")
    if error < 0.0 or tolerance < 0.0:
        fail("rewrite physical error/tolerance must be non-negative")
    # A rejected material rewrite can pass the nonlinear error threshold and still
    # fail an independent derivative oracle. Do not infer the full verifier verdict
    # from physical_error <= physical_tolerance alone.

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

    showcase = certificate.get("showcase")
    if not isinstance(showcase, dict) or not isinstance(showcase.get("produced"), bool):
        fail("showcase evidence object is malformed")
    showcase_produced = showcase["produced"]
    showcase_scene = showcase.get("scene")
    showcase_frames = showcase.get("turntable_frames")
    if showcase_produced:
        if showcase_scene not in {"studio_pedestal", "cloth_showcase"}:
            fail("unexpected showcase scene preset")
        if not isinstance(showcase_frames, int) or not (1 <= showcase_frames <= 72):
            fail("invalid showcase turntable frame count")
    else:
        if showcase_frames not in {0, None}:
            fail("disabled showcase reports turntable frames")
    if expected_showcase == "none" and showcase_produced:
        fail("showcase was produced but validator expected none")
    if expected_showcase not in {None, "none"}:
        if not showcase_produced:
            fail("expected showcase was not produced")
        if showcase_scene != expected_showcase:
            fail(f"expected showcase {expected_showcase}, got {showcase_scene}")

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
    if showcase_produced:
        required.update({
            "render/showcase/showcase_manifest.json",
            "render/showcase/contact_sheet.ppm",
            "render/showcase/summary_card.svg",
        })
        if rewrite_status == "verified":
            required.update({
                "render/showcase/hero_before.ppm",
                "render/showcase/hero_after.ppm",
                "render/showcase/closeup_before.ppm",
                "render/showcase/closeup_after.ppm",
            })
        else:
            required.update({
                "render/showcase/hero_baseline.ppm",
                "render/showcase/hero_rollback.ppm",
                "render/showcase/closeup_baseline.ppm",
                "render/showcase/closeup_rollback.ppm",
            })
        expected_turntable = {
            f"render/showcase/turntable/frame_{frame:03d}.ppm"
            for frame in range(showcase_frames)
        }
        required.update(expected_turntable)
        with (root / "render/showcase/showcase_manifest.json").open("r", encoding="utf-8") as stream:
            showcase_manifest = json.load(stream)
        if showcase_manifest.get("schema") != "vulkax_showcase" or showcase_manifest.get("schema_version") != 1:
            fail("unexpected showcase manifest schema/version")
        if showcase_manifest.get("rewrite_status") != rewrite_status:
            fail("showcase rewrite status disagrees with certificate")
        if showcase_manifest.get("rollback_state_shown") is not (rewrite_status == "rejected"):
            fail("showcase rollback visual semantics disagree with rewrite verdict")
        if showcase_manifest.get("turntable_frames") != showcase_frames:
            fail("showcase manifest turntable count disagrees with certificate")
        locked_sha = showcase_manifest.get("asset_lock_sha256")
        repository_lock = Path("assets/demo/showcase_assets.lock.json")
        if locked_sha and repository_lock.is_file() and locked_sha != sha256_file(repository_lock):
            fail("showcase asset-lock hash disagrees with repository lock")

    missing = sorted(required - paths)
    if missing:
        fail("certificate does not index required artifacts: " + ", ".join(missing))

    return {
        "bundle_id": certificate.get("bundle_id"),
        "source_kind": certificate.get("source_kind"),
        "artifact_count": len(artifacts),
        "render_backend": render.get("backend", "none"),
        "rewrite_status": rewrite_status,
        "rollback_performed": rollback,
        "showcase_scene": showcase_scene if showcase_produced else "none",
        "showcase_frames": showcase_frames or 0,
        "adaptive_particle_count": adaptive["particle_count"],
        "adaptive_gradient_fraction": fraction,
        "physical_error": error,
        "physical_tolerance": tolerance,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate Vulkax captured-world-run evidence bundle")
    parser.add_argument("output_dir", type=Path)
    parser.add_argument(
        "expected_backend_positional",
        nargs="?",
        choices=["Vulkan", "Metal", "OpenGL", "none"],
        help="legacy positional expected backend",
    )
    parser.add_argument(
        "--expected-backend",
        choices=["Vulkan", "Metal", "OpenGL", "none"],
        help="require a particular research render backend",
    )
    parser.add_argument(
        "--expected-rewrite",
        choices=["verified", "rejected"],
        help="require a particular rewrite verdict without conflating it with run success",
    )
    parser.add_argument(
        "--expected-showcase",
        choices=["studio_pedestal", "cloth_showcase", "none"],
        help="require a particular presentation showcase or no showcase",
    )
    args = parser.parse_args()
    expected_backend = args.expected_backend or args.expected_backend_positional
    if args.expected_backend and args.expected_backend_positional:
        parser.error("use either positional expected backend or --expected-backend, not both")
    try:
        result = validate(
            args.output_dir,
            expected_backend,
            args.expected_rewrite,
            args.expected_showcase,
        )
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"INVALID captured-world-run evidence: {error}", file=sys.stderr)
        return 1
    print(
        "VALID captured-world-run evidence: "
        f"bundle={result['bundle_id']} source={result['source_kind']} "
        f"artifacts={result['artifact_count']} render={result['render_backend']} "
        f"rewrite={result['rewrite_status']} rollback={int(result['rollback_performed'])} "
        f"showcase={result['showcase_scene']} frames={result['showcase_frames']} "
        f"adaptive_particles={result['adaptive_particle_count']} "
        f"adaptive_gradient_fraction={result['adaptive_gradient_fraction']:.12g} "
        f"physical_error={result['physical_error']:.12g} "
        f"physical_tolerance={result['physical_tolerance']:.12g}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

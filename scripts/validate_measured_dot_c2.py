#!/usr/bin/env python3
"""Validate the reproducible DOT C2 measured-source benchmark evidence.

This validator deliberately distinguishes real measured source geometry from the
Vulkax-derived physical/appearance proxy. A rejected verified-rewrite transaction
is a valid scientific outcome when it rolled back and the independent evidence
artifacts are complete.
"""

from __future__ import annotations

import csv
import math
import sys
from pathlib import Path

EXPECTED_DATASET_DOI = "10.13021/ORC2020/XXLVXM"
EXPECTED_ARCHIVE_MD5 = "0f347c3f95ed2def9fd81ba5236955b1"
EXPECTED_PARTICLES = 225
EXPECTED_OBSERVATIONS = 675
EXPECTED_ROBUSTNESS_ROWS = 6
MAX_REWRITE_LINEARIZATION_ERROR = 0.25
MAX_REWRITE_ADJOINT_ABSOLUTE_ERROR = 1.0e-8
MAX_REWRITE_ADJOINT_RELATIVE_ERROR = 5.0e-3
MIN_REWRITE_REFERENCE_DERIVATIVE_FOR_RELATIVE_CHECK = 1.0e-7


def fail(message: str) -> "None":
    raise SystemExit(f"DOT C2 measured benchmark INVALID: {message}")


def require_file(path: Path) -> Path:
    if not path.is_file() or path.stat().st_size == 0:
        fail(f"missing or empty artifact: {path}")
    return path


def read_csv(path: Path) -> list[dict[str, str]]:
    require_file(path)
    with path.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    if not rows:
        fail(f"CSV contains no data rows: {path}")
    return rows


def finite(value: str, label: str) -> float:
    try:
        parsed = float(value)
    except (TypeError, ValueError):
        fail(f"{label} is not numeric: {value!r}")
    if not math.isfinite(parsed):
        fail(f"{label} is not finite: {value!r}")
    return parsed


def integer(value: str, label: str) -> int:
    parsed = finite(value, label)
    rounded = int(round(parsed))
    if abs(parsed - rounded) > 1.0e-9:
        fail(f"{label} is not an integer: {value!r}")
    return rounded


def provenance_map(path: Path) -> dict[str, dict[str, str]]:
    rows = read_csv(path)
    required = {"field", "value", "provenance_class", "evidence_or_limitation"}
    if not required.issubset(rows[0]):
        fail(f"provenance schema is missing columns: {sorted(required - set(rows[0]))}")
    result: dict[str, dict[str, str]] = {}
    for row in rows:
        field = row["field"]
        if not field or field in result:
            fail(f"duplicate/empty provenance field: {field!r}")
        result[field] = row
    return result


def count_data_rows(path: Path) -> int:
    require_file(path)
    with path.open(encoding="utf-8") as stream:
        return max(sum(1 for _ in stream) - 1, 0)


def validate(root: Path) -> dict[str, str]:
    provenance = provenance_map(root / "source" / "provenance.csv")
    for field in (
        "dataset_doi",
        "archive_md5",
        "source_frame_rate_hz",
        "source_coordinate_scale_to_m",
        "source_correspondence_count",
        "reference_frame",
        "initialization_frame",
        "dynamic_frames",
        "uncertainty_scale_m",
        "gaussian_geometry",
        "gaussian_photometry",
        "material_ground_truth",
    ):
        if field not in provenance:
            fail(f"missing provenance field: {field}")

    if provenance["dataset_doi"]["value"] != EXPECTED_DATASET_DOI:
        fail("unexpected DOT dataset DOI")
    if provenance["archive_md5"]["value"].lower() != EXPECTED_ARCHIVE_MD5:
        fail("unexpected DOT C02 archive checksum")
    if provenance["archive_md5"]["provenance_class"] != "source":
        fail("archive checksum must be source provenance")
    if provenance["reference_frame"]["provenance_class"] != "model_proxy":
        fail("reference frame must remain labelled model_proxy")
    if provenance["initialization_frame"]["provenance_class"] != "measured":
        fail("initialization frame must be labelled measured")
    if provenance["dynamic_frames"]["provenance_class"] != "measured":
        fail("dynamic frames must be labelled measured")
    if provenance["gaussian_geometry"]["provenance_class"] != "measured":
        fail("Gaussian positions must remain labelled measured")
    if provenance["gaussian_photometry"]["provenance_class"] != "model_proxy":
        fail("neutral Gaussian photometry must remain labelled model_proxy")
    if provenance["material_ground_truth"]["provenance_class"] != "limitation":
        fail("missing-material-ground-truth statement must remain a limitation")
    if provenance["material_ground_truth"]["value"] != "not_available":
        fail("benchmark must not claim material ground truth")

    frame_rate = finite(provenance["source_frame_rate_hz"]["value"], "source frame rate")
    if abs(frame_rate - 60.0) > 1.0e-9:
        fail("DOT C2 benchmark expects 60 Hz source timing")
    scale = finite(provenance["source_coordinate_scale_to_m"]["value"], "coordinate scale")
    if abs(scale - 0.001) > 1.0e-12:
        fail("DOT coordinate conversion must remain explicit millimetres-to-metres")
    if integer(provenance["source_correspondence_count"]["value"], "correspondence count") != EXPECTED_PARTICLES:
        fail("unexpected stable correspondence count")

    if count_data_rows(root / "particles.csv") != EXPECTED_PARTICLES:
        fail("particles.csv must contain 225 particles")
    if count_data_rows(root / "observations.csv") != EXPECTED_OBSERVATIONS:
        fail("observations.csv must contain 675 observations")
    if count_data_rows(root / "uncertainty.csv") != EXPECTED_OBSERVATIONS:
        fail("uncertainty.csv must cover every observation")

    bundle_text = require_file(root / "bundle-validation.txt").read_text(encoding="utf-8")
    for token in (
        "VALID captured deformable bundle",
        "source_kind: derived",
        "physical_particles: 225",
        "observations: 675",
        "uncertainty_rows: 675",
    ):
        if token not in bundle_text:
            fail(f"bundle validator output missing: {token}")

    calibration = read_csv(root / "calibration" / "material_grid.csv")
    selected_rows = [row for row in calibration if row.get("selected") == "1"]
    if len(selected_rows) != 1:
        fail(f"expected exactly one selected calibration candidate, got {len(selected_rows)}")
    selected = selected_rows[0]
    for field in (
        "young_modulus",
        "poisson_ratio",
        "fit_dynamic_rms",
        "validation_dynamic_rms",
        "initialization_fit_rms",
        "appearance_roundtrip_rms",
    ):
        if field not in selected:
            fail(f"calibration schema missing {field}")
        finite(selected[field], f"calibration {field}")
    young = finite(selected["young_modulus"], "selected effective Young modulus")
    poisson = finite(selected["poisson_ratio"], "selected Poisson ratio")
    fit_rms = finite(selected["fit_dynamic_rms"], "fit dynamic RMS")
    validation_rms = finite(selected["validation_dynamic_rms"], "held-out validation RMS")
    if young <= 0.0 or not (0.0 <= poisson < 0.5):
        fail("selected model-conditioned material parameters are outside the supported range")
    if fit_rms <= 0.0 or validation_rms <= 0.0:
        fail("real measured replay errors must be positive and finite")

    robustness = read_csv(root / "robustness" / "robustness.csv")
    if len(robustness) != EXPECTED_ROBUSTNESS_ROWS:
        fail(f"expected {EXPECTED_ROBUSTNESS_ROWS} robustness rows, got {len(robustness)}")
    for row_index, row in enumerate(robustness):
        for key, value in row.items():
            if key is None or value is None or value == "":
                fail(f"empty robustness field at row {row_index}: {key}")
            if key in {"scenario", "noise_target"}:
                continue
            try:
                parsed = float(value)
            except ValueError:
                continue
            if not math.isfinite(parsed):
                fail(f"non-finite robustness value {key}={value}")

    adaptive = read_csv(root / "influence" / "adaptive_proposal_summary.csv")[0]
    regions = integer(adaptive["region_count"], "adaptive region count")
    proposed = integer(adaptive["proposed_particle_count"], "adaptive proposed particle count")
    gradient_fraction = finite(
        adaptive["proposed_absolute_gradient_fraction"], "adaptive absolute-gradient fraction"
    )
    if regions <= 0 or proposed <= 0 or proposed > EXPECTED_PARTICLES:
        fail("adaptive proposal is empty or exceeds the measured particle set")
    if not (0.0 < gradient_fraction <= 1.0 + 1.0e-12):
        fail("adaptive proposed absolute-gradient fraction is invalid")

    derivative_rows = read_csv(root / "influence" / "derivative_comparison.csv")
    max_adjoint_relative_error = 0.0
    max_adjoint_absolute_error = 0.0
    for row in derivative_rows:
        max_adjoint_relative_error = max(
            max_adjoint_relative_error,
            finite(row["relative_error"], "adjoint/reference relative error"),
        )
        max_adjoint_absolute_error = max(
            max_adjoint_absolute_error,
            finite(row["absolute_error"], "adjoint/reference absolute error"),
        )

    rewrite = read_csv(root / "rewrite" / "transaction_summary.csv")[0]
    status = rewrite.get("status", "")
    if status not in {"verified", "rejected"}:
        fail(f"unexpected rewrite status: {status!r}")
    rollback = integer(rewrite["rollback_performed"], "rewrite rollback flag")
    rewrite_error = finite(rewrite["physical_observable_error"], "rewrite physical observable error")
    rewrite_tolerance = finite(
        rewrite["physical_observable_tolerance"], "rewrite physical observable tolerance"
    )
    if rewrite_error < 0.0 or rewrite_tolerance < 0.0:
        fail("rewrite error/tolerance must be non-negative")
    if abs(rewrite_tolerance - MAX_REWRITE_LINEARIZATION_ERROR) > 1.0e-12:
        fail("rewrite nonlinear tolerance no longer matches the published verifier contract")

    require_file(root / "rewrite" / "transaction_evidence.csv")
    require_file(root / "rewrite" / "physical_evidence" / "reference.csv")
    require_file(root / "rewrite" / "physical_evidence" / "adjoint.csv")
    rewrite_counterfactual = read_csv(
        root / "rewrite" / "physical_evidence" / "counterfactual.csv"
    )
    rewrite_derivative = read_csv(
        root / "rewrite" / "physical_evidence" / "derivative_comparison.csv"
    )
    if len(rewrite_counterfactual) != 1 or len(rewrite_derivative) != 1:
        fail("verified rewrite evidence must contain exactly one selected region")

    nonlinear_error = finite(
        rewrite_counterfactual[0]["relative_linearization_error"],
        "rewrite nonlinear relative linearization error",
    )
    reference_derivative = finite(
        rewrite_derivative[0]["reference_derivative"], "rewrite reference derivative"
    )
    rewrite_adjoint_absolute_error = finite(
        rewrite_derivative[0]["absolute_error"], "rewrite adjoint absolute error"
    )
    rewrite_adjoint_relative_error = finite(
        rewrite_derivative[0]["relative_error"], "rewrite adjoint relative error"
    )
    nonlinear_passed = nonlinear_error <= MAX_REWRITE_LINEARIZATION_ERROR
    derivative_passed = rewrite_adjoint_absolute_error <= MAX_REWRITE_ADJOINT_ABSOLUTE_ERROR
    if abs(reference_derivative) > MIN_REWRITE_REFERENCE_DERIVATIVE_FOR_RELATIVE_CHECK:
        derivative_passed = (
            derivative_passed
            and rewrite_adjoint_relative_error <= MAX_REWRITE_ADJOINT_RELATIVE_ERROR
        )
    expected_status = "verified" if nonlinear_passed and derivative_passed else "rejected"
    if status != expected_status:
        fail(
            "transaction status disagrees with independently recomputed nonlinear/adjoint oracle verdict"
        )
    if abs(rewrite_error - nonlinear_error) > 1.0e-12:
        fail("transaction summary physical error disagrees with nonlinear counterfactual evidence")
    if status == "verified" and rollback != 0:
        fail("verified rewrite cannot be rolled back")
    if status == "rejected" and rollback != 1:
        fail("rejected rewrite must be rolled back")

    return {
        "source": "DOT C2 real measured correspondences; Vulkax physical/appearance bundle is derived",
        "archive_md5": EXPECTED_ARCHIVE_MD5,
        "particle_count": str(EXPECTED_PARTICLES),
        "observation_count": str(EXPECTED_OBSERVATIONS),
        "effective_young_modulus_pa": format(young, ".17g"),
        "effective_poisson_ratio": format(poisson, ".17g"),
        "fit_dynamic_rms_m": format(fit_rms, ".17g"),
        "validation_dynamic_rms_m": format(validation_rms, ".17g"),
        "adaptive_region_count": str(regions),
        "adaptive_proposed_particle_count": str(proposed),
        "adaptive_absolute_gradient_fraction": format(gradient_fraction, ".17g"),
        "max_adjoint_absolute_error": format(max_adjoint_absolute_error, ".17g"),
        "max_adjoint_relative_error": format(max_adjoint_relative_error, ".17g"),
        "rewrite_status": status,
        "rewrite_nonlinear_passed": "1" if nonlinear_passed else "0",
        "rewrite_independent_oracle_passed": "1" if derivative_passed else "0",
        "rewrite_physical_observable_error": format(rewrite_error, ".17g"),
        "rewrite_physical_observable_tolerance": format(rewrite_tolerance, ".17g"),
        "rewrite_adjoint_absolute_error": format(rewrite_adjoint_absolute_error, ".17g"),
        "rewrite_adjoint_relative_error": format(rewrite_adjoint_relative_error, ".17g"),
        "rewrite_rollback_performed": str(rollback),
        "material_ground_truth": "not_available",
    }


def write_summary(path: Path, summary: dict[str, str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(["field", "value"])
        for key, value in summary.items():
            writer.writerow([key, value])


def main() -> None:
    if len(sys.argv) not in {2, 3}:
        raise SystemExit(
            "usage: validate_measured_dot_c2.py <benchmark-root> [summary-output.csv]"
        )
    root = Path(sys.argv[1])
    if not root.is_dir():
        fail(f"benchmark root does not exist: {root}")
    summary = validate(root)
    output = Path(sys.argv[2]) if len(sys.argv) == 3 else root / "measured_benchmark_summary.csv"
    write_summary(output, summary)
    print("VALID DOT C2 measured-source benchmark")
    print(f"  particles: {summary['particle_count']}")
    print(f"  observations: {summary['observation_count']}")
    print(f"  effective_young_modulus_pa: {summary['effective_young_modulus_pa']}")
    print(f"  validation_dynamic_rms_m: {summary['validation_dynamic_rms_m']}")
    print(f"  adaptive_particles: {summary['adaptive_proposed_particle_count']}")
    print(f"  rewrite_status: {summary['rewrite_status']}")
    print(f"  rewrite_nonlinear_passed: {summary['rewrite_nonlinear_passed']}")
    print(
        "  rewrite_independent_oracle_passed: "
        f"{summary['rewrite_independent_oracle_passed']}"
    )
    print(f"  rewrite_rollback_performed: {summary['rewrite_rollback_performed']}")
    print(f"  summary: {output}")


if __name__ == "__main__":
    main()

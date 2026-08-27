#!/usr/bin/env python3
"""Validate the deterministic Vulkax import of the real DOT C2 sequence."""

from __future__ import annotations

import csv
import math
import sys
from pathlib import Path

EXPECTED_PARTICLES = 225
EXPECTED_FRAMES = {0.0, 5.0 / 60.0, 10.0 / 60.0}
EXPECTED_OBSERVATIONS = EXPECTED_PARTICLES * len(EXPECTED_FRAMES)
EXPECTED_VALIDATION_PER_DYNAMIC_FRAME = 45
EXPECTED_ARCHIVE_MD5 = "0f347c3f95ed2def9fd81ba5236955b1"
EXPECTED_DOI = "10.13021/ORC2020/XXLVXM"


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


def finite(value: str) -> float:
    parsed = float(value)
    if not math.isfinite(parsed):
        raise ValueError(f"non-finite numeric value: {value}")
    return parsed


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit("usage: validate_dot_c2_import.py <import-dir>")
    root = Path(sys.argv[1])
    required = [
        root / "object.ply",
        root / "particles.csv",
        root / "observations.csv",
        root / "uncertainty.csv",
        root / "source" / "provenance.csv",
        root / "source" / "geometry_metrics.csv",
        root / "source" / "calib_info.txt",
        root / "source" / "frame000011_cam001.jpg",
    ]
    for path in required:
        if not path.is_file() or path.stat().st_size == 0:
            raise SystemExit(f"missing or empty import artifact: {path}")

    particles = read_csv(root / "particles.csv")
    observations = read_csv(root / "observations.csv")
    uncertainty = read_csv(root / "uncertainty.csv")
    provenance_rows = read_csv(root / "source" / "provenance.csv")
    geometry = read_csv(root / "source" / "geometry_metrics.csv")

    if len(particles) != EXPECTED_PARTICLES:
        raise SystemExit(f"expected {EXPECTED_PARTICLES} particles, got {len(particles)}")
    ids = [int(row["particle_id"]) for row in particles]
    if ids != list(range(1, EXPECTED_PARTICLES + 1)):
        raise SystemExit("particle IDs are not deterministic 1..225")
    for row in particles:
        for key in ("rest_x", "rest_y", "rest_z", "mass", "rest_volume"):
            value = finite(row[key])
            if key in ("mass", "rest_volume") and value <= 0.0:
                raise SystemExit(f"non-positive {key} for particle {row['particle_id']}")

    if len(observations) != EXPECTED_OBSERVATIONS:
        raise SystemExit(f"expected {EXPECTED_OBSERVATIONS} observations, got {len(observations)}")
    if len(uncertainty) != EXPECTED_OBSERVATIONS:
        raise SystemExit(f"expected {EXPECTED_OBSERVATIONS} uncertainty rows, got {len(uncertainty)}")

    by_time: dict[float, list[dict[str, str]]] = {}
    marker_particle: dict[str, int] = {}
    for row in observations:
        time = finite(row["time"])
        by_time.setdefault(time, []).append(row)
        marker = row["marker_id"]
        particle_id = int(row["particle_id"])
        prior = marker_particle.setdefault(marker, particle_id)
        if prior != particle_id:
            raise SystemExit(f"marker {marker} changes particle identity")
        for key in ("x", "y", "z"):
            finite(row[key])
        if row["split"] not in {"fit", "validation"}:
            raise SystemExit(f"bad observation split: {row['split']}")

    if len(by_time) != len(EXPECTED_FRAMES):
        raise SystemExit(f"unexpected observation times: {sorted(by_time)}")
    expected_sorted = sorted(EXPECTED_FRAMES)
    actual_sorted = sorted(by_time)
    for actual, expected in zip(actual_sorted, expected_sorted):
        if abs(actual - expected) > 1e-12:
            raise SystemExit(f"unexpected observation time {actual}, expected {expected}")
    for time, rows in by_time.items():
        if len(rows) != EXPECTED_PARTICLES:
            raise SystemExit(f"time {time} has {len(rows)} rows, expected 225")
        validation = sum(row["split"] == "validation" for row in rows)
        if abs(time) <= 1e-15:
            if validation != 0:
                raise SystemExit("t=0 initialization must be fit-only")
        elif validation != EXPECTED_VALIDATION_PER_DYNAMIC_FRAME:
            raise SystemExit(
                f"dynamic time {time} has {validation} validation rows, "
                f"expected {EXPECTED_VALIDATION_PER_DYNAMIC_FRAME}"
            )

    uncertainty_keys = set()
    for row in uncertainty:
        key = (row["marker_id"], round(finite(row["time"]), 15))
        if key in uncertainty_keys:
            raise SystemExit(f"duplicate uncertainty row {key}")
        uncertainty_keys.add(key)
        for axis in ("sigma_x", "sigma_y", "sigma_z"):
            sigma = finite(row[axis])
            if abs(sigma - 0.00026) > 1e-15:
                raise SystemExit(f"unexpected literature robustness scale {sigma}")
    observation_keys = {
        (row["marker_id"], round(finite(row["time"]), 15)) for row in observations
    }
    if uncertainty_keys != observation_keys:
        raise SystemExit("uncertainty coverage does not match observations exactly")

    provenance = {row["field"]: row for row in provenance_rows}
    required_provenance = {
        "dataset_doi": EXPECTED_DOI,
        "archive_md5": EXPECTED_ARCHIVE_MD5,
        "source_frame_rate_hz": "60",
        "source_correspondence_count": "225",
        "reference_frame": "1",
        "initialization_frame": "11",
        "dynamic_frames": "16;21",
        "material_ground_truth": "not_available",
    }
    for field, expected in required_provenance.items():
        if field not in provenance or provenance[field]["value"] != expected:
            raise SystemExit(f"provenance field {field!r} does not equal {expected!r}")
    for field in ("reference_frame", "nominal_density_kg_m3", "nominal_thickness_m", "particle_mass_kg"):
        if provenance[field]["provenance_class"] != "model_proxy":
            raise SystemExit(f"{field} is not visibly labelled model_proxy")
    if provenance["uncertainty_scale_m"]["provenance_class"] != "literature_proxy":
        raise SystemExit("uncertainty scale is not visibly labelled literature_proxy")
    if provenance["gaussian_photometry"]["provenance_class"] != "model_proxy":
        raise SystemExit("Gaussian photometry proxy is not visibly labelled")

    if len(geometry) != 4:
        raise SystemExit(f"expected four measured geometry metric rows, got {len(geometry)}")
    frame_by_id = {int(row["frame"]): row for row in geometry}
    if set(frame_by_id) != {1, 11, 16, 21}:
        raise SystemExit(f"unexpected geometry metric frames: {sorted(frame_by_id)}")
    # These are identity/regression checks on the pinned public archive, not
    # model-quality thresholds.
    frame11_rms = finite(frame_by_id[11]["rms_from_reference_m"])
    frame21_rms = finite(frame_by_id[21]["rms_from_reference_m"])
    if abs(frame11_rms - 0.0018676733821488078) > 1e-12:
        raise SystemExit(f"pinned frame-11 geometry changed: {frame11_rms}")
    if abs(frame21_rms - 0.005793376642881163) > 1e-12:
        raise SystemExit(f"pinned frame-21 geometry changed: {frame21_rms}")

    print(
        "DOT C2 import PASS: real measured correspondence source, 225 stable points, "
        "675 SI observations, explicit derived physical/appearance proxies"
    )


if __name__ == "__main__":
    main()

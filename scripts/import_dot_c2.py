#!/usr/bin/env python3
"""Import the public DOT C2 sequence into the Vulkax capture payload contract.

The source is real measured/reconstructed data. The generated Vulkax payload is
explicitly *derived* because DOT does not supply stress-free rest geometry,
particle mass/volume, Young's modulus, or per-sample statistical uncertainty.
Those modelling choices are written to source/provenance.csv instead of being
silently presented as measurements.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import math
import shutil
import statistics
import zipfile
from pathlib import Path

DOT_DATASET_DOI = "10.13021/ORC2020/XXLVXM"
DOT_C02_FILE_PID = "doi:10.13021/orc2020/XXLVXM/ZVZHVR"
DOT_C02_MD5 = "0f347c3f95ed2def9fd81ba5236955b1"
SOURCE_FPS = 60.0
SOURCE_LENGTH_SCALE_TO_METRES = 1.0e-3
# The paper reports 0.26 mm average point-to-ray distance for its interpolated
# 2 ms trigger-delay experiment. This is used only as a robustness/noise scale,
# not claimed to be a source-supplied per-observation standard deviation.
REPORTED_ALIGNMENT_ERROR_SCALE_M = 0.26e-3
REFERENCE_FRAME = 1
INITIAL_FRAME = 11
DYNAMIC_FRAMES = (16, 21)
GRID_SIDE = 15
NOMINAL_DENSITY_KG_M3 = 1000.0
NOMINAL_THICKNESS_TO_SPACING = 0.10


def md5(path: Path) -> str:
    digest = hashlib.md5()  # nosec - source identity supplied by Dataverse
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def coordinate_name(frame: int) -> str:
    return f"C2/coordinates/3d/frame{frame:06d}_cam001.txt"


def parse_points(archive: zipfile.ZipFile, frame: int) -> list[tuple[float, float, float]]:
    name = coordinate_name(frame)
    rows = archive.read(name).decode("utf-8").splitlines()
    points: list[tuple[float, float, float]] = []
    for line_number, line in enumerate(rows, 1):
        fields = [value.strip() for value in line.split(",")]
        if len(fields) != 3:
            raise ValueError(f"{name}:{line_number}: expected three comma-separated coordinates")
        values = tuple(float(value) * SOURCE_LENGTH_SCALE_TO_METRES for value in fields)
        if not all(math.isfinite(value) for value in values):
            raise ValueError(f"{name}:{line_number}: non-finite coordinate")
        points.append(values)  # type: ignore[arg-type]
    if len(points) != GRID_SIDE * GRID_SIDE:
        raise ValueError(f"{name}: expected 225 correspondence rows, got {len(points)}")
    return points


def distance(a: tuple[float, float, float], b: tuple[float, float, float]) -> float:
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def characteristic_spacing(points: list[tuple[float, float, float]]) -> float:
    edges: list[float] = []
    for row in range(GRID_SIDE):
        for column in range(GRID_SIDE):
            index = row * GRID_SIDE + column
            if column + 1 < GRID_SIDE:
                edges.append(distance(points[index], points[index + 1]))
            if row + 1 < GRID_SIDE:
                edges.append(distance(points[index], points[index + GRID_SIDE]))
    value = statistics.median(edges)
    if not math.isfinite(value) or value <= 0.0:
        raise ValueError("DOT C2 correspondence spacing is invalid")
    return value


def rms_displacement(
    lhs: list[tuple[float, float, float]], rhs: list[tuple[float, float, float]]
) -> tuple[float, float]:
    distances = [distance(a, b) for a, b in zip(lhs, rhs)]
    return math.sqrt(sum(value * value for value in distances) / len(distances)), max(distances)


def write_gaussian_proxy(
    path: Path, points: list[tuple[float, float, float]], spacing: float
) -> None:
    # Positions are measured DOT correspondences. Appearance coefficients and
    # Gaussian size are deliberately neutral modelling proxies; no 3DGS capture
    # or photometric reconstruction claim is made.
    scale = max(spacing * 0.45, 1.0e-5)
    log_scale = math.log(scale)
    with path.open("w", newline="\n") as stream:
        stream.write("ply\nformat ascii 1.0\n")
        stream.write("comment Vulkax derived Gaussian proxy from measured DOT C2 3D correspondences\n")
        stream.write(f"element vertex {len(points)}\n")
        for declaration in (
            "property double x", "property double y", "property double z",
            "property double f_dc_0", "property double f_dc_1", "property double f_dc_2",
            "property double opacity",
            "property double scale_0", "property double scale_1", "property double scale_2",
            "property double rot_0", "property double rot_1", "property double rot_2", "property double rot_3",
            "property uint vulkax_id_namespace", "property uint vulkax_id_local",
        ):
            stream.write(declaration + "\n")
        stream.write("end_header\n")
        for local_id, (x, y, z) in enumerate(points, 1):
            stream.write(
                f"{x:.17g} {y:.17g} {z:.17g} 0 0 0 4 "
                f"{log_scale:.17g} {log_scale:.17g} {log_scale:.17g} "
                f"1 0 0 0 45 {local_id}\n"
            )


def write_particles(
    path: Path,
    rest_points: list[tuple[float, float, float]],
    spacing: float,
) -> tuple[float, float, float]:
    nominal_thickness = spacing * NOMINAL_THICKNESS_TO_SPACING
    rest_volume = spacing * spacing * nominal_thickness
    mass = NOMINAL_DENSITY_KG_M3 * rest_volume
    with path.open("w", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(["particle_id", "rest_x", "rest_y", "rest_z", "mass", "rest_volume"])
        for particle_id, point in enumerate(rest_points, 1):
            writer.writerow([particle_id, *[f"{value:.17g}" for value in point], f"{mass:.17g}", f"{rest_volume:.17g}"])
    return nominal_thickness, rest_volume, mass


def observation_time(frame: int) -> float:
    return (frame - INITIAL_FRAME) / SOURCE_FPS


def write_observations(
    path: Path,
    frame_points: dict[int, list[tuple[float, float, float]]],
) -> int:
    frames = (INITIAL_FRAME, *DYNAMIC_FRAMES)
    count = 0
    with path.open("w", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(["marker_id", "particle_id", "time", "x", "y", "z", "split"])
        for frame in frames:
            time = observation_time(frame)
            for particle_id, point in enumerate(frame_points[frame], 1):
                # All initialization points participate in the affine fit. The
                # dynamic held-out set is deterministic and spatially distributed.
                split = "fit" if frame == INITIAL_FRAME or particle_id % 5 != 0 else "validation"
                writer.writerow([
                    f"dot_c2_{particle_id:03d}", particle_id, f"{time:.17g}",
                    *[f"{value:.17g}" for value in point], split,
                ])
                count += 1
    return count


def write_uncertainty(path: Path) -> int:
    frames = (INITIAL_FRAME, *DYNAMIC_FRAMES)
    count = 0
    with path.open("w", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(["marker_id", "time", "sigma_x", "sigma_y", "sigma_z"])
        for frame in frames:
            time = observation_time(frame)
            for particle_id in range(1, GRID_SIDE * GRID_SIDE + 1):
                writer.writerow([
                    f"dot_c2_{particle_id:03d}", f"{time:.17g}",
                    f"{REPORTED_ALIGNMENT_ERROR_SCALE_M:.17g}",
                    f"{REPORTED_ALIGNMENT_ERROR_SCALE_M:.17g}",
                    f"{REPORTED_ALIGNMENT_ERROR_SCALE_M:.17g}",
                ])
                count += 1
    return count


def write_metrics(
    path: Path,
    frame_points: dict[int, list[tuple[float, float, float]]],
) -> None:
    with path.open("w", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(["frame", "time_from_initial_s", "rms_from_reference_m", "max_from_reference_m", "rms_from_initial_m", "max_from_initial_m"])
        reference = frame_points[REFERENCE_FRAME]
        initial = frame_points[INITIAL_FRAME]
        for frame in (REFERENCE_FRAME, INITIAL_FRAME, *DYNAMIC_FRAMES):
            rms_ref, max_ref = rms_displacement(reference, frame_points[frame])
            rms_initial, max_initial = rms_displacement(initial, frame_points[frame])
            writer.writerow([
                frame, f"{observation_time(frame):.17g}", f"{rms_ref:.17g}", f"{max_ref:.17g}",
                f"{rms_initial:.17g}", f"{max_initial:.17g}",
            ])


def write_provenance(
    path: Path,
    spacing: float,
    nominal_thickness: float,
    rest_volume: float,
    particle_mass: float,
) -> None:
    records = [
        ("dataset", "DOT - Deformable Object Tracking Dataset", "source", "real-world dataset"),
        ("dataset_doi", DOT_DATASET_DOI, "source", "George Mason University Dataverse"),
        ("sequence", "C2 (distributed as C02.zip)", "source", "cloth sequence"),
        ("archive_persistent_id", DOT_C02_FILE_PID, "source", "Dataverse file identity"),
        ("archive_md5", DOT_C02_MD5, "source", "Dataverse checksum"),
        ("license", "CC0-1.0", "source", "Dataverse license"),
        ("source_frame_rate_hz", f"{SOURCE_FPS:.17g}", "source", "paper: all cameras capture at 60 fps"),
        ("source_frame_interval_s", f"{1.0 / SOURCE_FPS:.17g}", "source", "paper: consecutive-frame interval about 16 ms"),
        ("source_coordinate_scale_to_m", f"{SOURCE_LENGTH_SCALE_TO_METRES:.17g}", "explicit_conversion", "paper reports system geometric distances in mm; conversion is explicit, not inferred at load time"),
        ("source_correspondence_count", str(GRID_SIDE * GRID_SIDE), "source", "225 stable 3D correspondence rows per frame"),
        ("reference_frame", str(REFERENCE_FRAME), "model_proxy", "earlier measured state; not asserted stress-free"),
        ("initialization_frame", str(INITIAL_FRAME), "measured", "measured DOT state used at t=0"),
        ("dynamic_frames", ";".join(map(str, DYNAMIC_FRAMES)), "measured", "measured held-out/fit checkpoints"),
        ("characteristic_spacing_m", f"{spacing:.17g}", "derived_from_measurement", "median horizontal/vertical neighbor distance in reference frame"),
        ("nominal_density_kg_m3", f"{NOMINAL_DENSITY_KG_M3:.17g}", "model_proxy", "not supplied by DOT; used only by Vulkax volumetric proxy"),
        ("nominal_thickness_m", f"{nominal_thickness:.17g}", "model_proxy", "0.1 x measured correspondence spacing; not a DOT measurement"),
        ("particle_rest_volume_m3", f"{rest_volume:.17g}", "model_proxy", "spacing^2 x nominal thickness"),
        ("particle_mass_kg", f"{particle_mass:.17g}", "model_proxy", "nominal density x proxy rest volume"),
        ("uncertainty_scale_m", f"{REPORTED_ALIGNMENT_ERROR_SCALE_M:.17g}", "literature_proxy", "0.26 mm reported average point-to-ray distance at interpolated 2 ms trigger-delay experiment; not a per-C2 statistical sigma"),
        ("gaussian_geometry", "frame-11 3D correspondences", "measured", "positions only"),
        ("gaussian_photometry", "neutral SH-DC", "model_proxy", "no 3DGS/photometric reconstruction claim"),
        ("material_ground_truth", "not_available", "limitation", "DOT does not provide stress-free state, loads, thickness, mass, or Young's modulus for C2"),
    ]
    with path.open("w", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(["field", "value", "provenance_class", "evidence_or_limitation"])
        writer.writerows(records)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=Path, help="pinned DOT C02.zip")
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()

    actual_md5 = md5(args.archive)
    if actual_md5 != DOT_C02_MD5:
        raise SystemExit(f"DOT C02 archive MD5 mismatch: expected {DOT_C02_MD5}, got {actual_md5}")
    if args.output_dir.exists() and any(args.output_dir.iterdir()):
        raise SystemExit("output directory must be absent or empty")
    args.output_dir.mkdir(parents=True, exist_ok=True)
    source_dir = args.output_dir / "source"
    source_dir.mkdir()

    required_frames = (REFERENCE_FRAME, INITIAL_FRAME, *DYNAMIC_FRAMES)
    with zipfile.ZipFile(args.archive) as archive:
        frame_points = {frame: parse_points(archive, frame) for frame in required_frames}
        spacing = characteristic_spacing(frame_points[REFERENCE_FRAME])
        for frame in required_frames:
            raw_name = coordinate_name(frame)
            (source_dir / Path(raw_name).name).write_bytes(archive.read(raw_name))
        (source_dir / "calib_info.txt").write_bytes(archive.read("C2/cam_info/calib_info.txt"))
        initial_image = "C2/images/normal_view/frame000011_cam001.jpg"
        (source_dir / "frame000011_cam001.jpg").write_bytes(archive.read(initial_image))

    write_gaussian_proxy(args.output_dir / "object.ply", frame_points[INITIAL_FRAME], spacing)
    nominal_thickness, rest_volume, particle_mass = write_particles(
        args.output_dir / "particles.csv", frame_points[REFERENCE_FRAME], spacing
    )
    observation_count = write_observations(args.output_dir / "observations.csv", frame_points)
    uncertainty_count = write_uncertainty(args.output_dir / "uncertainty.csv")
    if observation_count != uncertainty_count:
        raise RuntimeError("observation/uncertainty row count mismatch")
    write_metrics(source_dir / "geometry_metrics.csv", frame_points)
    write_provenance(source_dir / "provenance.csv", spacing, nominal_thickness, rest_volume, particle_mass)
    shutil.copy2(args.archive, source_dir / "C02.zip.identity-copy")

    print("Imported DOT C2 measured-source benchmark payload")
    print(f"  archive_md5: {actual_md5}")
    print(f"  reference_frame: {REFERENCE_FRAME}")
    print(f"  initialization_frame: {INITIAL_FRAME}")
    print(f"  dynamic_frames: {','.join(map(str, DYNAMIC_FRAMES))}")
    print(f"  correspondences: {GRID_SIDE * GRID_SIDE}")
    print(f"  observations: {observation_count}")
    print(f"  characteristic_spacing_m: {spacing:.10g}")
    print(f"  nominal_thickness_m: {nominal_thickness:.10g}")
    print(f"  source_frame_interval_s: {1.0 / SOURCE_FPS:.10g}")
    print("  bundle_classification: derived from real measured DOT data")
    print("  material_ground_truth: unavailable")


if __name__ == "__main__":
    main()

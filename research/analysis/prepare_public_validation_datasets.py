#!/usr/bin/env python3
"""Prepare downloaded GAUGE, IRIS, and RGBench data for publication validation.

Outputs:
  dataset_inventory.csv  - every discovered dataset scene/capture.
  world_manifest.csv     - only prospective-ready scenes used by the trial planner.
  preparation_report.json
  dataset_truth_index.json - dataset-native truth/provenance metadata only.

The script never fabricates Reality Probe scores.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import tempfile
from typing import Iterable

INVENTORY_FIELDS = [
    "dataset", "scene", "split", "campaign_role", "adapter", "source_uri",
    "truth_source", "units", "license", "exclusion_rule", "local_path",
]
WORLD_FIELDS = [
    "dataset", "scene", "split", "adapter", "source_uri", "truth_source",
    "units", "exclusion_rule",
]

IRIS_LAYOUT = {
    "Dropping_ball": ("drop_50", "drop_100", "drop_150"),
    "Falling_ball": ("small", "mid", "big"),
    "Sliding_cone": ("cone_45", "cone_60", "cone_80"),
    "Pendulum": ("pendulum_20", "pendulum_45", "pendulum_90"),
    "Rotation": ("slow", "mid", "fast"),
    "Hitting_cones": ("slow", "mid", "fast"),
    "Two_Moving_Pendulums": ("pendulum_20", "pendulum_45", "pendulum_90"),
    "Two_Moving_Pendulum_One_Static": ("pendulum_20", "pendulum_45", "pendulum_90"),
}
IRIS_KEY = {
    "Dropping_ball": "dropping_ball",
    "Falling_ball": "falling_ball",
    "Sliding_cone": "sliding_cone",
    "Pendulum": "pendulum",
    "Rotation": "rotation",
    "Hitting_cones": "hitting_cones",
    "Two_Moving_Pendulums": "two_moving_pendulums",
    "Two_Moving_Pendulum_One_Static": "two_moving_pendulum_one_static",
}

# Split by physical setting, not by repeated take, so final-test dynamics are not
# duplicated into development through another take of the same parameter setting.
IRIS_SETTING_SPLIT = {}
for folder, settings in IRIS_LAYOUT.items():
    IRIS_SETTING_SPLIT[(folder, settings[0])] = "development"
    IRIS_SETTING_SPLIT[(folder, settings[1])] = "validation"
    IRIS_SETTING_SPLIT[(folder, settings[2])] = "final_test"

RGBENCH_GARMENT_SPLIT = {
    "green_tshirt": "development",
    "beige_hoodie": "development",
    "brown_coat": "development",
    "khaki_blazer": "development",
    "grey_sunwear": "development",
    "blue_dress": "validation",
    "grey_pleat_skirt": "validation",
    "white_cakeskirt": "final_test",
    "white_shirt": "final_test",
}


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def read_download_manifest(root: Path, dataset: str) -> dict:
    p = root / dataset / "vulkax_download_manifest.json"
    if not p.is_file():
        raise FileNotFoundError(
            f"missing {p}; run fetch_public_validation_datasets.py first"
        )
    return json.loads(p.read_text(encoding="utf-8"))


def gauge_rows(root: Path) -> tuple[list[dict[str, str]], dict]:
    droot = root / "gauge"
    download = read_download_manifest(root, "gauge")
    rows: list[dict[str, str]] = []
    truth: dict[str, object] = {"dataset": "gauge", "scenes": {}}
    base = droot / "data" / "deformable"
    if not base.exists():
        return rows, truth

    for path in sorted(base.glob("*/json/*/*.json")):
        task = path.parts[-4]
        material = path.parts[-2]
        trial = int(path.stem)
        scene = f"{task}/{material}/{trial:02d}"
        # The paper already used all foam-shearing trials retrospectively.
        # Never promote those repeats into a new prospective final test.
        if task == "foam shearing":
            split = "development"
            role = "retrospective_existing_evidence"
            exclusion = "excluded_from_new_prospective_final_test_existing_retrospective_use"
        else:
            if trial <= 4:
                split = "development"
            elif trial <= 7:
                split = "validation"
            else:
                split = "final_test"
            role = "prospective_ready"
            exclusion = "predeclared_corrupt_or_schema_invalid_only"
        rows.append({
            "dataset": "gauge",
            "scene": scene,
            "split": split,
            "campaign_role": role,
            "adapter": "gauge_deformable_to_vulkax.py",
            "source_uri": download["citation_url"],
            "truth_source": f"GAUGE measured trajectory + metadata ({material})",
            "units": "source_mm_converted_to_SI",
            "license": download["license"],
            "exclusion_rule": exclusion,
            "local_path": path.relative_to(root).as_posix(),
        })
        truth["scenes"][scene] = {
            "task": task,
            "material": material,
            "trial": trial,
            "source_sha256": sha256(path),
            "campaign_role": role,
        }
    return rows, truth


def iris_rows(root: Path) -> tuple[list[dict[str, str]], dict]:
    droot = root / "iris"
    download = read_download_manifest(root, "iris")
    params_path = droot / "parameters.json"
    params = json.loads(params_path.read_text(encoding="utf-8")) if params_path.is_file() else {}
    rows: list[dict[str, str]] = []
    truth: dict[str, object] = {
        "dataset": "iris",
        "parameters_sha256": sha256(params_path) if params_path.is_file() else None,
        "scenes": {},
    }

    for path in sorted(droot.glob("*/*/*.mp4")):
        folder, setting, take = path.parts[-3], path.parts[-2], path.stem
        if folder not in IRIS_LAYOUT or setting not in IRIS_LAYOUT[folder]:
            continue
        split = IRIS_SETTING_SPLIT[(folder, setting)]
        key = IRIS_KEY[folder]
        scene = f"{key}/{setting}/{take}"
        param_entry = params.get(key, {}).get(setting)
        rows.append({
            "dataset": "iris",
            "scene": scene,
            "split": split,
            "campaign_role": "prospective_ready",
            "adapter": "iris_real_video_parameter_adapter",
            "source_uri": download["citation_url"],
            "truth_source": "IRIS independently measured parameters.json",
            "units": "dataset_native_SI_and_degrees",
            "license": download["license"],
            "exclusion_rule": "predeclared_corrupt_video_or_missing_ground_truth_only",
            "local_path": path.relative_to(root).as_posix(),
        })
        truth["scenes"][scene] = {
            "class": key,
            "setting": setting,
            "take": take,
            "parameters": param_entry,
            "video_sha256": sha256(path),
            "split": split,
        }
    return rows, truth


def rgbench_capture_roots(droot: Path) -> Iterable[tuple[str, Path]]:
    for garment_dir in sorted(p for p in droot.iterdir() if p.is_dir()):
        garment = garment_dir.name
        if garment in {"meshes", "reference_results", ".cache"}:
            continue
        for capture in sorted(p for p in garment_dir.iterdir() if p.is_dir()):
            if re.search(r"_(grasp|fold|fling)_", capture.name):
                yield garment, capture


def rgbench_rows(root: Path) -> tuple[list[dict[str, str]], dict]:
    droot = root / "rgbench"
    download = read_download_manifest(root, "rgbench")
    rows: list[dict[str, str]] = []
    truth: dict[str, object] = {"dataset": "rgbench", "scenes": {}}
    if not droot.exists():
        return rows, truth

    for garment, capture in rgbench_capture_roots(droot):
        split = RGBENCH_GARMENT_SPLIT.get(garment, "development")
        action_match = re.search(r"_(grasp|fold|fling)_", capture.name)
        action = action_match.group(1) if action_match else "unknown"
        scene = f"{garment}/{action}/{capture.name}"
        cal = capture / "calibration" / "world_to_camera_transform.json"
        joints = sorted((capture / "joints").glob("*.csv")) if (capture / "joints").is_dir() else []
        pcds = sorted((capture / "segment_pcds").glob("*.pcd")) if (capture / "segment_pcds").is_dir() else []

        missing = []
        if not cal.is_file():
            missing.append("calibration")
        if len(joints) < 2:
            missing.append("joint_streams")
        if not pcds:
            missing.append("segmented_point_clouds")

        exclusion = (
            "predeclared_missing_capture_component:" + "+".join(missing)
            if missing
            else "predeclared_corrupt_capture_or_calibration_only"
        )
        role = "adapter_blocked" if missing else "prospective_ready"
        rows.append({
            "dataset": "rgbench",
            "scene": scene,
            "split": split,
            "campaign_role": role,
            "adapter": "rgbench_cloth_capture_adapter",
            "source_uri": download["citation_url"],
            "truth_source": "real segmented cloth point clouds + calibrated robot trajectory",
            "units": "meters_seconds_radians_dataset_native",
            "license": download["license"],
            "exclusion_rule": exclusion,
            "local_path": capture.relative_to(root).as_posix(),
        })
        truth["scenes"][scene] = {
            "garment": garment,
            "action": action,
            "capture": capture.name,
            "split": split,
            "calibration_sha256": sha256(cal) if cal.is_file() else None,
            "joint_files": [
                {"path": p.relative_to(root).as_posix(), "sha256": sha256(p)}
                for p in joints
            ],
            "point_cloud_count": len(pcds),
            "first_point_cloud_sha256": sha256(pcds[0]) if pcds else None,
            "campaign_role": role,
        }
    return rows, truth


def write_csv(path: Path, fields: list[str], rows: list[dict[str, str]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        for row in rows:
            w.writerow({k: row.get(k, "") for k in fields})


def prepare(root: Path, out: Path) -> dict:
    inventory: list[dict[str, str]] = []
    truth_index: dict[str, object] = {"schema": "vulkax.public_dataset_truth_index", "version": 1, "datasets": {}}

    for fn in (gauge_rows, iris_rows, rgbench_rows):
        rows, truth = fn(root)
        inventory.extend(rows)
        truth_index["datasets"][truth["dataset"]] = truth

    inventory.sort(key=lambda r: (r["dataset"], r["scene"]))
    if not inventory:
        raise RuntimeError("no public validation scenes discovered")

    world = [
        {k: row[k] for k in WORLD_FIELDS}
        for row in inventory
        if row["campaign_role"] == "prospective_ready"
    ]
    if not world:
        raise RuntimeError("no prospective-ready public scenes discovered")

    # Hard leakage guards.
    pair_keys = {(r["dataset"], r["scene"]) for r in world}
    if len(pair_keys) != len(world):
        raise RuntimeError("duplicate world-manifest scene")
    if any(r["dataset"] == "gauge" and "foam shearing" in r["scene"] for r in world):
        raise RuntimeError("retrospective GAUGE shearing leaked into prospective manifest")

    iris_setting_splits: dict[tuple[str, str], set[str]] = {}
    for r in world:
        if r["dataset"] != "iris":
            continue
        cls, setting, _take = r["scene"].split("/", 2)
        iris_setting_splits.setdefault((cls, setting), set()).add(r["split"])
    if any(len(v) != 1 for v in iris_setting_splits.values()):
        raise RuntimeError("IRIS setting leaked across splits")

    out.mkdir(parents=True, exist_ok=True)
    write_csv(out / "dataset_inventory.csv", INVENTORY_FIELDS, inventory)
    write_csv(out / "world_manifest.csv", WORLD_FIELDS, world)
    (out / "dataset_truth_index.json").write_text(
        json.dumps(truth_index, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )

    counts = {}
    for row in inventory:
        key = (row["dataset"], row["campaign_role"], row["split"])
        counts[key] = counts.get(key, 0) + 1
    report = {
        "schema": "vulkax.public_dataset_preparation",
        "version": 1,
        "inventory_scene_count": len(inventory),
        "prospective_world_count": len(world),
        "counts": [
            {"dataset": k[0], "campaign_role": k[1], "split": k[2], "count": v}
            for k, v in sorted(counts.items())
        ],
        "leakage_guards": {
            "gauge_shearing_retrospective_only": True,
            "iris_split_by_physical_setting": True,
            "rgbench_split_by_garment": True,
        },
        "warning": (
            "world_manifest.csv is preparation metadata, not Reality Probe result evidence. "
            "Final-test methods must be frozen before using final-test truth to tune a method."
        ),
    }
    (out / "preparation_report.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return report


def self_test() -> None:
    with tempfile.TemporaryDirectory() as td:
        root = Path(td) / "data"
        out = Path(td) / "out"
        # Minimal fake manifests.
        for ds, license_name in (("gauge", "MIT"), ("iris", "CC"), ("rgbench", "CC")):
            p = root / ds
            p.mkdir(parents=True, exist_ok=True)
            (p / "vulkax_download_manifest.json").write_text(json.dumps({
                "citation_url": f"https://example/{ds}", "license": license_name
            }), encoding="utf-8")

        gp = root / "gauge/data/deformable/foam stretching/json/soft/8.json"
        gp.parent.mkdir(parents=True, exist_ok=True)
        gp.write_text("{}", encoding="utf-8")
        gs = root / "gauge/data/deformable/foam shearing/json/soft/10.json"
        gs.parent.mkdir(parents=True, exist_ok=True)
        gs.write_text("{}", encoding="utf-8")

        params = root / "iris/parameters.json"
        params.write_text(json.dumps({"pendulum": {
            "pendulum_20": {}, "pendulum_45": {}, "pendulum_90": {}
        }}), encoding="utf-8")
        for setting in ("pendulum_20", "pendulum_45", "pendulum_90"):
            p = root / "iris/Pendulum" / setting / "01.mp4"
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(b"video" + setting.encode())

        cap = root / "rgbench/green_tshirt/green_tshirt_grasp_2025-01-01"
        (cap / "calibration").mkdir(parents=True, exist_ok=True)
        (cap / "joints").mkdir()
        (cap / "segment_pcds").mkdir()
        (cap / "calibration/world_to_camera_transform.json").write_text("{}", encoding="utf-8")
        (cap / "joints/left.csv").write_text("x", encoding="utf-8")
        (cap / "joints/right.csv").write_text("x", encoding="utf-8")
        (cap / "segment_pcds/a.pcd").write_bytes(b"pcd")

        report = prepare(root, out)
        assert report["prospective_world_count"] >= 5
        world = list(csv.DictReader((out / "world_manifest.csv").open()))
        assert not any("foam shearing" in r["scene"] for r in world)
        p90 = next(r for r in world if r["scene"].startswith("pendulum/pendulum_90"))
        assert p90["split"] == "final_test"
    print("VALID public dataset preparation self-test")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=Path("build/public-datasets"))
    ap.add_argument("--out", type=Path, default=Path("build/publication-validation/public-data"))
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        self_test()
        return 0
    report = prepare(args.root, args.out)
    print("VALID prepared public validation datasets")
    print("INVENTORY", report["inventory_scene_count"])
    print("PROSPECTIVE", report["prospective_world_count"])
    print("WORLD_MANIFEST", args.out / "world_manifest.csv")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

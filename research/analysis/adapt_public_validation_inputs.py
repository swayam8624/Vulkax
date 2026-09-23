#!/usr/bin/env python3
"""Materialize public benchmark inputs into Vulkax-facing adapter packages.

This is an input-adaptation stage, not a method evaluation stage.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def safe_scene(scene: str) -> str:
    return scene.replace("/", "__").replace(" ", "_")


def load_inventory(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def load_truth(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def gauge_metadata_path(data_root: Path, task: str) -> Path:
    metadata_name = "foam compressing" if task == "foam compression" else task
    return data_root / "gauge" / "metadata" / "deformable" / f"{metadata_name}.json"


def adapt_gauge(
    repo_root: Path,
    data_root: Path,
    out_root: Path,
    row: dict[str, str],
) -> dict:
    scene = row["scene"]
    task, material, _trial = scene.split("/")
    trial_path = data_root / row["local_path"]
    metadata = gauge_metadata_path(data_root, task)
    if not trial_path.is_file():
        raise FileNotFoundError(trial_path)
    if not metadata.is_file():
        raise FileNotFoundError(metadata)
    out = out_root / "gauge" / safe_scene(scene)
    cmd = [
        sys.executable,
        str(repo_root / "research/adapters/gauge_deformable_to_vulkax.py"),
        "--metadata", str(metadata),
        "--trial", str(trial_path),
        "--material", material,
        "--out", str(out),
    ]
    subprocess.run(cmd, cwd=repo_root, check=True, stdout=subprocess.DEVNULL)
    manifest = json.loads((out / "manifest.json").read_text(encoding="utf-8"))
    manifest["validation_scene"] = scene
    manifest["validation_split"] = row["split"]
    manifest["campaign_role"] = row["campaign_role"]
    (out / "validation_manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return {
        "dataset": "gauge",
        "scene": scene,
        "out": str(out),
        "artifact": "markers.csv",
        "frame_count": manifest["frame_count"],
        "marker_count": manifest["marker_count"],
    }


def adapt_iris(
    data_root: Path,
    out_root: Path,
    row: dict[str, str],
    truth_index: dict,
) -> dict:
    scene = row["scene"]
    video = data_root / row["local_path"]
    if not video.is_file():
        raise FileNotFoundError(video)
    truth = truth_index["datasets"]["iris"]["scenes"].get(scene)
    if truth is None:
        raise RuntimeError(f"IRIS truth metadata missing for {scene}")
    out = out_root / "iris" / safe_scene(scene)
    out.mkdir(parents=True, exist_ok=True)
    manifest = {
        "schema": "vulkax.iris_real_video_input",
        "version": 1,
        "provenance": "measured_real_video",
        "scene": scene,
        "split": row["split"],
        "campaign_role": row["campaign_role"],
        "video": {
            "path": str(video),
            "sha256": sha256(video),
        },
        "ground_truth": {
            "source": "IRIS parameters.json independently measured physical parameters",
            "parameters": truth.get("parameters"),
        },
        "units": row["units"],
        "license": row["license"],
        "warning": (
            "Input/ground-truth package only. No Reality Probe score is implied by this manifest."
        ),
    }
    (out / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return {
        "dataset": "iris",
        "scene": scene,
        "out": str(out),
        "artifact": "manifest.json",
        "frame_count": "",
        "marker_count": "",
    }


def pcd_header(path: Path) -> dict:
    header = b""
    with path.open("rb") as f:
        for _ in range(64):
            line = f.readline()
            if not line:
                break
            header += line
            if line.upper().startswith(b"DATA "):
                break
    text = header.decode("ascii", errors="replace")
    fields = {}
    for line in text.splitlines():
        parts = line.strip().split()
        if parts:
            fields[parts[0].upper()] = parts[1:]
    if "DATA" not in fields or "FIELDS" not in fields:
        raise ValueError(f"invalid/unsupported PCD header: {path}")
    return {
        "fields": fields.get("FIELDS", []),
        "points": int(fields["POINTS"][0]) if fields.get("POINTS") else None,
        "width": int(fields["WIDTH"][0]) if fields.get("WIDTH") else None,
        "height": int(fields["HEIGHT"][0]) if fields.get("HEIGHT") else None,
        "data": fields["DATA"][0] if fields.get("DATA") else None,
    }


def adapt_rgbench(
    data_root: Path,
    out_root: Path,
    row: dict[str, str],
) -> dict:
    scene = row["scene"]
    capture = data_root / row["local_path"]
    if not capture.is_dir():
        raise FileNotFoundError(capture)
    cal = capture / "calibration" / "world_to_camera_transform.json"
    if not cal.is_file():
        raise FileNotFoundError(cal)
    calibration = json.loads(cal.read_text(encoding="utf-8"))
    joint_files = sorted((capture / "joints").glob("*.csv"))
    pcds = sorted((capture / "segment_pcds").glob("*.pcd"))
    if len(joint_files) < 2 or not pcds:
        raise RuntimeError(f"incomplete RGBench capture: {capture}")

    joint_headers = {}
    for path in joint_files:
        with path.open(newline="", encoding="utf-8", errors="replace") as f:
            reader = csv.reader(f)
            joint_headers[path.name] = next(reader, [])

    first_header = pcd_header(pcds[0])
    last_header = pcd_header(pcds[-1])
    out = out_root / "rgbench" / safe_scene(scene)
    out.mkdir(parents=True, exist_ok=True)
    manifest = {
        "schema": "vulkax.rgbench_cloth_input",
        "version": 1,
        "provenance": "measured_rgbd_robot_capture",
        "scene": scene,
        "split": row["split"],
        "campaign_role": row["campaign_role"],
        "capture_path": str(capture),
        "calibration": {
            "path": str(cal),
            "sha256": sha256(cal),
            "world_to_camera": calibration,
        },
        "joint_streams": [
            {
                "path": str(p),
                "sha256": sha256(p),
                "columns": joint_headers[p.name],
            }
            for p in joint_files
        ],
        "point_clouds": {
            "count": len(pcds),
            "first": {
                "path": str(pcds[0]),
                "sha256": sha256(pcds[0]),
                "header": first_header,
            },
            "last": {
                "path": str(pcds[-1]),
                "sha256": sha256(pcds[-1]),
                "header": last_header,
            },
        },
        "truth_source": row["truth_source"],
        "license": row["license"],
        "warning": (
            "Real capture adapter package. Point-cloud agreement is observational evidence; "
            "it is not automatically material-parameter ground truth."
        ),
    }
    (out / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return {
        "dataset": "rgbench",
        "scene": scene,
        "out": str(out),
        "artifact": "manifest.json",
        "frame_count": len(pcds),
        "marker_count": "",
    }


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo-root", type=Path, default=Path("."))
    ap.add_argument("--data-root", type=Path, default=Path("build/public-datasets"))
    ap.add_argument(
        "--prepared-root",
        type=Path,
        default=Path("build/publication-validation/public-data"),
    )
    ap.add_argument("--out", type=Path)
    args = ap.parse_args()

    repo_root = args.repo_root.resolve()
    data_root = args.data_root.resolve()
    prepared = args.prepared_root.resolve()
    out_root = (args.out or (prepared / "adapted")).resolve()
    inventory = load_inventory(prepared / "dataset_inventory.csv")
    truth = load_truth(prepared / "dataset_truth_index.json")

    results = []
    failures = []
    for row in inventory:
        try:
            if row["dataset"] == "gauge":
                results.append(adapt_gauge(repo_root, data_root, out_root, row))
            elif row["dataset"] == "iris":
                results.append(adapt_iris(data_root, out_root, row, truth))
            elif row["dataset"] == "rgbench":
                if row["campaign_role"] == "adapter_blocked":
                    failures.append({
                        "dataset": "rgbench", "scene": row["scene"],
                        "reason": row["exclusion_rule"],
                    })
                else:
                    results.append(adapt_rgbench(data_root, out_root, row))
        except Exception as exc:
            failures.append({
                "dataset": row["dataset"],
                "scene": row["scene"],
                "reason": f"{type(exc).__name__}: {exc}",
            })

    out_root.mkdir(parents=True, exist_ok=True)
    report = {
        "schema": "vulkax.public_input_adapter_report",
        "version": 1,
        "adapted_count": len(results),
        "failure_count": len(failures),
        "adapted": results,
        "failures": failures,
    }
    (out_root / "adapter_summary.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    if failures:
        # Keep the failures visible and make incomplete core preparation fail.
        for failure in failures:
            print("ADAPTER FAILURE", failure, file=sys.stderr)
        raise SystemExit(
            f"public input adaptation incomplete: {len(failures)} failure(s); "
            f"see {out_root / 'adapter_summary.json'}"
        )
    print(f"VALID adapted public inputs: {len(results)}")
    print("OUT", out_root)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

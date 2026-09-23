#!/usr/bin/env python3
"""Download pinned public datasets for the Reality Probe validation campaign.

Profiles:
  smoke - minimal network/disk check.
  core  - publication-development subset spanning every selected benchmark family.
  full  - complete public releases (large: GAUGE ~10.5 GB, RGBench ~6.7 GB,
          IRIS 240 4K videos).

The downloader records the exact Hugging Face revision and SHA-256 hashes of every
materialized file. It never generates scientific result rows.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import fnmatch
import json
from pathlib import Path
import re
import sys
from datetime import datetime, timezone


@dataclass(frozen=True)
class DatasetSpec:
    key: str
    repo_id: str
    license: str
    citation_url: str


SPECS = {
    "gauge": DatasetSpec(
        "gauge",
        "InternRobotics/GAUGE-Dataset",
        "MIT",
        "https://huggingface.co/datasets/InternRobotics/GAUGE-Dataset",
    ),
    "iris": DatasetSpec(
        "iris",
        "rasulkhanbayov/IRIS",
        "CC-BY-NC-4.0",
        "https://huggingface.co/datasets/rasulkhanbayov/IRIS",
    ),
    "rgbench": DatasetSpec(
        "rgbench",
        "RGBench/RGBench-Cloth-Sim2Real-v1",
        "CC-BY-4.0",
        "https://huggingface.co/datasets/RGBench/RGBench-Cloth-Sim2Real-v1",
    ),
}

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

RGBENCH_CORE_GARMENTS = ("green_tshirt", "grey_pleat_skirt", "white_shirt")
RGBENCH_ACTIONS = ("grasp", "fold", "fling")


def require_hf():
    try:
        from huggingface_hub import HfApi, snapshot_download
    except ImportError as exc:
        raise SystemExit(
            "huggingface_hub is required. Use "
            "bash research/scripts/prepare_public_validation_data.sh"
        ) from exc
    return HfApi, snapshot_download


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def normalize_token(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "", value.lower())


def gauge_patterns(profile: str) -> list[str] | None:
    common = ["README.md", "LICENSE*", "assets/obj/foam.obj"]
    if profile == "full":
        return None
    if profile == "smoke":
        return common + [
            "metadata/deformable/foam shearing.json",
            "data/deformable/foam shearing/json/soft/1.json",
            "data/deformable/foam shearing/json/hard/1.json",
        ]
    patterns = list(common)
    for task in ("foam stretching", "foam compressing", "foam shearing"):
        patterns.append(f"metadata/deformable/{task}.json")
        data_task = "foam compression" if task == "foam compressing" else task
        for material in ("soft", "hard"):
            patterns.append(f"data/deformable/{data_task}/json/{material}/*.json")
    return patterns


def iris_patterns(profile: str) -> list[str] | None:
    common = ["README.md", "parameters.json"]
    if profile == "full":
        return None
    if profile == "smoke":
        return common + [
            "Pendulum/pendulum_20/01.mp4",
            "Pendulum/pendulum_45/01.mp4",
            "Pendulum/pendulum_90/01.mp4",
        ]
    # Core: one independently recorded take from every one of the 24 settings.
    patterns = list(common)
    for folder, settings in IRIS_LAYOUT.items():
        for setting in settings:
            patterns.append(f"{folder}/{setting}/01.mp4")
    return patterns


def capture_dirs(repo_files: list[str], garment: str) -> dict[str, list[str]]:
    prefix = garment + "/"
    dirs: dict[str, set[str]] = {a: set() for a in RGBENCH_ACTIONS}
    for path in repo_files:
        if not path.startswith(prefix):
            continue
        parts = path.split("/")
        if len(parts) < 3:
            continue
        capture = parts[1]
        for action in RGBENCH_ACTIONS:
            if f"_{action}_" in capture:
                dirs[action].add("/".join(parts[:2]))
    return {k: sorted(v) for k, v in dirs.items()}


def rgbench_patterns(profile: str, repo_files: list[str]) -> list[str] | None:
    if profile == "full":
        return None
    common = ["README.md", "LICENSE*", "DATA_LICENSE*"]
    if profile == "smoke":
        return common + [
            "green_tshirt/green_tshirt_grasp_2025-07-19-19-14-58/**",
            "meshes/Green_Tshirt/**",
            "meshes/Green_Tshirt_Compare/**",
        ]

    patterns = list(common)
    for garment in RGBENCH_CORE_GARMENTS:
        by_action = capture_dirs(repo_files, garment)
        for action in RGBENCH_ACTIONS:
            candidates = by_action[action]
            if not candidates:
                raise RuntimeError(
                    f"RGBench core selection found no {action} capture for {garment}"
                )
            # Select exactly one capture/action before any benchmark result is seen.
            patterns.append(candidates[0] + "/**")

        want = normalize_token(garment)
        mesh_roots = sorted({
            "/".join(p.split("/")[:2])
            for p in repo_files
            if p.startswith("meshes/")
            and len(p.split("/")) >= 3
            and normalize_token(p.split("/")[1]) == want
        })
        if not mesh_roots:
            raise RuntimeError(f"RGBench core selection found no mesh for {garment}")
        for mesh_root in mesh_roots:
            patterns.append(mesh_root + "/**")
    return sorted(set(patterns))


def materialized_files(root: Path) -> list[Path]:
    return sorted(
        p for p in root.rglob("*")
        if p.is_file()
        and ".cache/huggingface" not in p.as_posix()
        and p.name != "vulkax_download_manifest.json"
    )


def write_dataset_manifest(
    target: Path,
    spec: DatasetSpec,
    revision: str,
    profile: str,
    requested_patterns: list[str] | None,
) -> dict:
    files = materialized_files(target)
    entries = []
    total = 0
    for path in files:
        rel = path.relative_to(target).as_posix()
        size = path.stat().st_size
        total += size
        entries.append({"path": rel, "bytes": size, "sha256": sha256(path)})
    manifest = {
        "schema": "vulkax.public_dataset_download",
        "version": 1,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "dataset": spec.key,
        "repo_id": spec.repo_id,
        "revision": revision,
        "license": spec.license,
        "citation_url": spec.citation_url,
        "profile": profile,
        "requested_patterns": requested_patterns,
        "file_count": len(entries),
        "total_bytes": total,
        "files": entries,
    }
    (target / "vulkax_download_manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return manifest


def catalog_one(key: str, profile: str) -> dict:
    HfApi, _snapshot_download = require_hf()
    spec = SPECS[key]
    api = HfApi()
    info = api.dataset_info(spec.repo_id)
    revision = str(info.sha)
    repo_files = api.list_repo_files(spec.repo_id, repo_type="dataset", revision=revision)
    if key == "gauge":
        patterns = gauge_patterns(profile)
    elif key == "iris":
        patterns = iris_patterns(profile)
    elif key == "rgbench":
        patterns = rgbench_patterns(profile, repo_files)
    else:
        raise ValueError(key)

    missing = []
    if patterns is not None:
        for pattern in patterns:
            if pattern in {"LICENSE*", "DATA_LICENSE*"}:
                continue
            if not any(fnmatch.fnmatch(path, pattern) for path in repo_files):
                missing.append(pattern)
    if missing:
        raise RuntimeError(
            f"{key} catalog @ {revision} is missing requested core paths: {missing}"
        )
    print(
        f"[catalog] VALID {key}: {spec.repo_id}@{revision[:12]} "
        f"files={len(repo_files)} profile={profile}"
    )
    return {
        "dataset": key,
        "repo_id": spec.repo_id,
        "revision": revision,
        "repo_file_count": len(repo_files),
        "requested_pattern_count": None if patterns is None else len(patterns),
    }


def download_one(key: str, profile: str, root: Path) -> dict:
    HfApi, snapshot_download = require_hf()
    spec = SPECS[key]
    api = HfApi()
    info = api.dataset_info(spec.repo_id)
    revision = str(info.sha)
    repo_files = api.list_repo_files(spec.repo_id, repo_type="dataset", revision=revision)

    if key == "gauge":
        patterns = gauge_patterns(profile)
    elif key == "iris":
        patterns = iris_patterns(profile)
    elif key == "rgbench":
        patterns = rgbench_patterns(profile, repo_files)
    else:
        raise ValueError(key)

    target = root / key
    target.mkdir(parents=True, exist_ok=True)
    print(f"[data] {key}: {spec.repo_id}@{revision}")
    print(f"[data] profile={profile} target={target}")
    snapshot_download(
        repo_id=spec.repo_id,
        repo_type="dataset",
        revision=revision,
        local_dir=str(target),
        allow_patterns=patterns,
    )
    manifest = write_dataset_manifest(target, spec, revision, profile, patterns)
    print(
        f"[data] VALID {key}: {manifest['file_count']} files, "
        f"{manifest['total_bytes'] / (1024**2):.1f} MiB"
    )
    return manifest


def self_test() -> None:
    gp = gauge_patterns("core")
    assert gp is not None
    assert "data/deformable/foam shearing/json/soft/*.json" in gp
    ip = iris_patterns("core")
    assert ip is not None and len([x for x in ip if x.endswith(".mp4")]) == 24

    fake = [
        "green_tshirt/green_tshirt_grasp_2025-01-01/calibration/a.json",
        "green_tshirt/green_tshirt_fold_2025-01-02/joints/a.csv",
        "green_tshirt/green_tshirt_fling_2025-01-03/segment_pcds/a.pcd",
        "meshes/Green_Tshirt/green_tshirt.obj",
        "grey_pleat_skirt/grey_pleat_skirt_grasp_2025-01-01/a",
        "grey_pleat_skirt/grey_pleat_skirt_fold_2025-01-02/a",
        "grey_pleat_skirt/grey_pleat_skirt_fling_2025-01-03/a",
        "meshes/Grey_Pleat_Skirt/grey_pleat_skirt.obj",
        "white_shirt/white_shirt_grasp_2025-01-01/a",
        "white_shirt/white_shirt_fold_2025-01-02/a",
        "white_shirt/white_shirt_fling_2025-01-03/a",
        "meshes/White_Shirt/white_shirt.obj",
    ]
    rp = rgbench_patterns("core", fake)
    assert rp is not None
    assert any(x.startswith("green_tshirt/green_tshirt_grasp") for x in rp)
    assert any(x.startswith("meshes/White_Shirt") for x in rp)
    print("VALID public dataset downloader self-test")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", type=Path, default=Path("build/public-datasets"))
    ap.add_argument("--profile", choices=("smoke", "core", "full"), default="core")
    ap.add_argument(
        "--datasets",
        nargs="+",
        choices=tuple(SPECS),
        default=["gauge", "iris", "rgbench"],
    )
    ap.add_argument("--self-test", action="store_true")
    ap.add_argument("--catalog-only", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        self_test()
        return 0

    if args.catalog_only:
        catalog = [catalog_one(key, args.profile) for key in args.datasets]
        print(json.dumps({"profile": args.profile, "datasets": catalog}, indent=2))
        return 0

    args.root.mkdir(parents=True, exist_ok=True)
    manifests = []
    for key in args.datasets:
        manifests.append(download_one(key, args.profile, args.root))
    campaign = {
        "schema": "vulkax.public_dataset_campaign_download",
        "version": 1,
        "profile": args.profile,
        "datasets": [
            {
                "dataset": m["dataset"],
                "repo_id": m["repo_id"],
                "revision": m["revision"],
                "license": m["license"],
                "file_count": m["file_count"],
                "total_bytes": m["total_bytes"],
            }
            for m in manifests
        ],
    }
    (args.root / "campaign_download_manifest.json").write_text(
        json.dumps(campaign, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    print(f"[data] campaign manifest: {args.root / 'campaign_download_manifest.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

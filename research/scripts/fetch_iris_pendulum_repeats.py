#!/usr/bin/env python3
"""Fetch only the IRIS pendulum repeats needed for the current validation phase.

Validation phase:
  - pendulum_20/{01..10}.mp4 (development)
  - pendulum_45/{01..10}.mp4 (validation)
  - never requests pendulum_90 beyond any file already present from the core profile

Final-test phase is refused unless a publication-validation lock is supplied.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from datetime import datetime, timezone


IRIS_REPO = "rasulkhanbayov/IRIS"


def require_hf():
    try:
        from huggingface_hub import HfApi, snapshot_download
    except ImportError as exc:
        raise SystemExit(
            "huggingface_hub is required. Run through "
            "research/scripts/run_iris_pendulum_validation.sh"
        ) from exc
    return HfApi, snapshot_download


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def materialized_files(root: Path):
    return sorted(
        p for p in root.rglob("*")
        if p.is_file()
        and ".cache/huggingface" not in p.as_posix()
        and p.name != "vulkax_download_manifest.json"
    )


def update_manifest(root: Path, revision: str, phase: str) -> dict:
    files = materialized_files(root)
    entries = []
    total = 0
    for p in files:
        rel = p.relative_to(root).as_posix()
        size = p.stat().st_size
        total += size
        entries.append({"path": rel, "bytes": size, "sha256": sha256(p)})
    manifest = {
        "schema": "vulkax.public_dataset_download",
        "version": 1,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "dataset": "iris",
        "repo_id": IRIS_REPO,
        "revision": revision,
        "license": "CC-BY-NC-4.0",
        "citation_url": "https://huggingface.co/datasets/rasulkhanbayov/IRIS",
        "profile": f"core+iris_pendulum_{phase}",
        "requested_patterns": (
            [
                "README.md",
                "parameters.json",
                "Pendulum/pendulum_20/*.mp4",
                "Pendulum/pendulum_45/*.mp4",
            ]
            if phase == "validation"
            else [
                "README.md",
                "parameters.json",
                "Pendulum/pendulum_90/*.mp4",
            ]
        ),
        "file_count": len(entries),
        "total_bytes": total,
        "files": entries,
    }
    (root / "vulkax_download_manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return manifest


def update_campaign_manifest(data_root: Path, iris_manifest: dict) -> None:
    path = data_root / "campaign_download_manifest.json"
    if not path.is_file():
        return
    data = json.loads(path.read_text(encoding="utf-8"))
    found = False
    for ds in data.get("datasets", []):
        if ds.get("dataset") == "iris":
            ds.update(
                {
                    "repo_id": iris_manifest["repo_id"],
                    "revision": iris_manifest["revision"],
                    "license": iris_manifest["license"],
                    "file_count": iris_manifest["file_count"],
                    "total_bytes": iris_manifest["total_bytes"],
                }
            )
            found = True
    if found:
        data["profile"] = str(data.get("profile", "core")) + "+iris-pendulum"
        path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--data-root", type=Path, default=Path("build/public-datasets"))
    ap.add_argument("--phase", choices=("validation", "final_test"), default="validation")
    ap.add_argument("--lock", type=Path)
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        assert args.phase in {"validation", "final_test"}
        print("VALID IRIS pendulum repeat fetcher self-test")
        return 0

    if args.phase == "final_test":
        if args.lock is None:
            raise SystemExit("final_test IRIS download requires --lock")
        subprocess.run(
            [
                "python3",
                "research/analysis/freeze_publication_validation.py",
                "--check",
                str(args.lock),
            ],
            check=True,
        )

    HfApi, snapshot_download = require_hf()
    api = HfApi()
    info = api.dataset_info(IRIS_REPO)
    remote_revision = str(info.sha)

    iris_root = args.data_root / "iris"
    iris_root.mkdir(parents=True, exist_ok=True)
    existing_manifest = iris_root / "vulkax_download_manifest.json"
    if existing_manifest.is_file():
        existing = json.loads(existing_manifest.read_text(encoding="utf-8"))
        pinned = str(existing.get("revision", ""))
        if pinned and pinned != remote_revision:
            raise SystemExit(
                "IRIS upstream revision changed since the core campaign: "
                f"local={pinned} remote={remote_revision}. "
                "Do not mix revisions in one validation campaign."
            )
        revision = pinned or remote_revision
    else:
        revision = remote_revision

    if args.phase == "validation":
        patterns = [
            "README.md",
            "parameters.json",
            "Pendulum/pendulum_20/*.mp4",
            "Pendulum/pendulum_45/*.mp4",
        ]
    else:
        patterns = [
            "README.md",
            "parameters.json",
            "Pendulum/pendulum_90/*.mp4",
        ]

    print(f"[iris-pendulum] repo={IRIS_REPO}@{revision}")
    print(f"[iris-pendulum] phase={args.phase}")
    snapshot_download(
        repo_id=IRIS_REPO,
        repo_type="dataset",
        revision=revision,
        local_dir=str(iris_root),
        allow_patterns=patterns,
    )

    manifest = update_manifest(iris_root, revision, args.phase)
    update_campaign_manifest(args.data_root, manifest)

    expected = []
    if args.phase == "validation":
        for setting in ("pendulum_20", "pendulum_45"):
            for take in range(1, 11):
                expected.append(iris_root / "Pendulum" / setting / f"{take:02d}.mp4")
    else:
        for take in range(1, 11):
            expected.append(iris_root / "Pendulum" / "pendulum_90" / f"{take:02d}.mp4")

    missing = [str(p) for p in expected if not p.is_file()]
    if missing:
        raise SystemExit(f"IRIS pendulum download incomplete: {len(missing)} missing files")

    bytes_expected = sum(p.stat().st_size for p in expected)
    print(
        f"[iris-pendulum] VALID {len(expected)} phase files "
        f"({bytes_expected/(1024**2):.1f} MiB)"
    )
    print(f"[iris-pendulum] manifest={iris_root/'vulkax_download_manifest.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

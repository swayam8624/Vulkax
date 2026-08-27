#!/usr/bin/env python3
"""Validate the locally fetched Vulkax showcase asset pack against its lock."""

from __future__ import annotations

import argparse
import hashlib
import json
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


def safe_path(root: Path, relative_text: str) -> Path:
    relative = Path(relative_text)
    if relative.is_absolute() or ".." in relative.parts:
        fail(f"unsafe asset destination: {relative_text}")
    root_resolved = root.resolve()
    candidate = (root / relative).resolve()
    try:
        candidate.relative_to(root_resolved)
    except ValueError:
        fail(f"asset destination escapes root: {relative_text}")
    return candidate


def validate(lock_file: Path, root: Path) -> list[str]:
    data = json.loads(lock_file.read_text(encoding="utf-8"))
    if data.get("schema") != "vulkax_showcase_assets" or data.get("version") != 1:
        fail("unexpected showcase asset lock schema/version")
    assets = data.get("assets")
    if not isinstance(assets, list) or not assets:
        fail("showcase asset lock contains no assets")

    ids: set[str] = set()
    destinations: set[str] = set()
    validated: list[str] = []
    for index, asset in enumerate(assets):
        if not isinstance(asset, dict):
            fail(f"asset entry {index} is not an object")
        asset_id = asset.get("id")
        if not isinstance(asset_id, str) or not asset_id:
            fail(f"asset entry {index} has invalid id")
        if asset_id in ids:
            fail(f"duplicate asset id: {asset_id}")
        ids.add(asset_id)
        if asset.get("license") != "CC0-1.0":
            fail(f"asset {asset_id} is not explicitly CC0-1.0")
        if not isinstance(asset.get("source_page"), str) or not asset["source_page"].startswith("https://"):
            fail(f"asset {asset_id} has invalid source page")
        if not isinstance(asset.get("download_url"), str) or not asset["download_url"].startswith("https://"):
            fail(f"asset {asset_id} has invalid download URL")
        relative = asset.get("destination")
        if not isinstance(relative, str) or not relative:
            fail(f"asset {asset_id} has invalid destination")
        if relative in destinations:
            fail(f"duplicate asset destination: {relative}")
        destinations.add(relative)
        path = safe_path(root, relative)
        if not path.is_file():
            fail(f"missing asset {asset_id}: {path}")
        expected_size = asset.get("bytes")
        if not isinstance(expected_size, int) or path.stat().st_size != expected_size:
            fail(f"asset {asset_id} byte count mismatch")
        expected_sha = asset.get("sha256")
        if not isinstance(expected_sha, str) or sha256_file(path) != expected_sha:
            fail(f"asset {asset_id} SHA-256 mismatch")
        validated.append(asset_id)
    return validated


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate Vulkax showcase assets")
    parser.add_argument("lock_file", type=Path, nargs="?", default=Path("assets/demo/showcase_assets.lock.json"))
    parser.add_argument("asset_root", type=Path, nargs="?", default=Path("build/demo-assets"))
    args = parser.parse_args()
    try:
        validated = validate(args.lock_file, args.asset_root)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"INVALID showcase asset pack: {error}", file=sys.stderr)
        return 1
    print("VALID showcase asset pack: " + ", ".join(validated))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

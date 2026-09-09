#!/usr/bin/env python3
"""Fetch checksum-pinned, license-reviewed video inputs for post-1.0 reality-loop development.

These assets are observation sources only. They are never Vulkax 1.0 verification
evidence, and the lock records the physical mechanism expected by a benchmark so a
visually similar clip cannot silently be paired with the wrong governing model.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import urllib.request


ALLOWED_LICENSES = {"CC0-1.0", "CC-BY-3.0", "CC-BY-4.0", "CC-BY-SA-4.0"}


def fail(message: str) -> "None":
    raise SystemExit(f"reality video asset fetch FAILED: {message}")


def sha1_file(path: Path) -> str:
    digest = hashlib.sha1()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_lock(path: Path) -> dict:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot read lock file {path}: {error}")
    if data.get("schema") != "vulkax_reality_video_assets" or data.get("version") != 2:
        fail("unexpected reality video asset lock schema/version")
    assets = data.get("assets")
    if not isinstance(assets, list) or not assets:
        fail("reality video asset lock contains no assets")
    return data


def safe_destination(root: Path, relative_text: str) -> Path:
    relative = Path(relative_text)
    if relative.is_absolute() or ".." in relative.parts:
        fail(f"unsafe video destination: {relative_text}")
    root_resolved = root.resolve()
    destination = (root / relative).resolve()
    try:
        destination.relative_to(root_resolved)
    except ValueError:
        fail(f"video destination escapes root: {relative_text}")
    return destination


def validate_asset_entry(asset: dict) -> None:
    required = {
        "id", "kind", "source", "source_page", "download_url", "license", "attribution",
        "sha1", "bytes", "duration_seconds", "width_pixels", "height_pixels", "destination",
        "mechanism", "evidence_scope", "role",
    }
    missing = sorted(required - set(asset))
    if missing:
        fail("video asset entry missing fields: " + ", ".join(missing))
    if asset["kind"] != "video":
        fail(f"asset {asset['id']} is not a video")
    for field in ("id", "source", "source_page", "download_url", "license", "attribution",
                  "destination", "mechanism", "evidence_scope", "role"):
        if not isinstance(asset[field], str) or not asset[field]:
            fail(f"asset field {field} must be a non-empty string")
    if asset["license"] not in ALLOWED_LICENSES:
        fail(f"asset {asset['id']} uses a license outside the reviewed allow-list")
    if not asset["source_page"].startswith("https://") or not asset["download_url"].startswith("https://"):
        fail(f"asset {asset['id']} must use https source/download URLs")
    checksum = asset["sha1"].lower()
    if len(checksum) != 40 or any(character not in "0123456789abcdef" for character in checksum):
        fail(f"asset {asset['id']} has invalid SHA-1")
    if not isinstance(asset["bytes"], int) or asset["bytes"] <= 0:
        fail(f"asset {asset['id']} has invalid byte count")
    if not isinstance(asset["duration_seconds"], (int, float)) or asset["duration_seconds"] <= 0:
        fail(f"asset {asset['id']} has invalid duration")
    if not isinstance(asset["width_pixels"], int) or not isinstance(asset["height_pixels"], int) or \
            asset["width_pixels"] <= 0 or asset["height_pixels"] <= 0:
        fail(f"asset {asset['id']} has invalid dimensions")


def select_assets(assets: list[dict], requested_ids: list[str]) -> list[dict]:
    if not requested_ids:
        return assets
    by_id = {asset.get("id"): asset for asset in assets}
    missing = [asset_id for asset_id in requested_ids if asset_id not in by_id]
    if missing:
        fail("unknown --asset-id value(s): " + ", ".join(missing))
    return [by_id[asset_id] for asset_id in requested_ids]


def fetch_asset(asset: dict, root: Path, force: bool) -> None:
    validate_asset_entry(asset)
    destination = safe_destination(root, asset["destination"])
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_file() and not force:
        if destination.stat().st_size == asset["bytes"] and sha1_file(destination) == asset["sha1"]:
            print(f"OK cached {asset['id']}: {destination}")
            return
        fail(f"existing video does not match lock: {destination}")

    request = urllib.request.Request(
        asset["download_url"],
        headers={"User-Agent": "Vulkax-reality-loop/2.0 (+https://github.com/swayam8624/Vulkax)"},
    )
    temporary_path: Path | None = None
    try:
        with urllib.request.urlopen(request, timeout=180) as response:
            with tempfile.NamedTemporaryFile(delete=False, dir=destination.parent) as temporary:
                temporary_path = Path(temporary.name)
                shutil.copyfileobj(response, temporary)
        actual_size = temporary_path.stat().st_size
        actual_sha1 = sha1_file(temporary_path)
        if actual_size != asset["bytes"]:
            fail(f"{asset['id']} byte count mismatch: expected {asset['bytes']}, got {actual_size}")
        if actual_sha1 != asset["sha1"]:
            fail(f"{asset['id']} SHA-1 mismatch: expected {asset['sha1']}, got {actual_sha1}")
        temporary_path.replace(destination)
        temporary_path = None
    finally:
        if temporary_path is not None and temporary_path.exists():
            temporary_path.unlink()
    print(f"FETCHED {asset['id']}: {destination}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Fetch checksum-pinned Vulkax reality-loop video assets")
    parser.add_argument("lock_file", type=Path, nargs="?", default=Path("assets/reality/video_sources.lock.json"))
    parser.add_argument("output_root", type=Path, nargs="?", default=Path("build/reality-assets"))
    parser.add_argument("--asset-id", action="append", default=[], help="fetch only the named locked asset (repeatable)")
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args()

    lock = load_lock(args.lock_file)
    for asset in lock["assets"]:
        validate_asset_entry(asset)
    selected = select_assets(lock["assets"], args.asset_id)
    if args.validate_only:
        print(f"VALID reality video asset lock: {len(lock['assets'])} asset(s); selected {len(selected)}")
        return
    for asset in selected:
        fetch_asset(asset, args.output_root, args.force)
    print(f"VALID reality video asset pack: {len(selected)} selected asset(s)")


if __name__ == "__main__":
    main()

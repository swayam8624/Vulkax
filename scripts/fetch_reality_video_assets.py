#!/usr/bin/env python3
"""Fetch checksum-pinned development videos for the post-1.0 reality loop."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import urllib.request

SCHEMA = "vulkax_reality_video_assets"
VERSION = 1
ALLOWED_LICENSES = {"CC0-1.0", "CC-BY-3.0", "CC-BY-4.0"}


def fail(message: str) -> "None":
    raise SystemExit(f"reality video asset fetch FAILED: {message}")


def sha1_file(path: Path) -> str:
    digest = hashlib.sha1()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def safe_destination(root: Path, relative_text: str) -> Path:
    relative = Path(relative_text)
    if relative.is_absolute() or ".." in relative.parts:
        fail(f"unsafe destination: {relative_text}")
    root_resolved = root.resolve()
    destination = (root / relative).resolve()
    try:
        destination.relative_to(root_resolved)
    except ValueError:
        fail(f"destination escapes output root: {relative_text}")
    return destination


def validate_entry(asset: dict) -> None:
    required = {
        "id", "kind", "source", "source_page", "download_url", "license",
        "attribution", "sha1", "bytes", "duration_seconds", "width_pixels",
        "height_pixels", "destination", "role",
    }
    missing = sorted(required - set(asset))
    if missing:
        fail("asset entry missing fields: " + ", ".join(missing))
    if asset["kind"] != "video":
        fail(f"asset {asset['id']} must have kind=video")
    if asset["license"] not in ALLOWED_LICENSES:
        fail(f"asset {asset['id']} uses unsupported development license {asset['license']}")
    if not asset["attribution"]:
        fail(f"asset {asset['id']} requires attribution text")
    if not str(asset["source_page"]).startswith("https://") or not str(asset["download_url"]).startswith("https://"):
        fail(f"asset {asset['id']} must use HTTPS source/download URLs")
    digest = str(asset["sha1"]).lower()
    if len(digest) != 40 or any(character not in "0123456789abcdef" for character in digest):
        fail(f"asset {asset['id']} has invalid SHA-1")
    if not isinstance(asset["bytes"], int) or asset["bytes"] <= 0:
        fail(f"asset {asset['id']} has invalid byte count")
    if not isinstance(asset["width_pixels"], int) or asset["width_pixels"] <= 0:
        fail(f"asset {asset['id']} has invalid width")
    if not isinstance(asset["height_pixels"], int) or asset["height_pixels"] <= 0:
        fail(f"asset {asset['id']} has invalid height")
    if not isinstance(asset["duration_seconds"], (int, float)) or asset["duration_seconds"] <= 0:
        fail(f"asset {asset['id']} has invalid duration")


def load_lock(path: Path) -> dict:
    try:
        lock = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot read lock file {path}: {error}")
    if lock.get("schema") != SCHEMA or lock.get("version") != VERSION:
        fail("unexpected reality-video lock schema/version")
    assets = lock.get("assets")
    if not isinstance(assets, list) or not assets:
        fail("reality-video lock contains no assets")
    identifiers: set[str] = set()
    destinations: set[str] = set()
    for asset in assets:
        if not isinstance(asset, dict):
            fail("reality-video asset entry must be an object")
        validate_entry(asset)
        if asset["id"] in identifiers:
            fail(f"duplicate asset id: {asset['id']}")
        if asset["destination"] in destinations:
            fail(f"duplicate asset destination: {asset['destination']}")
        identifiers.add(asset["id"])
        destinations.add(asset["destination"])
    return lock


def fetch(asset: dict, output_root: Path, force: bool) -> None:
    destination = safe_destination(output_root, asset["destination"])
    destination.parent.mkdir(parents=True, exist_ok=True)
    expected_sha1 = asset["sha1"].lower()
    expected_bytes = asset["bytes"]
    if destination.is_file() and not force:
        if destination.stat().st_size == expected_bytes and sha1_file(destination) == expected_sha1:
            print(f"OK cached {asset['id']}: {destination}")
            return
        fail(f"existing file does not match lock: {destination}")

    request = urllib.request.Request(
        asset["download_url"],
        headers={"User-Agent": "Vulkax-RealityLoop/0.1 (+https://github.com/swayam8624/Vulkax)"},
    )
    temporary_path: Path | None = None
    try:
        with urllib.request.urlopen(request, timeout=120) as response:
            with tempfile.NamedTemporaryFile(delete=False, dir=destination.parent) as temporary:
                temporary_path = Path(temporary.name)
                shutil.copyfileobj(response, temporary)
        if temporary_path.stat().st_size != expected_bytes:
            fail(f"{asset['id']} byte mismatch: expected {expected_bytes}, got {temporary_path.stat().st_size}")
        actual_sha1 = sha1_file(temporary_path)
        if actual_sha1 != expected_sha1:
            fail(f"{asset['id']} SHA-1 mismatch: expected {expected_sha1}, got {actual_sha1}")
        temporary_path.replace(destination)
        temporary_path = None
    finally:
        if temporary_path is not None and temporary_path.exists():
            temporary_path.unlink()
    print(f"FETCHED {asset['id']}: {destination}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Fetch pinned post-1.0 Vulkax video inputs")
    parser.add_argument("lock_file", type=Path, nargs="?", default=Path("assets/reality/video_sources.lock.json"))
    parser.add_argument("output_root", type=Path, nargs="?", default=Path("build/reality-assets"))
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args()

    lock = load_lock(args.lock_file)
    if args.validate_only:
        print(f"VALID reality video asset lock: {len(lock['assets'])} asset(s)")
        return
    for asset in lock["assets"]:
        fetch(asset, args.output_root, args.force)
    print(f"VALID reality video asset pack: {len(lock['assets'])} asset(s)")


if __name__ == "__main__":
    main()

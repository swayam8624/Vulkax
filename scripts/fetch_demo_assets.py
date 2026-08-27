#!/usr/bin/env python3
"""Fetch pinned presentation-only assets for the Vulkax 0.80 showcase.

The research/verification pipeline does not depend on these files. Downloads are
controlled by an in-repository lock file and are rejected on path escape, size
mismatch, or SHA-256 mismatch.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import urllib.request


def fail(message: str) -> "None":
    raise SystemExit(f"showcase asset fetch FAILED: {message}")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def load_lock(path: Path) -> dict:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot read lock file {path}: {error}")
    if data.get("schema") != "vulkax_showcase_assets" or data.get("version") != 1:
        fail("unexpected showcase asset lock schema/version")
    assets = data.get("assets")
    if not isinstance(assets, list) or not assets:
        fail("showcase asset lock contains no assets")
    return data


def safe_destination(root: Path, relative_text: str) -> Path:
    relative = Path(relative_text)
    if relative.is_absolute() or ".." in relative.parts:
        fail(f"unsafe asset destination: {relative_text}")
    root_resolved = root.resolve()
    destination = (root / relative).resolve()
    try:
        destination.relative_to(root_resolved)
    except ValueError:
        fail(f"asset destination escapes root: {relative_text}")
    return destination


def validate_asset_entry(asset: dict) -> None:
    required = {
        "id", "kind", "source", "source_page", "download_url", "license",
        "sha256", "bytes", "destination", "role",
    }
    missing = sorted(required - set(asset))
    if missing:
        fail("asset entry missing fields: " + ", ".join(missing))
    if not isinstance(asset["id"], str) or not asset["id"]:
        fail("asset id must be a non-empty string")
    if not isinstance(asset["bytes"], int) or asset["bytes"] <= 0:
        fail(f"asset {asset['id']} has invalid byte count")
    digest = asset["sha256"]
    if not isinstance(digest, str) or len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest.lower()):
        fail(f"asset {asset['id']} has invalid SHA-256")
    url = asset["download_url"]
    if not isinstance(url, str) or not url.startswith("https://"):
        fail(f"asset {asset['id']} must use an https download URL")
    if asset["license"] != "CC0-1.0":
        fail(f"asset {asset['id']} is not explicitly pinned as CC0-1.0")


def fetch_asset(asset: dict, root: Path, force: bool) -> None:
    validate_asset_entry(asset)
    destination = safe_destination(root, asset["destination"])
    destination.parent.mkdir(parents=True, exist_ok=True)

    if destination.is_file() and not force:
        if destination.stat().st_size == asset["bytes"] and sha256_file(destination) == asset["sha256"]:
            print(f"OK cached {asset['id']}: {destination}")
            return
        fail(f"existing asset does not match lock; use --force only if you intend to refetch: {destination}")

    request = urllib.request.Request(
        asset["download_url"],
        headers={"User-Agent": "Vulkax-0.80-showcase/1.0 (+https://github.com/swayam8624/Vulkax)"},
    )
    temporary_path: Path | None = None
    try:
        with urllib.request.urlopen(request, timeout=120) as response:
            with tempfile.NamedTemporaryFile(delete=False, dir=destination.parent) as temporary:
                temporary_path = Path(temporary.name)
                shutil.copyfileobj(response, temporary)
        actual_size = temporary_path.stat().st_size
        actual_sha = sha256_file(temporary_path)
        if actual_size != asset["bytes"]:
            fail(f"{asset['id']} byte count mismatch: expected {asset['bytes']}, got {actual_size}")
        if actual_sha != asset["sha256"]:
            fail(f"{asset['id']} SHA-256 mismatch: expected {asset['sha256']}, got {actual_sha}")
        temporary_path.replace(destination)
        temporary_path = None
    finally:
        if temporary_path is not None and temporary_path.exists():
            temporary_path.unlink()

    print(f"FETCHED {asset['id']}: {destination}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Fetch checksum-pinned Vulkax showcase assets")
    parser.add_argument("lock_file", type=Path, nargs="?", default=Path("assets/demo/showcase_assets.lock.json"))
    parser.add_argument("output_root", type=Path, nargs="?", default=Path("build/demo-assets"))
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    lock = load_lock(args.lock_file)
    for asset in lock["assets"]:
        fetch_asset(asset, args.output_root, args.force)
    print(f"VALID showcase asset pack: {len(lock['assets'])} asset(s)")


if __name__ == "__main__":
    main()

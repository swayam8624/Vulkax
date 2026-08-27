#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


def fail(message: str) -> None:
    raise ValueError(message)


def load_json(path: Path) -> dict:
    if not path.is_file():
        fail(f"missing file: {path}")
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        fail(f"expected JSON object: {path}")
    return value


def validate(repo_root: Path) -> None:
    registry_path = repo_root / "schemas" / "evidence_registry.json"
    registry = load_json(registry_path)
    if registry.get("schema") != "vulkax_evidence_registry":
        fail("unexpected evidence registry schema")
    if registry.get("schema_version") != 1:
        fail("unexpected evidence registry schema version")

    entries = registry.get("entries")
    if not isinstance(entries, list) or not entries:
        fail("evidence registry must contain entries")

    ids: set[str] = set()
    by_id: dict[str, dict] = {}
    for entry in entries:
        if not isinstance(entry, dict):
            fail("registry entries must be objects")
        entry_id = entry.get("id")
        if not isinstance(entry_id, str) or not entry_id:
            fail("registry entry id is missing")
        if entry_id in ids:
            fail(f"duplicate registry id: {entry_id}")
        ids.add(entry_id)
        by_id[entry_id] = entry
        if not isinstance(entry.get("artifact"), str) or not entry["artifact"]:
            fail(f"{entry_id}: artifact is missing")
        if not isinstance(entry.get("schema"), str) or not entry["schema"]:
            fail(f"{entry_id}: schema is missing")
        if not isinstance(entry.get("schema_version"), int) or entry["schema_version"] <= 0:
            fail(f"{entry_id}: schema_version must be a positive integer")
        if not isinstance(entry.get("validator"), str) or not entry["validator"]:
            fail(f"{entry_id}: validator command is missing")

    required = {"capture_bundle", "captured_world_run", "showcase_manifest", "showcase_asset_lock"}
    missing = required - ids
    if missing:
        fail(f"registry is missing required entries: {', '.join(sorted(missing))}")

    world_entry = by_id["captured_world_run"]
    world_validator = (repo_root / "scripts" / "validate_captured_world_run.py").read_text(encoding="utf-8")
    expected_schema = world_entry["schema"]
    expected_version = world_entry["schema_version"]
    if f'certificate.get("schema") != "{expected_schema}"' not in world_validator:
        fail("captured-world validator schema does not match registry")
    if f'certificate.get("schema_version") != {expected_version}' not in world_validator:
        fail("captured-world validator version does not match registry")

    asset_entry = by_id["showcase_asset_lock"]
    asset_lock = load_json(repo_root / asset_entry["artifact"])
    if asset_lock.get("schema") != asset_entry["schema"]:
        fail("showcase asset-lock schema does not match registry")
    version_field = asset_entry.get("version_field", "schema_version")
    if asset_lock.get(version_field) != asset_entry["schema_version"]:
        fail("showcase asset-lock version does not match registry")

    showcase_entry = by_id["showcase_manifest"]
    showcase_source = (repo_root / "src" / "render" / "showcase.cpp").read_text(encoding="utf-8")
    if showcase_entry["schema"] not in showcase_source:
        fail("showcase source does not emit the registered schema name")
    if str(showcase_entry["schema_version"]) not in showcase_source:
        fail("showcase source does not contain the registered schema version")

    capture_entry = by_id["capture_bundle"]
    capture_source = (repo_root / "src" / "capture" / "deformable_bundle.cpp").read_text(encoding="utf-8")
    if capture_entry["schema"] not in capture_source:
        fail("capture bundle source does not contain the registered schema name")
    if str(capture_entry["schema_version"]) not in capture_source:
        fail("capture bundle source does not contain the registered schema version")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Validate the Vulkax evidence-schema registry against implementation files.")
    parser.add_argument("repo_root", nargs="?", default=".")
    args = parser.parse_args()
    try:
        validate(Path(args.repo_root).resolve())
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"INVALID evidence registry: {error}", file=sys.stderr)
        raise SystemExit(1)
    print("VALID evidence registry: capture=v1 captured-world=v2 showcase=v1 asset-lock=v1")

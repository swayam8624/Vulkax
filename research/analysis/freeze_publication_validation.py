#!/usr/bin/env python3
"""Create/check an immutable logical lock for a final publication-validation run."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def git_text(*args: str) -> str:
    try:
        return subprocess.check_output(
            ["git", *args], text=True, stderr=subprocess.DEVNULL
        ).strip()
    except Exception:
        return ""


def snapshot(paths: list[Path]) -> list[dict[str, object]]:
    result = []
    for p in paths:
        if not p.is_file():
            raise FileNotFoundError(p)
        result.append(
            {
                "path": str(p),
                "bytes": p.stat().st_size,
                "sha256": sha256(p),
            }
        )
    return result


def create_lock(paths: list[Path], out: Path, require_clean: bool) -> dict[str, object]:
    dirty = bool(git_text("status", "--porcelain"))
    if require_clean and dirty:
        raise RuntimeError("repository is dirty; refuse final-test freeze")
    lock = {
        "schema": "vulkax.publication_validation_lock",
        "version": 1,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "git_commit": git_text("rev-parse", "HEAD"),
        "git_branch": git_text("branch", "--show-current"),
        "git_dirty": dirty,
        "files": snapshot(paths),
        "rule": "Do not tune primary decisions or truth definitions after final-test data are opened.",
    }
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(lock, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return lock


def check_lock(lock_path: Path) -> None:
    lock = json.loads(lock_path.read_text(encoding="utf-8"))
    if lock.get("schema") != "vulkax.publication_validation_lock":
        raise RuntimeError("unexpected lock schema")
    errors = []
    for item in lock["files"]:
        p = Path(item["path"])
        if not p.is_file():
            errors.append(f"missing: {p}")
            continue
        actual = sha256(p)
        if actual != item["sha256"]:
            errors.append(f"changed: {p} expected={item['sha256']} actual={actual}")
    if errors:
        raise RuntimeError("publication-validation lock mismatch:\n" + "\n".join(errors))
    print(f"VALID final-test lock: {lock_path}")


def self_test() -> None:
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        p = root / "protocol.json"
        p.write_text("{}\n", encoding="utf-8")
        lock = root / "lock.json"
        create_lock([p], lock, require_clean=False)
        check_lock(lock)
        p.write_text('{"changed":true}\n', encoding="utf-8")
        try:
            check_lock(lock)
        except RuntimeError:
            pass
        else:
            raise AssertionError("lock failed to detect changed input")
    print("VALID publication-validation freeze self-test")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--protocol", type=Path, default=Path("research/validation/protocol_v1.json"))
    ap.add_argument("--schema", type=Path, default=Path("research/validation/record_schema_v1.json"))
    ap.add_argument("--extra", type=Path, action="append", default=[])
    ap.add_argument("--out", type=Path, default=Path("build/publication-validation/final_test_lock.json"))
    ap.add_argument("--check", type=Path)
    ap.add_argument("--require-clean", action="store_true")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()

    if args.self_test:
        self_test()
        return 0
    if args.check:
        check_lock(args.check)
        return 0
    paths = [args.protocol, args.schema, *args.extra]
    lock = create_lock(paths, args.out, require_clean=args.require_clean)
    print(f"VALID final-test freeze -> {args.out}")
    print("FILES", len(lock["files"]))
    print("COMMIT", lock["git_commit"])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

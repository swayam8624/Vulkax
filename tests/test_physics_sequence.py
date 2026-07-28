#!/usr/bin/env python3
"""Validate that a Physics Studio sequence is self-consistent and reproducible."""

import json
import hashlib
import struct
import sys
from pathlib import Path


def png_size(path: Path) -> tuple[int, int]:
    with path.open("rb") as stream:
        signature = stream.read(8)
        if signature != b"\x89PNG\r\n\x1a\n":
            raise AssertionError(f"not a PNG: {path}")
        length = struct.unpack(">I", stream.read(4))[0]
        kind = stream.read(4)
        if kind != b"IHDR" or length != 13:
            raise AssertionError(f"missing PNG IHDR: {path}")
        return struct.unpack(">II", stream.read(8))


def main() -> None:
    directory = Path(sys.argv[1])
    requires_variation = "--requires-variation" in sys.argv[2:]
    manifest = json.loads((directory / "sequence_manifest.json").read_text())
    assert manifest["format"] == "vulkax.physics-sequence"
    assert manifest["frames"] == len(manifest["files"])
    expected = (manifest["width"], manifest["height"])
    assert expected[0] > 0 and expected[1] > 0
    hashes = []
    for name in manifest["files"]:
        path = directory / name
        assert png_size(path) == expected
        hashes.append(hashlib.sha256(path.read_bytes()).hexdigest())
    if requires_variation:
        assert len(set(hashes)) > 1, "dynamic sequence frames must not all be identical"


if __name__ == "__main__":
    main()

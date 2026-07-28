#!/usr/bin/env python3
"""Check that the native Physics Studio export is a structurally plausible OpenEXR file."""

import sys
from pathlib import Path


def main() -> None:
    path = Path(sys.argv[1])
    data = path.read_bytes()
    assert len(data) > 512, "EXR output is unexpectedly small"
    assert data[:4] == b"\x76\x2f\x31\x01", "OpenEXR magic number missing"
    assert b"channels\x00" in data[:4096], "OpenEXR channels header missing"
    assert b"compression\x00" in data[:4096], "OpenEXR compression header missing"


if __name__ == "__main__":
    main()

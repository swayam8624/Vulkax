#!/usr/bin/env python3
"""Validate the raw fixed/adaptive quality benchmark artifact."""

import csv
import json
import sys
from pathlib import Path


def main() -> None:
    directory = Path(sys.argv[1])
    summary = json.loads((directory / "quality_summary.json").read_text())
    assert summary["measurement_class"] == "cpu_analytical_preview_quality_benchmark"
    rows = list(csv.DictReader((directory / "quality_frames.csv").open()))
    assert len(rows) == summary["frames"] * 2
    assert {row["policy"] for row in rows} == {"fixed", "adaptive"}
    assert all(float(row["frame_ms"]) >= 0.0 for row in rows)
    assert all(float(row["visual_mse"]) >= 0.0 for row in rows)


if __name__ == "__main__":
    main()

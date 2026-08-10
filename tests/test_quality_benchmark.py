#!/usr/bin/env python3
"""Validate the raw fixed/screen/numerical/equation-aware benchmark artifact."""

import csv
import json
import math
import sys
from pathlib import Path


POLICIES = {"fixed", "screen_space_only", "numerical_only", "equation_aware"}


def main() -> None:
    directory = Path(sys.argv[1])
    summary = json.loads((directory / "quality_summary.json").read_text())
    assert summary["measurement_class"] == "cpu_analytical_preview_policy_comparison"
    assert "screen_space_rms" in summary["visual_metric_note"]
    assert summary["frames"] > 0
    assert summary["target_frame_ms"] > 0.0
    for policy in POLICIES:
        entry = summary[policy]
        assert entry["p50_frame_ms"] >= 0.0
        assert entry["p95_frame_ms"] >= entry["p50_frame_ms"]
        assert entry["mean_numerical_mse"] >= 0.0
        assert entry["mean_screen_space_rms"] >= 0.0
        assert entry["changes"] >= 0

    rows = list(csv.DictReader((directory / "quality_frames.csv").open()))
    assert len(rows) == summary["frames"] * len(POLICIES)
    assert {row["policy"] for row in rows} == POLICIES
    assert all(float(row["frame_ms"]) >= 0.0 for row in rows)
    assert all(float(row["numerical_mse"]) >= 0.0 for row in rows)
    assert all(float(row["screen_space_rms"]) >= 0.0 for row in rows)
    assert all(0.0 < float(row["resolution_scale"]) <= 1.0 for row in rows)

    # The benchmark intentionally leaves unavailable evidence as NaN rather
    # than fabricating zero error. Verify each controller policy only exposes
    # the measurements it actually consumes.
    by_policy = {policy: [row for row in rows if row["policy"] == policy] for policy in POLICIES}
    assert all(math.isnan(float(row["numerical_ewma"])) for row in by_policy["fixed"])
    assert all(math.isnan(float(row["visual_ewma"])) for row in by_policy["fixed"])
    assert all(math.isnan(float(row["numerical_ewma"])) for row in by_policy["screen_space_only"])
    assert all(math.isfinite(float(row["visual_ewma"])) for row in by_policy["screen_space_only"])
    assert all(math.isfinite(float(row["numerical_ewma"])) for row in by_policy["numerical_only"])
    assert all(math.isnan(float(row["visual_ewma"])) for row in by_policy["numerical_only"])
    assert all(math.isfinite(float(row["numerical_ewma"])) for row in by_policy["equation_aware"])
    assert all(math.isfinite(float(row["visual_ewma"])) for row in by_policy["equation_aware"])


if __name__ == "__main__":
    main()

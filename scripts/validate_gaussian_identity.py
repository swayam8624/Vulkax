#!/usr/bin/env python3

import csv
import math
import sys
from pathlib import Path

REQUIRED = {
    "splats",
    "selection_count",
    "query_count",
    "stable_id_payload_bytes",
    "index_build_ms",
    "hierarchy_build_ms",
    "hierarchy_query_ms",
    "selection_resolve_ms",
    "correspondence_validate_ms",
    "reorder_identity_ok",
    "selection_reorder_ok",
    "correspondence_reorder_ok",
    "hierarchy_reorder_ok",
}
TIMINGS = {
    "index_build_ms",
    "hierarchy_build_ms",
    "hierarchy_query_ms",
    "selection_resolve_ms",
    "correspondence_validate_ms",
}
FLAGS = {
    "reorder_identity_ok",
    "selection_reorder_ok",
    "correspondence_reorder_ok",
    "hierarchy_reorder_ok",
}


def fail(message: str) -> None:
    raise SystemExit(f"Gaussian identity benchmark validation failed: {message}")


def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: validate_gaussian_identity.py <benchmark.csv>")
    path = Path(sys.argv[1])
    if not path.is_file():
        fail(f"missing CSV: {path}")

    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        fields = set(reader.fieldnames or [])
        missing = REQUIRED - fields
        if missing:
            fail("missing columns: " + ", ".join(sorted(missing)))
        rows = list(reader)

    if len(rows) < 2:
        fail("benchmark must contain at least two increasing sizes")

    previous_splats = 0
    previous_payload = 0
    for number, row in enumerate(rows, start=2):
        try:
            splats = int(row["splats"])
            selection_count = int(row["selection_count"])
            query_count = int(row["query_count"])
            payload = int(row["stable_id_payload_bytes"])
        except ValueError as error:
            fail(f"row {number} contains a non-integer count: {error}")
        if splats <= previous_splats:
            fail(f"row {number} splat count is not strictly increasing")
        if not (0 < selection_count <= splats):
            fail(f"row {number} selection count is outside (0, splats]")
        if not (0 < query_count <= splats):
            fail(f"row {number} hierarchy query count is outside (0, splats]")
        if payload != splats * 8:
            fail(f"row {number} stable-ID payload is not exactly 8 bytes per splat")
        if payload <= previous_payload:
            fail(f"row {number} stable-ID payload did not grow with splat count")

        for name in TIMINGS:
            try:
                value = float(row[name])
            except ValueError as error:
                fail(f"row {number} {name} is not numeric: {error}")
            if not math.isfinite(value) or value < 0.0:
                fail(f"row {number} {name} must be finite and non-negative")
        for name in FLAGS:
            if row[name] != "1":
                fail(f"row {number} {name} did not pass")

        previous_splats = splats
        previous_payload = payload

    print(f"VALID Gaussian identity scaling evidence: rows={len(rows)} max_splats={previous_splats}")


if __name__ == "__main__":
    main()

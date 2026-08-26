#!/usr/bin/env python3
"""Validate machine-readable Gaussian scaling evidence.

The image policy intentionally combines a small local byte-error ceiling with much
stricter global-error limits. Native Vulkan projection can differ from the CPU
reference by a few quantized channel values at a tiny number of ellipse-edge
pixels while remaining globally indistinguishable. Metal is currently exact on
the controlled benchmark. Timing is evidence only; no speedup is required.
"""

from __future__ import annotations

import csv
import math
import pathlib
import sys

MAX_CHANNEL_DIFFERENCE = 3
MAX_RMSE = 1.0e-4
MAX_CHANGED_PIXEL_FRACTION = 1.0e-4
EXPECTED_ROWS = 3

REQUIRED_COLUMNS = {
    "input_splats",
    "visible_splats",
    "tile_references",
    "max_splats_per_tile",
    "projection_input_bytes",
    "projection_output_bytes",
    "tile_reference_bytes",
    "cpu_projection_ms",
    "native_projection_ms",
    "scalable_total_ms",
    "max_channel_difference",
    "rmse",
    "psnr_db",
    "changed_pixel_fraction",
    "used_native_projection",
    "fallback_reason",
}


def fail(message: str) -> None:
    raise SystemExit(f"Gaussian scaling evidence validation failed: {message}")


def finite_nonnegative(row: dict[str, str], field: str, index: int) -> float:
    try:
        value = float(row[field])
    except ValueError as error:
        fail(f"row {index}: {field} is not numeric: {row[field]!r} ({error})")
    if not math.isfinite(value) or value < 0.0:
        fail(f"row {index}: {field} must be finite and nonnegative, got {value}")
    return value


def positive_int(row: dict[str, str], field: str, index: int) -> int:
    try:
        value = int(row[field])
    except ValueError as error:
        fail(f"row {index}: {field} is not an integer: {row[field]!r} ({error})")
    if value <= 0:
        fail(f"row {index}: {field} must be positive, got {value}")
    return value


def main() -> None:
    if len(sys.argv) != 2:
        fail("usage: validate_gaussian_scaling.py <evidence.csv>")

    path = pathlib.Path(sys.argv[1])
    if not path.is_file() or path.stat().st_size == 0:
        fail(f"missing or empty CSV: {path}")

    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames is None:
            fail("CSV has no header")
        missing = REQUIRED_COLUMNS.difference(reader.fieldnames)
        if missing:
            fail(f"CSV is missing columns: {', '.join(sorted(missing))}")
        rows = list(reader)

    if len(rows) != EXPECTED_ROWS:
        fail(f"expected {EXPECTED_ROWS} evidence rows, found {len(rows)}")

    previous_input_bytes: int | None = None
    for index, row in enumerate(rows, start=1):
        input_splats = positive_int(row, "input_splats", index)
        visible_splats = positive_int(row, "visible_splats", index)
        tile_references = positive_int(row, "tile_references", index)
        max_splats_per_tile = positive_int(row, "max_splats_per_tile", index)
        input_bytes = positive_int(row, "projection_input_bytes", index)
        output_bytes = positive_int(row, "projection_output_bytes", index)
        tile_bytes = positive_int(row, "tile_reference_bytes", index)

        if visible_splats > input_splats:
            fail(f"row {index}: visible_splats exceeds input_splats")
        if max_splats_per_tile > visible_splats:
            fail(f"row {index}: max_splats_per_tile exceeds visible_splats")
        if tile_references < visible_splats:
            fail(f"row {index}: tile_references is unexpectedly smaller than visible_splats")
        if previous_input_bytes is not None and input_bytes <= previous_input_bytes:
            fail(
                f"row {index}: projection_input_bytes did not increase "
                f"({input_bytes} <= {previous_input_bytes})"
            )
        previous_input_bytes = input_bytes

        finite_nonnegative(row, "cpu_projection_ms", index)
        finite_nonnegative(row, "native_projection_ms", index)
        finite_nonnegative(row, "scalable_total_ms", index)

        try:
            max_channel_difference = int(row["max_channel_difference"])
        except ValueError as error:
            fail(
                f"row {index}: max_channel_difference is not an integer: "
                f"{row['max_channel_difference']!r} ({error})"
            )
        rmse = finite_nonnegative(row, "rmse", index)
        changed_fraction = finite_nonnegative(row, "changed_pixel_fraction", index)

        if max_channel_difference > MAX_CHANNEL_DIFFERENCE:
            fail(
                f"row {index}: max_channel_difference {max_channel_difference} exceeds "
                f"{MAX_CHANNEL_DIFFERENCE}"
            )
        if rmse >= MAX_RMSE:
            fail(f"row {index}: rmse {rmse} must be < {MAX_RMSE}")
        if changed_fraction >= MAX_CHANGED_PIXEL_FRACTION:
            fail(
                f"row {index}: changed_pixel_fraction {changed_fraction} must be < "
                f"{MAX_CHANGED_PIXEL_FRACTION}"
            )

        try:
            psnr = float(row["psnr_db"])
        except ValueError as error:
            fail(f"row {index}: psnr_db is not numeric: {row['psnr_db']!r} ({error})")
        if math.isnan(psnr) or psnr == -math.inf:
            fail(f"row {index}: psnr_db is invalid: {psnr}")
        if psnr == math.inf and rmse != 0.0:
            fail(f"row {index}: infinite PSNR is only valid when RMSE is exactly zero")
        if math.isfinite(psnr) and rmse == 0.0:
            fail(f"row {index}: zero RMSE must report infinite PSNR")

        if row["used_native_projection"] != "1":
            fail(
                f"row {index}: benchmark used fallback projection: "
                f"{row['fallback_reason'] or '<no reason>'}"
            )

        # Make sure the memory fields parsed above are materially populated.
        if output_bytes <= 0 or tile_bytes <= 0:
            fail(f"row {index}: projection/tile memory evidence is empty")

    print(
        "Gaussian scaling evidence PASS: "
        f"rows={len(rows)}, max_channel<={MAX_CHANNEL_DIFFERENCE}, "
        f"rmse<{MAX_RMSE:g}, changed_fraction<{MAX_CHANGED_PIXEL_FRACTION:g}"
    )


if __name__ == "__main__":
    main()

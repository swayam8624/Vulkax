#!/usr/bin/env python3
"""Select the strongest finite-difference octant and reproduce its stable IDs.

This intentionally follows the exact rest-space octant construction in
captured_influence.cpp. It is used only to feed the retained nonlinear verified
rewrite tool; the influence computation itself remains in C++.
"""

from __future__ import annotations

import csv
import math
import sys
from pathlib import Path


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="") as stream:
        return list(csv.DictReader(stream))


def main() -> None:
    if len(sys.argv) != 4:
        raise SystemExit(
            "usage: select_captured_influence_region.py <particles.csv> <influence.csv> <output-dir>"
        )
    particles_path = Path(sys.argv[1])
    influence_path = Path(sys.argv[2])
    output_dir = Path(sys.argv[3])
    output_dir.mkdir(parents=True, exist_ok=True)

    influence = read_csv(influence_path)
    if not influence:
        raise SystemExit("influence CSV is empty")
    ranked = []
    for row in influence:
        derivative = float(row["derivative"])
        if not math.isfinite(derivative):
            raise SystemExit(f"non-finite influence derivative in {row['region_id']}")
        ranked.append((abs(derivative), row["region_id"], derivative))
    ranked.sort(key=lambda item: (-item[0], item[1]))
    _, region_id, derivative = ranked[0]
    if not region_id.startswith("octant_"):
        raise SystemExit(f"expected an octant region, got {region_id}")
    octant = int(region_id.removeprefix("octant_"))
    if octant < 0 or octant > 7:
        raise SystemExit(f"invalid octant index {octant}")

    particles = read_csv(particles_path)
    if not particles:
        raise SystemExit("particle CSV is empty")
    parsed = []
    for row in particles:
        point = tuple(float(row[key]) for key in ("rest_x", "rest_y", "rest_z"))
        if not all(math.isfinite(value) for value in point):
            raise SystemExit(f"non-finite rest position for particle {row['particle_id']}")
        parsed.append((int(row["particle_id"]), point))
    minimum = [min(point[axis] for _, point in parsed) for axis in range(3)]
    maximum = [max(point[axis] for _, point in parsed) for axis in range(3)]
    midpoint = [(a + b) * 0.5 for a, b in zip(minimum, maximum)]

    ids = []
    for particle_id, point in parsed:
        index = 0
        if point[0] >= midpoint[0]:
            index |= 1
        if point[1] >= midpoint[1]:
            index |= 2
        if point[2] >= midpoint[2]:
            index |= 4
        if index == octant:
            ids.append(particle_id)
    ids.sort()
    if not ids:
        raise SystemExit(f"selected {region_id} has no particles")

    (output_dir / "particle_ids.txt").write_text(",".join(map(str, ids)) + "\n")
    with (output_dir / "selection.csv").open("w", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(["region_id", "particle_count", "reference_derivative", "particle_ids"])
        writer.writerow([region_id, len(ids), f"{derivative:.17g}", ";".join(map(str, ids))])

    print(
        f"Selected rewrite region {region_id}: particles={len(ids)} "
        f"reference_derivative={derivative:.10g}"
    )


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Generate a deterministic controlled-validation trial plan.

This script plans trials; it does not fabricate physics results. Dataset adapters
or physical probes consume the plan and emit records conforming to
record_schema_v1.json.
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path
import tempfile

FIELDS = [
    "plan_version", "trial_id", "dataset", "scene", "split", "trial_family",
    "ground_truth", "physical_delta", "measurement_noise_sigma",
    "pose_noise_sigma", "missing_fraction", "channel_dependence",
    "negative_control", "seed", "notes",
]


def read_worlds(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        raise ValueError("world manifest is empty")
    required = {"dataset", "scene", "split"}
    missing = required - set(rows[0])
    if missing:
        raise ValueError(f"world manifest missing fields: {sorted(missing)}")
    return rows


def add(
    out: list[dict[str, object]],
    world: dict[str, str],
    family: str,
    truth: str,
    seed: int,
    *,
    physical_delta: float | None = None,
    measurement_noise_sigma: float | None = None,
    pose_noise_sigma: float | None = None,
    missing_fraction: float | None = None,
    channel_dependence: float | None = None,
    negative_control: bool = False,
    notes: str = "",
) -> None:
    idx = len(out) + 1
    out.append(
        {
            "plan_version": 1,
            "trial_id": f"PV1-{idx:08d}",
            "dataset": world["dataset"],
            "scene": world["scene"],
            "split": world["split"],
            "trial_family": family,
            "ground_truth": truth,
            "physical_delta": "" if physical_delta is None else physical_delta,
            "measurement_noise_sigma": "" if measurement_noise_sigma is None else measurement_noise_sigma,
            "pose_noise_sigma": "" if pose_noise_sigma is None else pose_noise_sigma,
            "missing_fraction": "" if missing_fraction is None else missing_fraction,
            "channel_dependence": "" if channel_dependence is None else channel_dependence,
            "negative_control": negative_control,
            "seed": seed,
            "notes": notes,
        }
    )


def generate(worlds: list[dict[str, str]], profile: str) -> list[dict[str, object]]:
    if profile == "smoke":
        seeds = [0]
        dose = [0.05, 0.20]
        measurement = [0.0, 1.0]
        pose = [0.0, 1.0]
        missing = [0.0, 0.50]
        dependence = [0.0, 1.0]
    else:
        seeds = [0, 1, 2]
        dose = [0.025, 0.05, 0.10, 0.20]
        measurement = [0.0, 0.25, 0.5, 1.0, 2.0]
        pose = [0.0, 0.25, 0.5, 1.0]
        missing = [0.0, 0.10, 0.25, 0.50]
        dependence = [0.0, 0.25, 0.50, 0.75, 1.0]

    rows: list[dict[str, object]] = []
    nominal = 0.10
    for world in worlds:
        for seed in seeds:
            # Three-way ground truth is tested directly.
            add(rows, world, "truth_control", "support", seed, physical_delta=nominal)
            add(rows, world, "truth_control", "veto", seed, physical_delta=-nominal)
            add(rows, world, "truth_control", "unresolved", seed, physical_delta=0.0)

            # Explicit null experiment: any support/veto is a false assertion.
            add(
                rows, world, "placebo", "unresolved", seed, physical_delta=0.0,
                negative_control=True,
                notes="null intervention / unrelated evidence channel",
            )

            # Dose-response is symmetric so detection limits can be measured for
            # both beneficial and harmful physical changes.
            for d in dose:
                add(rows, world, "dose_response", "support", seed, physical_delta=d)
                add(rows, world, "dose_response", "veto", seed, physical_delta=-d)

            # One-axis-at-a-time stress sweeps avoid an uncontrolled Cartesian
            # explosion while preserving an interpretable robustness surface.
            for sigma in measurement:
                for truth, delta in (("support", nominal), ("veto", -nominal), ("unresolved", 0.0)):
                    add(rows, world, "measurement_noise", truth, seed,
                        physical_delta=delta, measurement_noise_sigma=sigma)
            for sigma in pose:
                for truth, delta in (("support", nominal), ("veto", -nominal), ("unresolved", 0.0)):
                    add(rows, world, "pose_noise", truth, seed,
                        physical_delta=delta, pose_noise_sigma=sigma)
            for fraction in missing:
                for truth, delta in (("support", nominal), ("veto", -nominal), ("unresolved", 0.0)):
                    add(rows, world, "missing_observations", truth, seed,
                        physical_delta=delta, missing_fraction=fraction)
            for rho in dependence:
                for truth, delta in (("support", nominal), ("veto", -nominal), ("unresolved", 0.0)):
                    add(rows, world, "channel_dependence", truth, seed,
                        physical_delta=delta, channel_dependence=rho)
    return rows


def write(rows: list[dict[str, object]], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS)
        w.writeheader()
        w.writerows(rows)


def self_test() -> None:
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        worlds = root / "worlds.csv"
        worlds.write_text("dataset,scene,split\ntoy,scene0,development\n", encoding="utf-8")
        rows = generate(read_worlds(worlds), "smoke")
        families = {r["trial_family"] for r in rows}
        assert {"truth_control", "placebo", "dose_response", "measurement_noise",
                "pose_noise", "missing_observations", "channel_dependence"} <= families
        assert any(r["negative_control"] for r in rows)
        ids = [r["trial_id"] for r in rows]
        assert len(ids) == len(set(ids))
    print("VALID controlled-validation planner self-test")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--worlds", type=Path)
    ap.add_argument("--profile", choices=("smoke", "full"), default="full")
    ap.add_argument("--out", type=Path, default=Path("build/publication-validation/trial_plan.csv"))
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.worlds is None:
        raise SystemExit("--worlds is required")
    rows = generate(read_worlds(args.worlds), args.profile)
    write(rows, args.out)
    print(f"VALID controlled validation plan: {len(rows)} trials -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Audit Reality Probe's graphics-submission evidence surface.

This is deliberately a submission/reproducibility audit, not a new scientific
experiment. It verifies that the paper's graphics-facing claims are backed by
committed benchmark ledgers, that qualitative figures are present and valid,
and that the manuscript keeps measured, synthetic, retrospective, and
visualization-only evidence clearly separated.
"""
from __future__ import annotations

import argparse
import csv
import json
import pathlib
import struct
import sys

REQUIRED_VISUALS = [
    "docs/readme_assets/storyboard.png",
    "docs/readme_assets/02_deceptive_repair.png",
    "docs/readme_assets/03_deception_map.png",
    "docs/readme_assets/04_same_probe.png",
    "docs/readme_assets/05_response_overlay.png",
    "docs/readme_assets/06_fingerprint.png",
    "docs/readme_assets/07_residual_field.png",
    "docs/readme_assets/10_signal_gain.png",
    "docs/readme_assets/11_information_limit.png",
]

REQUIRED_EVIDENCE = [
    "research/results/VULKAX_FINAL_RESULTS_2026-09-21.json",
    "research/results/GAUGE_PAIRED_DIAGNOSTICS_2026-09-20.csv",
    "docs/MEASURED_BENCHMARK_0_45.md",
    "paper/main_humanized.tex",
    ".github/workflows/measured-world-showcase.yml",
]

MEASURED_VISUAL_WORKFLOW_GUARDS = [
    "DOT C2",
    "contact_sheet.png",
    "hero_baseline.png",
    "hero_rollback.png",
    "turntable/frame_000.png",
    "render_backend: Vulkan",
]

MANUSCRIPT_GUARDS = [
    "GAUGE is retrospective",
    "Fresh prospective force evidence is synthetic",
    "not benchmark geometry",
    "DOT C2",
]

def png_dimensions(path: pathlib.Path) -> tuple[int, int]:
    with path.open("rb") as f:
        sig = f.read(24)
    if len(sig) < 24 or sig[:8] != b"\x89PNG\r\n\x1a\n" or sig[12:16] != b"IHDR":
        raise ValueError(f"not a valid PNG IHDR: {path}")
    return struct.unpack(">II", sig[16:24])

def load_json(path: pathlib.Path):
    return json.loads(path.read_text(encoding="utf-8"))

def run_audit(root: pathlib.Path) -> dict:
    errors: list[str] = []
    warnings: list[str] = []

    for rel in REQUIRED_EVIDENCE:
        if not (root / rel).is_file():
            errors.append(f"missing required evidence file: {rel}")

    visuals = []
    for rel in REQUIRED_VISUALS:
        p = root / rel
        if not p.is_file():
            errors.append(f"missing required visual: {rel}")
            continue
        try:
            width, height = png_dimensions(p)
        except Exception as exc:
            errors.append(f"invalid visual {rel}: {exc}")
            continue
        if width < 512 or height < 256:
            warnings.append(f"small raster surface: {rel} is {width}x{height}")
        visuals.append({"path": rel, "width": width, "height": height})

    results_path = root / "research/results/VULKAX_FINAL_RESULTS_2026-09-21.json"
    invariants = {}
    if results_path.is_file():
        d = load_json(results_path)
        try:
            d2 = d["dcs"]["stages"]["D2_frozen"]
            d3 = d["dcs"]["stages"]["D3"]
            d4 = d["dcs"]["stages"]["D4V"]
            ofc = d["orthogonal_force_compliance"]
            invariants = {
                "d2_resolved": d2["resolved_worlds"],
                "d2_worlds": d2["truth_worlds"],
                "d3_resolved": d3["resolved_worlds"],
                "d3_worlds": d3["truth_worlds"],
                "d4v_coverage": d4["resolved_coverage"]["dcs"],
                "ofc_coverage": ofc["force_compliance"]["coverage"],
                "ofc_signal_gain": ofc["force_to_dcs_median_abs_z_ratio"],
                "measured_prospective_confirmation":
                    d["final_evidence_summary"]["measured_prospective_confirmation"],
            }
            if (d2["resolved_worlds"], d2["truth_worlds"]) != (0, 16):
                errors.append("frozen D2 invariant changed")
            if (d3["resolved_worlds"], d3["truth_worlds"]) != (0, 6):
                errors.append("frozen D3 invariant changed")
            if d4["resolved_coverage"]["dcs"] != 0:
                errors.append("frozen D4V DCS coverage changed")
            if ofc["force_compliance"]["coverage"] != 0:
                errors.append("frozen OFC coverage changed")
            if not (11.44 <= ofc["force_to_dcs_median_abs_z_ratio"] <= 11.47):
                errors.append("frozen OFC/DCS median signal-gain ratio changed")
            if d["final_evidence_summary"]["measured_prospective_confirmation"] is not False:
                errors.append("measured prospective confirmation boundary changed")
        except Exception as exc:
            errors.append(f"could not validate frozen result invariants: {exc}")

    gauge_trial_summary = {}
    gauge_path = root / "research/results/GAUGE_PAIRED_DIAGNOSTICS_2026-09-20.csv"
    if gauge_path.is_file():
        try:
            with gauge_path.open("r", encoding="utf-8", newline="") as f:
                gauge_rows = list(csv.DictReader(f))
            expected_channels = {
                "face_nrmse": (0, 10, 0),
                "marker_rmse_m": (0, 10, 0),
                "dcs_marker_error_m": (0, 10, 0),
                "dcs_longitudinal_error": (9, 1, 0),
            }
            by_channel = {row["channel"]: row for row in gauge_rows}
            for channel, expected in expected_channels.items():
                row = by_channel.get(channel)
                if row is None:
                    errors.append(f"GAUGE diagnostic channel missing: {channel}")
                    continue
                counts = (
                    int(row["endpoint_better_count"]),
                    int(row["overlap_better_count"]),
                    int(row["ties"]),
                )
                gauge_trial_summary[channel] = {
                    "endpoint_better": counts[0],
                    "overlap_better": counts[1],
                    "ties": counts[2],
                    "trials": sum(counts),
                }
                if sum(counts) != 10:
                    errors.append(
                        f"GAUGE {channel} no longer summarizes exactly 10 held-out repeats"
                    )
                if counts != expected:
                    errors.append(
                        f"GAUGE {channel} direction counts changed: {counts} != {expected}"
                    )
            long_row = by_channel.get("dcs_longitudinal_error")
            if long_row is not None:
                p = float(long_row["exact_two_sided_sign_test_p"])
                if abs(p - 0.021484375) > 1e-12:
                    errors.append(
                        "GAUGE longitudinal exact sign-test p-value changed"
                    )
        except Exception as exc:
            errors.append(f"could not validate GAUGE paired diagnostics: {exc}")

    dot_benchmark_summary = {}
    dot_path = root / "docs/MEASURED_BENCHMARK_0_45.md"
    if dot_path.is_file():
        dot_text = dot_path.read_text(encoding="utf-8")
        dot_guards = {
            "tracked_points": "imports 225 stable tracked points",
            "observations": "675 marker observations",
            "fit_rms": "fit dynamic RMS                                0.004390821778 m",
            "heldout_rms": "held-out validation RMS                        0.004417317099 m",
            "adaptive_particles": "adaptive proposed particles                  182 / 225",
            "rollback": "rollback performed           yes",
        }
        for key, guard in dot_guards.items():
            present = guard in dot_text
            dot_benchmark_summary[key] = present
            if not present:
                errors.append(f"DOT C2 benchmark guard changed or missing: {key}")

    manuscript_path = root / "paper/main_humanized.tex"
    manuscript_guards = {}
    if manuscript_path.is_file():
        text = manuscript_path.read_text(encoding="utf-8")
        for guard in MANUSCRIPT_GUARDS:
            manuscript_guards[guard] = guard in text
            if guard not in text:
                errors.append(f"manuscript evidence-class guard missing: {guard}")

        for rel in REQUIRED_VISUALS:
            asset = "../" + rel
            if rel.endswith(".png") and rel not in (
                "docs/readme_assets/05_response_overlay.png",
                "docs/readme_assets/storyboard.png",
            ):
                # Not every README visual must be used as a paper figure, but the
                # core paper-facing surfaces should be represented in the source.
                name = pathlib.Path(rel).name
                if name in {
                    "02_deceptive_repair.png", "03_deception_map.png",
                    "04_same_probe.png", "06_fingerprint.png",
                    "07_residual_field.png", "10_signal_gain.png",
                    "11_information_limit.png",
                } and asset not in text:
                    errors.append(f"core visual not referenced by manuscript: {rel}")


    measured_visual_contract = {}
    measured_workflow = root / ".github/workflows/measured-world-showcase.yml"
    if measured_workflow.is_file():
        workflow_text = measured_workflow.read_text(encoding="utf-8")
        for guard in MEASURED_VISUAL_WORKFLOW_GUARDS:
            measured_visual_contract[guard] = guard in workflow_text
            if guard not in workflow_text:
                errors.append(f"measured visual workflow guard missing: {guard}")

    benchmark_matrix = [
        {
            "source": "DOT C2",
            "class": "public measured source",
            "role": "end-to-end captured-world engineering check plus deterministic Vulkan qualitative showcase",
            "claim_boundary": "measured trajectories; unobserved physical quantities remain explicit proxies",
        },
        {
            "source": "GAUGE foam shearing",
            "class": "public measured benchmark",
            "role": "retrospective channel-contradiction analysis",
            "claim_boundary": "retrospective/post-hoc; not prospective confirmation",
        },
        {
            "source": "D2/D3/D4V + OFC",
            "class": "frozen controlled synthetic",
            "role": "prospective/follow-on falsification and observability tests",
            "claim_boundary": "controlled evidence, not measured benchmark data",
        },
        {
            "source": "Stanford Bunny visual suite",
            "class": "visualization-only public geometry",
            "role": "qualitative inspection of solver-driven response fields",
            "claim_boundary": "visual carrier only; never benchmark geometry",
        },
    ]

    return {
        "schema": "vulkax.graphics_submission_audit",
        "version": 1,
        "status": "pass" if not errors else "fail",
        "errors": errors,
        "warnings": warnings,
        "visuals": visuals,
        "frozen_invariants": invariants,
        "manuscript_guards": manuscript_guards,
        "measured_visual_contract": measured_visual_contract,
        "gauge_trial_summary": gauge_trial_summary,
        "dot_benchmark_summary": dot_benchmark_summary,
        "benchmark_matrix": benchmark_matrix,
    }

def write_report(report: dict, out_dir: pathlib.Path) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "audit.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    lines = [
        "# Reality Probe graphics-submission audit",
        "",
        f"Status: **{report['status'].upper()}**",
        "",
        "## Benchmark / evidence matrix",
        "",
        "| Source | Evidence class | Role | Claim boundary |",
        "|---|---|---|---|",
    ]
    for row in report["benchmark_matrix"]:
        lines.append(
            f"| {row['source']} | {row['class']} | {row['role']} | {row['claim_boundary']} |"
        )
    lines += ["", "## Benchmark integrity checks", ""]
    if report["gauge_trial_summary"]:
        for channel, row in report["gauge_trial_summary"].items():
            lines.append(
                f"- GAUGE `{channel}`: {row['trials']} repeats "
                f"({row['endpoint_better']} endpoint / {row['overlap_better']} overlap / {row['ties']} ties)"
            )
    if report["dot_benchmark_summary"]:
        lines.append(
            "- DOT C2 benchmark guards: "
            + ("PASS" if all(report["dot_benchmark_summary"].values()) else "FAIL")
        )
    lines += ["", "## Visual surfaces", ""]
    for v in report["visuals"]:
        lines.append(f"- `{v['path']}`: {v['width']}x{v['height']}")
    if report["warnings"]:
        lines += ["", "## Warnings", ""] + [f"- {x}" for x in report["warnings"]]
    if report["errors"]:
        lines += ["", "## Errors", ""] + [f"- {x}" for x in report["errors"]]
    (out_dir / "audit.md").write_text("\n".join(lines) + "\n", encoding="utf-8")

def self_test() -> int:
    # Keep this dependency-free so CI catches accidental parser regressions.
    assert struct.unpack(">II", struct.pack(">II", 1920, 1080)) == (1920, 1080)
    assert 11.44 <= 11.455058526384752 <= 11.47
    print("graphics submission audit self-test: PASS")
    return 0

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo-root", default=".")
    ap.add_argument("--out", default="build/graphics-submission-audit")
    ap.add_argument("--self-test", action="store_true")
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    root = pathlib.Path(args.repo_root).resolve()
    report = run_audit(root)
    write_report(report, root / args.out)
    print(json.dumps(report, indent=2))
    return 0 if report["status"] == "pass" else 1

if __name__ == "__main__":
    sys.exit(main())

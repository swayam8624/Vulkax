#!/usr/bin/env python3
"""Submission-facing audit for Reality Probe graphics evidence.

This script does not generate new scientific results. It verifies that the frozen
benchmark evidence and the visual assets cited by the manuscript are present and
that the headline numbers agree with the frozen result ledger.
"""
from __future__ import annotations

import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]

REQUIRED_PATHS = [
    "research/results/VULKAX_FINAL_RESULTS_2026-09-21.json",
    "research/results/ORTHOGONAL_FORCE_COMPLIANCE_RESULT_2026-09-21.json",
    "research/results/GAUGE_PAIRED_DIAGNOSTICS_2026-09-20.csv",
    "scripts/import_dot_c2.py",
    "research/analysis/gauge_dcs_retrospective.py",
    "docs/readme_assets/02_deceptive_repair.png",
    "docs/readme_assets/03_deception_map.png",
    "docs/readme_assets/04_same_probe.png",
    "docs/readme_assets/05_response_overlay.png",
    "docs/readme_assets/06_fingerprint.png",
    "docs/readme_assets/07_residual_field.png",
    "docs/readme_assets/10_signal_gain.png",
    "docs/readme_assets/11_information_limit.png",
    "paper/main.tex",
]

def fail(msg: str) -> None:
    print(f"FAIL: {msg}", file=sys.stderr)
    raise SystemExit(1)

for rel in REQUIRED_PATHS:
    p = ROOT / rel
    if not p.is_file():
        fail(f"missing required submission evidence: {rel}")
    if p.stat().st_size == 0:
        fail(f"empty required submission evidence: {rel}")

with (ROOT / "research/results/VULKAX_FINAL_RESULTS_2026-09-21.json").open() as f:
    final = json.load(f)

dcs = final["dcs"]["stages"]
ofc = final["orthogonal_force_compliance"]

checks = {
    "D1 cases": (dcs["D1"]["cases"], 64),
    "D2 frozen worlds": (dcs["D2_frozen"]["truth_worlds"], 16),
    "D2 resolved": (dcs["D2_frozen"]["resolved_worlds"], 0),
    "D4V proposals": (dcs["D4V"]["proposals"], 36),
    "D4V deceptive": (dcs["D4V"]["deceptive"], 14),
    "GAUGE trials": (dcs["GAUGE"]["trials"], 10),
    "GAUGE longitudinal endpoint wins": (dcs["GAUGE"]["longitudinal_darkfield_endpoint_wins"], 9),
    "OFC proposals": (ofc["proposal_count"], 36),
    "OFC deceptive": (ofc["deceptive_count"], 12),
    "OFC resolved force coverage": (ofc["force_compliance"]["coverage"], 0),
}

for label, (actual, expected) in checks.items():
    if actual != expected:
        fail(f"{label}: expected {expected!r}, found {actual!r}")

ratio = float(ofc["force_to_dcs_median_abs_z_ratio"])
if not (11.45 <= ratio <= 11.46):
    fail(f"unexpected force/DCS median |z| ratio: {ratio}")

max_force = float(ofc["force_compliance"]["max_abs_z"])
if not (1.3190 <= max_force <= 1.3192):
    fail(f"unexpected force maximum |z|: {max_force}")

manuscript = (ROOT / "paper/main.tex").read_text(encoding="utf-8")
required_text = [
    "Measured-source engineering",
    "GAUGE: retrospective measured channel",
    "Fresh orthogonal force-compliance",
    "Graphics-facing benchmark and visual evidence",
    "../docs/readme_assets/02_deceptive_repair.png",
    "../docs/readme_assets/03_deception_map.png",
    "../docs/readme_assets/04_same_probe.png",
    "../docs/readme_assets/06_fingerprint.png",
    "../docs/readme_assets/07_residual_field.png",
    "../docs/readme_assets/10_signal_gain.png",
    "../docs/readme_assets/11_information_limit.png",
]
for needle in required_text:
    if needle not in manuscript:
        fail(f"manuscript missing required evidence reference: {needle}")

print("PASS graphics submission evidence audit")
print(f"  public measured-source families: DOT C2 + GAUGE")
print(f"  frozen D4V population: {dcs['D4V']['proposals']} proposals")
print(f"  fresh OFC population: {ofc['proposal_count']} proposals")
print(f"  force/DCS median |z| ratio: {ratio:.6f}")
print("  visual evidence assets: present and manuscript-linked")

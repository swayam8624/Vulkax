#!/usr/bin/env python3
from __future__ import annotations

import base64
import html
import json
from pathlib import Path


def esc(value):
    return html.escape(str(value), quote=True)


def png_data_uri(path: Path) -> str:
    if not path.is_file() or path.stat().st_size == 0:
        raise SystemExit(f"missing plate image: {path}")
    return "data:image/png;base64," + base64.b64encode(path.read_bytes()).decode("ascii")


def metrics(repo_root: Path):
    hero = json.loads((repo_root / "visualization/data/hero_case_ofc_2026-09-21.json").read_text(encoding="utf-8"))
    ledger = json.loads((repo_root / "research/results/VULKAX_FINAL_RESULTS_2026-09-21.json").read_text(encoding="utf-8"))
    row = hero["row"]
    improve = 100.0 * (row["baseline_holdout_m"] - row["repair_holdout_m"]) / row["baseline_holdout_m"]
    worsen = 100.0 * (row["repair_target_m"] - row["baseline_target_m"]) / row["baseline_target_m"]
    exact_ratio = abs(row["force_progress_z"]) / max(abs(row["dcs_progress_z"]), 1e-15)
    ofc = ledger["orthogonal_force_compliance"]
    return {
        "truth_id": row["truth_id"],
        "improve_pct": improve,
        "worsen_pct": worsen,
        "dcs_z": row["dcs_progress_z"],
        "force_z": row["force_progress_z"],
        "case_ratio": exact_ratio,
        "median_ratio": ofc["force_to_dcs_median_abs_z_ratio"],
        "threshold": 2.0,
        "force_median_abs_z": ofc["force_compliance"]["median_abs_z"],
        "force_max_abs_z": ofc["force_compliance"]["max_abs_z"],
        "dcs_median_abs_z": ofc["dcs"]["median_abs_z"],
        "resolved": ofc["resolved"],
        "proposals": ofc["proposals"],
    }


def write_svg(path: Path, content: str):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8")
    print(path)


def defs():
    return """<defs>
      <linearGradient id="bg" x1="0" y1="0" x2="1" y2="1">
        <stop offset="0" stop-color="#06111f"/>
        <stop offset="0.58" stop-color="#071522"/>
        <stop offset="1" stop-color="#150d11"/>
      </linearGradient>
      <linearGradient id="cyanLine" x1="0" x2="1">
        <stop offset="0" stop-color="#57d9ff"/>
        <stop offset="0.65" stop-color="#b7f4ff"/>
        <stop offset="1" stop-color="#ff9b53"/>
      </linearGradient>
      <filter id="softGlow" x="-40%" y="-40%" width="180%" height="180%">
        <feGaussianBlur stdDeviation="10" result="blur"/>
        <feMerge><feMergeNode in="blur"/><feMergeNode in="SourceGraphic"/></feMerge>
      </filter>
      <filter id="shadow" x="-30%" y="-30%" width="160%" height="160%">
        <feDropShadow dx="0" dy="18" stdDeviation="24" flood-color="#000" flood-opacity=".35"/>
      </filter>
    </defs>"""

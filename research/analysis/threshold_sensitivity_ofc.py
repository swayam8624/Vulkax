#!/usr/bin/env python3
"""Post-hoc threshold sensitivity for the frozen Reality Probe OFC population.

This analysis MUST NOT be used to retune the frozen decision rule. It reads only the
committed fresh OFC proposal ledger and reports what the already-observed signed z
values would do under a small diagnostic sweep of decision magnitudes.

Primary rule in the paper remains |z| >= 2.
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path

THRESHOLDS = (1.0, 1.5, 2.0, 2.5, 3.0)
CHANNELS = ("dcs_progress_z", "force_progress_z")


def load_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as f:
        return list(csv.DictReader(f))


def summarize(rows: list[dict[str, str]]) -> list[dict[str, object]]:
    deceptive_n = sum(r["label"] == "deceptive" for r in rows)
    beneficial_n = sum(r["label"] == "beneficial" for r in rows)
    out: list[dict[str, object]] = []
    for channel in CHANNELS:
        for tau in THRESHOLDS:
            support = [r for r in rows if float(r[channel]) >= tau]
            veto = [r for r in rows if float(r[channel]) <= -tau]
            resolved = support + veto
            deceptive_veto = [r for r in veto if r["label"] == "deceptive"]
            beneficial_false_veto = [r for r in veto if r["label"] == "beneficial"]
            deceptive_false_support = [r for r in support if r["label"] == "deceptive"]
            beneficial_support = [r for r in support if r["label"] == "beneficial"]
            out.append(
                {
                    "channel": "DCS" if channel == "dcs_progress_z" else "Force",
                    "tau": f"{tau:.1f}",
                    "support_n": len(support),
                    "veto_n": len(veto),
                    "resolved_n": len(resolved),
                    "coverage": len(resolved) / len(rows),
                    "deceptive_veto_n": len(deceptive_veto),
                    "deceptive_veto_recall": len(deceptive_veto) / deceptive_n,
                    "beneficial_false_veto_n": len(beneficial_false_veto),
                    "beneficial_false_veto_rate": len(beneficial_false_veto) / beneficial_n,
                    "deceptive_false_support_n": len(deceptive_false_support),
                    "beneficial_support_n": len(beneficial_support),
                }
            )
    return out


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--input",
        type=Path,
        default=Path("visualization/data/ofc_proposals_visualization_2026-09-21.csv"),
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("research/paper_data/THRESHOLD_SENSITIVITY_OFC_2026-09-21.csv"),
    )
    args = parser.parse_args()

    rows = load_rows(args.input)
    if len(rows) != 36:
        raise SystemExit(f"expected frozen 36-proposal OFC population, found {len(rows)}")
    if sum(r["label"] == "deceptive" for r in rows) != 12:
        raise SystemExit("expected 12 deceptive proposals in frozen OFC population")
    if sum(r["label"] == "beneficial" for r in rows) != 24:
        raise SystemExit("expected 24 beneficial proposals in frozen OFC population")

    summary = summarize(rows)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fields = list(summary[0].keys())
    with args.output.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(summary)

    for row in summary:
        print(
            row["channel"], row["tau"],
            f"resolved={row['resolved_n']}/36",
            f"veto_recall={float(row['deceptive_veto_recall']):.3f}",
            f"false_veto={float(row['beneficial_false_veto_rate']):.3f}",
        )


if __name__ == "__main__":
    main()

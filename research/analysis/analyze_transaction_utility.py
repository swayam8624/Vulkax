#!/usr/bin/env python3
"""Quantify practical value of a support/veto/unresolved commit gate.

For records with target_error_delta = repair target error - baseline target error:
- positive delta is harm if the repair is committed;
- negative delta is benefit if the repair is committed.

The script reports prevented harm and foregone benefit without inventing a monetary
utility function.
"""
from __future__ import annotations

import argparse
import csv
from pathlib import Path


def decision(score: float, threshold: float) -> str:
    if score >= threshold:
        return "support"
    if score <= -threshold:
        return "veto"
    return "unresolved"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--records", type=Path, default=Path("build/publication-validation/records.csv"))
    ap.add_argument("--threshold", type=float, default=2.0)
    ap.add_argument("--out", type=Path, default=Path("build/publication-validation/analysis/transaction_utility.csv"))
    args = ap.parse_args()

    with args.records.open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))

    groups: dict[tuple[str, str], list[dict[str, str]]] = {}
    for r in rows:
        if not r.get("target_error_delta", "").strip():
            continue
        groups.setdefault((r["dataset"], r["method"]), []).append(r)

    out_rows = []
    for (dataset, method), subset in sorted(groups.items()):
        deltas = [float(r["target_error_delta"]) for r in subset]
        decisions = [decision(float(r["score"]), args.threshold) for r in subset]
        always_harm = sum(max(d, 0.0) for d in deltas)
        gate_harm = sum(max(d, 0.0) for d, dec in zip(deltas, decisions) if dec == "support")
        possible_benefit = sum(max(-d, 0.0) for d in deltas)
        realized_benefit = sum(max(-d, 0.0) for d, dec in zip(deltas, decisions) if dec == "support")
        out_rows.append({
            "dataset": dataset,
            "method": method,
            "n": len(subset),
            "commit_rate": sum(d == "support" for d in decisions) / len(subset),
            "always_commit_harm_target_error": always_harm,
            "gated_commit_harm_target_error": gate_harm,
            "prevented_harm_target_error": always_harm - gate_harm,
            "possible_benefit_target_error": possible_benefit,
            "realized_benefit_target_error": realized_benefit,
            "foregone_benefit_target_error": possible_benefit - realized_benefit,
            "harmful_commits": sum(delta > 0 and dec == "support" for delta, dec in zip(deltas, decisions)),
            "beneficial_commits": sum(delta < 0 and dec == "support" for delta, dec in zip(deltas, decisions)),
        })

    args.out.parent.mkdir(parents=True, exist_ok=True)
    if out_rows:
        with args.out.open("w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=list(out_rows[0]))
            w.writeheader()
            w.writerows(out_rows)
    else:
        args.out.write_text("", encoding="utf-8")
    print(f"VALID transaction utility: {len(out_rows)} method summaries -> {args.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

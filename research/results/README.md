# Vulkax Research Results Index

Canonical research state date: **2026-09-20**

## Start here

1. [Final benchmark summary](DCS_FINAL_BENCHMARK_SUMMARY_2026-09-20.md)
2. [Machine-readable benchmark table](DCS_FINAL_BENCHMARK_TABLE_2026-09-20.csv)
3. [Machine-readable result ledger](DCS_FINAL_RESULTS_2026-09-20.json)
4. [Paper-data map](../paper_data/README.md)
5. [Current research state](../status/CURRENT_RESEARCH_STATE_2026-09-20.md)
6. [Implementation completion](../status/DCS_IMPLEMENTATION_COMPLETE_2026-09-20.md)

## Reproduce everything

```bash
bash run_everything.sh
```

The final run generates:

- fresh analysis JSON/CSV for D1/D2/D3/D4V;
- GAUGE effective-span and per-trial retrospective evidence;
- captured-world certificate and optional timing data;
- deterministic paper-facing SVG/CSV assets;
- system/compiler/GPU provenance;
- command logs;
- SHA-256 artifact index;
- frozen-result reproduction validation.

Final bundle:

`build/paper-evidence/`

Generated paper assets:

`build/paper-figures/`

## Frozen stage results

- [D2 fresh validation](../status/DCS_D2_VALIDATION_RESULT.md)
- [D3 witness-space/adaptive-order discovery](../status/DCS_D3_RESULT_2026-09-20.md)
- [D4V repair-veto discovery](../status/DCS_D4V_RESULT_2026-09-20.md)
- [GAUGE retrospective](../status/DCS_GAUGE_RETROSPECTIVE_RESULT_2026-09-20.md)

## Protocols

- [D2 validation protocol](../benchmarks/DCS_D2_VALIDATION_PROTOCOL.md)
- [D3 discovery protocol](../benchmarks/DCS_D3_DISCOVERY_PROTOCOL.md)
- [D4V repair-veto protocol](../benchmarks/DCS_D4V_REPAIR_VETO_DISCOVERY_PROTOCOL.md)
- [Full benchmark ledger](../benchmarks/DCS_BENCHMARK_PLAN.md)

## Deterministic figure/table outputs

`research/analysis/generate_paper_assets.py` converts the frozen result JSON into:

- target-ranking agreement figure;
- standardized-separation figure;
- numerical-witness-floor figure;
- D4V repair-proposal figure;
- GAUGE channel-contradiction figure;
- stage-outcome table;
- claim-boundary table.

Every generated figure has a source CSV.

## Interpretation rule

The repository distinguishes:

- **implementation correctness** — D0/D1 and CI;
- **controlled synthetic evidence** — D2/D3/D4V;
- **retrospective measured evidence** — GAUGE;
- **fresh confirmatory evidence** — D5, not yet executed.

Do not use implementation success or retrospective GAUGE analysis as a substitute
for fresh prospective confirmation.

## Current disposition

DCS is fully implemented but is **not** a validated flagship verifier.

The strongest current prospective observation is that the bottleneck is
experimental information content: D4V contains 14 genuinely deceptive
held-out-improving repairs, yet DCS and all matched verification baselines remain
unresolved at the frozen credibility standard.

The strongest measured retrospective observation is the GAUGE channel conflict:
ordinary/marker evidence favors the finite-overlap repair while the longitudinal
mechanism channel favors the original endpoint model in 9/10 held-out repeats.

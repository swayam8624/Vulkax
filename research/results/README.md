# Vulkax Research Results Index

Canonical research state date: **2026-09-20**

## Start here

1. [Final benchmark summary](DCS_FINAL_BENCHMARK_SUMMARY_2026-09-20.md)
2. [Machine-readable benchmark table](DCS_FINAL_BENCHMARK_TABLE_2026-09-20.csv)
3. [Machine-readable result ledger](DCS_FINAL_RESULTS_2026-09-20.json)
4. [Current research state](../status/CURRENT_RESEARCH_STATE_2026-09-20.md)
5. [Implementation completion](../status/DCS_IMPLEMENTATION_COMPLETE_2026-09-20.md)

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

The strongest final observation is that the current bottleneck is experimental
information content: D4V contains 14 genuinely deceptive held-out-improving repairs,
yet DCS and all matched verification baselines remain unresolved at the frozen
credibility standard.

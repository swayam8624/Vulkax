# Vulkax Research Results Index

Canonical research snapshot: **2026-09-21**  
Publication-validation evidence extended through **2026-09-24**.

## Start here

1. [Final research summary](VULKAX_FINAL_RESEARCH_SUMMARY_2026-09-21.md)
2. [Final paper-level JSON ledger](VULKAX_FINAL_RESULTS_2026-09-21.json)
3. [Orthogonal force-compliance result](ORTHOGONAL_FORCE_COMPLIANCE_RESULT_2026-09-21.md)
4. [DCS benchmark summary](DCS_FINAL_BENCHMARK_SUMMARY_2026-09-20.md)
5. [Machine-readable benchmark table](DCS_FINAL_BENCHMARK_TABLE_2026-09-20.csv)
6. [Machine-readable DCS result ledger](DCS_FINAL_RESULTS_2026-09-20.json)
7. [Post-hoc information frontier](DCS_INFORMATION_FRONTIER_2026-09-20.md)
8. [Locked IRIS pendulum final result](IRIS_PENDULUM_FINAL_TEST_RESULT_2026-09-23.json)
9. [Post-final IRIS reviewer hardening](IRIS_PENDULUM_POSTFINAL_REVIEWER_HARDENING_2026-09-23.md)
10. [Machine-readable post-final hardening ledger](IRIS_PENDULUM_POSTFINAL_REVIEWER_HARDENING_2026-09-23.json)
11. [Failed IRIS free-fall validation](IRIS_FREEFALL_V6_VALIDATION_RESULT_2026-09-24.json)
12. [IRIS free-fall retrospective closure](IRIS_FREEFALL_V67_RETROSPECTIVE_CLOSURE_2026-09-24.json)
13. [Reality Probe novelty map](../status/REALITY_PROBE_NOVELTY_MAP_2026-09-24.md)
14. [Paper-data map](../paper_data/README.md)
15. [Current publication-facing research state](../status/CURRENT_RESEARCH_STATE_2026-09-24.md)
16. [Frozen 2026-09-21 research state](../status/CURRENT_RESEARCH_STATE_2026-09-21.md)
17. [Implementation completion](../status/DCS_IMPLEMENTATION_COMPLETE_2026-09-20.md)

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
- [GAUGE prospective validation failure](../status/GAUGE_PROSPECTIVE_VALIDATION_RESULT_2026-09-23.md)
- [Locked IRIS pendulum final result](IRIS_PENDULUM_FINAL_TEST_RESULT_2026-09-23.json)
- [Post-final IRIS reviewer hardening](IRIS_PENDULUM_POSTFINAL_REVIEWER_HARDENING_2026-09-23.md) — secondary analysis only; does not replace the locked final result.
- [Failed IRIS free-fall validation](IRIS_FREEFALL_V6_VALIDATION_RESULT_2026-09-24.json)
- [Free-fall negative-result closure](IRIS_FREEFALL_V67_RETROSPECTIVE_CLOSURE_2026-09-24.json)

## Protocols

- [D2 validation protocol](../benchmarks/DCS_D2_VALIDATION_PROTOCOL.md)
- [D3 discovery protocol](../benchmarks/DCS_D3_DISCOVERY_PROTOCOL.md)
- [D4V repair-veto protocol](../benchmarks/DCS_D4V_REPAIR_VETO_DISCOVERY_PROTOCOL.md)
- [Orthogonal force-compliance protocol](../benchmarks/ORTHOGONAL_FORCE_COMPLIANCE_PROTOCOL_2026-09-21.md)
- [Full benchmark ledger](../benchmarks/DCS_BENCHMARK_PLAN.md)

## Deterministic figure/table outputs

`research/analysis/generate_paper_assets.py` converts the frozen result JSON into:

- target-ranking agreement figure;
- standardized-separation figure;
- numerical-witness-floor figure;
- D4V repair-proposal figure;
- GAUGE channel-contradiction figure;
- post-hoc information-frontier figure;
- orthogonal known-force information-gain figure;
- stage-outcome table;
- claim-boundary table.

Every generated figure has a source CSV.

## Interpretation rule

The repository distinguishes:

- **implementation correctness** — D0/D1 and CI;
- **controlled synthetic evidence** — D2/D3/D4V and known-force compliance;
- **retrospective measured evidence** — GAUGE;
- **locked fresh measured evidence** — the 10-video IRIS pendulum final test;
- **failed fresh measured validation** — IRIS free-fall `drop_100/06..10`;
- **post-final secondary analysis** — clustered statistics, stronger baselines,
  GT-hidden proposals, truth uncertainty, channel dependence, and robustness.

Do not use implementation success, retrospective GAUGE analysis, or post-final
diagnostics as substitutes for the locked final results. The failed free-fall
validation remains failed, and its unopened final split must not be consumed to
rescue the method.

## Current disposition

DCS is fully implemented but is **not** a validated universal verifier.

The strongest synthetic result is an information gradient rather than a successful
decision rule: D4V contains 14 deceptive repairs with 0% resolved coverage, and a
separately repository-frozen known-force compliance channel raises median |z| by
11.46× on fresh truth worlds but still reaches only 1.319 at best against the
frozen |z|=2 decision reference.

The strongest locked measured positive result is the IRIS pendulum final set:
10/10 quality-pass videos, 90/110 correct controlled decisions for the
finite-amplitude probe versus 40/110 for the matched small-angle diagnostic
baseline, and 10/10 placebo cases left unresolved. These are 110 nested controlled
cases across 10 physical videos, not 110 independent experiments.

The second measured domain is a preserved failure: IRIS free-fall passes low-level
quality on 5/5 untouched validation videos but has 111.7% median acceleration
relative error. That validation split is permanently retrospective and the
designated final split remains unopened.

The resulting Reality Probe claim is conditional: rewrite verification depends on
whether the separately reserved physical channel contains discriminating
information for the requested rewrite.

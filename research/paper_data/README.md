# Vulkax Paper Data Package

This directory is the committed map for everything needed to build a paper from the
current Vulkax research state **without containing manuscript prose**.

The actual reproduced run artifacts are generated into:

`build/paper-evidence/`

by:

```bash
bash run_everything.sh
```

## Canonical result sources

Human-readable:
- `../results/DCS_FINAL_BENCHMARK_SUMMARY_2026-09-20.md`

Machine-readable:
- `../results/DCS_FINAL_BENCHMARK_TABLE_2026-09-20.csv`
- `../results/DCS_FINAL_RESULTS_2026-09-20.json`

Frozen stage evidence:
- `../status/DCS_D2_VALIDATION_RESULT.md`
- `../status/DCS_D3_RESULT_2026-09-20.md`
- `../status/DCS_D4V_RESULT_2026-09-20.md`
- `../status/DCS_GAUGE_RETROSPECTIVE_RESULT_2026-09-20.md`

Protocols:
- `../benchmarks/DCS_D2_VALIDATION_PROTOCOL.md`
- `../benchmarks/DCS_D3_DISCOVERY_PROTOCOL.md`
- `../benchmarks/DCS_D4V_REPAIR_VETO_DISCOVERY_PROTOCOL.md`

Claim boundaries:
- `../literature/CLAIM_GUARD.md`
- `../status/DCS_LIMITATIONS_AND_KILL_CRITERIA.md`
- `../literature/DCS_NOVELTY_THREAT_MAP.md`

## Structured paper-support sources

- `EXPERIMENT_MATRIX.csv` — complete D0–D6 experiment/evidence matrix
- `ABLATION_MATRIX.md` — executed method/ablation comparisons and frozen outcomes
- `REPRODUCIBILITY_CHECKLIST.md` — repository/data/reproduction completeness checklist
- `FIGURE_TABLE_SOURCE_MAP.md` — exact source mapping for figures and tables
- `PAPER_DATA_MANIFEST.json` — machine-readable package map
- [full reproduction guide](../../docs/PAPER_EVIDENCE_REPRODUCTION.md)

## Generated paper assets

The one-command runner generates:

`build/paper-figures/`

with:

1. `fig_ranking_agreement.svg` + CSV source
2. `fig_standardized_separation.svg` + CSV source
3. `fig_numerical_floor.svg` + CSV source
4. `fig_d4v_proposals.svg` + CSV source
5. `fig_gauge_channel_contradiction.svg` + CSV source
6. `fig_information_frontier.svg` + CSV source
7. `table_stage_outcomes.csv`
8. `table_claim_boundaries.csv`
9. `table_information_frontier.csv`
10. `figure_manifest.json`

These are deterministic transforms of the committed result ledger; they do not alter
the scientific decision.

## Fresh generated evidence

The full runner reproduces and packages:

- D1 positive control
- solver-native fixed-witness discovery
- automatic active-selection discovery
- D2 frozen validation
- D3 adaptive-order discovery
- D4V repair-veto discovery
- captured-world certificate
- controlled timing evidence
- GAUGE effective-span validation
- GAUGE retrospective per-trial table and summary
- command logs
- system/compiler/GPU provenance
- SHA-256 artifact index

## What a paper can currently state from this package

Supported:
- held-out observational improvement can be deceptive;
- D2/D3/D4V prospectively failed to produce useful resolved DCS verification
  coverage under their frozen criteria;
- direct witness-space numerical treatment reduces the numerical floor;
- the GAUGE retrospective shows a channel-specific contradiction: aggregate/marker
  evidence favors the overlap repair while the longitudinal mechanism channel
  favors the original endpoint model in 9/10 repeats;
- Vulkax contains a complete reproducible implementation for counterfactual
  annihilation, witness synthesis, uncertainty handling, spatial localization and
  future frozen confirmatory replay.

Not supported:
- DCS superiority over Fisher/raw baselines;
- useful prospective DCS repair-veto deployment coverage;
- fresh measured-domain prospective DCS confirmation;
- a universal physical correctness certificate.

## Remaining external-data boundary

A future positive flagship result still requires a genuinely higher-information
physical channel, a new untouched validation partition, and then fresh D5 measured
confirmation.

The infrastructure to replay and package that future confirmation is already in the
repository.

## Post-hoc information frontier

Additional diagnostic sources:

- `../results/DCS_INFORMATION_FRONTIER_2026-09-20.md`
- `../results/DCS_INFORMATION_FRONTIER_2026-09-20.json`
- `../results/DCS_INFORMATION_FRONTIER_2026-09-20.csv`
- `../results/GAUGE_PAIRED_DIAGNOSTICS_2026-09-20.csv`

These quantify how far unresolved D4V signals are from the frozen `|z|=2`
reference and provide paired GAUGE sign-direction summaries. They are explicitly
post-hoc/retrospective and do not modify any frozen gate.

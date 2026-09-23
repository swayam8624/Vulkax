# Vulkax Research Workspace

Current canonical state: **2026-09-21**

Canonical stable branch:
`main`

## One-command reproduction

From the repository root:

```bash
bash run_everything.sh
```

This performs the current paper-facing research campaign end to end:

- configure/build the complete project;
- run the full CTest suite;
- run release/evidence/CLI validation;
- check the available native backend;
- generate and execute a controlled captured-world run;
- reproduce D1, solver-native discovery, D2, D3 and D4V;
- reproduce the fresh preregistered orthogonal force-compliance test;
- download and validate the GAUGE foam subset;
- run the GAUGE retrospective;
- validate fresh outputs against the frozen canonical result ledger;
- generate deterministic SVG/CSV paper assets;
- assemble a SHA-256-indexed paper-evidence bundle.

Primary generated output:

`build/paper-evidence/`

Useful options:

```bash
bash run_everything.sh --clean
bash run_everything.sh --backend Metal
bash run_everything.sh --backend Vulkan
bash run_everything.sh --backend none
bash run_everything.sh --skip-gauge
bash run_everything.sh --skip-performance
bash run_everything.sh --exhaustive
```

The GAUGE retrospective now prints live material/trial progress during its 20
definitive forward simulations.

## Publication-validation extension (2026-09-23)

The original paper-facing result remains frozen. A separate validation lane now
implements the stronger experiment design needed for publication review:
three-way ground truth, placebo controls, dose response, robustness, channel
dependence, clustered confidence intervals, risk/coverage, failure ledgers, and a
final-test protocol lock.

Diagnostic analysis of existing D4V/OFC evidence:

```bash
bash research/scripts/run_publication_validation.sh --diagnostic
```

This command does **not** convert those historical results into confirmatory
evidence. New multi-dataset or measured records are supplied through `--extra`
only after the corresponding trials have actually run.

Protocol and execution guide:
- `validation/README.md`
- `validation/protocol_v1.json`
- `benchmarks/REAL_MEASURED_VALIDATION_PROTOCOL_2026-09-23.md`
- `literature/VALIDATION_RELATED_WORK_MAP_2026-09-23.md`

## Paper-data entrypoints

- `paper_data/README.md`
- `paper_data/PAPER_DATA_MANIFEST.json`
- `paper_data/FIGURE_TABLE_SOURCE_MAP.md`
- `results/README.md`

Generated figure/table assets are written to:

`build/paper-figures/`

A full reproduction is accepted only if
`research/analysis/validate_paper_reproduction.py` confirms that the fresh outputs
match the frozen result ledger within the declared cross-platform tolerances.

## Navigation

### Current state
- `status/CURRENT_RESEARCH_STATE_2026-09-21.md`
- `status/CURRENT_RESEARCH_STATE_2026-09-20.md` — historical pre-force snapshot
- `status/DCS_IMPLEMENTATION_COMPLETE_2026-09-20.md`
- `status/BRANCH_CLEANUP_2026-09-20.md`
- `status/DOCUMENTATION_BENCHMARK_CLEANUP_COMPLETE_2026-09-20.md`

### Results
- `results/README.md`
- `results/VULKAX_FINAL_RESEARCH_SUMMARY_2026-09-21.md`
- `results/VULKAX_FINAL_RESULTS_2026-09-21.json`
- `results/ORTHOGONAL_FORCE_COMPLIANCE_RESULT_2026-09-21.md`
- `results/DCS_FINAL_BENCHMARK_SUMMARY_2026-09-20.md`
- `results/DCS_FINAL_BENCHMARK_TABLE_2026-09-20.csv`
- `results/DCS_FINAL_RESULTS_2026-09-20.json`

### Frozen DCS experiments
- `status/DCS_D2_VALIDATION_RESULT.md`
- `status/DCS_D3_RESULT_2026-09-20.md`
- `status/DCS_D4V_RESULT_2026-09-20.md`
- `status/DCS_GAUGE_RETROSPECTIVE_RESULT_2026-09-20.md`

### Method and limits
- `status/DCS_MATH_IMPLEMENTATION_AUDIT_2026-09-21.md`
- `paper_data/PAPER_POSITIONING.md`
- `status/PAPER_FREEZE_2026-09-21.md`
- `status/DCS_RESEARCH_PROGRAM.md`
- `status/DCS_LIMITATIONS_AND_KILL_CRITERIA.md`
- `literature/DCS_NOVELTY_THREAT_MAP.md`
- `literature/CLAIM_GUARD.md`

### Protocols
- `benchmarks/ORTHOGONAL_FORCE_COMPLIANCE_PROTOCOL_2026-09-21.md`
- `benchmarks/DCS_BENCHMARK_PLAN.md`
- `benchmarks/DCS_D2_VALIDATION_PROTOCOL.md`
- `benchmarks/DCS_D3_DISCOVERY_PROTOCOL.md`
- `benchmarks/DCS_D4V_REPAIR_VETO_DISCOVERY_PROTOCOL.md`

### Historical provenance
- `status/2026-09-20.md` — chronological research log
- `status/PROBLEM_SOLUTION_LOCK.md` — historical DCS positive lock, now superseded
- `hypotheses/FILTERED_LOCK_2026-09-20.md` — historical filtering record
- `hypotheses/priority_queue.json` — current execution priority

## Current conclusion

The research story is frozen.

DCS is fully implemented, but its tested prospective formulations did not achieve
useful resolved verification coverage.

A separately preregistered known-force compliance channel was then tested on fresh
truth worlds. It increased median standardized discriminating signal by **11.455×**
relative to fresh kinematic DCS, but still produced **0% resolved coverage** and a
maximum `|z|` of **1.319 < 2**.

Therefore the final result is stronger and more specific than “DCS failed”:

> **physical verification is information-limited; changing the physical
> information channel can dramatically improve observability without necessarily
> providing enough evidence for a credible support/veto decision.**

GAUGE remains a retrospective measured channel contradiction.

For the frozen paper, do not retune DCS, the force amplitude, or the decision
threshold. Future real sensing experiments are separate follow-on work.

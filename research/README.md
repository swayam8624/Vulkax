# Vulkax Research Workspace

Current canonical state: **2026-09-20**

Canonical branch:
`research/integration-20260920`

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
- `status/CURRENT_RESEARCH_STATE_2026-09-20.md`
- `status/DCS_IMPLEMENTATION_COMPLETE_2026-09-20.md`
- `status/BRANCH_CLEANUP_2026-09-20.md`
- `status/DOCUMENTATION_BENCHMARK_CLEANUP_COMPLETE_2026-09-20.md`

### Results
- `results/README.md`
- `results/DCS_FINAL_BENCHMARK_SUMMARY_2026-09-20.md`
- `results/DCS_FINAL_BENCHMARK_TABLE_2026-09-20.csv`
- `results/DCS_FINAL_RESULTS_2026-09-20.json`

### Frozen DCS experiments
- `status/DCS_D2_VALIDATION_RESULT.md`
- `status/DCS_D3_RESULT_2026-09-20.md`
- `status/DCS_D4V_RESULT_2026-09-20.md`
- `status/DCS_GAUGE_RETROSPECTIVE_RESULT_2026-09-20.md`

### Method and limits
- `status/DCS_RESEARCH_PROGRAM.md`
- `status/DCS_LIMITATIONS_AND_KILL_CRITERIA.md`
- `literature/DCS_NOVELTY_THREAT_MAP.md`
- `literature/CLAIM_GUARD.md`

### Protocols
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

DCS is fully implemented, but the tested prospective formulations did not achieve
useful resolved verification coverage.

Do not interpret:
- implementation success,
- constructed positive controls,
- or the retrospective GAUGE result

as fresh prospective validation.

The next positive research mechanism must create a genuinely higher-information
physical channel and must use new untouched validation data.

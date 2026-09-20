# Paper-Data / Reproducibility Checklist

This checklist defines completion of the repository-side evidence package. It does
not imply acceptance, novelty, or a positive prospective result.

## Code and environment

- [x] canonical research branch documented
- [x] exact git commit captured by full runner
- [x] compiler/CMake/Python versions captured
- [x] CPU/GPU/runtime information captured
- [x] complete CMake build in one-command runner
- [x] full CTest suite in one-command runner
- [x] native backend conformance when available
- [x] release/evidence/CLI contract validation

## Synthetic research evidence

- [x] D1 constructed positive control
- [x] solver-native fixed-witness discovery
- [x] automatic active-selection comparison
- [x] D2 fresh frozen validation
- [x] D3 witness-space/adaptive-order study
- [x] D4V pair-specific repair-veto study
- [x] fair raw/Fisher/max-motion/random baselines
- [x] negative outcomes preserved rather than threshold-tuned

## Measured evidence

- [x] public GAUGE subset fetcher
- [x] measured-only effective-span prerequisite
- [x] preregistered effective-span validation
- [x] definitive endpoint/overlap forward simulations
- [x] per-trial GAUGE retrospective table
- [x] aggregate GAUGE summary
- [x] measured result explicitly marked retrospective
- [ ] fresh positive D5 prospective measured confirmation — not executed because no
      synthetic DCS formulation cleared the advancement gates

## Paper-facing data

- [x] canonical human-readable benchmark summary
- [x] machine-readable final JSON ledger
- [x] machine-readable final CSV benchmark table
- [x] experiment matrix
- [x] executed ablation matrix
- [x] deterministic figure generator
- [x] source CSV for every generated figure
- [x] stage-outcome table
- [x] claim-boundary table
- [x] figure/table source map
- [x] limitations and kill criteria
- [x] novelty-threat map
- [x] final claim guard

## Reproduction package

- [x] root one-command runner
- [x] live progress for long GAUGE stage
- [x] reproduction validator against frozen numbers
- [x] evidence bundle assembler
- [x] SHA-256 manifest
- [x] SHA256SUMS
- [x] command logs
- [x] system provenance
- [x] portable tar.gz archive + checksum
- [x] smoke CI
- [x] manual/full CI workflow

## Manuscript boundary

Deliberately not included:
- manuscript title/abstract prose;
- Introduction/Related Work prose;
- Methods prose;
- Results prose;
- Discussion/Conclusion prose.

All source evidence needed to write those sections from the **executed research
sequence** is indexed in this package.

## Scientific boundary

The current package is complete for the research that was actually executed.

It does not contain a fresh positive D5 measured confirmation because the preceding
prospective DCS gates were negative. That absence is a scientific result/boundary,
not missing repository plumbing.

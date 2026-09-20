# DCS / Vulkax Implementation Completion — 2026-09-20

Status: **ENGINEERING IMPLEMENTATION COMPLETE**

This document marks completion of the currently specified DCS/Vulkax implementation.
It does **not** claim that DCS has passed scientific validation.

## Completion checklist

### Core mathematics
- [x] counterfactual cumulants
- [x] arbitrary-order annihilating stencils
- [x] stencil moment validation
- [x] response distance/norm utilities
- [x] mechanism order of contact
- [x] basis-aware jet contact estimate
- [x] mechanism resolution
- [x] witness scale-flow diagnostics
- [x] standardized dark-field discrepancy
- [x] deceptive-repair classification

### Experiment synthesis
- [x] global model-disagreement stencil synthesis
- [x] maximin standardized separation
- [x] pair-aware numerical uncertainty
- [x] direct witness-space numerical uncertainty
- [x] multi-seed nullspace search
- [x] pair-specific repair-veto synthesis

### Solver-native research harnesses
- [x] constructed deceptive-repair positive control
- [x] solver-native fixed-witness discovery
- [x] active-selection discovery
- [x] frozen D2 validation
- [x] D3 witness-space/adaptive-order discovery
- [x] D4V pair-specific repair-veto discovery
- [x] Fisher baseline
- [x] same-cost raw bundle
- [x] raw pairwise selector
- [x] maximum-motion baseline
- [x] deterministic random baseline

### Measured-data / confirmatory infrastructure
- [x] GAUGE measured-only effective-span prerequisite
- [x] GAUGE retrospective dark-field runner
- [x] generic D5 confirmatory replay runner
- [x] frozen-stencil enforcement
- [x] no-fit confirmatory replay semantics
- [x] support / veto / unresolved output
- [x] direct nominal-vs-refined numerical witness uncertainty

### Spatial / captured-world layer
- [x] per-region spatial dark-field residuals
- [x] standardized local residuals
- [x] resolved/unresolved region flags
- [x] arbitrary particle/Gaussian/surface component mapping
- [x] PLY export for visualization

### Reproducibility / output
- [x] JSON result artifacts
- [x] CSV case/proposal/stencil tables
- [x] D3 paper-table exporter
- [x] unified evidence-pack exporter
- [x] branch cleanup manifest
- [x] frozen negative-result documents
- [x] novelty/limitations/kill-criteria documentation
- [x] final implementation-complete CI gate

## Scientific result state

Implementation completion does not override experimental outcomes.

Current evidence:
- D1 constructed positive control: PASS
- fixed order-2 ranking: negative
- D2 fresh frozen validation: negative / 0 resolved
- D3 witness-space adaptive-order discovery: negative / 0 resolved
- D4V pair-specific repair veto: negative / 0 resolved
- GAUGE retrospective:
  - ordinary overlap repair wins 10/10 in face+marker metrics
  - marker dark-field prefers endpoint 0/10
  - longitudinal dark-field prefers endpoint 9/10
  - retrospective only, not confirmatory

Therefore no manuscript may claim that DCS has prospectively solved physical repair
verification.

## What remains after implementation

Only research/paper execution remains:
1. choose or discover the final positive physical-information channel;
2. execute a fresh independent confirmatory dataset if a positive mechanism emerges;
3. generate final paper figures/animations from the existing exporters;
4. write the manuscript.

No further core DCS engineering is required to run those experiments.

# DCS Mathematics-to-Code Audit

Date: 2026-09-21

Status: **AUDITED AGAINST `main` SOURCE**

This document separates mathematical ideas that are actually executable from ideas
that appeared only in exploratory research discussion.

## Implemented mathematical core

| Mathematical object | Implementation |
|---|---|
| vector response norm/distance | `responseNorm`, `responseDistance` |
| signed dark-field response | `applyAnnihilatingStencil` |
| arbitrary-order lower-moment annihilation | `validateAnnihilatingStencil` |
| Boolean-lattice Möbius interaction | `counterfactualCumulant` |
| response-order separation | `mechanismOrderOfContact` |
| multi-witness operational jet contact | `estimateJetOrderOfContact` |
| witness refinement flow | `analyzeWitnessScaleFlow` |
| deceptive repair predicate | `classifyDeceptiveRepair` |
| standardized witness discrepancy | `standardizedDarkFieldDiscrepancy` |
| stencil uncertainty propagation | `propagateStencilUncertainty` |
| worst-case standardized separation | `worstCaseStandardizedSeparation` |
| pair-aware standardized separation | `worstCasePairAwareStandardizedSeparation` |
| direct witness-space numerical separation | `worstCaseWitnessSpaceStandardizedSeparation` |
| automatic nullspace stencil synthesis | `synthesizeAnnihilatingStencil` |
| maximin synthesis | `synthesizeMaximinAnnihilatingStencil` |
| pair-aware maximin synthesis | `synthesizePairAwareMaximinStencil` |
| witness-space maximin synthesis | `synthesizeWitnessSpaceMaximinStencil` |
| observable mechanism order | `mechanismResolution` |
| spatial standardized residual localization | `localizeSpatialWitnessResidual` |
| frozen support/veto/unresolved replay | `research/analysis/dcs_confirmatory_replay.py` |

## Moment-annihilation implementation

For a declared order k, the implementation enumerates every multivariate monomial
whose total degree is less than k and evaluates:

```math
r_\alpha
=
\sum_i w_i u_i^\alpha
```

The stencil is valid only when the maximum absolute residual is below the supplied
moment tolerance.

This means the code implements the general moment contract; it is not hard-coded to
only the familiar symmetric second difference.

## Möbius interaction implementation

For n primitive interventions, the code requires exactly 2^n subset responses and
computes:

```math
\kappa(S)
=
\sum_{T\subseteq S}
(-1)^{|S|-|T|}F(T).
```

The implementation and documentation correctly distinguish this finite-amplitude
interaction from a pure mixed derivative.

## Uncertainty implementation

The executable uncertainty budget separates:

- measurement variance;
- numerical variance;
- repeat variance.

For signed stencils:

- independent measurement/repeat variance scales with sum(w_i^2);
- numerical variance uses a conservative squared-L1 gain because solver error may
  be correlated across intervention points.

D3 additionally measures numerical uncertainty directly in witness space by comparing
the same stencil at nominal and refined numerical resolution.

## Maximin selection implementation

The code projects candidate disagreement into the exact lower-moment nullspace and
searches deterministic candidate directions.

The implementation is a heuristic for the non-convex maximin problem; it is **not**
claimed to be a globally optimal solver.

## Frozen decision implementation

The D4V and generic confirmatory runner implement:

```math
z
=
\frac{e_{baseline}-e_{repair}}{\sigma}.
```

with:

```text
z >= +threshold -> support
z <= -threshold -> veto
otherwise        -> unresolved
```

D4V freezes threshold = 2.0.

## Implemented but operational rather than mathematically exact

### Response jet

The repository does not symbolically reconstruct a complete Taylor jet tensor.
It estimates order-of-contact from a basis of validated order-selective witnesses.

### Active maximin optimum

The repository does not prove a global optimum. It uses deterministic projected
pairwise/nullspace candidates and evaluates their worst-case standardized separation.

### Numerical flow

The log-log witness flow is a convergence diagnostic. It is not a claim of a
renormalization-group law.

## Not implemented / not part of the paper

The following exploratory ideas are **not** represented as completed mathematics:

- full symbolic Hessian/third-order response-tensor reconstruction;
- globally certified non-convex optimal stencil design;
- holonomy/commutator mechanism algebra as a headline method;
- isotope-style physical perturbation experiments;
- frequency lock-in/intermodulation spectroscopy;
- a real force-sensor confirmatory dataset.

These should not appear as implemented contributions.

## Audit conclusion

The mathematical backbone used by the executed DCS experiments is genuinely
implemented.

The paper must claim the operational implementation that exists, not the broader set
of exploratory mathematical analogies considered during research.

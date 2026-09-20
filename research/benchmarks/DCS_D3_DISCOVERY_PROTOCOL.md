# DCS D3 Witness-Space / Adaptive-Order Discovery Protocol

Date: 2026-09-20
Status: **discovery only**. This partition may be used to choose the next method
configuration, but can never be reused as confirmatory validation.

## Why D3 exists

Frozen D2 order-2 validation failed at 0/16 coverage because raw-response numerical
uncertainty dominated the surviving dark-field signal.

D3 tests a new falsifiable hypothesis:

> Numerical uncertainty relevant to a dark-field witness should be measured after
> applying that same annihilating stencil, because lower-order numerical error that
> is cancelled by the witness should not be charged against the witness.

D3 also tests whether the first usable separating response order is adaptive rather
than fixed.

## Fresh discovery truth worlds

These values are disjoint from both the original 4-world discovery set and the
frozen 16-world D2 validation set.

- E = {13,125; 15,125; 16,975} Pa
- nu = {0.26; 0.33}

Total: **6 truth worlds**.

Truth simulator:
- APIC
- dt = 2.5e-5 s
- same body/grid/horizon used in prior DCS solver-native studies.

## Candidate fitting

Unchanged from D2:
1. APIC, dt=1e-4, fit E/nu
2. PIC, dt=1e-4, fit E/nu
3. APIC constrained nu=0.10, fit E
4. coarse APIC, dt=4e-4, fit E/nu

Calibration:
- (+0.025,0)
- (-0.025,0)
- (0,+0.025)
- (0,-0.025)

Fit grid:
- E={12,000;15,000;18,000} Pa
- nu={0.20;0.30;0.40}

## Intervention basis

Use a fixed 3x3 lattice:

shear in {-0.060,0,+0.060}
axial in {-0.060,0,+0.060}

Total: **9 physical probes**.

No amplitude tuning is permitted inside D3.

## Numerical witness construction

For each fitted candidate:
1. evaluate all 9 probes at nominal dt;
2. evaluate all 9 probes again at dt/2 with identical fitted physical parameters;
3. for every synthesized stencil mu, compute:
   - nominal witness D_mu[F_h]
   - refined witness D_mu[F_{h/2}]
4. define candidate numerical witness variance as the squared RMS difference between
   those two witness vectors.

Measurement and repeat noise are propagated through signed weights using sum(w_i^2).

## Adaptive mechanism order

Generate:
- best order-2 witness-space maximin stencil;
- best order-3 witness-space maximin stencil.

Choose the order **without truth labels**:

k* = argmax_{k in {2,3}} min_{i != j} Gamma_mu_k(M_i,M_j)

where Gamma uses direct witness-space numerical uncertainty.

Record:
- chosen order;
- order-2 predicted separation;
- order-3 predicted separation;
- annihilation residual;
- per-model witness-space numerical RMS.

## Baselines

Same physical 9-probe budget where applicable:

1. adaptive witness-space DCS
2. same-cost raw 9-probe bundle
3. raw maximin single probe
4. Fisher/Jacobian E/nu sensitivity probe
5. maximum-motion probe
6. deterministic pseudo-random probe
7. fixed order-2 witness-space DCS
8. fixed order-3 witness-space DCS

## Hidden scoring target

Selectors cannot inspect the stronger target:
- shear = +0.095
- axial = +0.075

Truth target is used only for post-selection ranking analysis.

## Discovery questions

D3 is considered promising only if the following qualitative pattern appears:

- witness-space uncertainty produces materially larger standardized separation than
  frozen D2's raw-response uncertainty treatment;
- at least some worlds clear the existing >=2 standardized-separation observability
  reference;
- adaptive order does not collapse to an arbitrary single order because of a
  software artifact;
- adaptive DCS target-ranking agreement exceeds the fixed order-2 discovery result;
- same-cost raw bundle remains a serious comparator;
- any gain is present across more than one candidate-pair family.

No threshold or intervention amplitude may be tuned to maximize these six worlds.

## If D3 is promising

Freeze:
- the exact witness-space uncertainty definition;
- 3x3 lattice;
- candidate set;
- adaptive-order rule;
- observability threshold or threshold-selection rule;

then evaluate on a **new D4 synthetic validation partition**.

## If D3 fails

If both order-2 and order-3 witnesses remain unresolvable or fail to add target
ranking value beyond same-cost raw observations, DCS must be narrowed substantially.
Do not keep increasing response order indefinitely.

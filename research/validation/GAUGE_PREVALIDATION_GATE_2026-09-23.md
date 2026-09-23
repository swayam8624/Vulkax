# GAUGE pre-validation promotion gate

Frozen: 2026-09-23  
Scope: development trials 1–4 only

This gate is frozen before the development dose-response and rigid-nuisance aggregate
results are inspected. It determines whether the redesigned Reality Probe may advance to
GAUGE validation trials 5–7.

The primary decision threshold remains `|score| = 2`. No criterion below authorizes
threshold tuning.

## Candidate operator

Primary candidate:
`reality_probe_rigid_invariant_strain`

Permanent ablations/baselines:
- legacy temporal `reality_probe_dcs`;
- `same_cost_raw_bundle`;
- `same_cost_rigid_aligned_bundle`;
- `simple_residual_or_uncertainty_baseline`.

The rigid-aligned raw baseline is included specifically so rigid invariance is not treated
as novel by itself.

## Gate A — strong controlled rewrite

On all 16 development worlds, for the 2x-E controlled truth test:

- SUPPORT recall >= 0.75;
- VETO recall >= 0.75;
- directional sign accuracy >= 0.80;
- false assertion rate on exact placebos = 0;
- no task family (stretch/compression) directional sign accuracy < 0.70.

Already observed after redesign: **PASS**. This result is frozen separately and does not
waive any later gate.

## Gate B — dose response

Use the same worlds and unchanged threshold with relative E perturbations:

- 2.5%;
- 5%;
- 10%;
- 20%.

The 2.5% and 5% levels are characterization levels: failure to resolve is allowed and
defines the low-dose information limit.

Promotion requirements:

### 20% E perturbation
- directional sign accuracy >= 0.90;
- SUPPORT recall >= 0.75;
- VETO recall >= 0.75;
- false assertion rate on placebo = 0.

### 10% E perturbation
- directional sign accuracy >= 0.80;
- SUPPORT recall >= 0.50;
- VETO recall >= 0.50;
- false assertion rate on placebo = 0.

Across the ordered dose levels, median absolute evidence for the primary probe should show
a non-decreasing physical trend. A single tied level is acceptable. A strong systematic
decrease with larger perturbation fails the mechanism check.

No threshold is chosen from the resulting dose curve.

## Gate C — rigid verification-channel nuisance

Apply a fixed, predeclared global transform to the measured verification channel:

- rotation: 20 degrees;
- rotation axis: normalized [1, 2, 3];
- translation magnitude: 0.025 m;
- fixed translation direction as implemented in the frozen runner.

On the strong 2x-E truth controls, the primary invariant-strain probe must retain:

- directional sign accuracy >= 0.90;
- SUPPORT recall >= 0.75;
- VETO recall >= 0.75;
- exact-placebo false assertion rate = 0.

The same-cost rigid-aligned raw baseline is reported alongside it. The paper may not claim
that pairwise strain is uniquely capable of rigid invariance if rigid registration obtains
comparable results.

## Gate D — numerical sensitivity

Before validation, rerun the strong controlled development test using the definitive
numerics already defined by the repository:

- n_cross = 7;
- n_long = 49;
- dt = 1 / 48000 s;
- APIC;
- released-asset aspect geometry;
- same material metadata and truth pairs.

Requirements for the primary probe:

- directional sign accuracy >= 0.80;
- SUPPORT recall >= 0.75;
- VETO recall >= 0.75;
- placebo false assertion rate = 0;
- no reversal of the clean pilot conclusion caused solely by refinement.

## Promotion rule

Validation trials 5–7 may be opened only when Gates A, B, C, and D all pass.

If a gate fails:
- retain the failed evidence;
- do not tune against trials 5–7;
- revise or kill the mechanism using development data only;
- rerun the full affected development gate before reconsidering promotion.

Final-test trials 8–10 remain locked regardless of validation performance until the final
publication protocol is frozen.

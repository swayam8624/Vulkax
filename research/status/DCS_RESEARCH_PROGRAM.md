# Dark-Field Counterfactual Spectroscopy (DCS) — Research Program

Status: **flagship research direction, implementation active**

Date locked for implementation: 2026-09-20

This document is an engineering/research contract, not paper prose.

## 1. Central problem

A physically editable captured world can contain a **deceptive repair**: a change to
boundary conditions, material parameters, transfer scheme, correspondence, or model
family that improves ordinary held-out trajectory/appearance metrics while moving the
underlying physical mechanism farther from reality.

The definitive GAUGE finite-support result is the motivating discovery:

- 10/10 held-out repeats improved both face-area NRMSE and marker RMSE;
- the preregistered longitudinal coupling error worsened for both materials;
- the candidate repair was therefore killed despite unanimous ordinary-metric wins.

DCS asks:

> Can we falsify apparently successful physical repairs by constructing
> counterfactual experiments whose signed combination mathematically annihilates
> every lower-order response the candidate worlds already agree on, leaving only
> irreducible mechanism interactions?

## 2. Mathematical objects

### 2.1 Counterfactual response

Let (u in R^p) denote controllable physical interventions and let a candidate world
M predict a vector-valued observable F_M(u).

Locally:

F_M(u) = F_M(0) + J_M u + 1/2 H_M[u,u] + 1/6 T_M[u,u,u] + ...

DCS does not claim Taylor/Volterra expansion as novel. It uses signed interventions
to suppress lower-order/common response when validating captured-world repairs.

### 2.2 Annihilating intervention stencil

A signed stencil mu={(w_i,u_i)} has declared order k when all intervention monomials
with total degree < k satisfy

sum_i w_i prod_j u_{ij}^{alpha_j} = 0.

The dark-field response is

D_mu(M) = sum_i w_i F_M(u_i).

If the cancellation contract holds, the leading signal comes from order k or above.

Implemented API:
- validateAnnihilatingStencil
- applyAnnihilatingStencil

### 2.3 Counterfactual cumulant

For a set S of discrete primitive interventions:

kappa(S) = sum_{T subseteq S} (-1)^(|S|-|T|) F(T).

This is Möbius inversion over the intervention lattice. DCS interprets kappa(S) as
the observable interaction that cannot be explained by proper subsets of S.

Implemented API:
- counterfactualCumulant

### 2.4 Mechanism order of contact

Given ordered response spectra for real/candidate worlds, define the first response
order whose normalized discrepancy exceeds a frozen tolerance.

nu(M_real,M) = min{k : D^(k)_real != D^(k)_M}.

This is an operational **deception depth**:
a repair can imitate the world through first order, second order, etc., before its
mechanism becomes distinguishable.

Implemented API:
- mechanismOrderOfContact

### 2.5 Numerical witness flow

A mechanism claim is not allowed unless the dark-field witness survives a numerical
fidelity ladder.

For witness norm W(h), record adjacent log slopes

beta(h_i,h_{i+1}) =
  log(W(h_{i+1})/W(h_i)) / log(h_{i+1}/h_i).

This is a convergence diagnostic only. DCS does not claim renormalization-group
theory as novelty and does not assume a universal power law.

Implemented API:
- analyzeWitnessScaleFlow

### 2.6 Deceptive repair

A repair R is deceptive when:

1. ordinary observation loss improves materially; and
2. an independent mechanism-witness error worsens materially.

Implemented API:
- classifyDeceptiveRepair

### 2.7 Dark-field experiment score

For candidate model dark-field responses D_1,...,D_K, an experiment can be ranked by
between-model dark-field dispersion divided by measurement variance, numerical
variance, and acquisition cost.

This is a DCS acquisition objective. Fisher/Jacobian OED and generic active model
discrimination remain mandatory baselines.

Implemented API:
- darkFieldDiscriminationScore

## 3. Implementation phases

### Phase D0 — mathematical core
Status: IMPLEMENTED on research/dcs-darkfield.

- arbitrary-dimensional annihilation validation;
- arbitrary declared order k;
- vector-valued signed response extraction;
- Boolean-lattice counterfactual cumulants;
- mechanism order of contact;
- numerical witness flow;
- deceptive-repair classifier;
- dark-field discrimination score;
- C++ unit tests.

Kill condition:
- any primitive fails deterministic polynomial controls.

### Phase D1 — constructed deceptive-repair positive control
Status: IMPLEMENTED, CI running.

Construct worlds where:
- baseline has visible first-order error but near-correct mixed coupling;
- a candidate repair drastically improves first-order/ordinary trajectory error;
- the same repair corrupts the mixed interaction.

The ordinary metric should accept the repair while the second-order dark-field
stencil rejects it.

This is implementation validation only, not scientific evidence.

Frozen gate:
- >=60/64 cases must exhibit the deceptive-repair pattern;
- >=60/64 must be preferred by the ordinary metric.

Failure kills the current DCS implementation before any expensive experiments.

### Phase D2 — solver-native synthetic truth
Status: NEXT.

Use actual Vulkax MPM worlds, not analytic response formulas.

Generate fresh truth worlds across:
- Young's modulus;
- Poisson ratio;
- support/boundary width;
- APIC/PIC/FLIP transfer;
- timestep;
- particle/grid resolution;
- constitutive family when available;
- observation/correspondence support.

For each truth:
1. fit/evaluate candidate worlds using a calibration subset;
2. generate repairs that improve conventional held-out loss;
3. label whether each repair moves the known mechanism/truth closer or farther;
4. evaluate raw response, scalar certificates, and DCS witnesses.

Primary output:
- deceptive-repair classification dataset with known cause labels.

### Phase D3 — automatic stencil synthesis
Status: PLANNED.

Candidate primitive interventions:
- shear +/- amplitude;
- longitudinal compression/extension;
- transverse compression;
- fixture displacement;
- attachment relocation;
- calibrated local force;
- contact displacement;
- known mass/stiffness perturbation.

Search over signed ensembles subject to:
- annihilation order contract;
- displacement/force safety;
- visibility;
- reversibility where required;
- numerical witness stability;
- experiment cost.

Objectives to compare:
1. DCS dark-field model dispersion;
2. Fisher/Jacobian information;
3. generic maximum-output disagreement;
4. maximum-motion;
5. random;
6. active model discrimination objective where implementable.

### Phase D4 — GAUGE retrospective mechanism study
Status: PLANNED; discovery evidence only.

The current GAUGE fixture result was used to invent/refine DCS and therefore must
not be presented as confirmatory validation.

Use it only to answer:
- can a dark-field witness expose the already-known metric mirage?
- which response orders separate endpoint and finite-support models?
- how does witness separation change with numerical resolution?

Potential real-data stencils from cyclic shear trajectories:
- symmetric second difference along driver displacement:
  F(+a)+F(-a)-2F(0);
- multi-amplitude finite-difference stencils;
- cross-observable cumulants where independently measured channels allow it.

Material fitting remains locked.

### Phase D5 — fresh real confirmatory regime
Status: REQUIRED BEFORE FLAGSHIP PAPER.

Need data not used to design thresholds/witnesses.

Preferred order:
1. untouched GAUGE task/material/deformation family if sufficiently independent;
2. IRIS/simple dynamics regime;
3. controlled newly captured tabletop deformable experiment.

The confirmatory benchmark must contain at least one ordinary-metric deceptive
repair or a prospective model-discrimination task.

### Phase D6 — captured Gaussian-world integration
Status: PLANNED.

Expose DCS witnesses simultaneously in:
- physical particle coordinates;
- surface/marker observables;
- Gaussian positions/scales/orientations;
- region-local mechanism maps.

Goal:
produce a spatial "dark-field mechanism image" showing where candidate worlds stop
agreeing after lower-order response has been cancelled.

## 4. Baselines

Every flagship result must compare against:

1. always accept / always simulate;
2. always refuse;
3. calibration RMSE;
4. held-out trajectory RMSE/NRMSE;
5. task-specific physical scalar metrics;
6. smallest sensitivity singular value / local identifiability;
7. ensemble/model disagreement on raw outputs;
8. scalar multi-evidence certificate;
9. Fisher/Jacobian optimal experiment design;
10. generic active model discrimination / maximum model-output separation;
11. metamorphic relation checks where a known relation exists;
12. higher-order response without lower-order-annihilation experiment synthesis;
13. DCS without numerical witness flow;
14. DCS fixed second order only;
15. DCS adaptive mechanism order.

## 5. Primary metrics

### Repair falsification
- deceptive-repair AUROC/AUPRC;
- false-accept rate;
- false-refuse rate;
- risk-coverage curve;
- accepted unsafe rate at frozen coverage;
- cause-stratified performance.

### Mechanism separation
- first separating order nu;
- dark-field SNR;
- raw-response SNR;
- SNR gain = dark-field/raw;
- candidate-model separation margin;
- noise sensitivity.

### Active acquisition
- probability of selecting an experiment that separates the true mechanism;
- experiments required to eliminate incorrect candidates;
- acquisition cost;
- target-counterfactual error after acquisition.

### Numerical validity
- witness change over timestep refinement;
- witness change over particle/grid refinement;
- transfer-family disagreement;
- sign/orientation stability of the witness.

## 6. Frozen scientific rules

- no thresholds may be tuned on the final confirmatory domain;
- GAUGE fixture-overlap result is discovery/motivation, not confirmatory evidence;
- positive controls do not count as publication results;
- a zero-coverage "safe" policy is a failure;
- a witness that fails numerical survival cannot be interpreted physically;
- a repair may not absorb a numerical error by fitting material parameters;
- DCS cannot claim novelty for Taylor expansions, Volterra kernels, higher-order
  FRFs, Möbius inversion, metamorphic testing, OED, active model discrimination,
  or renormalization individually;
- negative results remain in the repository.

## 7. Tonight's execution order

1. pass D0/D1 CI;
2. implement solver-native D2 generator;
3. implement second-order mixed/symmetric MPM stencils;
4. benchmark raw-vs-dark-field SNR on fresh synthetic truths;
5. add Fisher/max-output/random experiment selectors;
6. run first active-selection ablation;
7. implement GAUGE retrospective D4 analysis without fitting;
8. freeze a fresh confirmatory D5 protocol;
9. only then start paper prose.

## 8. Flagship claim shape if successful

Allowed claim shape:

> DCS detects and actively falsifies physically deceptive repairs in captured
> executable worlds by constructing signed intervention ensembles that suppress
> lower-order/common response and expose irreducible mechanism interactions; the
> resulting witness is accepted only if it survives numerical fidelity checks.

Forbidden shortcuts:
- "first higher-order physics";
- "first nonlinear system identification";
- "first intervention decomposition";
- "first active model discrimination";
- "first metamorphic simulator test";
- "guaranteed physical correctness".

# DCS Benchmark Plan and Result Ledger

Date: 2026-09-20
Status: preregistered implementation plan; update this file only by appending results.

## A. Benchmark ladder

| Stage | Data | Purpose | Selection labels available? | Counts as paper evidence? |
|---|---|---|---:|---:|
| D0 | deterministic polynomial controls | implementation correctness | yes/constructed | no |
| D1 | 64 analytic deceptive repairs | positive control | yes/constructed | no |
| D2a | Vulkax MPM synthetic fresh truth | repair falsification | yes | yes, controlled |
| D2b | Vulkax MPM cause families | cause generalization | yes | yes, controlled |
| D3 | active intervention selection | acquisition efficiency | yes | yes |
| D4 | existing GAUGE fixture case | retrospective sanity check | partially | motivation only |
| D5 | fresh measured regime | confirmatory validation | no direct parameter truth | **required** |
| D6 | captured Gaussian world | representation localization/system result | mixed | supporting |

## B. D2 solver-native truth matrix

Fresh seeds must be generated after thresholds are frozen.

Truth axes:
- E: off-grid continuous values in a frozen range;
- nu: off-grid continuous values;
- boundary/support: endpoint, finite support, compliant support when implemented;
- transfer: APIC / PIC / FLIP / APIC-FLIP;
- dt: converged-reference and deliberately coarse variants;
- particle/grid scale;
- model family: Neo-Hookean plus next independently implemented constitutive family;
- observation support/correspondence perturbations.

Primitive interventions:
- +/- shear;
- +/- axial strain;
- +/- transverse strain;
- mixed shear+axial;
- mixed shear+transverse;
- boundary displacement change;
- calibrated local force/impulse when available.

Mandatory candidate classes:
1. correct family / wrong parameters;
2. wrong support / compensated parameters;
3. wrong transfer;
4. coarse numerics;
5. wrong constitutive family;
6. wrong correspondence/support;
7. correctly repaired model.

Minimum initial matrix:
- >=256 fresh synthetic worlds;
- >=4 candidate repairs/world where computationally practical;
- no final-confirmatory threshold tuning from these labels.

## C. Frozen D2 headline metrics

Primary:
- deceptive repair AUROC;
- deceptive repair AUPRC;
- false accept rate at >=50% usable coverage;
- median mechanism order of contact;
- dark-field SNR / raw-response SNR;
- cause-stratified false accept rate.

Initial advancement gate to D5:
- DCS false accept <=10% at >=50% coverage on fresh synthetic domain;
- DCS materially improves AUPRC over ordinary held-out residual;
- DCS improves median separating SNR over raw model-output disagreement;
- no single cause family accounts for >80% of all successful detections.

These thresholds are a research advancement gate, not a universal physical guarantee.

## D. Active acquisition benchmark

Given a fixed candidate set, each selector chooses one allowed experiment.

Selectors:
1. DCS dark-field dispersion / uncertainty / cost;
2. Fisher/Jacobian information;
3. raw maximum model-output separation;
4. maximum predicted motion;
5. random;
6. safe active model-discrimination baseline where implementation permits.

Evaluation:
- whether chosen experiment separates the true model from deceptive candidate;
- target counterfactual error after acquisition;
- number/cost of experiments to reach a frozen candidate-set size;
- safety/feasibility violations;
- numerical-witness survival.

Success gate:
- DCS must beat random and maximum-motion clearly;
- DCS must add value over Fisher/Jacobian and raw output separation on deceptive
  cases where first-order responses are intentionally similar.

If DCS only ties generic active model discrimination, the active-selection claim is
demoted and DCS remains a falsification representation only.

## E. GAUGE retrospective protocol

Important: the existing fixture-overlap case is already known and cannot validate
generalization.

Questions:
1. Does a symmetric dark-field witness expose the finite-support metric mirage?
2. At what response order do endpoint and finite-support candidates separate?
3. Does the witness conclusion survive the exact frozen numerical ladder?
4. Is the witness spatially concentrated near boundary/support or distributed?

Candidate observable channels:
- marker displacement field;
- face-area response;
- signed longitudinal strain;
- transverse/shear strain;
- spatially resolved deformation descriptors.

Do not:
- refit E/nu;
- tune stencil amplitude on the final even repeats;
- call success on ordinary RMSE alone.

## F. Fresh real benchmark D5

Before opening the final data:
- freeze primitive interventions;
- freeze stencil-generation algorithm;
- freeze numerical adequacy gate;
- freeze ordinary baselines;
- freeze DCS thresholds or threshold-selection method.

Preferred measured scenarios:
1. unseen GAUGE deformation/material task with independent intervention richness;
2. IRIS or another public measured dynamics benchmark;
3. controlled new deformable bench if public data cannot support independent edits.

Minimum evidence needed:
- one real deceptive-repair detection;
- one real non-deceptive repair accepted rather than universally refused;
- one example where DCS admits uncertainty because the witness is numerically or
  observationally unresolved.

## G. Ablation table to fill

| Method | Deceptive AUROC | AUPRC | False accept @50% cov. | Median SNR | Experiments | Notes |
|---|---:|---:|---:|---:|---:|---|
| Held-out RMSE | TBD | TBD | TBD | n/a | 0 | |
| Physical scalar metric | TBD | TBD | TBD | n/a | 0 | |
| Identifiability s_min | TBD | TBD | TBD | n/a | 0 | |
| Ensemble disagreement | TBD | TBD | TBD | TBD | 0 | |
| Fisher/Jacobian OED | TBD | TBD | TBD | TBD | TBD | |
| Raw max model separation | TBD | TBD | TBD | TBD | TBD | |
| DCS k=2 fixed | TBD | TBD | TBD | TBD | TBD | |
| DCS adaptive k | TBD | TBD | TBD | TBD | TBD | |
| DCS no numerical guard | TBD | TBD | TBD | TBD | TBD | |
| Full DCS | TBD | TBD | TBD | TBD | TBD | |

## H. Cause-stratified table to fill

| Failure cause | Cases | RMSE false accepts | DCS false accepts | Median separating order |
|---|---:|---:|---:|---:|
| parameter | TBD | TBD | TBD | TBD |
| support/boundary | TBD | TBD | TBD | TBD |
| transfer | TBD | TBD | TBD | TBD |
| timestep | TBD | TBD | TBD | TBD |
| spatial discretization | TBD | TBD | TBD | TBD |
| constitutive | TBD | TBD | TBD | TBD |
| correspondence | TBD | TBD | TBD | TBD |

## I. Required figures/animations

1. **GAUGE metric mirage:** ordinary errors improve while longitudinal mechanism worsens.
2. **Dark-field cancellation visual:** raw motion vs lower-order-cancelled residual.
3. **Mechanism order of contact:** two models visually coincide at k=1 and split at k=2/3.
4. **Spatial dark-field map:** Gaussian/particle regions colored by witness residual.
5. **Noise curve:** raw vs DCS separation as observation noise increases.
6. **Numerical flow:** witness vs timestep/grid scale; unresolved witnesses visibly refused.
7. **Active selection:** candidate worlds before/after DCS-selected intervention.
8. **Risk-coverage:** DCS vs RMSE/UQ/selective baselines.
9. **Ablation:** k=2, adaptive-k, no numerical guard, no active selection.
10. **Failure case:** a world DCS cannot separate, documented explicitly.

## J. Results ledger

### Existing motivating result: GAUGE finite-support definitive
Status: COMPLETE; discovery only.

Soft:
- face NRMSE 0.461275 -> 0.372667;
- marker RMSE 3.277 mm -> 3.060 mm;
- longitudinal abs error 0.043545 -> 0.051016.

Hard:
- face NRMSE 0.413863 -> 0.343322;
- marker RMSE 3.144 mm -> 3.111 mm;
- longitudinal abs error 0.036697 -> 0.040421.

Dual metric wins: 10/10.
Longitudinal mechanism wins: 0/10.
Decision: kill finite support as primary explanation; motivating deceptive-repair case.

### D0 mathematical regression
Status: CI pending.

### D1 analytic deceptive-repair positive control
Status: CI pending.

### D2+ results
Status: NOT RUN. Do not pre-populate expected wins.

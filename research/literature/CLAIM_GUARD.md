# Vulkax research claim guard

Updated: 2026-09-20

This file exists to prevent accidental novelty inflation.

## Do not claim as novel

- 3D Gaussian + MPM physical simulation.
- Gaussian-based material/property inference.
- monocular or multi-view video -> physical parameters.
- local Jacobian/sensitivity/condition-number identifiability.
- Fisher-information / Jacobian optimal experiment design.
- counterfactual prediction from an inferred physical model.
- changed-law counterfactual transfer from structured/factored physical models in the abstract (Wang et al., arXiv:2609.19674).
- hierarchical validation of simulated digital-twin counterfactuals in the abstract (Laudy, *Digital Twin Counterfactual Framework*, arXiv:2604.01325).
- explicit model discrepancy in inverse calibration.
- generic source-of-error indicators, model/mesh adequacy diagnosis, or adaptive model/mesh selection from full-field measurements; modified Constitutive Relation Error work already does this (Computational Mechanics 2025, DOI: 10.1007/s00466-025-02598-1).
- unknown/uncertain boundary-condition identification from interior/full-field displacement data, including joint hyperelastic material + boundary identification; this is established inverse-mechanics work (e.g. ASME J. Applied Mechanics 2018, DOI: 10.1115/1.4039170; BC-free inverse elasticity / coupled adjoint-state formulations in CMAME 2019).
- partial-field/sub-domain material identification specifically designed to handle uncertain or inaccessible supports/boundaries; established FEMU/M-CRE/Bayesian literature already treats this. Observation-support/body-support mismatch is therefore not a standalone novelty claim.
- model-form uncertainty embedded in digital-twin calibration in the abstract (Arcones et al., arXiv:2609.10171).
- abstention/selective prediction in the abstract.
- finite-sample selective-risk calibration in the abstract (Learn-Then-Test, arXiv:2110.01052; SCoRE, arXiv:2603.24704).
- numerical convergence testing in the abstract.
- held-out validation in the abstract.
- rollback/transaction semantics without a scientific mechanism/evaluation.

## Historical candidate mechanism — no longer a positive flagship lock

The intervention-specific governance family below is retained as provenance. It is
**not currently authorized as the positive flagship mechanism** after the full
DCS/D2/D3/D4V elimination sequence.

**Historical intervention-specific verified rewrite governance**:

1. receive a requested physical edit/intervention;
2. evaluate evidence sufficiency for that specific counterfactual;
3. diagnose failure class:
   - insufficient observations / identifiability,
   - numerical non-convergence,
   - transfer/discretization disagreement,
   - model-family inadequacy,
   - unsupported geometry/boundary assumptions;
   - observation-support/body-support mismatch (e.g. tracked markers do not reach the physical fixture);
4. if repairable, request the next measurement/intervention using established experiment-design machinery;
5. refit only after the new evidence is acquired;
6. evaluate a disjoint target intervention;
7. independently verify;
8. commit or rollback;
9. retain the failure reason/evidence in the certificate.

## Current evidence status

- same-domain synthetic closed-loop repair: promising;
- Validation-1: risk reduction but unsafe accepted > target;
- Validation-2: fixed absolute numerical threshold collapsed to zero coverage;
- Validation-3 adaptive computational-evidence repair restored **59.375% coverage** but still accepted **15.789% unsafe** cases versus the <=10% preregistered target; PIC was rejected 16/16, while constrained-nu/fine-APIC still leaked unsafe cases. The policy is not promoted and Validation-3 labels are not used for retuning;
- real GAUGE: current no-fit APIC + Neo-Hookean + proxy-geometry + prescribed-boundary instantiation loses to affine null on all 20 shearing repeats;
- GAUGE timestep refinement: dominant real-data miss is not explained by dt at the current stable baseline.
- GAUGE structural forensics: all signed gravity axes and a stable fine-resolution run completed. PIC is the only tested one-factor variant that improves both mean face-area NRMSE and mean marker RMSE versus the current Vulkax baseline, but it still loses badly to the zero-fit affine null; no structural variant unlocks inverse fitting.
- GAUGE constitutive×transfer matrix: 3 transfer schemes × 3 constitutive laws all preserve the soft/hard separation sign, but **0/9** beat the affine null for both materials. Constitutive-law choice is nearly negligible at this operating point; transfer scheme changes the error more, but does not repair model adequacy. Inverse fitting remains locked.
- GAUGE mode diagnostic on frozen run 35514461226: baseline Vulkax preserves much of average shear/transverse timing but over-amplifies shear/face-area heterogeneity and predicts the **wrong-sign longitudinal mean response** in both soft and hard foam. This is a mechanism-level inadequacy signal, not a Poynting-effect novelty claim.
- GAUGE fixture-envelope hypothesis: released foam aspect + mass/density imply a 50×50×200 mm body while the selected marker envelope spans only ~150–152 mm longitudinally. The historical proxy therefore put prescribed boundary layers at interior observation support. A no-fit released-asset geometry repair is pre-registered to require simultaneous benchmark, marker-trajectory, and mode-signature improvement before promotion.

## Publication claim rule

No sentence containing “novel”, “first”, “state of the art”, “reliable”, “verified”, “physically correct”, “material recovery”, or “generalizes” may be promoted from research notes into manuscript claims unless:
- direct prior-art search is complete for that exact mechanism;
- the relevant experiment has an independent held-out domain;
- numerical and model-form alternatives have been attacked;
- the repository contains the raw evidence and failure cases.


## Final DCS claim guard — 2026-09-20

The completed DCS sequence imposes additional hard restrictions.

Executed facts:
- D2 fresh validation: **0/16 resolved worlds**;
- D3 witness-space/adaptive-order: **0/6 resolved worlds**;
- D4V pair-specific repair veto: **36 proposals**, **14 deceptive**, **22
  beneficial**, but **0% resolved coverage** for DCS and all matched verification
  baselines at the inherited credibility threshold;
- GAUGE retrospective: ordinary face+marker metrics prefer overlap **10/10**,
  marker dark-field endpoint wins **0/10**, longitudinal dark-field endpoint wins
  **9/10**.

Therefore do not claim:
- that DCS prospectively verifies physical repairs;
- that DCS outperforms Fisher/raw matched-cost baselines;
- that DCS provides useful deployment coverage under the tested regime;
- that adaptive response order solves the observability problem;
- that the GAUGE retrospective demonstrates generalization;
- that implementation completion implies physical verification.

Currently allowed high-level statement:

> Vulkax implements and systematically falsifies several mechanism-selective
> counterfactual verification strategies. The tested DCS variants did not achieve
> resolved prospective verification coverage on fresh synthetic partitions, while a
> retrospective GAUGE analysis exposed a channel-specific contradiction between
> aggregate observation metrics and longitudinal mechanism response.

A future positive claim requires:
1. a new physical-information channel;
2. a fresh discovery partition;
3. a newly frozen untouched validation partition;
4. then a fresh measured D5 confirmation.

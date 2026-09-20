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
- explicit model discrepancy in inverse calibration.
- abstention/selective prediction in the abstract.
- numerical convergence testing in the abstract.
- held-out validation in the abstract.
- rollback/transaction semantics without a scientific mechanism/evaluation.

## Candidate research mechanism that is still alive

**Intervention-specific verified rewrite governance**:

1. receive a requested physical edit/intervention;
2. evaluate evidence sufficiency for that specific counterfactual;
3. diagnose failure class:
   - insufficient observations / identifiability,
   - numerical non-convergence,
   - transfer/discretization disagreement,
   - model-family inadequacy,
   - unsupported geometry/boundary assumptions;
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
- real GAUGE: current no-fit APIC + Neo-Hookean + proxy-geometry + prescribed-boundary instantiation loses to affine null on all 20 shearing repeats;
- GAUGE timestep refinement: dominant real-data miss is not explained by dt at the current stable baseline.

## Publication claim rule

No sentence containing “novel”, “first”, “state of the art”, “reliable”, “verified”, “physically correct”, “material recovery”, or “generalizes” may be promoted from research notes into manuscript claims unless:
- direct prior-art search is complete for that exact mechanism;
- the relevant experiment has an independent held-out domain;
- numerical and model-form alternatives have been attacked;
- the repository contains the raw evidence and failure cases.

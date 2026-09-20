# Observation-support mismatch positive-control protocol

## Status

Synthetic solver-integrated mechanism test. **Not a novelty experiment.**

Inverse-mechanics literature already studies:

- unknown boundary-condition recovery from interior/full-field displacement measurements;
- joint boundary-condition and hyperelastic material identification;
- BC-free inverse elasticity;
- partial-field/sub-domain material identification under uncertain supports.

Therefore this probe can only establish that observation-support mismatch is a useful **planted failure family** for Vulkax refusal/remediation experiments.

## Synthetic construction

Truth:

- physical body length: 200 mm;
- fixed volume: 0.0005 m^3;
- density: 1000 kg/m^3;
- Young's modulus: 80 kPa;
- Poisson ratio: 0.30;
- APIC MPM;
- end-displacement controlled shear;
- visible marker strip spans only the central 150 mm.

Wrong world:

- assumes the 150 mm marker span is the complete body length;
- preserves the same total volume, therefore inflating cross-section;
- uses the same observation marker locations;
- is allowed to refit Young's modulus only over a fixed predeclared grid.

Training intervention:

- 15 mm end shear.

Independent target intervention:

- 30 mm end shear.

The wrong world is not allowed to change body length, Poisson ratio, transfer scheme, volume, density, marker support, or boundary type during fitting.

## Frozen positive-control rule

The failure family survives only if:

1. correct-support model recovers the exact 80 kPa grid truth;
2. correct-support training and target errors are numerically negligible;
3. truncated-support model achieves <=10% normalized training error;
4. truncated-support model exceeds 10% normalized error on the unseen larger-shear target;
5. target error is at least 2x its own normalized training error;
6. neither world inverts;
7. prescribed boundary projection remains exact.

If the truncated world cannot fit the training intervention, then support mismatch is too obvious in this synthetic regime and does **not** validate the intended hidden-confound failure family.

## Interpretation

A pass means only:

> under at least one controlled nonlinear-solver regime, an incorrect physical-support assumption can be partially absorbed by material refitting and exposed more clearly by an unseen intervention.

A pass does not mean:

- this is novel;
- GAUGE has this exact failure cause;
- the refitted material is identifiable;
- Vulkax can already diagnose the cause from real observations.

The next step after a pass is a **cause-classification** test against other planted failures, not a manuscript claim.

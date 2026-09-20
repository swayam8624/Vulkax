# GAUGE observation-mapping sensitivity protocol — frozen before results

## Motivation

The current GAUGE no-fit forward path contains two conceptually separate maps:

1. MPM evolves the volumetric physical particles.
2. A fixed affine-MLS embedding maps those particles back to the observed surface-marker/Gaussian positions.

In the current implementation, the reconstructed Gaussian/marker cloud does **not** feed back into the MPM update. Therefore changing MLS neighbor support changes only the observation reconstruction, not the simulated physical state.

The historical forward path uses 24 MPM neighbors per observed marker.

The frozen affine/non-affine baseline shows:

- median global deformation-gradient error around 10–12%;
- predicted non-affine marker amplitude about 1.5x measured;
- predicted non-affine face-area amplitude about 2.4–2.6x measured.

This motivates a direct layer-isolation test.

## Candidate mapping supports

Run the same frozen representative soft/hard GAUGE shearing worlds with:

- 8 neighbors;
- 12 neighbors;
- 16 neighbors;
- 24 neighbors — historical control;
- 32 neighbors;
- 48 neighbors.

Everything else remains exactly fixed:

- material metadata;
- geometry mode;
- grid/particle resolution;
- boundary layers;
- APIC;
- constitutive model;
- gravity;
- driver trajectory;
- timestep.

No neighbor count is selected from target error.

## Required invariance check

Because mapping does not feed back into MPM, solver evidence must be identical across mapping supports up to serialization precision:

- minimum J;
- maximum actual timestep;
- total substeps;
- grid dimensions/cell size;
- momentum-accounting diagnostics;
- accumulated constraint-impulse diagnostic.

If physical solver evidence changes, classify the test as a harness failure.

## Mechanism metrics

For every support size and material:

- ordinary marker-position RMSE;
- benchmark-native face-area NRMSE;
- median normalized global affine deformation-gradient error;
- predicted/measured non-affine marker-amplitude ratio;
- predicted/measured non-affine face-area-amplitude ratio;
- non-affine residual-vector errors.

## Frozen interpretation

### Observation-mapping-sensitive local failure

Support this interpretation only if:

- changing mapping support materially changes local/non-affine amplitude errors in the same direction for both materials;
- at least one non-historical support moves both non-affine marker and face ratios closer to 1 for both materials;
- global affine deformation-gradient error changes much less than the local/non-affine quantities;
- physical solver evidence is invariant.

This remains a forensic result. The best support is **not** automatically adopted.

### Observation mapping not dominant

If local/non-affine errors remain large and nearly unchanged across support sizes, demote MLS support choice as a primary explanation and return focus to physical/boundary/model structure.

## Guardrails

- This is not hyperparameter tuning.
- No material parameter is fit.
- No neighbor-count winner unlocks inverse fitting.
- Generic correspondence/support selection is not a novelty claim.
- Any future adoption requires a separately justified correspondence rule and fresh held-out evaluation.

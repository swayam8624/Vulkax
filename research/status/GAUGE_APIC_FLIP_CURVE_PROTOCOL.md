# GAUGE APIC–FLIP transfer-curve protocol — frozen before results

## Motivation

Historical structural forensics produced a scale-dependent transfer signature:

- pure FLIP reduced both non-affine marker and face over-amplification in the archived soft/hard GAUGE predictions;
- its global affine deformation-gradient error worsened relative to APIC;
- PIC changed the global field differently and did not repair both local ratios.

Vulkax already implements the standard `APIC_FLIP` transfer path:

- APIC-style affine particle-to-grid transfer is retained;
- grid-to-particle velocity is blended between PIC and FLIP;
- affine velocity is retained.

The GAUGE forward harness has not yet exposed this existing solver control.

## Fixed curve

Evaluate the frozen representative soft/hard no-fit world at:

- FLIP blend 0.00;
- FLIP blend 0.25;
- FLIP blend 0.50;
- FLIP blend 0.75;
- FLIP blend 1.00.

Everything else remains fixed to the historical APIC baseline:

- measured-aspect geometry for this diagnostic;
- GAUGE material metadata;
- zero gravity;
- one prescribed boundary layer;
- 5 x 5 x 13 particle resolution;
- historical stable timestep;
- 24-neighbor marker mapping;
- Neo-Hookean log-J.

No blend is selected from target error.

## Harness identity control

The `APIC_FLIP` arm at blend 0 must reproduce historical `APIC` to serialization/numerical tolerance because both use:

- affine P2G;
- PIC velocity G2P;
- affine-velocity update.

If blend 0 materially differs from APIC, classify the experiment as a harness/implementation discrepancy before interpreting the curve.

## Metrics

For each blend and material:

### Ordinary
- marker-position RMSE;
- benchmark-native face-area NRMSE.

### Macro
- median normalized global deformation-gradient error;
- final determinant error.

### Local
- predicted/measured non-affine marker-amplitude ratio;
- predicted/measured non-affine face-area-amplitude ratio;
- non-affine residual-vector errors.

### Numerical
- minimum J;
- timestep evidence;
- constraint/momentum accounting.

## Frozen interpretation

A blend is a **forensic balanced point** only if, versus blend 0:

- both non-affine marker and face ratio distances-to-one improve for both materials;
- median global F error changes by no more than 10% relative for either material;
- both ordinary marker RMSE and face-area NRMSE improve in group mean;
- no inversion or new stability failure occurs.

This label does **not** authorize adoption.

The complete blend curve should also be retained even if no balanced point exists. A clear tradeoff is itself evidence that transfer numerics influence the local residual but do not solve model adequacy.

## Guardrails

- APIC–FLIP blending is established numerical machinery, not novelty.
- This is a mechanism curve, not parameter fitting.
- No blend unlocks inverse material fitting.
- Any production/default transfer decision would require a target-independent numerical rule and fresh held-out validation.

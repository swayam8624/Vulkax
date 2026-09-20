# GAUGE structural mechanism-fingerprint protocol

## Purpose

The existing one-factor GAUGE structural sweep evaluates:

- baseline APIC;
- finer spatial resolution with stability-motivated smaller dt;
- two prescribed boundary layers;
- PIC;
- FLIP;
- square-cross-section geometry;
- released-asset-aspect geometry;
- positive/negative gravity on each axis.

A single face-area NRMSE cannot explain *why* a variant changes the result. This protocol re-analyzes the already generated marker predictions without launching new physical simulations.

## Decomposition

For every successful structural arm and both materials, compute:

### Macro scale
- median normalized best-affine deformation-gradient error;
- final determinant error.

### Local scale
- predicted/measured non-affine marker RMS ratio;
- predicted/measured non-affine face-area RMS ratio;
- non-affine marker residual-vector RMSE;
- non-affine face residual-vector RMSE.

### Ordinary guard
- mean marker-position RMSE;
- mean benchmark-native face-area NRMSE.

## Fingerprint categories

Relative to historical baseline:

- **macro-dominant change**: macro F error changes while local-ratio distances-to-one do not both improve;
- **local-dominant change**: both local amplitude-ratio distances improve while macro F error is comparatively stable;
- **macro+local change**: both scales improve;
- **metric tradeoff**: one ordinary metric improves while the other worsens;
- **no useful movement**: neither macro nor local evidence improves consistently.

This classification is descriptive and per-variant. No arm is selected as a new default.

## Expected diagnostic roles

These are hypotheses, not assumed outcomes:

- spatial resolution primarily tests discretization-driven local structure;
- PIC/FLIP primarily test transfer-induced local structure/dissipation;
- released-asset geometry primarily tests body/fixture support;
- gravity axes primarily test missing world-frame/body-force assumptions;
- boundary-layer thickness tests hard-constraint support.

If the observed fingerprints contradict these expectations, retain the contradiction.

## Guardrail

The same prediction files are re-analyzed; no material parameter, geometry dimension, timestep, or structural setting is chosen using this decomposition. Generic ablation/model diagnosis is not a novelty claim. The result exists to decide which failure family deserves an independently designed next experiment.

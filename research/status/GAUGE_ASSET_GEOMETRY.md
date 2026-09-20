# GAUGE published-asset geometry falsification

## Why this branch exists

The frozen GAUGE foam-shearing no-fit test currently constructs a rectangular MPM body from marker support plus mass/density. GAUGE also publishes a simulation-ready `foam.obj` and a task translation for the foam. Marker support is an observation layout, not an object-geometry specification, so using its extrema as the body extent can confound a simulator-family adequacy test.

This branch tests that structural assumption **before any inverse material fitting**.

## Frozen question

> Does replacing the historical marker-derived geometry proxy with the published GAUGE foam-asset bounding box and task pose materially improve no-fit prediction under the same measured E, nu, density, mass and measured driver trajectory?

## Pre-registered design

- Data: the already frozen representative soft/hard foam-shearing repeats.
- Material values: unchanged GAUGE metadata.
- Driver: unchanged measured base trajectory.
- Constitutive law: Neo-Hookean log-J.
- Boundary: unchanged one-layer prescribed ends.
- Gravity: zero.
- Geometry conditions:
  1. historical marker-span + mass/density proxy;
  2. published `assets/obj/foam.obj` bounding box positioned by GAUGE task translation.
- Transfer schemes: APIC, PIC, FLIP.
- No trajectory residual is used to choose geometry, material values, transfer, or representative trial.

## Observables

The same dual-metric guard remains active:

1. benchmark-native marker-face area trajectory NRMSE;
2. absolute marker-position RMSE.

The affine kinematic null remains the external gate.

## Decision rule

A geometry variant is **not** permitted to unlock material fitting merely because it improves one metric or ranks first. If a published-asset variant beats the affine null for both materials and preserves the measured soft-vs-hard separation sign, it only earns an all-20-repeat replication. Otherwise asset-bbox geometry is recorded as insufficient and inverse fitting stays locked.

## Important limitation

This experiment uses the published mesh **bounding box** to define the regular MPM body. It does not yet voxelize the full surface mesh. Therefore a failure does not prove that exact GAUGE geometry is irrelevant; it only falsifies the hypothesis that the dominant miss is caused by gross cuboid dimensions/pose.

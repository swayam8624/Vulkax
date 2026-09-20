# GAUGE marker-correspondence falsification

## Trigger

The published-asset geometry experiment produced a large, dual-metric improvement but still failed the affine-null gate. The next structural mismatch is in **how the simulated trajectory is observed**.

GAUGE's physics-engine protocol assigns every initial motion-capture marker to a **unique nearby simulated vertex or particle using Hungarian matching**, requires initial matching error below 1 cm, and tracks those assigned elements. The current Vulkax adapter instead moves each marker through a 24-neighbor MLS embedding.

## Frozen question

> How much of the remaining GAUGE foam-shearing miss is caused by an observation/correspondence mismatch rather than the MPM material dynamics?

## Pre-registered 2 x 2 design

All physical controls remain frozen:
- published `foam.obj` bounding-box geometry and GAUGE task pose;
- measured E, nu, density and mass;
- measured driver;
- APIC transfer;
- Neo-Hookean log-J;
- zero gravity;
- one prescribed boundary layer.

Factors:

1. marker tracking:
   - historical Vulkax MLS24;
   - GAUGE-style one-to-one minimum-cost particle assignment (Hungarian).
2. resolution:
   - 5 x 5 x 13, dt = 1/12000 s;
   - 7 x 7 x 19, dt = 1/24000 s.

The fine grid is **not** selected from trajectory error. It is the existing CFL-safe fine setting and is included to test whether the published <1 cm correspondence requirement can be met.

## Protocol gate

For a Hungarian condition to count as benchmark-aligned, its maximum initial marker-to-particle distance must be < 0.01 m.

## Scientific gate

A benchmark-aligned condition must also:
- beat the affine null for both soft and hard material on the benchmark-native face-area trajectory;
- preserve the measured soft-vs-hard separation sign.

Marker-position RMSE is retained as an anti-metric-gaming diagnostic.

A representative-trial pass still does **not** unlock inverse material fitting. It only earns all-20-repeat replication.

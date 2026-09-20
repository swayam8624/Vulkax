# GAUGE held-out fixture-overlap protocol — staged, not yet authorized

This protocol is executable **only if** `GAUGE_EFFECTIVE_SPAN_PROTOCOL.md` passes its frozen all-repeat stability criteria.

## Scientific question

If measured kinematics support a stable effective deforming span shorter than the full 200 mm foam body, does representing that difference as finite fixture overlap improve no-fit GAUGE shearing prediction on held-out repeats?

This is a boundary-support test, not a material-calibration experiment.

## Data split

- calibration: odd-numbered shearing repeats (1,3,5,7,9) from both soft and hard;
- held-out evaluation: even-numbered repeats (2,4,6,8,10) from both materials.

The split is fixed by repeat parity and cannot be changed after results.

## Calibration quantity

Use only the measured effective-span diagnostic.

Primary calibration estimate:

- motion threshold: **50% of each trial's driver peak**;
- per trial: median effective deforming span across qualifying frames;
- one shared span for both materials: median over all odd soft + odd hard trials.

The 30% and 70% thresholds are sensitivity checks only. They may reject the scalar-span hypothesis but may not replace the primary 50% estimate after target errors are seen.

No Vulkax simulation error enters this calibration.

## Physical representation

Keep full released-asset geometry fixed:

- volume from measured mass/density;
- aspect ratio 1:1:4 from released GAUGE foam asset;
- physical body dimensions 50 x 50 x 200 mm.

Interpret the difference between 200 mm body length and calibrated effective free span as symmetric fixture overlap:

[
t_{fixture} = (0.200 - L_{effective})/2.
]

This is a modeling hypothesis, not an assertion about literal clamp hardware.

## Numerical control

To resolve a ~10–20 mm overlap without conflating it with a one-particle endpoint condition:

- APIC;
- compressible Neo-Hookean log-J material, exactly as the historical baseline;
- GAUGE E, nu, density and mass unchanged;
- zero gravity in the primary comparison;
- nCross = 7;
- nLong = 49;
- requested dt = 1/48000 s;
- same Gaussian↔MPM correspondence for both arms.

Before using held-out GAUGE labels, run a synthetic prescribed-slab resolution/control test at the same nCross/nLong/dt to confirm exact boundary projection and non-inversion.

## Comparator arms

For every held-out repeat:

**A. Endpoint-only control**
- released 200 mm body;
- same 7x7x49 discretization;
- prescribe only the terminal particle plane at each end.

**B. Calibration-derived fixture overlap**
- same body, material, transfer, grid logic and dt;
- prescribe all particles whose rest longitudinal coordinate lies inside the odd-repeat-derived overlap thickness at each end.

The only intended difference is boundary-support thickness.

## Primary metrics

Both must improve; one metric cannot hide degradation in the other:

1. benchmark-native face-area trajectory NRMSE;
2. full marker-position RMSE.

Mechanism-level secondary checks:

- final longitudinal mean-strain signed error;
- face-area RMS over-amplification;
- shear-RMS peak-amplitude ratio;
- average shear/transverse timing correlation.

## Frozen success rule

The overlap model advances only if, across the 10 held-out repeats:

- it improves both primary metrics versus endpoint-only control in at least 8/10 repeats;
- mean face-area NRMSE decreases;
- mean marker RMSE decreases;
- neither material has fewer than 4/5 dual-metric wins;
- longitudinal mean-strain signed error improves on both material-group means;
- no new inversion/stability failure occurs.

Passing does **not** unlock inverse material fitting yet. It authorizes a fresh no-fit adequacy gate versus the affine null.

## Kill conditions

Kill finite fixture overlap as the primary explanation if:

- the effective-span prerequisite fails;
- held-out dual-metric wins < 8/10;
- improvement is isolated to one material;
- face-area gain comes with marker degradation;
- the wrong-sign longitudinal response is unchanged or worsened;
- required fine resolution is numerically unstable.

All failures remain in the research record.

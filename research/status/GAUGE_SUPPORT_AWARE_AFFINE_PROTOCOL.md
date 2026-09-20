# GAUGE support-aware affine null — held-out protocol

## Why this test comes before another simulator change

The historical affine null distributes driver displacement from 0 at the minimum observed marker coordinate to 1 at the maximum observed marker coordinate. The measured-only effective-span diagnostic suggests that the marker envelope may be an interior observation window rather than the actual deforming span.

Before changing MPM boundaries, test whether correcting **only the kinematic support assumption** explains a substantial fraction of the measured signal.

## Split and calibration

- calibration repeats: odd trial IDs 1,3,5,7,9 from both soft and hard foam;
- held-out repeats: even IDs 2,4,6,8,10;
- effective-span estimator: per-trial 50%-motion-threshold estimate from the frozen measured-only diagnostic;
- one shared calibration span: median across all odd soft + odd hard repeats.

No simulation result and no held-out trajectory error is used to choose the span.

For each held-out trial, center the calibrated span on that trial's **initial marker-envelope center**. Centering uses frame-0 geometry only and does not use later motion.

## Two analytic nulls

### A. Marker-envelope affine null

Historical baseline:

[
alpha_i = (x_i-x_{min})/(x_{max}-x_{min}).
]

### B. Support-aware affine null

[
alpha_i = (x_i-(c-L/2))/L
]

where (L) is the odd-repeat calibrated effective span and (c) is the held-out trial's frame-0 marker-envelope center.

Predicted marker motion is measured driver displacement multiplied by (alpha_i). No material model is used.

## Held-out metrics

For each of the 10 even repeats:

1. full-marker position RMSE;
2. GAUGE benchmark-native triangular-face-area trajectory NRMSE;
3. temporal correlation of the face-area trajectory.

## Frozen interpretation rule

The support-aware null is considered a meaningful boundary-support correction only if:

- it improves **both** marker RMSE and face-area NRMSE on at least 8/10 held-out repeats;
- each material has at least 4/5 dual-metric wins;
- mean marker RMSE improves;
- mean face-area NRMSE improves;
- no held-out trial requires clipping/extrapolation outside [0,1] by more than 0.15 at any observed marker in frame-0 geometry.

If these gates fail, a single scalar effective span is not sufficient to explain the measured kinematics.

## Scientific meaning

A pass would show that the historical zero-parameter affine null was itself structurally unfair because it treated observation support as physical support. It would **not** validate MPM, identify fixture mechanics, or constitute novelty.

A failure would be equally useful: it would force the next explanation toward non-affine boundary/contact behavior, constitutive coupling, or observation-to-volume correspondence rather than scalar support extent.

Inverse material fitting remains locked.

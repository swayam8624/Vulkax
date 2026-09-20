# GAUGE effective deforming-span protocol — frozen before all-repeat result

## Motivation

Two already frozen representative shearing trials suggested that the observed displacement field is almost affine along the foam's longitudinal marker coordinate, but its extrapolated zero-to-full-driver span is intermediate between:

- the measured marker envelope (~150–152 mm), and
- the full released foam asset length (200 mm).

Representative-trial values were approximately 173–175 mm with R^2 about 0.996–0.997.

Those two observations motivate this test but do **not** establish a reusable boundary-support parameter.

## Hypothesis

**H-GAUGE-EFFECTIVE-SPAN**

The measured shearing kinematics contain a repeatable effective deforming/free span that is distinct from both the visible marker envelope and the full physical body length.

Possible physical interpretations include fixture overlap, contact compliance/slip, or unobserved support outside the marker region. The diagnostic itself does not distinguish among them.

## Measurement

For every one of the 20 public GAUGE foam-shearing repeats:

1. infer the longest initial marker axis;
2. project each marker displacement onto the instantaneous measured driver direction;
3. normalize by current driver displacement;
4. fit an affine field versus initial longitudinal marker coordinate;
5. extrapolate virtual positions where the field equals 0 and 1;
6. define their separation as the effective deforming span.

Repeat the calculation using frames whose driver magnitude is at least 30%, 50%, and 70% of that trial's peak.

No Vulkax trajectory, material fit, or simulation error is consulted.

## Frozen survival criteria

The hypothesis is considered sufficiently stable to justify a **future independent boundary-model test** only if all of the following hold:

- median affine-field R^2 >= 0.98 for both materials at all three motion thresholds;
- within-material cross-repeat coefficient of variation of effective span <= 0.03;
- odd-repeat median used as a calibration-only estimate predicts even repeats with relative MAE <= 0.03;
- soft-vs-hard median effective-span difference <= 0.005 m at every threshold;
- pooled effective-span median varies by <= 0.005 m across the 30/50/70% motion thresholds.

These are diagnostic engineering gates, not universal physical constants.

## Kill conditions

Kill the reusable-effective-span direction if:

- the inferred span is strongly threshold dependent;
- repeat variation exceeds the frozen limits;
- soft and hard require materially different spans;
- affine field quality degrades below the R^2 gate;
- odd-repeat calibration fails even-repeat prediction.

A killed result remains useful evidence that boundary support cannot be summarized by one scalar span.

## What success would and would not mean

Success would justify testing a boundary-support model in which the **full 200 mm body geometry** and the **effective deforming span** are represented separately.

It would not prove:

- a clamp-overlap value;
- fixture contact mechanics;
- Young's modulus or Poisson ratio;
- model correctness;
- novelty.

Any later solver test must use a disjoint protocol: derive span without target simulation error, then evaluate no-fit predictions on held-out repeats. Inverse material fitting remains locked.

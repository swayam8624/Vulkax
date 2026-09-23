# Prospective GAUGE Real-Trajectory Validation Protocol — 2026-09-23

Status: validation-stage protocol. Final-test repeats remain unopened until a
publication-validation lock is created and checked.

## Dataset partition

For GAUGE foam stretching and compression, independently within each material:
- repeats 1–4: development only, used to estimate repeat/acquisition variance;
- repeats 5–7: validation;
- repeats 8–10: final test.

Previously analyzed foam-shearing repeats remain retrospective and are excluded
from this prospective lane.

## Controlled repair truth

The GAUGE metadata Young's modulus is treated as externally measured truth.

For each trial:
- baseline: E = 1.25 * E_true;
- SUPPORT repair: E = E_true;
- VETO repair: E = 1.60 * E_true;
- UNRESOLVED/placebo: E = baseline E.

Other released material parameters remain fixed to their measured metadata values.

This is not inverse fitting. The candidate values are fixed before observing any
validation/final-test Reality Probe score.

## Verification witness

The measured driver trajectory defines normalized rising-phase progress.
Responses are sampled at progress 0.25, 0.50, and 0.75.

Primary witness:
- second-order annihilating temporal contrast;
- normalized weights [0.25, -0.50, 0.25];
- cancels constant and linear response in the sampled progress coordinate.

Required baseline:
- raw concatenated marker response at the same three progress points.

## Uncertainty

Repeat/acquisition variance is estimated only from development repeats 1–4.
Numerical uncertainty is measured independently for baseline and repair using
nominal dt and refined dt/2 simulations.

For either witness family:

z = (error_baseline - error_repair) /
    sqrt(repeat_variance + numerical_baseline^2 + numerical_repair^2)

Decision rule:
- z >= +2: SUPPORT;
- z <= -2: VETO;
- otherwise: UNRESOLVED.

## Claim guard

This experiment validates a real measured trajectory holdout and the lower-order
annihilation principle. It is not described as an orthogonal sensor modality:
proposal and verification observations come from disjoint temporal response
regions of the same GAUGE measurement system.

Validation repeats do not count as final confirmatory evidence. Final-test repeats
may run only after the protocol/world manifest lock is checked.

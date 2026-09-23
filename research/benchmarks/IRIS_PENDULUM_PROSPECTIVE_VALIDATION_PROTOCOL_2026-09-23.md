# IRIS Pendulum Prospective Validation Protocol — 2026-09-23

Status: development + validation protocol. The 90-degree setting is reserved for
final test and must not be processed before a final-test lock is created.

## Dataset and partition

IRIS single-pendulum videos provide ten repeated real takes at each controlled
initial-angle setting and independently measured physical parameters.

This Reality Probe lane uses:

- `pendulum_20`, takes 01–10: **development**;
- `pendulum_45`, takes 01–10: **validation**;
- `pendulum_90`, takes 01–10: **final_test**, unopened before lock.

The existing public-data core download already contains take 01 from each setting.
The validation runner may download the remaining development/validation takes, but
must not request the remaining 90-degree final-test takes.

## Physical target

Primary target: pendulum rope length `L`.

The independently measured IRIS `parameters.json` value is used only as benchmark
truth. The verification score is computed from the video trajectory, not from that
truth value.

The controlled experimental initial angle is an admissible known input to the
physics probe.

## Video verification channel

A deterministic classical tracker operates on the real monocular video:

1. resize frames to a frozen analysis width;
2. estimate a static background from uniformly sampled frames;
3. construct a high-percentile frame-difference motion signal;
4. compute the horizontal weighted centroid of moving pixels;
5. interpolate short tracking gaps and reject runs with insufficient coverage;
6. estimate oscillation period from both FFT and autocorrelation;
7. derive same-direction zero-crossing cycle periods;
8. reject a take if period estimators disagree beyond the frozen quality gate.

No neural network, training, manual ROI, or per-video hand tuning is used.

## Pendulum model

The primary method uses the finite-amplitude simple-pendulum period

```text
T(L, theta_0) = 4 sqrt(L / g) K(sin(theta_0 / 2))
```

where `K` is the complete elliptic integral of the first kind, evaluated by the
arithmetic-geometric mean, and `g = 9.80665 m/s^2`.

Required baseline:

```text
T_small(L) = 2 pi sqrt(L / g)
```

which ignores the finite-amplitude correction.

## Controlled repair cases

For each take:

- baseline proposal: `L_base = 1.25 * L_true`;
- placebo: `L_candidate = L_base` -> UNRESOLVED;
- truth-control SUPPORT: `L_candidate = L_true`;
- truth-control VETO: `L_candidate = 1.60 * L_true`;
- dose-response SUPPORT candidates:
  `L_candidate/L_true = 1.25 - d`;
- dose-response VETO candidates:
  `L_candidate/L_true = 1.25 + d`;
- `d in {0.025, 0.05, 0.10, 0.20}`.

Truth labels are defined only by independent distance to the measured rope length,
never by the Reality Probe score.

## Standardized evidence score

For each observed full-cycle period `T_i`:

```text
delta_i =
    abs(T_i - T_pred(L_base))
  - abs(T_i - T_pred(L_candidate))
```

The evidence score is

```text
z = mean(delta_i) / sigma_delta
```

with

```text
sigma_delta = max(
    robust cycle-to-cycle scale of delta_i,
    1 / video_fps
)
```

The one-frame floor prevents artificially infinite confidence when paired residual
differences happen to be nearly constant.

Frozen decision rule:

- `z >= +2`: SUPPORT;
- `z <= -2`: VETO;
- otherwise: UNRESOLVED.

## Development gate

The 20-degree development partition is processed first with exactly the same
tracker and physics code.

Before the 45-degree validation partition may execute:

- at least 8/10 development takes must pass tracking quality;
- median finite-amplitude rope-length relative error must be <= 20%;
- every emitted record must satisfy schema/provenance checks.

Failure of this gate stops the campaign. Validation is not opened and the tracker
may be improved using development data only.

## Validation and final-test policy

The 45-degree validation partition may be used to decide whether this IRIS lane is
scientifically viable, but not to alter the global |z|=2 threshold for final claims.

The 90-degree final-test partition must remain unopened until:

1. the tracker and candidate schedule are frozen;
2. the world manifest and method configuration are hash-locked;
3. the final-test runner verifies that lock.

No final-test scene may be substituted after result inspection.

## Claim guard

This lane tests whether an independent period-based physical interrogation can
adjudicate controlled rope-length repairs in real monocular video.

It does not claim:
- that monocular video provides a fully independent sensor modality;
- that the simple-pendulum model explains collision or multi-body IRIS classes;
- that success on pendulum length validates every Reality Probe physical channel.

Channel dependence is recorded explicitly as same-video / independent-observable.

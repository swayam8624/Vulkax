# IRIS Free-Fall Blind Replication Protocol — 2026-09-23

Purpose: prospectively replicate the three-way physical rewrite verification contract
on a non-pendulum equation family while eliminating the prior "take 01 already local"
objection.

## Population

Dataset revision is pinned to the same IRIS revision used by the earlier campaign.

- development: `dropping_ball/drop_50/{02,03,04,05}`
- validation: `dropping_ball/drop_100/{02,03,04,05}`
- final test: `dropping_ball/drop_150/{02,03,04,05,06,07,08,09,10}`

**Take 01 is forbidden** because the original core profile downloaded it before this
replication was designed.

The nine final videos may not be downloaded by the blind-domain runner before the
final-domain lock is created and checked.

## Physical observable

A deterministic static-camera tracker extracts the vertical trajectory of the ball.
The known controlled drop height calibrates pixels to meters. The free-fall segment
is selected by frozen motion/monotonicity rules before any candidate is evaluated.

The target physical quantity is vertical acceleration `g`; canonical truth for the
controlled benchmark is `9.80665 m/s^2`.

## Candidate schedule

- baseline: `1.25 g`
- SUPPORT truth control: `1.00 g`
- VETO truth control: `1.60 g`
- placebo: `1.25 g`
- SUPPORT doses: `(1.25-d)g`
- VETO doses: `(1.25+d)g`
- `d in {0.025, 0.05, 0.10, 0.20}`

## Probe

For each candidate acceleration, nuisance initial position and velocity are fit on
the first 40% of the detected free-fall interval. No acceleration parameter is fit.

The primary verifier evaluates candidate-vs-baseline absolute trajectory residual
on the disjoint final 60% of the free-fall interval. Frame residual improvements are
aggregated into temporal blocks, and the reported standardized evidence score is:

```
S = mean(block_improvements) /
    max(robust_scale(block_improvements)/sqrt(B), one_pixel_in_meters)
```

The frozen decision rule is unchanged:

- `S >= +2`: SUPPORT
- `S <= -2`: VETO
- otherwise: UNRESOLVED

The paper should call this a **standardized evidence score**, not assume a
standard-normal z statistic.

Required baseline: endpoint-only kinematic residual using the same fitted nuisance
state and candidate accelerations.

## Development gate

Before validation:
- >=3/4 development videos pass tracking/free-fall quality;
- median direct acceleration relative error <=20%;
- all candidate records/schema checks pass.

## Validation gate before final download

Before the nine final videos may be downloaded:
- >=3/4 validation videos pass quality;
- truth controls >=75% correct for the primary method;
- placebo false assertion rate = 0;
- expected score direction >=90% across directional controlled cases;
- no code/config change after the validation freeze except result-only records.

Failure stops the blind final campaign. The final files remain unrequested.

## Claim boundary

This domain tests replication of the verification contract on a different real-video
equation family. It does not make gravity estimation itself novel and does not
replace the locked pendulum result.

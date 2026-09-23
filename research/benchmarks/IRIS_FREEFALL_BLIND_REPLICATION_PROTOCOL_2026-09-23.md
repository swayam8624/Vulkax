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

## Development-only tracker revision after failed v1 gate

The first `drop_50/02..05` development execution on 2026-09-23 failed the
predeclared acceleration gate (median relative error 98.56%) even though all four
videos tracked and the controlled repair directions were ordered correctly. The v1
event selector used the **longest** run between global position quantiles; inspection
of the algorithm showed that a slow reset/handling traversal could therefore be
selected instead of the ballistic descent.

Because this occurred on the development split, before any free-fall validation
result was analyzed and before any final-test video was opened, tracker revision 2
is permitted as development-stage method design.

Revision 2 is frozen before validation:
- test both image-axis directions;
- identify candidate 10%-90% traversals;
- require >=80% monotone consistency;
- select the **fastest** credible traversal, which is the ballistic release rather
  than slow repositioning;
- estimate full fall duration from crossing times fitted to
  `t(p) = t0 + T sqrt(p)`;
- use the independently measured `drop_height` from IRIS as a known geometric
  input, not as the target acceleration;
- require timing-fit RMS <=2.5 frames;
- retain the same candidate schedule and global `|S|=2` decision rule.

The failed v1 development result remains part of the audit trail. It is not
relabelled as validation evidence.

## Second development failure and tracker revision 3

Tracker revision 2 was then executed on the same development split
`drop_50/{02,03,04,05}`. It still failed the predeclared gate:

- quality-pass videos: 2/4;
- median direct acceleration relative error: 0.36057242713824783;
- truth-control accuracy: 0.0;
- placebo false-assertion rate: 0.0;
- directional score sign rate: 1.0;
- development gate: FAIL.

No free-fall validation analysis followed this failure and no final-test video was
opened.

Revision 3 is therefore another development-only redesign, frozen before validation.
It replaces whole-mask centroiding with compact moving-component tracking and
requires an explicit physical sequence:

1. stationary top plateau;
2. monotone accelerating traversal;
3. stationary bottom plateau.

Candidate traversals are ranked by internal consistency with
`t(p)=t0+T*sqrt(p)`, plus quadratic trajectory-shape and plateau-stability
residuals. The target value of `g` is **not** used to select among candidates.
The independently measured drop height remains a known geometric input.

Revision 3 quality requirements:
- >=80% monotone consistency;
- timing-fit RMS <=2.5 frames;
- normalized quadratic trajectory-shape RMS <=0.10;
- combined top/bottom plateau MAD <=0.12 of travel span.

The global candidate schedule and `|S|=2` decision rule remain unchanged.

## Third development failure and tracker revision 4

Tracker revision 3 was executed only on the development population
`drop_50/{02,03,04,05}`. It over-constrained the event and produced:

- quality-pass videos: 0/4;
- median acceleration relative error: unavailable;
- emitted records: 0;
- development gate: FAIL.

No free-fall validation result was analyzed after this failure and no final-test
video was opened.

Revision 4 is therefore frozen before validation and changes only the
development-stage visual tracker. It:

1. extracts compact moving components in each frame;
2. associates components temporally using predicted position, area consistency,
   and continuity;
3. searches contiguous track segments for release-from-rest motion;
4. fits `y(t)=a+b t+c t^2` in image space;
5. requires acceleration-dominated motion, near-zero release velocity, mostly
   monotone vertical travel, bounded horizontal drift, and low normalized
   trajectory residual;
6. ranks candidates only by those image/model-consistency quantities;
7. does **not** use the target value of `g` to choose the track;
8. uses independently measured IRIS `drop_height` only after selection to map the
   accepted image trajectory into physical units.

Revision 4 quality requirements:
- trajectory-shape RMS <=0.10 of fitted span;
- release-speed ratio <=0.65;
- horizontal drift <=0.45 of vertical span;
- temporal gap penalty <=0.50;
- >=78% monotone consistency.

The candidate schedule and global `|S|=2` decision rule remain unchanged.

## Physical observable

A deterministic static-camera tracker extracts the vertical trajectory of the ball.
The known controlled drop height is read from the IRIS measured parameter manifest.
The free-fall event is the fastest credible monotone 10%-90% traversal, selected
before any candidate is evaluated; this prevents slow hand/reset motion from being
mistaken for ballistic descent.

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

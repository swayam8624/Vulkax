# IRIS Free-Fall V6 Fresh-Split Rescue Protocol — 2026-09-23

Purpose: attempt a scientifically clean rescue of the non-pendulum IRIS free-fall
replication after `full_flight_v5` failed held-out validation.

This protocol does **not** overwrite, reinterpret, or reuse the failed V5 validation
as held-out evidence.

## Prior failure

V5 passed its original development gate on `drop_50/02..05` but failed held-out
validation on `drop_100/02..05`:

- quality-pass videos: 3/4;
- median acceleration relative error: 2.223747613561782;
- truth-control accuracy: 0.0;
- placebo false-assertion rate: 0.0;
- direction-sign rate: 0.0.

Those four validation takes are permanently nonconfirmatory after 2026-09-23.

## Fresh V6 population

Dataset revision:
`c253822f55431ca80ef2084de4bc5e79d1a488f1`.

- fresh development: `dropping_ball/drop_50/{06,07,08,09,10}`;
- fresh validation: `dropping_ball/drop_100/{06,07,08,09,10}`;
- locked final population remains:
  `dropping_ball/drop_150/{02,03,04,05,06,07,08,09,10}`.

Take 01 remains forbidden.

The V6 development/validation downloader exposes **no final-test phase**.

## V6 hypothesis

The V5 failure showed that a numerically clean 2D parabola is not sufficient to
identify the physical ball. V6 therefore makes **object identity** part of the
measurement model before physical inference.

The target object is a ball. Candidate moving components record:

- contour circularity;
- bounding-box aspect;
- apparent area;
- fill / contrast;
- temporal position and scale continuity.

A candidate track is eligible only if the track remains ball-like across time.
The selection logic does not use the target value of gravity.

Frozen V6.1 identity requirements:

- median circularity >= 0.35;
- median solidity >= 0.65;
- median enclosing-circle fill >= 0.45;
- median minimum-area-rectangle axis ratio >= 0.55;
- radius coefficient of variation <= 0.45;
- area coefficient of variation <= 0.65;
- median absolute log aspect <= 0.45;
- detected fraction in the accepted full-flight interval >= 0.50.

All shape ratios are mathematically bounded to [0,1] before aggregation.

## V6 initial development failure and V6.1 correction

The first `ball_identity_v6` run on the fresh development population
`drop_50/06..10` failed its frozen development gate:

- quality-pass videos: 4/5;
- median acceleration relative error: 0.49908529289123515;
- truth-control accuracy: 0.875;
- placebo false-assertion rate: 0.0;
- directional sign rate: 0.95.

Fresh validation `drop_100/06..10` was not downloaded or analyzed.

Development diagnostics exposed two non-scientific implementation/design defects:
the circularity feature could exceed 1.0 because pixel-count area was mixed with
contour perimeter, and the candidate rank explicitly preferred longer traversals,
which can select slow same-ball reset motion over the gravity-driven drop.

`ball_identity_v6_1` is frozen before fresh validation and changes only the
development-stage identity/event selection:

- circularity uses contour area and the same contour's perimeter and is bounded
  to [0,1];
- median contour solidity is required;
- median fill inside the minimum enclosing circle is required;
- median minimum-area-rectangle axis ratio is required;
- temporal radius coefficient of variation is bounded;
- these bounded features form a ball-identity score;
- among ball-like release-from-rest candidates, the fastest full-travel event is
  preferred rather than the longest event;
- the existing 12-frame minimum full-flight duration remains in force.

No numerical value of gravity is used to choose a track or event. Development and
validation populations, the 20% acceleration gate, repair factors, and `|S|=2`
remain unchanged.


## V6.1 development failure and V6.2 correction

`ball_identity_v6_1` was then executed on the same fresh development set
`drop_50/06..10`. It still failed:

- quality-pass videos: 3/5;
- median acceleration relative error: 0.6082737827106389;
- truth-control accuracy: 0.8333333333333334;
- placebo false-assertion rate: 0.0;
- directional sign rate: 0.9333333333333333.

Fresh validation `drop_100/06..10` remained unopened.

The bounded geometry descriptors were now sane, but the selector still optimized
identity score before event structure. That can prefer a slow same-ball reset over
the gravity-driven release.

`ball_identity_v6_2` is frozen before fresh validation:

1. ball identity remains a hard eligibility gate;
2. a candidate must span at least 70% of the maximum eligible ball travel in that
   video;
3. among those near-full-span candidates, choose the shortest full-flight duration;
4. timing/trajectory residuals, release-speed ratio, horizontal drift, detected
   fraction, identity score and span are tie-breakers only;
5. target gravity is absent from this ranking.

A `candidate_audit.csv` is emitted with the top plausible events and descriptive
development-only acceleration diagnostics. The audit never participates in
selection.

The next execution must use `--development-only`. Even if the gate passes, the
runner stops before requesting `drop_100/06..10`.

## V6.2 implementation reliability patch

The first development-only V6.2 execution hit a software contract error before any
candidate was selected: a short track caused `fit_full_flight_progress()` to
return `None`, while its caller required a list.

This run is not scientific evidence. Fresh validation remained unopened.

The V6.2 scientific selector is unchanged. The implementation now has total
producer/consumer contracts, candidate schema and finite-value validation,
controlled no-candidate errors, saved tracebacks for unexpected failures, archived
prior outputs, and a mandatory compile/config/contract/synthetic-video preflight in
the runner.

For each eligible object track:

1. use the track's full observed vertical travel to define normalized progress
   `p`;
2. measure ordered 10%,20%,...,90% progress crossing times;
3. fit

   `t(p) = t0 + T sqrt(p)`;

4. require `T * fps >= 12`;
5. require timing-fit RMS <= 2.5 frames;
6. require normalized trajectory-shape RMS <= 0.10;
7. require release-speed ratio <= 0.65;
8. require horizontal drift <= 0.45 of vertical span;
9. require gap penalty <= 0.50 and monotone fraction >= 0.78.

Only after selection is the independently measured IRIS drop height used to derive

`g_hat = 2h/T^2`.

Target gravity is not an input to object or event selection.

## Candidate schedule and verification

Unchanged from the previous protocol:

- baseline: `1.25 g`;
- SUPPORT truth control: `1.00 g`;
- VETO truth control: `1.60 g`;
- placebo: `1.25 g`;
- dose levels: `0.025, 0.05, 0.10, 0.20`;
- standardized evidence threshold: `|S| = 2`.

No threshold or repair-factor retuning is permitted.

## Fresh development gate

Before fresh validation may be downloaded/analyzed:

- >=4/5 development videos pass quality;
- median direct acceleration relative error <=20%.

## Fresh validation gate

Before any V6 final lock may be created:

- >=4/5 validation videos pass quality;
- median direct acceleration relative error <=20%;
- truth-control accuracy >=75%;
- placebo false-assertion rate = 0;
- directional score-sign rate >=90%.

A failed V6 validation stops the free-fall confirmatory campaign. It must not be
retuned and re-evaluated on the same validation takes.

## Final boundary

The V6 development/validation command cannot download `drop_150` media.

The final population is allowed to open only after a new V6-specific lock covers:

- the V6 config;
- analyzer and data-adapter code;
- fresh development and validation summaries/records;
- exact final world manifest;
- runtime versions;
- the final runner.

Until such a lock exists, `drop_150/02..10` remains unopened.

## Claim boundary

V6 is a rescue replication after a documented V5 validation failure. If V6 passes,
the paper must still disclose the failed V5 attempt and the fresh-split redesign.
If it fails, free fall remains a negative external-validation result and does not
invalidate the separately locked pendulum confirmation.

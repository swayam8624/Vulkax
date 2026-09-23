# IRIS free-fall V5 validation failure — 2026-09-23

Tracker revision `full_flight_v5` passed its original development gate on
`dropping_ball/drop_50/{02,03,04,05}`, then failed the first genuinely held-out
validation regime `dropping_ball/drop_100/{02,03,04,05}`.

## Development result

- quality-pass videos: 3/4;
- median acceleration relative error: 0.13621841233877155;
- truth-control accuracy: 0.5;
- placebo false-assertion rate: 0.0;
- direction-sign rate: 0.6;
- development gate: PASS under the original development criteria.

## Validation result

- quality-pass videos: 3/4;
- median acceleration relative error: **2.223747613561782**;
- truth-control accuracy: **0.0**;
- placebo false-assertion rate: **0.0**;
- direction-sign rate: **0.0**;
- validation gate: **FAIL**.

Per-take validation acceleration relative errors:

- drop_100/02: 0.6459752877497072;
- drop_100/03: 0.9978225983731964 (quality reject: gap penalty);
- drop_100/04: 2.223747613561782;
- drop_100/05: 3.9077889770971828.

The internal timing/trajectory fits on several wrong tracks were numerically clean,
showing that 2D parabolic motion alone is insufficient to identify the falling ball.

## Scientific disposition

This validation result is permanent and nonconfirmatory. The four validation takes
`drop_100/02..05` may never again be described as held-out validation for a
redesigned method.

No `drop_150/02..10` final-test video was downloaded or analyzed.

## V6 rescue policy

Revision 6 uses new, previously unused takes:

- fresh development: `drop_50/06..10`;
- fresh validation: `drop_100/06..10`;
- final population remains `drop_150/02..10`.

V6 adds a class-semantic object-identity prior (ball circularity, near-square aspect,
stable apparent area, and temporal appearance consistency). These criteria do not
use target gravity. The failed V5 validation is retained as failure evidence and is
not overwritten.

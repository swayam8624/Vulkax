# IRIS Free-Fall V6.4 Development Failure — 2026-09-23

## Disposition

V6.4 is a **development failure**, not a software/implementation failure.

Fresh validation `drop_100/06..10` was not requested or analyzed by the
development-only command. Final `drop_150/02..10` remains protected by the
final-test lock path.

## Frozen observed development result

Population: `drop_50/06..10`

- tracker: `ball_identity_v6_4`
- expected videos: 5
- quality-pass videos: 2
- implementation errors: 0
- median acceleration relative error: 0.5327609326324485
- truth-control accuracy: 0.5
- placebo false-assertion rate: 0.0
- directional sign rate: 1.0
- development gate: FAIL

Accepted takes:
- `drop_50/08`: inferred full fall 15 frames, acceleration relative error
  0.6315459407646853.
- `drop_50/10`: inferred full fall 16 frames, acceleration relative error
  0.43397592450021166.

Controlled no-candidate failures:
- `drop_50/06`
- `drop_50/07`
- `drop_50/09`

These were `TrackSelectionError` outcomes, not Python crashes.

## Diagnosis

V6.4 fixed the previous downstream duration-contract bug, but two scientific
assumptions remained too brittle:

1. **Exact release/impact plateau dependence.** The selector required a top hold
   and bottom hold to pair tightly around a monotone ball fragment. Real
   detections fragment those holds, so otherwise plausible gravity-direction
   tracks on 06/07/09 were rejected.

2. **Zero-velocity release timing.** The primary acceleration diagnostic was
   still `2h/T^2`. That formula is valid only when the inferred interval is the
   complete drop beginning at a zero-velocity release. Development evidence
   shows the tracker often acquires an interior flight fragment. Treating that
   fragment as the complete zero-velocity fall biases acceleration high.

The V6.4 failure must remain visible even if a later development revision
succeeds.

## V6.5 permitted development change

Before opening fresh validation, V6.5 may:

- retain the independently measured drop height as the spatial metric scale;
- retain the global top-bottom image envelope as the scene-space ruler;
- select only ball-identity, expected-gravity-direction motion;
- fit `p(t) = a + b t + 0.5 c t^2` with nuisance position and nuisance velocity;
- infer release/impact roots from the fitted constant-acceleration trajectory;
- rank candidates using progress coverage and internal fit/constancy diagnostics;
- use `c * drop_height` as the development acceleration estimate.

V6.5 may **not**:

- use 9.80665 m/s^2 in candidate selection or ranking;
- lower the existing development or validation gates;
- alter the controlled repair schedule;
- alter the global standardized-evidence decision threshold `|S|=2`;
- open `drop_100/06..10` while development is still failing;
- erase V5 or V6.1–V6.4 failures from provenance.

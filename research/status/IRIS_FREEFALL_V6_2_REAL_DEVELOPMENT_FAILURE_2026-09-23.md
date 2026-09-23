# IRIS free-fall V6.2 real-development failure — 2026-09-23

After the V6.2 implementation-contract bug was fixed, `ball_identity_v6_2`
was executed in GitHub Actions on the actual public fresh development split
`dropping_ball/drop_50/{06,07,08,09,10}`.

This was the first development run whose complete candidate audit was collected in
cloud CI before asking for another local rerun.

Observed result:

- quality-pass videos: **3/5**;
- implementation errors: **0**;
- median acceleration relative error: **0.7747299951571976**;
- truth-control accuracy: **0.3333333333333333**;
- placebo false-assertion rate: **0.0**;
- directional sign rate: **0.6**;
- gate: **FAIL**.

Fresh validation `drop_100/06..10` remained unopened.

## Candidate-audit diagnosis

The audit disproved the global relative-span heuristic.

Examples:

- take 08 contained a candidate with only ~10.6% acceleration error, but the
  selector chose an earlier/faster false event;
- take 10 contained a ~9.4% descriptive-error candidate with a physically large
  span in ball-radius units, but it was rejected because reset/handling motion
  defined a much larger global pixel span;
- takes 06 and 09 showed that motion-only segmentation did not reliably isolate the
  actual falling-ball event.

The public IRIS benchmark figure shows that `dropping_ball` uses the same highly
saturated soccer ball against a largely gray scene. This provides a class-semantic
visual identity cue independent of gravity.

## V6.3 correction

`ball_identity_v6_3` is frozen before fresh validation.

It:
- adds a saturated-color ball tracker as the primary measurement path;
- keeps the motion tracker only as fallback;
- requires candidate travel >=4 apparent ball radii rather than a percentage of
  the largest motion in the clip;
- prefers valid color-source candidates when available;
- chooses the earliest valid release event, preventing later same-ball reset motion
  from winning;
- retains the same ball-shape, timing, trajectory and continuity quality gates.

No target gravity, validation label, or final-test data is used for event selection.

A dedicated GitHub Actions job now downloads only the five development videos and
must pass the real development gate before V6.3 can merge.

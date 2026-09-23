# IRIS free-fall V6.1 development failure — 2026-09-23

Tracker revision `ball_identity_v6_1` was executed only on the fresh development
population `dropping_ball/drop_50/{06,07,08,09,10}`.

Observed result:

- quality-pass videos: **3/5**;
- median acceleration relative error: **0.6082737827106389**;
- truth-control accuracy: **0.8333333333333334**;
- placebo false-assertion rate: **0.0**;
- directional sign rate: **0.9333333333333333**;
- development gate: **FAIL**.

Fresh validation `drop_100/06..10` remained untouched.

## What V6.1 fixed

V6.1 correctly repaired the invalid shape descriptor:

- circularity values are now bounded to [0,1];
- solidity, enclosing-circle fill, axis ratio, radius stability and area stability
  are numerically sane.

The remaining failure is therefore not the old geometry bug.

## Remaining development-only defect

Identity was still the **first lexicographic ranking objective**. Once multiple
same-ball events existed, the most ball-like track could win even when it was the
slow manual reset rather than the gravity-driven release.

The selected intervals make the failure visible:

- drop_50/06: 71 frames, relative acceleration error 0.9266;
- drop_50/10: 31 frames, relative acceleration error 0.6083.

Both are far longer than other eligible traversals in the same class and are
consistent with same-object reset/handling being preferred.

## V6.2 correction

`ball_identity_v6_2` is frozen before fresh validation.

Selection is now two-stage:

1. ball identity remains a **hard eligibility gate**;
2. candidate vertical span must be at least 70% of the maximum eligible ball span
   in that video;
3. among those near-full-span candidates, choose the shortest full-flight duration;
4. timing residual, trajectory residual, release-speed ratio, horizontal drift,
   detected fraction, identity score and span are only tie-breakers.

The target value of gravity is not used in selection.

A top-candidate audit CSV is emitted for development so every plausible event can
be inspected without changing the selector after validation opens.

Unchanged:
- development and validation takes;
- 20% acceleration gate;
- repair factors;
- `|S|=2`;
- final population and lock policy.

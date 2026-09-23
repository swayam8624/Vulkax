# IRIS free-fall V6 initial development failure — 2026-09-23

The first `ball_identity_v6` execution used only the fresh development population
`dropping_ball/drop_50/{06,07,08,09,10}`.

Observed result:

- quality-pass videos: **4/5**;
- median acceleration relative error: **0.49908529289123515**;
- truth-control accuracy: **0.875**;
- placebo false-assertion rate: **0.0**;
- directional sign rate: **0.95**;
- development gate: **FAIL**.

The fresh V6 validation population `drop_100/06..10` was **not downloaded or
analyzed** because the runner stopped at the development gate.

## Development-only diagnosis

Two implementation/design defects were visible directly in the development output.

### 1. Circularity was not geometrically valid

The component feature used connected-component pixel count as area together with a
contour perimeter. That mixed discretizations and produced values greater than
1.0 (for example 1.286, 1.963, and 1.617), which is impossible for the standard
geometric circularity

`4*pi*A/P^2`.

A ball-identity prior cannot be trusted when its primary shape feature is outside
its mathematical range.

### 2. Candidate ranking favored slow reset motion

After a candidate passed the release-from-rest shape checks, the V6 rank included
`-full_interval_frames`, explicitly preferring **longer** traversals.

IRIS defines the class as a ball released under gravity. The same physical ball can
also appear during slower manual reset/handling. On the failed development run,
selected intervals included 71 and 31 frames, while shorter candidates existed.
Preferring the longest same-ball traversal can therefore select reset motion even
when object identity is correct.

## V6.1 development-only correction

`ball_identity_v6_1` is frozen before any fresh validation is opened.

It:
- computes contour area and contour perimeter from the same contour;
- clips circularity to [0,1];
- adds contour solidity;
- adds fill ratio inside the minimum enclosing circle;
- adds minimum-area-rectangle axis ratio;
- adds temporal radius coefficient of variation;
- combines those bounded features into a ball-identity score;
- after ball identity and release-from-rest consistency pass, prefers the
  **fastest full-travel event**, not the longest event;
- retains the existing 12-frame minimum so tiny fragments remain ineligible.

No target value of gravity is used for object/event selection.

Unchanged:
- fresh development split;
- fresh validation split;
- 20% acceleration gate;
- repair factors;
- `|S|=2` decision threshold;
- final population and lock policy.

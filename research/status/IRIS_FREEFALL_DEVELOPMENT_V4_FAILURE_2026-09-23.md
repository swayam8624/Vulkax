# IRIS free-fall development v4 failure — 2026-09-23

Tracker revision `temporal_ballistic_v4` was executed only on the development
population `dropping_ball/drop_50/{02,03,04,05}`.

Observed result:
- quality-pass videos: **0/4**;
- every take failed the existing `active_frames` quality predicate;
- normalized quadratic trajectory-shape RMS was low on every take;
- relative acceleration errors were approximately:
  - 1.1632
  - 3.2261
  - 3.0237
  - 3.0080
- development gate: **FAIL**.

No free-fall validation result was analyzed after this failure and no final-test
video was opened.

## Root cause

Revision 4 searched arbitrary subsegments inside temporal tracks and ranked low
quadratic residual ahead of physical interval completeness. This favored short,
cleanly quadratic fragments.

The implementation then used the independently measured *entire* drop height to
scale those truncated intervals. That is an invalid calibration: a short partial
flight cannot be assigned the full physical drop distance. The inflated recovered
acceleration follows directly from that mismatch.

## Revision 5 response

`full_flight_v5` is frozen before validation. It:
- allows brief detection gaps inside one coherent track;
- requires the accepted interval to satisfy the pre-existing minimum-active-frame
  duration before physical calibration;
- permits only the whole coherent track chunk with <=2 edge detections trimmed;
- forbids arbitrary interior subsegments;
- prioritizes longer/larger coherent flights over tiny low-residual fragments;
- leaves target gravity out of candidate selection;
- leaves the 20% development gate, repair schedule, and global `|S|=2` rule
  unchanged.

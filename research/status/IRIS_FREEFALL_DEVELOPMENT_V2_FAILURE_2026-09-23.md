# IRIS free-fall development v2 failure — 2026-09-23

Tracker revision `ballistic_event_v2` was executed only on the development
population `dropping_ball/drop_50/{02,03,04,05}`.

Observed result:

- expected videos: 4;
- quality-pass videos: **2/4**;
- median direct acceleration relative error: **0.36057242713824783**;
- records emitted from quality-pass videos: 44;
- truth-control accuracy: **0.0**;
- placebo false-assertion rate: **0.0**;
- directional score sign rate: **1.0**;
- development gate: **FAIL**.

No free-fall validation analysis was allowed after this failure. The locked final
population `drop_150/02..10` remained unopened.

## Interpretation

Revision 2 solved the gross v1 failure mode of selecting a slow reset traversal, but
the combination of whole-mask centroiding and endpoint calibration remained too
unstable across takes. Two of four development videos failed quality and the
physical acceleration diagnostic remained outside the predeclared 20% gate.

Because this is still the development split, revision 3 is permitted as method
design. The 20% gate, candidate schedule, and global `|S|=2` decision rule are not
relaxed.

Revision 3 replaces whole-mask centroiding with compact moving-component tracking
and requires stationary top/bottom plateaus plus ballistic-shape self-consistency.
Target gravity is not used by the event selector.

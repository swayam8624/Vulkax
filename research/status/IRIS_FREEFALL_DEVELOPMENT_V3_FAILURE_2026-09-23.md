# IRIS free-fall development v3 failure — 2026-09-23

Tracker revision `plateau_ballistic_v3` was executed only on the development
population `dropping_ball/drop_50/{02,03,04,05}`.

Observed result:
- quality-pass videos: **0/4**;
- median acceleration relative error: unavailable;
- standardized records emitted: **0**;
- truth-control accuracy: 0;
- placebo false-assertion rate: 1;
- directional score sign rate: 0;
- development gate: **FAIL**.

No free-fall validation result was analyzed after this failure and no final-test
video was opened.

## Interpretation

Revision 3 over-constrained the visual event definition. Requiring independently
selected compact components to simultaneously satisfy plateau, trajectory-shape,
and ballistic timing constraints caused every development take to fail quality.

Because this remains development-only evidence, tracker revision 4 is permitted as
method design. The development acceptance gate, candidate schedule, and global
`|S|=2` decision rule are unchanged.

Revision 4 temporally associates compact moving components and fits a
release-from-rest quadratic directly to track segments. Target gravity is not used
to choose the track.

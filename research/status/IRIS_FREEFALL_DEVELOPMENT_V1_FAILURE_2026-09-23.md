# IRIS free-fall development v1 failure — 2026-09-23

The first blind-domain development execution stopped exactly where the protocol
required it to stop.

Observed on `dropping_ball/drop_50/{02,03,04,05}`:

- quality-pass videos: 4/4;
- controlled truth accuracy: 1.0;
- placebo false-assertion rate: 0.0;
- directional score sign: 1.0;
- median direct acceleration relative error: **0.9856055698673896**;
- development gate: **FAIL**.

No free-fall validation result was analyzed after this gate failure and no
`drop_150/02..10` final-test video was opened.

## Root cause in v1 implementation

The v1 tracker selected the **longest** run lying between global vertical-position
quantiles. A real drop video can also contain slow repositioning/reset/handling
motion. The longest occupancy run is therefore not a valid invariant for selecting
the ballistic descent.

This explains the apparently contradictory development outcome: the controlled
candidate ordering could remain directionally correct while the direct physical
acceleration diagnostic was grossly miscalibrated.

## Development-only redesign

Tracker revision `ballistic_event_v2` is frozen before validation. It:

1. considers both image-axis directions;
2. finds 10%-90% endpoint traversals;
3. rejects traversals with <80% monotone consistency;
4. selects the fastest credible traversal (the ballistic release rather than slow
   reset motion);
5. fits crossing times to `t(p)=t0+T*sqrt(p)`;
6. computes the diagnostic `g=2h/T^2` using the independently measured IRIS
   `drop_height` as a known input;
7. requires timing-fit RMS <=2.5 frames;
8. leaves the candidate schedule and global `|S|=2` verification decision rule
   unchanged.

The failed v1 result remains an audit artifact. It is not discarded, relabelled,
or used as validation evidence.

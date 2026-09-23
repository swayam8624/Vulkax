# IRIS free-fall V6.2 development failure — 2026-09-23

Tracker revision `ball_identity_v6_2` completed normally on the fresh development
population `dropping_ball/drop_50/{06,07,08,09,10}`.

Observed result:

- implementation errors: **0**;
- quality-pass videos: **3/5**;
- median acceleration relative error: **0.3898968030718314**;
- truth-control accuracy: **1.0**;
- placebo false-assertion rate: **0.0**;
- directional sign rate: **1.0**;
- development gate: **FAIL**.

Fresh validation `drop_100/06..10` remained unopened.

## Candidate-audit diagnosis

The 70% relative-span rule was the wrong abstraction.

Fast gravity candidates were visible but rejected solely because they covered a
smaller fraction of the image-space trajectory than the later slow reset:

- drop_50/06: fast candidate T=0.2586 s, relative span 0.369, rejected;
- drop_50/07: fast candidate T=0.2717 s, relative span 0.386, rejected;
- drop_50/09: fast candidate T=0.2821 s, relative span 0.481, rejected;
- drop_50/10: fast candidate T=0.3210 s, relative span 0.258, rejected, with
  development-only relative g error approximately 0.010;
- drop_50/08: selected T=0.3165 s and relative g error approximately 0.018.

The reset motion often covered the largest image-space span, so a span rule designed
to avoid partial fragments systematically favored the wrong physical event.

## V6.3 correction

`ball_identity_v6_3` is frozen before fresh validation.

V6.3 separates spatial calibration from temporal physics:

1. all ball-like motion may contribute to a robust top/bottom pixel envelope;
2. slow reset/handling contributes **only** spatial extent, never its timing;
3. only the configured gravity image direction is eligible for the dynamic event;
4. a partial downward fragment is mapped into global full-drop progress;
5. full fall time is fitted from
   `t(p)=t0+T*sqrt(p)` in that global coordinate;
6. candidates are ranked by global timing/trajectory consistency and observed
   global-progress coverage.

Target gravity is not used in envelope construction, event selection, or timing
fit. The measured physical drop height is applied only after T is inferred.

Unchanged:
- development / validation / final populations;
- >=4/5 development quality requirement;
- <=20% development median acceleration error;
- repair schedule;
- |S|=2 decision threshold;
- final lock policy.

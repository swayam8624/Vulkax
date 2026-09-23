# IRIS free-fall V6.3 duration-contract failure and V6.4 redesign — 2026-09-23

The post-fix V6.3 development-only execution passed static/preflight checks. Three
takes completed, while takes 06 and 07 terminated with:

`RuntimeError: selected full-flight window violates minimum duration`

This was an implementation/contract mismatch, not validation evidence. Fresh
validation `drop_100/06..10` remained unopened.

## Duration-contract defect

V6.3 intentionally allowed a partially observed gravity fragment and enforced the
minimum duration on the **inferred full fall**:

`T * fps >= minimum_active_frames`.

Downstream `extract()` then incorrectly re-applied the old V5 assumption:

`len(observed_fragment) >= minimum_active_frames`.

The per-take quality gate duplicated the same obsolete check through
`active_frames`.

V6.4 now records both quantities separately:

- `observed_fragment_frames`;
- `inferred_full_fall_frames`.

The existing 12-frame physical-duration requirement is applied to the inferred
full event, not to the number of observed samples.

## Calibration defect visible in completed V6.3 takes

The three completed V6.3 development takes still had relative acceleration errors
of approximately 0.57--0.62. The global reset-derived spatial envelope therefore
did not provide a trustworthy physical ruler.

## V6.4 release-impact timing

V6.4 removes reset-envelope calibration. For each ball-like downward event it:

1. identifies a stationary top plateau before release;
2. identifies a stationary bottom plateau after impact;
3. excludes the later upward reset from dynamic evidence;
4. refines release/impact boundary crossings from the plateau positions;
5. infers full fall time directly from release-to-impact timing;
6. validates the observed trajectory against normalized
   `p(t) ~= (t/T)^2`;
7. computes `g=2h/T^2` using the measured IRIS drop height only after T is
   inferred.

Plateaus may come from nearby track chunks when tracking fragments across the
event. Pairing is constrained by temporal proximity and horizontal position.

Unchanged:
- fresh development/validation/final populations;
- minimum 4/5 quality videos;
- maximum 20% median development/validation acceleration error;
- repair factors;
- |S|=2;
- final lock policy.

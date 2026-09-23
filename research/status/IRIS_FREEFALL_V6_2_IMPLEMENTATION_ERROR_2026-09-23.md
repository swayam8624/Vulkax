# IRIS free-fall V6.2 implementation error — 2026-09-23

The first `ball_identity_v6_2` development-only execution did not produce a
scientific result. All five development takes terminated with:

`TypeError: 'NoneType' object is not iterable`

## Root cause

`fit_full_flight_progress()` had two incompatible return conventions:

- normal path: `list[candidate]`;
- short-track path: `None`.

`choose_ballistic_track()` unconditionally executed `candidates.extend(result)`.
A short temporal track therefore caused a Python type error before candidate
selection.

This is classified as a **software contract failure**, not a failed physics or
validation experiment.

Fresh validation `drop_100/06..10` remained unopened.

## Reliability correction

The intended V6.2 selector is unchanged. The implementation now enforces:

- candidate producers always return a list, including `[]` for no candidates;
- producer return types are checked before iteration;
- every candidate is schema-checked before ranking;
- all ranking numerics must be finite;
- bounded shape features must remain in [0,1];
- selector configuration is range-checked;
- empty candidate populations raise a controlled `TrackSelectionError`;
- unexpected implementation/I/O errors are saved with full traceback in
  `failure_details.json` and force `gate_pass=false`;
- prior development outputs are archived instead of deleted;
- the V6 runner performs compile/config/contract/synthetic-video preflight before
  touching the real development run.

No development/validation split, gate, repair factor, score threshold, or final
policy was changed.

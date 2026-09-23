# IRIS free-fall V6.3 implementation error — 2026-09-23

The first local `ball_identity_v6_3` development-only execution passed all existing
preflight checks, then all five development takes failed with:

`NameError: name 'reject' is not defined`

This is classified as a software implementation failure, not scientific evidence.
Fresh validation `drop_100/06..10` remained unopened.

## Root cause

During the V6.3 refactor, several rejection branches inside
`_fit_global_fragment()` still called a closure-style helper named `reject`.
That helper existed only inside a different legacy function scope. The synthetic
success path did not exercise those rejection branches, so the undefined name was
not caught by the previous preflight.

A second latent issue was discovered during the same pass: the legacy
recalibration helper referenced `diagnostics` without declaring it in the
function signature.

## Reliability correction

- all rejection paths now use one module-level
  `_reject(diagnostics, reason)` helper;
- no rejection branch depends on closure scope;
- the legacy recalibration helper declares `diagnostics=None` explicitly;
- direct self-tests exercise controlled rejection and verify reason accounting;
- the self-test statically rejects reintroduction of `return reject(...)`;
- `pyflakes` is now mandatory in both GitHub publication-validation CI and the
  local V6 runner preflight before real videos are touched;
- software failures remain separate from scientific gate failures.

No tracker science, dataset split, gate, repair schedule, score threshold, or final
lock policy was changed.

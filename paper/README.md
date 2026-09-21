# Reality Probe paper source

This directory contains the paper-facing manuscript for the Vulkax research program.

## Canonical manuscript

- `main.tex` — review-hardened full internal manuscript.
- Paper-facing title: **Reality Probe: Information-Limited Physical Verification of Captured Worlds**.
- Repository/system codename: **Vulkax**.

## Scientific freeze

The manuscript must not silently change the frozen experimental result.

- frozen tag: `paper-freeze-2026-09-21`
- frozen commit: `a9da8c0aa8689ebeea0d84baf95a74907659a837`

Machine-readable source hierarchy:

1. `research/results/VULKAX_FINAL_RESULTS_2026-09-21.json`
2. `research/results/ORTHOGONAL_FORCE_COMPLIANCE_RESULT_2026-09-21.json`
3. `research/results/DCS_FINAL_RESULTS_2026-09-20.json`
4. frozen protocol documents
5. `research/status/DCS_MATH_IMPLEMENTATION_AUDIT_2026-09-21.md`
6. paper-data and visualization summaries

The review-hardening changes in `main.tex` alter framing, robustness analysis, cross-references, and presentation only. They do not retune the threshold, force amplitude, truth worlds, candidate grid, or frozen result ledgers.

## Figures

The current manuscript resolves committed paper-facing graphics from
`../docs/readme_assets/`. The former dangling `paper/figures/` references were
removed during the review-hardening pass. The manuscript-only system-architecture
graphic was not present in the repository, so its scientific content is retained as
prose rather than referenced through a nonexistent file.

Before venue submission, inspect all panels at final two-column print size. If labels
are too small, regenerate the underlying deterministic asset; do not scale or edit a
plot in a way that changes its scientific meaning.

## Threshold sensitivity

The primary rule remains the prospectively frozen `|z| >= 2` decision magnitude.

The manuscript now records a post-hoc robustness audit that follows directly from the
frozen aggregate maxima:

- fresh DCS max `|z| = 0.172788679...`, therefore 0/36 crossings even at tau=1;
- fresh force max `|z| = 1.319111318...`, therefore 0/36 crossings for every
  tau >= 1.5;
- at tau=1, at least one force case crosses, but the aggregate publication ledger does
  not retain the full signed per-proposal vector, so the exact count is intentionally
  not asserted.

This audit does not replace or weaken the frozen tau=2 primary decision rule.

## Remaining submission engineering

The research story and full manuscript source are present. Remaining work is
submission engineering: compile/format audit, bibliography verification, printed-scale
figure typography review, anonymous supplementary export, and venue-specific checks.

A genuinely new prospective measured-force experiment would strengthen the paper, but
it is new research. It must use a separately frozen protocol and must not be presented
as part of the existing 2026-09-21 scientific freeze.

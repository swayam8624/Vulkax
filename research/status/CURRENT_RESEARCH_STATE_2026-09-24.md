# Reality Probe Current Research State — 2026-09-24

This document is the current publication-facing state. It does not replace the
immutable 2026-09-21 scientific snapshot or any frozen experiment ledger.

## Current paper claim

Reality Probe is a rewrite-level physical verification transaction for captured
executable worlds. Proposal generation is separated from a separately reserved
verification channel. A proposed physical rewrite can end in SUPPORT, VETO, or
UNRESOLVED.

The paper does **not** claim a universal physical verifier, a new pendulum/gravity
estimator, new finite-difference mathematics, generic VVUQ novelty, or independent
sensor evidence when proposal and verification are extracted from the same video.

## Frozen evidence ladder

### Synthetic / controlled

- D1 constructed positive control: 64/64 deliberately deceptive repairs rejected.
- D2: 0/16 truth worlds resolved.
- D3: 0/6 truth worlds resolved.
- D4V: 36 repair proposals, including 14 deceptive; 0/36 resolved.
- Orthogonal force compliance: 36 fresh proposals; median standardized evidence
  magnitude improves by 11.46x over DCS, but 0/36 cross the frozen |score|=2
  decision boundary.

Interpretation: changing the information channel can increase observability, but
stronger evidence is not automatically sufficient evidence.

### Measured GAUGE

Two GAUGE evidence classes are kept separate.

1. **Retrospective foam-shearing channel contradiction.** The longitudinal endpoint
   is preferred in 9/10 held-out repeats while other channels favor the overlap
   model. This is retrospective diagnostic evidence only.
2. **Prospective validation-stage stretching/compression failure.** On 12 measured
   validation worlds, the frozen temporal annihilating witness gives 0/12 correct
   SUPPORT, 0/12 correct VETO, and 12/12 correct UNRESOLVED placebo decisions.
   Final repeats 8--10 remain unopened.

Interpretation: the tested GAUGE forward-model/evidence channel does not provide
directional decision coverage at the frozen threshold.

### Locked IRIS pendulum final test

- 10 physical `pendulum_90` videos.
- 10/10 pass the quality gate.
- Finite-amplitude period probe: 90/110 controlled decisions correct.
- Matched small-angle diagnostic baseline: 40/110 correct.
- Primary probe: 40/50 SUPPORT correct, 40/50 VETO correct, 10/10 placebo cases
  correctly UNRESOLVED.
- Paired case comparison: 50 primary-only correct, 0 baseline-only correct, 60 ties.
- Median period-inferred length relative error: 16.81%.

The 110 rows are nested controlled cases inside 10 physical videos; they are not
110 independent physical experiments.

Interpretation: a physically better-matched reserved observable can produce useful
rewrite decisions on fresh measured video.

### IRIS free-fall second-domain failure

V6.6 passed the `drop_50` development gate, then failed untouched
`drop_100/06..10` validation:

- 5/5 videos pass low-level quality checks;
- median acceleration relative error: 111.7%;
- frozen truth-control accuracy: 0;
- frozen direction-sign rate: 0.

Post-validation V6.7 diagnostics did not recover a robust target-free estimator.
The validation split is permanently retrospective and the designated
`drop_150` final split remains unopened.

Interpretation: the pendulum success does not transfer automatically across physical
domains. This failure is retained as evidence of the method/channel boundary.

## Novelty position

The central novelty claim is the operational composition around an individual
physical rewrite:

1. propose a rewrite from fitting / ordinary evidence;
2. reserve a distinct verification channel or intervention;
3. carry measurement/repeat/numerical uncertainty into a standardized evidence
   score;
4. return SUPPORT, VETO, or UNRESOLVED before commit/rollback;
5. preserve failed information channels rather than retuning them into success.

Adjacent fields such as inverse physics, structural identifiability, VVUQ,
experimental design, abstention/selective prediction, finite differences, and
pendulum dynamics are prior art/context rather than novelty claims.

See `research/status/REALITY_PROBE_NOVELTY_MAP_2026-09-24.md`.

## Manuscript state

Both:
- `paper/main.tex`
- `paper/main_humanized.tex`

contain the same current scientific claims and compile successfully under the
branch-only ACM/LaTeX manuscript check after the 2026-09-24 evidence revision.

The paper now explicitly contains:
- DCS/OFC information-boundary results;
- retrospective GAUGE contradiction;
- prospective GAUGE validation failure;
- locked IRIS pendulum success;
- failed IRIS free-fall second domain;
- explicit evidence-class and novelty boundaries.

## Reviewer-hardening state

The post-final pendulum reviewer-hardening battery was already executed on
2026-09-23 on the original Apple-silicon/macOS environment at repository commit
`3aa0fb14e98956ddf6e3a8a7388347c9a2ba278d`, using the full Metal robustness
profile.

Canonical secondary-result ledgers:

- `research/results/IRIS_PENDULUM_POSTFINAL_REVIEWER_HARDENING_2026-09-23.json`
- `research/results/IRIS_PENDULUM_POSTFINAL_REVIEWER_HARDENING_2026-09-23.md`

Major results:

- physical-video clustered comparison: primary better on 10/10 videos, baseline
  better on 0/10, no ties; two-sided exact sign-test p=0.001953125;
- stronger post-final comparators: direct finite-amplitude residual 45.45%
  strict accuracy, damped nonlinear ODE residual 9.09%, locked probe 81.82%;
- GT-hidden proposals: 10/10 ordinary-improving proposals, 8 physically worse
  and 2 physically better, verifier correct on all 10 after proposal SHA closure;
- measured rope-length uncertainty: 110/110 canonical labels stable through both
  ±1 sigma and ±2 sigma intervals;
- proposal/probe overlap diagnostic: 10/10 correct across rho=0 through rho=1;
- full corruption robustness: 180 condition-video runs, zero failed videos,
  10/10 clean Metal equivalence;
- tested noise, blur, frame duplication/drop, occlusion, and crop preserve the
  locked 81.82% accuracy;
- temporal-resolution boundary: 63.64% at 30 fps and 45.45% at 15 fps, with
  placebo abstention remaining 100%.

These analyses are secondary only and do not replace the locked final result.

A cross-platform GitHub Actions replay is maintained as a reproducibility audit.
On Ubuntu/current dependencies, the frozen development and validation stages
reproduce through the original validation-readiness gate with 10/10 quality-pass
videos in each split, 100% truth-control/placebo/large-effect correctness, and
dose-direction sign rate 1.0. The remaining final-test replay is still an
engineering audit of the historical freeze/runtime path assumptions; it has not
yet completed the exact locked-result assertion. This audit is not allowed to
change the frozen result. The original 2026-09-23 Mac/Metal execution remains the
canonical post-final secondary evidence until an independent final replay closes
R35.

## Repository disposition

- Open pull requests: 0.
- Open issues: 0.
- Obsolete free-fall rescue PRs are closed with negative-result dispositions.
- The old GAUGE prospective PR is closed with its frozen validation failure
  preserved in the repository.
- Seven temporary V6.7 free-fall matrix/campaign workflows have been pruned.
- `main` contains the publication-facing scientific state. Temporary
  reproducibility-audit commits are isolated on the research branch and are not
  required for the scientific result.

## Submission boundary

A defensible submission should say:

> Reality Probe provides an evidence-governed transaction for deciding whether to
> commit a physical rewrite in a captured world. In the tested experiments,
> verification usefulness depends on whether the reserved physical channel
> contains discriminating information for that rewrite. A matched channel can
> support or veto on fresh measured video; information-poor or mismatched channels
> must remain unresolved or be retired.

It should not claim universal coverage, a successful free-fall replication, a
successful GAUGE directional verifier, or statistical independence of nested case
rows.

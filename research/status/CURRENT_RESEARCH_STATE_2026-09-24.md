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

Implemented post-final tooling exists for:
- cluster-correct video-level bootstrap/statistics;
- direct finite-amplitude and nonlinear/damped baselines;
- GT-hidden proposal -> verification analysis;
- measured-truth uncertainty sensitivity;
- proposal/probe overlap dependence;
- video corruption robustness;
- reviewer-facing deterministic figures/demo.

These analyses are secondary only and must not replace the locked IRIS final
result.

The authoritative execution gate is the GitHub Actions workflow:

**Reality Probe post-final reviewer hardening replay**

It reconstructs the original frozen IRIS validation -> lock -> final-test chain
from the historical code commit, requires exact agreement with the committed
locked final summary, and only then executes post-final hardening analyses.

At the time this state ledger is written, that replay is still an execution gate;
no post-final numerical result is promoted here until the workflow completes.

## Repository disposition

- Open pull requests: 0.
- Open issues: 0.
- Obsolete free-fall rescue PRs are closed with negative-result dispositions.
- The old GAUGE prospective PR is closed with its frozen validation failure
  preserved in the repository.
- Seven temporary V6.7 free-fall matrix/campaign workflows have been pruned.
- The research branch remains a pure fast-forward descendant of `main`; no
  scientific history rewrite is required to land it.

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

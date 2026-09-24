# Reality Probe Pre-Manuscript Checkpoint — 2026-09-24

This checkpoint marks the end of research/repository hardening before any further
manuscript work.

## Research state

- Core Reality Probe implementation and research pipeline: complete.
- DCS synthetic falsification ladder: complete and frozen.
- Orthogonal force-compliance experiment: complete and frozen.
- GAUGE retrospective channel contradiction: preserved as retrospective evidence.
- GAUGE prospective validation: failed at frozen threshold and preserved as a
  negative result; final repeats remain unopened.
- IRIS pendulum development, validation, and locked final test: complete.
- IRIS pendulum locked final result: 10/10 quality-pass videos; 90/110 controlled
  decisions correct for the finite-amplitude probe versus 40/110 for the matched
  small-angle diagnostic baseline; 10/10 placebo cases correctly UNRESOLVED.
- IRIS free-fall V6.6 prospective validation: failed and frozen as a negative
  second-domain result.
- IRIS free-fall V6.7 retrospective rescue search: closed without a robust
  target-free generalizing estimator.
- IRIS `drop_150` final split: unopened.

## Reviewer hardening

Completed secondary hardening includes:

- video-clustered statistics over 10 physical pendulum videos;
- stronger post-final comparators;
- GT-hidden proposal gate with proposal hash closure;
- measured ground-truth uncertainty analysis;
- proposal/probe overlap dependence sweep;
- full corruption/robustness campaign;
- explicit temporal-resolution failure boundary.

These analyses remain secondary and do not replace the locked final result.

## Reproducibility status

Repository/data revisions, result ledgers, hashes, exact commands, and runtime
records are present.

Independent Ubuntu replay has reproduced the frozen IRIS pendulum development
and validation chain through the original readiness gate, including:

- development 10/10 quality-pass;
- validation 10/10 quality-pass;
- truth-control correct rate 1.0;
- placebo correct rate 1.0;
- large-effect correct rate 1.0;
- dose-direction sign rate 1.0;
- validation readiness: `validation_supports_freeze_without_retuning`.

R35 is **not marked closed yet**. The exact independent Linux replay of the
locked `pendulum_90` final result is still a reproducibility-engineering audit.
Earlier CI attempts exposed historical worktree/path assumptions rather than a
scientific mismatch. The frozen Mac/Metal result remains canonical and may not be
retuned based on replay behavior.

## Repository state

At this checkpoint:

- default branch: `main`;
- open pull requests: 0;
- open issues: 0;
- obsolete free-fall rescue PRs: closed with negative-result dispositions;
- stale GAUGE prospective PR: closed with its frozen failure preserved;
- temporary V6.7 campaign/matrix workflows: removed from `main`;
- temporary locked-final replay workflow: isolated from `main`;
- publication-facing evidence/status documents: current;
- core/release/demo/evidence CI at the preceding publication-facing head: green.

Historical remote research branches remain because the connected GitHub mutation
surface does not support branch deletion. Their existence does not alter
`main` or the frozen evidence ledgers.

## Claim boundary before manuscript

The strongest supported claim remains conditional:

> Reality Probe provides an evidence-governed transaction for deciding whether to
> commit a physical rewrite in a captured world. Verification usefulness depends
> on whether the separately reserved physical channel contains discriminating
> information for that rewrite. A matched channel can support or veto on fresh
> measured video; information-poor or mismatched channels must remain unresolved
> or be retired.

Do not claim:

- universal physical verification;
- independent sensor evidence for same-video pendulum channels;
- statistical independence of the 110 nested case rows;
- successful GAUGE directional validation;
- successful free-fall replication;
- novelty of pendulum/gravity estimation or finite-difference cancellation.

## Stop point

Research execution, negative-result preservation, reviewer hardening, repository
cleanup, evidence indexing, and claim scoping are complete enough to hand off to
the manuscript phase.

No further manuscript editing is performed as part of this checkpoint.

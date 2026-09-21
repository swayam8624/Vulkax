# Orthogonal Force-Compliance Information Test — Result

Date: 2026-09-21

Status: **COMPLETE — FROZEN NEGATIVE ADVANCEMENT RESULT WITH LARGE INFORMATION GAIN**

Protocol:
`research/benchmarks/ORTHOGONAL_FORCE_COMPLIANCE_PROTOCOL_2026-09-21.md`

Workflow run: **35555415551**

## Why this experiment exists

D4V could not resolve any repair using the available kinematic verification channels.

Rather than retuning DCS, this experiment introduced a different physical control
channel:

> a known external force applied to the simulated object, followed by measurement of
> the resulting displacement compliance.

The force experiment was frozen before execution and used fresh off-grid truth
worlds.

## Population

- repair proposals: **36**
- deceptive: **12**
- beneficial: **24**

The ordinary held-out kinematic metric proposed the repairs before the
force-compliance evidence was evaluated.

## Fresh DCS comparator

On the same fresh worlds:

- resolved coverage: **0%**
- median |z|: **0.04877**
- maximum |z|: **0.17279**

Median signal amplification still required to reach |z|=2:

**41.01×**

## Orthogonal force-compliance channel

- resolved coverage: **0%**
- median |z|: **0.55863**
- maximum |z|: **1.31911**
- sign accuracy: **55.56%**
- deceptive repairs with negative progress: **75%**

The force channel increased median standardized signal by:

**11.455×**

relative to fresh kinematic DCS.

That is a large information gain.

However the frozen credibility reference remained:

`|z| >= 2`

and therefore:

- support decisions: **0**
- veto decisions: **0**
- unresolved decisions: **36**

Median additional signal amplification still required:

**3.58×**

Best-case additional amplification still required:

**1.52×**

## Frozen advancement gate

| Gate | Result |
|---|---|
| at least 4 deceptive repairs | PASS |
| force coverage >= 30% | **FAIL** |
| deceptive veto recall >= 50% | **FAIL** |
| beneficial false-veto <= 25% | PASS |
| force coverage > DCS coverage | **FAIL** |
| force median |z| > DCS median | PASS |
| finite numerical comparison | PASS |

Final decision:

**orthogonal_information_gate_failed**

## Exploratory post-hoc discrimination

These statistics were not preregistered gates and are reported only as diagnostics.

Using `-progress_z` as a deceptive-repair score:

| Channel | AUROC | Average precision |
|---|---:|---:|
| fresh kinematic DCS | 0.4444 | 0.3109 |
| force compliance | **0.7604** | **0.5784** |

This suggests that the force channel contains materially more class-relevant
information, even though its uncertainty-normalized magnitude remains too small for
the frozen support/veto decision.

## Scientific conclusion

The experiment supports a more precise information-limit story:

1. changing the physical information channel can substantially increase
   discriminability;
2. the gain can be large — here approximately 11.46× in median |z|;
3. a large gain still does not guarantee credible verification coverage;
4. the remaining gap is measurable rather than rhetorical.

This is stronger than saying “DCS failed.”

The result is:

> **orthogonal physical information materially improves observability, but the tested
> force-compliance channel still does not provide enough evidence for a reliable
> support/veto decision under the frozen credibility standard.**

## Claim boundary

This experiment is synthetic.

It does not establish:

- measured-world force-sensor verification;
- deployable repair certification;
- prospective D5 confirmation;
- that 40 N is an optimal force amplitude.

The 40 N amplitude is frozen for this partition and may not be retuned using these
results.

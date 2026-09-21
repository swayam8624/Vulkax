# Vulkax Current Research State — 2026-09-21

Status: **RESEARCH EXECUTION COMPLETE FOR THE FROZEN PAPER STORY**

Canonical stable branch:
`main`

## Executive state

- core Vulkax engineering: **complete**
- DCS mathematical implementation: **complete and source-audited**
- D1/D2/D3/D4V execution: **complete**
- GAUGE measured retrospective: **complete**
- fresh orthogonal force-compliance follow-on: **complete**
- paper-level result ledger: **complete**
- deterministic paper figures/tables: **complete**
- branch cleanup/archive: **complete**
- open pull requests: **0 at last tracker audit**
- open issues: **0 at last tracker audit**
- measured prospective force-sensor confirmation: **not part of the frozen paper evidence**
- remaining work after final reproduction/tag: **manuscript writing and presentation only**

## Final scientific story

Vulkax studies a failure mode of executable captured worlds:

> a physical repair can improve ordinary held-out observations while making an
> independent physical mechanism worse.

The project calls this a **deceptive physical repair**.

The research then asks whether an independent counterfactual verifier can reliably
support or veto such edits.

## DCS disposition

The DCS mathematical and systems program is fully implemented.

Frozen prospective outcomes:

- D2: **0/16 resolved**
- D3: **0/6 resolved**
- D4V: **36 proposals**, **14 deceptive**, **22 beneficial**, **0% resolved coverage**
- matched raw/Fisher/max-motion D4V verification channels: **0% resolved coverage**

DCS is therefore an implemented research instrument and falsification laboratory,
not a validated prospective repair-verification policy.

## Quantified information frontier

At the inherited absolute decision reference `|z| >= 2`, D4V produced:

- median DCS `|z|`: approximately **0.0502**
- maximum DCS `|z|`: approximately **0.1765**
- median additional signal required to reach 2: approximately **39.8×**

This rules out the interpretation that DCS merely missed the threshold by a small
amount.

## Fresh orthogonal-information test

To test the information-limit interpretation without retuning DCS, a separate
known-force compliance experiment was preregistered before execution.

Fresh population:

- truth worlds: **6**
- ordinary-heldout-improving repair proposals: **36**
- deceptive: **12**
- beneficial: **24**

Fresh kinematic DCS:

- median `|z|`: **0.04877**
- maximum `|z|`: **0.17279**
- resolved coverage: **0%**

Known-force compliance:

- median `|z|`: **0.55863**
- maximum `|z|`: **1.31911**
- resolved coverage: **0%**
- median standardized-signal gain over fresh DCS: **11.455×**
- median additional signal still required to reach `|z|=2`: **3.58×**

Frozen advancement decision:

`orthogonal_information_gate_failed`

Interpretation:

> A genuinely different physical channel can dramatically increase
> discriminating information, but the tested force-compliance channel still does
> not provide enough uncertainty-normalized evidence to issue credible repair
> support/veto decisions.

The 40 N amplitude, fresh truth worlds, fitting grid and `|z|=2` threshold are
frozen on this partition and may not be retuned.

## Measured retrospective result

GAUGE remains retrospective because the metric mirage motivated/refined the DCS
research.

Across 10 held-out even repeats:

- ordinary face + marker metrics prefer overlap: **10/10**
- marker dark-field prefers overlap: **10/10**
- longitudinal dark-field prefers endpoint: **9/10**

This demonstrates a measured-world channel contradiction: an observational repair
can look better while a mechanism-relevant observable favors the original model.

## Final paper-level claim

Supported:

> Observational improvement is not equivalent to physical improvement in captured
> executable worlds. Mechanism-sensitive verification can itself be limited by
> experimental information content. A separately preregistered orthogonal
> force-compliance channel increased median standardized discriminating signal by
> about 11.46× on fresh synthetic worlds, but still failed to cross the frozen
> credibility threshold.

Not supported:

- DCS as a reliable prospective repair verifier;
- the tested force channel as a reliable prospective repair verifier;
- measured prospective force-sensor generalization;
- universal physical-correctness certification;
- lowering the decision threshold after observing these data.

## Canonical evidence order

1. `research/results/VULKAX_FINAL_RESEARCH_SUMMARY_2026-09-21.md`
2. `research/results/VULKAX_FINAL_RESULTS_2026-09-21.json`
3. `research/paper_data/PAPER_POSITIONING.md`
4. `research/status/DCS_MATH_IMPLEMENTATION_AUDIT_2026-09-21.md`
5. `research/results/ORTHOGONAL_FORCE_COMPLIANCE_RESULT_2026-09-21.md`
6. `research/benchmarks/ORTHOGONAL_FORCE_COMPLIANCE_PROTOCOL_2026-09-21.md`
7. `research/results/DCS_FINAL_BENCHMARK_SUMMARY_2026-09-20.md`
8. `research/results/DCS_INFORMATION_FRONTIER_2026-09-20.md`
9. `research/status/DCS_GAUGE_RETROSPECTIVE_RESULT_2026-09-20.md`
10. `research/literature/CLAIM_GUARD.md`

## Repository state

Working branches are intentionally reduced to:

```text
main
release/1.0.0
legacy/studio-v1-2026-08-10
```

Former divergent working branches were preserved as annotated archive tags before
their branch refs were deleted.

## Stop rule

For this paper, research discovery is frozen.

Do not:

- retune D2/D3/D4V;
- change the force amplitude on the executed force partition;
- lower `|z|=2`;
- reuse frozen truth worlds for a replacement method;
- relabel GAUGE as prospective;
- treat exploratory force AUROC/AUPRC as preregistered headline metrics.

A future real force-sensor or frequency/modal experiment is separate follow-on work
and requires a separately frozen protocol and new data.

## Remaining operation

Before declaring the repository snapshot immutable for manuscript production:

1. latest main smoke CI must pass;
2. exact-head `run_everything.sh` full reproduction must pass;
3. evidence and publication artifacts must upload;
4. annotated tag `paper-freeze-2026-09-21` must be created on that exact successful commit.

After that, only manuscript and presentation work remain.

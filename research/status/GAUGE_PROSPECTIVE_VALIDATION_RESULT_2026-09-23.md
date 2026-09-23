# GAUGE prospective validation result — 2026-09-23

## Frozen validation-stage result

The prospective GAUGE validation lane was executed on the predeclared validation
partition only:

- foam stretching and compression;
- soft and hard materials;
- repeats 5, 6, and 7;
- 12 measured validation worlds;
- 3 predeclared repair cases/world;
- 2 methods/case;
- 36 cases / 72 standardized records.

Observed primary decisions at |z| = 2:

| truth | temporal annihilating witness | raw held-out bundle |
|---|---:|---:|
| SUPPORT | 0 / 12 correct | 0 / 12 correct |
| VETO | 0 / 12 correct | 0 / 12 correct |
| UNRESOLVED/placebo | 12 / 12 correct | 12 / 12 correct |

No final-test repeat was opened.

## Scientific decision

**Do not open GAUGE repeats 8–10.**

This validation result establishes that the current real-trajectory lane has zero
directional decision coverage at the frozen threshold while correctly abstaining on
the placebo cases.

The result must not be repaired by:
- lowering the primary threshold after seeing validation outcomes;
- moving final-test repeats into development;
- changing the support/veto material candidates and calling the same test
  confirmatory;
- discarding unresolved directional cases.

## Required diagnosis before any new GAUGE protocol

Run the saved-output forensic:

```bash
python3 research/analysis/analyze_gauge_validation_failure.py
```

The forensic performs no new simulation. It decomposes the failed decisions into:
- pre-threshold truth ordering;
- baseline-vs-candidate physical response separation;
- repeat/acquisition variance;
- nominal-vs-refined numerical variance.

If the measured trajectory does not reliably rank the metadata-truth material model
ahead of the baseline before uncertainty is applied, the failure is a forward-model
adequacy / material-identifiability problem, not a threshold problem.

This interpretation is consistent with the previously frozen GAUGE evidence that
the current forward world predicts the wrong sign of longitudinal coupling and that
inverse material fitting remained locked.

## Evidence class

This is **prospective validation-stage evidence**, not historical diagnostic
evidence and not final-test confirmation. The global publication summary must keep
those three classes separate.

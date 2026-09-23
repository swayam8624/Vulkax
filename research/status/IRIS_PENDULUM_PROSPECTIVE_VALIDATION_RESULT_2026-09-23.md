# IRIS pendulum prospective validation result — 2026-09-23

## Executed partitions

The frozen IRIS pendulum campaign was executed without opening the reserved
`pendulum_90` final-test repeats.

Development:
- setting: `pendulum_20`
- takes: 01–10
- quality pass: **10 / 10**
- median period-inferred rope-length relative error: **0.0423067838**
- development gate: **PASS**
- standardized records: **220**

Validation:
- setting: `pendulum_45`
- takes: 01–10
- quality pass: **10 / 10**
- median period-inferred rope-length relative error: **0.0242593950**
- standardized records: **220**

The common publication-validation table after combining the IRIS validation records
with frozen historical evidence and the GAUGE validation-stage records contained:
- 688 total records;
- 4 datasets;
- 10 methods;
- 292 prospective validation-stage records;
- 396 historical diagnostic records;
- 0 final-test confirmatory records.

## Aggregate three-way validation result

At the frozen global decision rule |z| = 2:

| method | SUPPORT | VETO | UNRESOLVED/placebo |
|---|---:|---:|---:|
| finite-amplitude period probe | **30 / 50** | **30 / 50** | **10 / 10** |
| small-angle period baseline | **12 / 50** | **30 / 50** | **10 / 10** |

This is a mixed positive validation-stage result:
- the finite-amplitude physical probe retains perfect placebo abstention;
- it resolves materially more SUPPORT cases than the small-angle baseline;
- VETO performance is equal at the aggregate level;
- not every controlled directional repair crosses the frozen |z|=2 threshold.

The aggregate 30/50 directional counts combine the truth-control cases and four
dose-response levels. They must not be interpreted as a single undifferentiated
accuracy number before the per-dose validation forensic is inspected.

## Final-test policy

The `pendulum_90` final-test setting remains unopened.

Before deciding whether to freeze the finite-amplitude method for final test, run
the descriptive validation forensic over the existing 220 validation records. It
reports:
- truth-control accuracy;
- placebo accuracy;
- per-dose detection/coverage;
- expected-sign correctness;
- dose-response monotonicity;
- paired finite-amplitude vs small-angle wins;
- threshold margins.

This forensic does not rerun video tracking and does not authorize threshold or
candidate-schedule retuning.

## Claim guard

The current IRIS result is prospective **validation-stage** evidence, not final-test
confirmation. Any final-test protocol must keep:
- the global |z| = 2 threshold unchanged;
- the candidate schedule frozen;
- the 90-degree setting unopened until the final-test lock is verified.


## Validation forensic

The no-rerun validation forensic reported:

- truth-control correct rate: **1.0**
- placebo correct rate: **1.0**
- large-effect correct rate: **1.0**
- dose-direction sign rate: **1.0**
- dose-response Spearman: **0.9749430094**
- paired finite-amplitude-only wins: **18**
- paired small-angle-only wins: **0**
- paired ties: **92**
- validation median rope-length relative error: **0.0242593950**
- readiness: **validation_supports_freeze_without_retuning**

This supports freezing the finite-amplitude method for the reserved 90-degree final
test without changing the global |z|=2 threshold, tracker, candidate schedule, or
quality gates. This readiness classification is explicitly post-hoc descriptive;
it is not presented as a preregistered confirmatory gate.

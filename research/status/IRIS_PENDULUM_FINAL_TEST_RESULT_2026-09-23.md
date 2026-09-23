# IRIS pendulum locked final-test result — 2026-09-23

## Final-test execution

The reserved IRIS `pendulum_90` split was opened only after creation and
verification of the IRIS-specific final-test lock at repository commit:

`9e56f06145b8a56dd26c1c81e064ed9b185c77d7`

The lock covered the global validation protocol, record schema, exact ten-scene
final world manifest, frozen IRIS final-test configuration, tracker/scoring
implementation, downloader, public-data preparation and adapter code, final
summarizer/runner, validation records and forensic, and runtime package versions.

No threshold, tracker, candidate schedule, scene population, or quality gate was
changed after final-test opening.

Final population:
- dataset revision: `rasulkhanbayov/IRIS@c253822f55431ca80ef2084de4bc5e79d1a488f1`
- setting: `pendulum_90`
- takes: 01–10
- physical experimental units: **10 real videos**
- quality-pass takes: **10 / 10**
- standardized confirmatory rows: **220**

## Physical parameter recovery

Median period-inferred rope-length relative error on the 90-degree final setting:

**0.1681086451 (16.81%)**

This is substantially larger than the 45-degree validation median error
(2.43%), establishing a real generalization/large-amplitude difficulty rather
than a tuned easy-case result. All ten final videos still passed the frozen
tracking quality criteria.

## Locked three-way decision result

At the unchanged global decision rule `|z| = 2`:

| method | strict accuracy | SUPPORT | VETO | UNRESOLVED/placebo |
|---|---:|---:|---:|---:|
| finite-amplitude period probe | **0.8181818182** | **40 / 50** | **40 / 50** | **10 / 10** |
| small-angle period baseline | **0.3636363636** | **0 / 50** | **30 / 50** | **10 / 10** |

The primary finite-amplitude method therefore produced:
- **90 / 110** correct controlled decisions;
- **40 / 50** SUPPORT decisions correct;
- **40 / 50** VETO decisions correct;
- **10 / 10** correct abstentions on placebo/UNRESOLVED cases.

The matched small-angle baseline produced:
- **40 / 110** correct controlled decisions;
- **0 / 50** SUPPORT decisions correct;
- **30 / 50** VETO decisions correct;
- **10 / 10** correct placebo abstentions.

## Paired comparison

Across the 110 paired candidate/take cases:

- finite-amplitude primary only correct: **50**
- small-angle baseline only correct: **0**
- ties: **60**

Thus the primary method was never uniquely worse than the matched baseline on
the locked final population and was uniquely correct on 50 paired cases.

## Combined publication-validation evidence

After adding the locked final-test records, the common evidence table contained:

- total records: **908**
- datasets: **4**
- methods: **10**
- final-test confirmatory records: **220**
- prospective validation-stage records: **292**
- historical diagnostic records: **396**

The 220 confirmatory rows must not be treated as 220 independent physical
experiments. They arise from **10 locked real videos** crossed with 11 controlled
repair cases and 2 methods. Statistical interpretation should retain the
video/pair clustering structure.

## Scientific interpretation

This is positive locked confirmatory evidence for the narrow claim that an
independent period-based physical interrogation can improve adjudication of
controlled rope-length repairs in real monocular pendulum video compared with the
matched small-angle baseline.

It does **not** establish that:
- every Reality Probe channel or physical quantity is identifiable;
- the same method applies unchanged to cloth, collision, or multi-body dynamics;
- the verification observation is an independent sensor modality;
- 220 records equal 220 independent physical trials.

The GAUGE prospective result remains an information-limit negative result, so the
combined research story is intentionally mixed: some channels are non-identifying
and should remain unresolved, while a better-matched physical interrogation on
IRIS generalizes to an untouched final setting with materially stronger
three-way decisions than its matched baseline.

## Post-final rule

The final-test result is frozen. Do not:
- retune the global `|z|=2` threshold against these results;
- change the candidate factors and relabel the same final population confirmatory;
- drop difficult final videos;
- modify the tracker and replace this final result with the modified run;
- reinterpret validation or GAUGE failures as though they had not occurred.

Any later method improvement must be reported as a new post-final experiment.

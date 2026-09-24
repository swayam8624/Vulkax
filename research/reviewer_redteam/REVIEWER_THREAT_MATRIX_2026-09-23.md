# Reviewer Red-Team Matrix — 2026-09-23

Purpose: convert hostile-reviewer objections into explicit closure tests. This is a
post-final hardening plan. It must not modify or replace the locked IRIS pendulum
final result.

| ID | Reviewer attack | Severity | Closure requirement |
|---|---|---|---|
| R01 | Central contribution is unclear / looks like several papers stapled together | fatal | One causal story: deceptive rewrite -> failed weak probe -> information diagnosis -> matched probe -> locked confirmation |
| R02 | Incomplete validation | major | Robustness, clustering-aware statistics, strong baselines, second domain |
| R03 | Not a graphics contribution | fatal | End-to-end captured-world rewrite/commit/veto visual demo |
| R04 | Generic VVUQ/credibility is old | fatal | Novelty restricted to rewrite-level counterfactual commit/veto/unresolved contract |
| R05 | This is generic identifiability | major | Show rewrite-specific decision differs from global parameter identification |
| R06 | IRIS already does inverse physics | fatal if mispositioned | Treat IRIS/other estimators as proposal generators, not novelty |
| R07 | Prior video-physics estimators already exist | major | Import/compare public estimator proposals |
| R08 | Pendulum is too toy-like | major | Add a second pre-frozen non-pendulum real-video domain |
| R09 | 90-degree model omits damping/nonidealities | major | Post-final nonlinear/damped alternative baseline and model-discrepancy analysis |
| R10 | Finite-amplitude vs small-angle is textbook | fatal to baseline story | Add strong baselines; small-angle stays only as a diagnostic baseline |
| R11 | Small-angle baseline is a straw man | fatal | Compare direct inversion, nonlinear/damped fit, public IRIS estimator |
| R12 | Ten takes are one physical regime | major | Report video-level units and add second physical regime/domain |
| R13 | 220 rows are pseudoreplication | fatal if mishandled | Cluster bootstrap by video and paired scene-level inference |
| R14 | The reported z is not a statistical z-score | major | Call it standardized evidence score outside frozen records; derive/label semantics clearly |
| R15 | Why threshold 2? | major | Keep frozen threshold; add development/null calibration and sensitivity as secondary analysis only |
| R16 | Abstention/selective prediction is old | major | Do not claim abstention itself as novel; evaluate empirical risk/coverage |
| R17 | Ground-truth measurement uncertainty ignored | major | Propagate independent measurement uncertainty into labels/sensitivity |
| R18 | Verification vs validation terminology is sloppy | major | Use explicit code verification / empirical validation / uncertainty language |
| R19 | Digital-twin credibility already exists | major | Scope contribution to individual physical rewrite decisions |
| R20 | Parameter closeness is not counterfactual utility | fatal to broad claim | Evaluate unseen future/interventional behavior after commit/veto |
| R21 | IRIS selected only after GAUGE failed | major | Freeze a second positive domain before downloading unseen final media |
| R22 | “Preregistered” overclaim | moderate | Use “repository-frozen before access” unless externally registered |
| R23 | One 90-degree video existed before final lock | serious | Second blind final set excludes all take-01 files and is not downloaded before freeze |
| R24 | Candidate repairs use ground truth | fatal for deployment | GT-free proposal generator; GT only labels/evaluates after proposal |
| R25 | No real repair pipeline | fatal | Proposal -> ordinary improvement -> independent verifier -> commit/veto -> future behavior |
| R26 | IRIS controlled candidates disconnected from deceptive repairs | fatal | Mine naturally generated observationally improving but physically worse proposals |
| R27 | No image/video corruption robustness | major | Noise/blur/frame loss/occlusion/crop/frame-rate sweeps |
| R28 | Tracker succeeds only on easy static camera | major | Tracker-error/pose/crop stress tests and scoped claims |
| R29 | Numerical error bounds weak in synthetic work | major | Preserve as empirical unless convergence analysis supports stronger wording |
| R30 | GAUGE model misspecified, so abstention is unsurprising | moderate | Present GAUGE as model-adequacy/information boundary, not success benchmark |
| R31 | Information/computation budgets are unfair | major | Report frames, observations, compute, priors, and interventions per method |
| R32 | Missing modern/public baselines | major | Official IRIS result importer + analytic/direct/damped baselines |
| R33 | Missing ablations | major | Physics model, standardization, three-way gate, uncertainty floor, raw heuristic ablations |
| R34 | No systems cost | moderate | Wall-clock, memory, video frames, extra evidence per decision |
| R35 | Reproduction questionable | major | Pinned data revisions, hashes, env/runtime, exact commands, evidence bundle |
| R36 | Paper narrative incoherent | fatal | Every section serves rewrite verification under information limits |

## Submission gate

Do not call the hardening campaign complete until:
1. R10/R11 strong-baseline objection is closed;
2. R13 clustered statistics are generated from the locked final records;
3. R24/R25 GT-free proposal-to-gate experiment is executed;
4. R21/R23 second blind non-pendulum final domain is executed under a pre-access lock;
5. R27 robustness battery is executed and failures preserved;
6. R03 end-to-end graphics/captured-world decision demo exists;
7. R04/R19 novelty map explicitly distinguishes rewrite-level verification from generic VVUQ;
8. all resulting claims are reflected in the claim guard without rewriting the locked IRIS result.


## Closure status — 2026-09-24

This status is evidence-based. A threat is not marked closed merely because tooling
exists; execution-dependent items remain open until their post-final outputs are
generated.

### Closed by committed evidence / manuscript changes

- **R01 / R36 — contribution and narrative:** manuscript now follows one causal
  sequence: deceptive rewrite -> weak/information-poor probes -> changed physical
  channel -> locked IRIS pendulum success -> preserved GAUGE/free-fall failures.
- **R04 / R19 — VVUQ / digital-twin novelty:** explicit novelty map scopes the
  contribution to the rewrite-level SUPPORT/VETO/UNRESOLVED transaction rather
  than generic verification, validation, UQ, identifiability, or credibility.
- **R06 — IRIS positioning:** IRIS is used as measured inverse-physics / validation
  context and a public data source; pendulum/free-fall equations are not claimed
  as novel estimators.
- **R14 — score semantics:** manuscript uses "standardized evidence score" rather
  than claiming a standard-normal z statistic.
- **R18 — terminology:** code verification, empirical validation, frozen
  confirmatory evidence, retrospective evidence, and post-final diagnostics are
  kept as separate evidence classes.
- **R22 — preregistration wording:** "preregistered" overclaim removed; experiments
  are described as repository-frozen before final-data access.
- **R23 — contaminated take-01:** locked IRIS pendulum final result excludes all
  take-01 media.
- **R30 — GAUGE interpretation:** retrospective GAUGE is a channel/model-adequacy
  diagnostic; a separate frozen prospective GAUGE validation failure is now
  surfaced explicitly rather than presented as success.
- **R36 — evidence consistency:** README, results index, novelty map, both manuscript
  sources, and claim plan now agree on the locked pendulum success, prospective
  GAUGE failure, and failed IRIS free-fall validation.

### Partially closed / bounded by preserved negative evidence

- **R02 / R08 / R12 / R21 — second real-video domain:** a separately frozen IRIS
  free-fall domain was attempted. Development passed, untouched validation then
  failed badly (111.7% median acceleration relative error on 5/5 quality-pass
  videos). The failure is retained and the designated final split remains unopened.
  This is valid cross-domain falsification evidence, but it is not a second positive
  final-domain confirmation.
- **R15 — threshold sensitivity:** the frozen |score|=2 rule remains primary and a
  secondary threshold sweep is reported without redefining the decision policy.
  A full calibration argument remains secondary rather than a new primary claim.
- **R28 — tracker/general-camera scope:** the failed free-fall lane exposes a real
  event/scale/tracking limitation and is now part of the stated scope. Broader
  camera/pose stress remains an optional extension, not evidence already claimed.
- **R29 — numerical-error strength:** manuscript keeps numerical claims empirical
  where convergence does not justify stronger theory.
- **R35 — reproducibility:** data revisions, frozen result ledgers, exact commands,
  hashes, and replay tooling exist. Final closure waits on the independent CI replay
  of the locked pendulum campaign.

### Closed by completed post-final execution

The full post-final pendulum hardening battery was already executed on
2026-09-23 on the original Apple-silicon/macOS environment at repository
commit `3aa0fb14e98956ddf6e3a8a7388347c9a2ba278d`. The canonical result
ledger is now
`research/results/IRIS_PENDULUM_POSTFINAL_REVIEWER_HARDENING_2026-09-23.json`
with a human-readable companion Markdown file.

- **R03 — reviewer-facing end-to-end measured rewrite decision:** closed for the
  post-final pendulum lane by the GT-hidden proposal -> ordinary holdout ->
  reserved verification transaction and generated storyboard.
- **R07 — official IRIS context:** closed as a contextual benchmark-positioning
  item. Pinned upstream IRIS parameter-recovery tables are fetched and retained
  with an explicit task-mismatch guard; they are not represented as matched
  SUPPORT/VETO/UNRESOLVED baselines.
- **R09 / R10 / R11 / R32 — stronger baselines / damping:** closed for the
  pendulum study. A direct finite-amplitude residual reaches 45.45% strict
  accuracy and a damped nonlinear ODE residual reaches 9.09%, versus 81.82% for
  the locked probe. The locked probe is uniquely correct on 40 and 80 cases,
  respectively; neither comparator is uniquely correct on any case.
- **R13 — statistical dependence / pseudoreplication:** closed for the reported
  pendulum final set. The physical video is the inferential unit. The primary
  method is better on 10/10 videos, the baseline on 0/10, with no ties and a
  two-sided exact sign-test p=0.001953125. Video-level bootstrap summaries are
  retained as secondary descriptive intervals.
- **R17 — measured ground-truth uncertainty:** closed for the available IRIS
  rope-length uncertainty metadata. Canonical labels are stable on 110/110 cases
  through both ±1 sigma and ±2 sigma intervals.
- **R24 / R25 / R26 — ground-truth leakage / manually chosen proposals /
  transaction realism:** closed for a separate post-final GT-hidden experiment.
  The proposal artifact is written and SHA-256 closed before truth join; all 10
  generated candidates improve the ordinary holdout, 8 are physically worse and
  2 are physically better, and the reserved verifier is correct on all 10.
- **R27 — corruption robustness:** closed as an empirical robustness study, with
  an explicit failure boundary. A full Metal sweep covers 180 condition-video
  runs with zero failed videos and 10/10 clean-equivalence. Tested noise, blur,
  frame duplication/drop, occlusion, and crop preserve 81.82% accuracy. Temporal
  downsampling does not: 30 fps gives 63.64%, 15 fps gives 45.45%. Placebo
  abstention remains 100%.
- **R28 — tracker robustness:** substantially closed for the tested image
  corruptions, but bounded by the observed frame-rate sensitivity and the failed
  free-fall domain. It must not be generalized to arbitrary cameras, motion
  regimes, or temporal sampling.

### Still partial / scoped limitations after hardening

- **R20 — counterfactual usefulness:** strengthened by the GT-hidden natural
  proposal transaction, but broad usefulness across unrelated edit classes is
  still outside the evidence.
- **R31 — wall-clock/system cost:** the full Metal robustness sweep reports
  694.05 s for 180 condition-video runs, but the paper does not yet provide a
  complete end-to-end cost model for every Reality Probe subsystem.
- **R33 — component ablation:** the paper now contains channel, baseline,
  dependence, uncertainty, and corruption analyses; it still does not contain a
  complete factorial ablation of every systems component.
- **R34 — system advantage over simpler pipelines:** stronger scientific
  comparators are quantified, but the systems-level cost/complexity tradeoff is
  not established as a universal advantage.
- **R02 / R08 / R12 / R21 — domain breadth:** remains a true limitation. The
  second real-video equation family, IRIS free-fall, failed untouched validation.
  This negative result is retained rather than converted into a positive
  generalization claim.

The completed post-final evidence is secondary only. It strengthens the locked
pendulum result but does not replace or retune it. Cross-platform CI replays are
reproducibility checks; they are not allowed to redefine the already frozen
measured result.


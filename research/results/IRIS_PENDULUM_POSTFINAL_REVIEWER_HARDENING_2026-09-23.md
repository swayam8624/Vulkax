# IRIS pendulum post-final reviewer hardening — 2026-09-23

**Evidence class:** post-final secondary analysis.  
**Locked result remains unchanged:** `IRIS_PENDULUM_FINAL_TEST_RESULT_2026-09-23.json`.

## Provenance

Executed on the user's Apple-silicon macOS machine after updating `main` to:

```text
3aa0fb14e98956ddf6e3a8a7388347c9a2ba278d
perf: Metal-accelerate IRIS robustness on Apple Silicon
```

Command:

```bash
bash research/scripts/run_postfinal_reviewer_hardening.sh \
  --profile full \
  --with-freefall-validation
```

The pendulum hardening analyses and deterministic visuals completed before the
optional free-fall add-on. The shell command later stopped because that older
free-fall development gate failed. That exit does **not** invalidate the already
completed pendulum post-final evidence.

## Cluster-correct statistics

The physical experimental unit is the **video**, not the nested controlled row.

| Quantity | Result |
|---|---:|
| Physical videos | 10 |
| Primary accuracy | 0.8181818 |
| Primary 95% video-bootstrap CI | [0.8181818, 0.8181818] |
| Small-angle baseline accuracy | 0.3636364 |
| Baseline 95% video-bootstrap CI | [0.3636364, 0.3636364] |
| Accuracy difference | +0.4545455 |
| Difference 95% video-bootstrap CI | [0.4545455, 0.4545455] |
| Videos primary better / baseline better / ties | 10 / 0 / 0 |
| Two-sided exact video sign-test p | 0.001953125 |

The degenerate bootstrap intervals are a property of this final set: every
physical video has the same per-video aggregate accuracy for each compared
method. The sign test is therefore the more transparent small-(n) paired
summary.

## Stronger post-final comparators

| Method | Strict accuracy | SUPPORT | VETO | Placebo | Locked-primary-only correct | Comparator-only correct |
|---|---:|---:|---:|---:|---:|---:|
| Locked finite-amplitude probe | 0.8182 | 0.80 | 0.80 | 1.00 | — | — |
| Direct finite-amplitude period residual | 0.4545 | 0.40 | 0.40 | 1.00 | 40 | 0 |
| Damped nonlinear ODE residual | 0.0909 | 0.00 | 0.00 | 1.00 | 80 | 0 |

These are post-final comparators. They strengthen the baseline story but do not
replace the locked confirmatory result.

## GT-hidden proposal -> verification transaction

The post-final proposal experiment generated the candidate before joining ground
truth, wrote the blind proposal artifact, and hashed it before evaluation.

```text
blind proposal SHA-256:
00e78c7755dec7f40a34cb1eaff0685ece5b8be79b78185778a441a76fb37834
```

Results across 10 physical videos:

- ordinary-improving proposals: **10**
- physically better: **2**
- physically worse / deceptive: **8**
- verifier correct on ordinary-improving proposals: **10/10**
- verifier accuracy on these proposals: **1.0**

This closes the reviewer concern that the measured positive result exists only
on manually chosen candidate factors. It remains post-final evidence rather than
a replacement confirmatory experiment.

## Measured-truth uncertainty

Canonical support/veto/unresolved labels remain stable over the adapter's
measured rope-length uncertainty intervals:

| Interval | Stable labels |
|---|---:|
| (pm 1sigma) | 110 / 110 |
| (pm 2sigma) | 110 / 110 |

## Proposal/probe dependence sweep

The blind proposal uses the first 40% of each video. The verification window is then moved to produce overlap \(\rho \in \{0,0.25,0.5,0.75,1\}\).

| $\rho$ | n videos | Strict accuracy | Coverage | Median absolute score |
|---:|---:|---:|---:|---:|
| 0.00 | 10 | 1.0 | 1.0 | 18.0780843 |
| 0.25 | 10 | 1.0 | 1.0 | 18.0780843 |
| 0.50 | 10 | 1.0 | 1.0 | 17.9331842 |
| 0.75 | 10 | 1.0 | 1.0 | 17.2341062 |
| 1.00 | 10 | 1.0 | 1.0 | 16.2068900 |

The post-final GT-hidden proposal decision remains correct throughout the tested
overlap sweep. This does **not** turn same-video evidence into an independent
sensor modality; it only shows that the observed post-final decision is not
explained by the tested proposal/probe window overlap.

## Full corruption robustness

Backend: **Metal**  
Profile: **full**  
Physical videos per condition: **10**  
Total condition-video runs: **180**  
Failed videos: **0**  
Clean Metal decision-vector equivalence: **10/10**  
Elapsed time: **694.05 s**

| Condition | Value | Strict accuracy | SUPPORT | VETO | Placebo |
|---|---:|---:|---:|---:|---:|
| clean | 0 | 0.8182 | 0.80 | 0.80 | 1.00 |
| noise | 5 | 0.8182 | 0.80 | 0.80 | 1.00 |
| noise | 15 | 0.8182 | 0.80 | 0.80 | 1.00 |
| noise | 30 | 0.8182 | 0.80 | 0.80 | 1.00 |
| blur | 3 | 0.8182 | 0.80 | 0.80 | 1.00 |
| blur | 7 | 0.8182 | 0.80 | 0.80 | 1.00 |
| blur | 15 | 0.8182 | 0.80 | 0.80 | 1.00 |
| frame drop | 0.10 | 0.8182 | 0.80 | 0.80 | 1.00 |
| frame drop | 0.25 | 0.8182 | 0.80 | 0.80 | 1.00 |
| frame drop | 0.50 | 0.8182 | 0.80 | 0.80 | 1.00 |
| fps | 30 | 0.6364 | 0.60 | 0.60 | 1.00 |
| fps | 15 | 0.4545 | 0.40 | 0.40 | 1.00 |
| occlusion | 0.05 | 0.8182 | 0.80 | 0.80 | 1.00 |
| occlusion | 0.15 | 0.8182 | 0.80 | 0.80 | 1.00 |
| occlusion | 0.30 | 0.8182 | 0.80 | 0.80 | 1.00 |
| crop | 0.02 | 0.8182 | 0.80 | 0.80 | 1.00 |
| crop | 0.05 | 0.8182 | 0.80 | 0.80 | 1.00 |
| crop | 0.10 | 0.8182 | 0.80 | 0.80 | 1.00 |

### Robustness boundary

The method is invariant over the tested noise, blur, duplicated/dropped-frame,
occlusion, and crop conditions. The clear failure mode is **temporal
downsampling**: 30 fps reduces strict accuracy to 63.64%, and 15 fps reduces it
to 45.45%. Placebo abstention remains 100% even in those degraded cases.

This limitation should be stated in the manuscript. The robustness experiment
should not be summarized as universal corruption invariance.

## Deterministic reviewer-facing assets

The completed run produced:

- `fig_postfinal_strong_baselines.svg`
- `fig_postfinal_clustered_videos.svg`
- `fig_postfinal_robustness.svg`
- `fig_gt_hidden_proposal_gate.svg`
- `fig_gt_hidden_rewrite_storyboard.png`

under `build/visualization/reviewer-hardening/`.

## Official IRIS context

The run also fetched pinned upstream IRIS parameter-recovery tables. These are
**contextual parameter-recovery references**, not matched
SUPPORT/VETO/UNRESOLVED verification baselines and must not be compared as if the
tasks were identical.

For example, the published IRIS baseline table reports mean relative rope-length
error 1.20878 on `pendulum_90` (n=10). That contextual number is useful for
showing task difficulty, but the manuscript must preserve the task mismatch.

## Free-fall add-on

After all pendulum hardening outputs and visuals were generated, the optional
`--with-freefall-validation` add-on ran the then-current older free-fall lane
and failed its development gate:

```text
quality-pass: 4/4
median acceleration relative error: 0.9856055699
truth-control accuracy: 1.0
direction-sign rate: 1.0
gate pass: false
```

That result is **not** the canonical free-fall paper result. It was superseded by
the later V6.6 protocol, which passed development and then failed untouched
validation with 111.7% median acceleration relative error. The V6.6/V6.7
negative-result ledger remains authoritative for the free-fall claim.

## Disposition

These results close the major reviewer-hardening gaps around:

- physical-video clustering;
- stronger comparator baselines;
- GT-hidden / naturally generated proposals;
- measured ground-truth uncertainty;
- same-video proposal/probe overlap;
- corruption robustness and its temporal-resolution failure mode.

They do not close the broader cross-domain limitation: the second real-video
free-fall domain failed prospective validation and remains a preserved negative
result.

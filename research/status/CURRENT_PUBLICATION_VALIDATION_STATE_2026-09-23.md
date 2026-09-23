# Current publication-validation state — 2026-09-23

## Completed evidence

### Frozen historical program
- D2: frozen negative validation, zero resolved coverage.
- D3: adaptive-order/witness-space follow-on, negative.
- D4V: 36 repair proposals, zero resolved coverage at the inherited credibility rule.
- OFC: 11.455x median standardized-signal gain over DCS but still below |z|=2.
- GAUGE retrospective: mechanism-channel contradiction preserved.

### Prospective GAUGE validation
- 12 real stretch/compression validation worlds.
- SUPPORT: 0/12 correct for both temporal annihilating witness and raw bundle.
- VETO: 0/12 correct for both.
- placebo/UNRESOLVED: 12/12 correct for both.
- raw truth ordering: 100% correct before thresholding.
- median signal/repeat ratio: 0.0027697064.
- median signal/numerical ratio: 0.1179258867.
- disposition: **information-limited; final GAUGE repeats not opened**.

### Prospective IRIS development + validation
Development `pendulum_20`:
- 10/10 quality-pass.
- median rope-length relative error: 4.23%.

Validation `pendulum_45`:
- 10/10 quality-pass.
- median rope-length relative error: 2.43%.
- finite-amplitude aggregate SUPPORT: 30/50.
- finite-amplitude aggregate VETO: 30/50.
- placebo: 10/10.
- validation forensic:
  - truth control: 100%;
  - placebo: 100%;
  - large effects: 100%;
  - directional sign: 100%;
  - dose Spearman: 0.974943;
  - paired finite-amplitude wins: 18;
  - paired small-angle wins: 0.

### Locked IRIS final test
Final `pendulum_90`:
- exact population frozen before opening: 10 videos.
- exact code/config/runtime lock verified.
- 10/10 quality-pass.
- median rope-length relative error: 16.81%.
- finite-amplitude period probe: **90/110 = 81.82% strict accuracy**.
- small-angle baseline: **40/110 = 36.36% strict accuracy**.
- primary SUPPORT: **40/50**.
- primary VETO: **40/50**.
- primary placebo: **10/10**.
- paired: **50 primary-only wins, 0 baseline-only wins, 60 ties**.
- confirmatory standardized rows: **220** from **10 physical video units**.
- no post-final retuning.

## Current scientific position

The evidence no longer supports an “everything is information-limited” story.

The supported position is mixed:

1. ordinary observational improvement does not guarantee physical improvement;
2. some verification channels are genuinely non-identifying and should abstain;
3. changing or improving the physical interrogation can increase observability;
4. on a locked real-video pendulum regime, a physically matched finite-amplitude
   period probe generalizes materially better than a matched small-angle baseline;
5. success on that one channel does not authorize universal physical-verification
   claims.

## Reviewer-hardening extension

A post-final hostile-reviewer campaign is now implemented but has not yet been
promoted into result claims. It includes:

- video-clustered statistics using the ten physical videos as the resampling unit;
- direct-period and damped-nonlinear strong verification baselines;
- GT-hidden proposal generation with proposal SHA closure before truth join;
- noise/blur/frame-loss/temporal-subsampling/occlusion/crop robustness;
- proposal/probe overlap-dependence sweeps;
- measured rope-length truth-uncertainty sensitivity;
- pinned official IRIS parameter-recovery reference tables;
- evidence-driven measured-video rewrite-gating visuals;
- a separately frozen non-pendulum IRIS free-fall replication.

The first free-fall campaign explicitly forbade all take-01 videos. Its V5 method
passed development on drop_50/02-05 but **failed held-out validation** on
drop_100/02-05 (3/4 quality-pass, median acceleration relative error 2.2237,
truth-control 0.0, placebo FAR 0.0, directional sign 0.0). That validation is now
permanently nonconfirmatory.

A fresh V6 rescue is frozen on disjoint unused takes:
- development: drop_50/06-10;
- validation: drop_100/06-10;
- final remains unopened: drop_150/02-10.

V6 remains **development-only and unconfirmed**. The fresh validation
`drop_100/06-10` has not been opened by the development-only rescue commands.

Development history is retained rather than overwritten:
- initial V6: 4/5 quality-pass, median acceleration relative error 0.4991, FAIL;
- V6.1: 3/5 quality-pass, median error 0.6083, FAIL;
- V6.2: implementation error first, then 3/5 quality-pass / median 0.3899, FAIL;
- V6.3: implementation defects were found and fixed before validation;
- V6.4: 2/5 quality-pass, zero implementation errors, median error 0.5328, FAIL.

The active development revision is V6.5. It retains ball identity and the global
top-bottom spatial scale, but replaces the brittle assumption that every observed
fragment begins at zero-velocity release. It fits
`p(t)=a+b t+0.5 c t^2` on the moving interior with nuisance
position/velocity, requires sufficient observed motion support, and treats
release/impact roots as optional diagnostics rather than eligibility conditions.
Target gravity is not used for candidate selection/ranking. Existing development/validation gates, repair
schedule, and `|S|=2` evidence threshold are unchanged.

The V6 development/validation runner has no final-download mode. A separate V6
final runner can request `drop_150/02-10` only after fresh validation passes and
a V6-specific hash lock is created. Until that happens, the paper has **no
free-fall confirmation claim**.

## Remaining publication extensions

After the hardening runs are executed, remaining optional strengthening includes:

- RGBench cloth validation;
- genuinely independent-sensor / physical-bench confirmation;
- any further graphics/system demonstration required by manuscript positioning.

Any new method introduced after the IRIS pendulum final opening is a
**post-final experiment** and must not replace the locked confirmatory result.

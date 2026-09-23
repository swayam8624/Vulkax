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

The free-fall campaign explicitly forbids all take-01 videos. Development uses
drop_50/02-05, validation uses drop_100/02-05, and its nine-video final set is
drop_150/02-10. The final downloader cannot request those nine videos until the
free-fall validation gate passes and a domain-specific lock is created.

## Remaining publication extensions

After the hardening runs are executed, remaining optional strengthening includes:

- RGBench cloth validation;
- genuinely independent-sensor / physical-bench confirmation;
- any further graphics/system demonstration required by manuscript positioning.

Any new method introduced after the IRIS pendulum final opening is a
**post-final experiment** and must not replace the locked confirmatory result.

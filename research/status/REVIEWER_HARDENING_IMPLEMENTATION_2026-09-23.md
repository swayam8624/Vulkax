# Reviewer-hardening implementation state — 2026-09-23

This document records infrastructure state only. It does not claim experiment
outcomes that have not yet been executed on the local frozen data.

## Implemented post-final attack batteries

- video-clustered bootstrap and exact paired scene-level sign test;
- strong post-final verification baselines:
  - direct finite-amplitude period residual;
  - damped nonlinear pendulum ODE residual;
- GT-hidden baseline->candidate repair proposal pipeline:
  - baseline from first 20% of video;
  - candidate from first 40%;
  - ordinary held-out acceptance on 40-50%;
  - Reality Probe verification on disjoint final 50%;
  - proposal artifact SHA-256 closed before truth join;
- deterministic corruption robustness:
  - pixel noise;
  - Gaussian blur;
  - frame loss;
  - temporal subsampling;
  - central occlusion;
  - crop/pose proxy;
- proposal/probe overlap dependence sweep;
- independently measured rope-length uncertainty sensitivity;
- pinned official IRIS parameter-recovery reference import at upstream commit
  `0688a57e0e8b5a7306d67c5e22d999bec7566a70`;
- deterministic reviewer-hardening SVGs;
- measured-video GT-hidden rewrite storyboard and MP4;
- one-command post-final hardening runner.

## Second blind domain implemented

IRIS Dropping_ball is frozen as a different equation family:

- development: drop_50 takes 02-05;
- validation: drop_100 takes 02-05;
- final: drop_150 takes 02-10;
- take 01 is forbidden because it existed in the original core profile;
- final downloader refuses to run without a valid free-fall-specific lock;
- final lock covers code/config/runtime, validation evidence, and exact nine-video
  final world manifest;
- synthetic free-fall video tracker regression is included in CI.

## Execution boundary

The pendulum post-final attack batteries require the existing local
`build/publication-validation/iris-pendulum-final-test` outputs.

The blind free-fall campaign additionally requires downloading new IRIS videos.
The final nine drop_150 videos are intentionally not requested by the
development/validation runner.

No result from these new scripts should enter manuscript claims until the actual
generated output is inspected and committed as a result-only record.

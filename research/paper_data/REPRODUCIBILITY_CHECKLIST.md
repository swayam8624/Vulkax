# Paper-Data / Reproducibility Checklist

This checklist defines completion of the repository-side evidence package. It does
not imply acceptance, novelty, or a positive prospective result.

## Code and environment

- [x] canonical research branch documented
- [x] exact git commit captured by full runner
- [x] compiler/CMake/Python versions captured
- [x] CPU/GPU/runtime information captured
- [x] complete CMake build in one-command runner
- [x] full CTest suite in one-command runner
- [x] native backend conformance when available
- [x] release/evidence/CLI contract validation

## Synthetic research evidence

- [x] D1 constructed positive control
- [x] solver-native fixed-witness discovery
- [x] automatic active-selection comparison
- [x] D2 fresh frozen validation
- [x] D3 witness-space/adaptive-order study
- [x] D4V pair-specific repair-veto study
- [x] fair raw/Fisher/max-motion/random baselines
- [x] negative outcomes preserved rather than threshold-tuned
- [x] orthogonal force-compliance follow-on preregistered before execution
- [x] fresh force-compliance experiment executed on untouched truth worlds
- [x] 11.46x median signal gain preserved without retuning the failed advancement gate

## Measured evidence

- [x] public GAUGE subset fetcher
- [x] measured-only effective-span prerequisite
- [x] preregistered effective-span validation
- [x] definitive endpoint/overlap forward simulations
- [x] per-trial GAUGE retrospective table
- [x] aggregate GAUGE summary
- [x] measured result explicitly marked retrospective
- [x] D5 measured-confirmation stop rule completed — fresh positive measured confirmation was intentionally not consumed because the fresh orthogonal force-compliance channel still remained below the frozen resolved-decision threshold

## Paper-facing data

- [x] canonical human-readable benchmark summary
- [x] machine-readable final JSON ledger
- [x] machine-readable final CSV benchmark table
- [x] experiment matrix
- [x] executed ablation matrix
- [x] deterministic figure generator
- [x] source CSV for every generated figure
- [x] orthogonal-force publication figure/table
- [x] mathematics-to-code audit
- [x] stage-outcome table
- [x] claim-boundary table
- [x] figure/table source map
- [x] limitations and kill criteria
- [x] novelty-threat map
- [x] final claim guard

## Reproduction package

- [x] root one-command runner
- [x] live progress for long GAUGE stage
- [x] reproduction validator against frozen numbers
- [x] evidence bundle assembler
- [x] SHA-256 manifest
- [x] SHA256SUMS
- [x] command logs
- [x] system provenance
- [x] portable tar.gz archive + checksum
- [x] smoke CI
- [x] manual/full CI workflow

## Manuscript boundary

Deliberately not included:
- manuscript title/abstract prose;
- Introduction/Related Work prose;
- Methods prose;
- Results prose;
- Discussion/Conclusion prose.

All source evidence needed to write those sections from the **executed research
sequence** is indexed in this package.

## Scientific boundary

The current package is complete for the research that was actually executed.

It intentionally does not contain a fresh positive D5 measured confirmation. The
frozen DCS gates were negative, and the separately preregistered force-compliance
follow-on improved median standardized signal by 11.46× but still produced 0%
resolved coverage. The stop decision is therefore complete; the absence of a D5
measured run is a scientific boundary, not unfinished repository work.


## Publication-validation extension — 2026-09-23

The following items are intentionally separate from the frozen 2026-09-21 result.
Checked items mean infrastructure exists; unchecked items require new scientific
execution and may not be claimed from old data.

### Reviewer-facing infrastructure
- [x] common SUPPORT/VETO/UNRESOLVED record schema
- [x] frozen primary |z|=2 decision semantics
- [x] final-test protocol hash/lock utility
- [x] controlled truth/null/dose/robustness/dependence trial planner
- [x] cluster-bootstrap confidence intervals
- [x] risk-coverage and selective-risk tables
- [x] negative-control false-assertion analysis
- [x] evidence reliability bins
- [x] paired baseline comparison
- [x] dose-response rank analysis
- [x] robustness slice analysis
- [x] failure-case ledger
- [x] commit-gate harm/benefit accounting
- [x] precision-based sample-size planning
- [x] measured-world prospective protocol
- [x] CI smoke test for the validation framework

### New evidence still required
- [x] execute SUPPORT/VETO/UNRESOLVED controlled trials on prospective GAUGE validation worlds (negative information-limit result preserved)
- [x] execute GAUGE placebo/null controls (12/12 correct unresolved for both methods)
- [x] execute IRIS pendulum SUPPORT/VETO/UNRESOLVED + placebo + dose-response validation campaign
- [x] execute IRIS dose-response sweep across validation and locked final-test partitions
- [ ] report final detection limits from frozen per-factor tables without retuning
- [ ] execute measurement-noise, pose-noise, and missing-observation sweeps
- [ ] execute proposal/probe channel-dependence sweep
- [x] ingest GAUGE prospective validation-stage records into the common schema
- [x] ingest IRIS pendulum validation-stage records into the common schema
- [ ] ingest RGBench validation-stage records into the common schema
- [x] freeze IRIS pendulum final-test configuration and lock machinery before opening the reserved 90-degree split
- [x] execute locked IRIS pendulum 90-degree final-test campaign without threshold retuning
- [ ] obtain prospective measured physical confirmation if feasible
- [ ] update manuscript claims only after those results exist


### Executed GAUGE prospective gate
- [x] GAUGE stretch/compression validation partition executed without opening final repeats
- [x] truth-ordering forensic: 100% directional ordering before thresholding
- [x] information-limit diagnosis recorded (signal/repeat = 0.00277 median; signal/numerical = 0.11793 median)
- [x] GAUGE final-test repeats kept unopened after failed validation gate

### IRIS pendulum lane
- [x] validation protocol frozen before execution
- [x] validation-only repeat downloader implemented (20-degree development + 45-degree validation)
- [x] deterministic classical video tracker implemented
- [x] finite-amplitude pendulum period verifier implemented
- [x] small-angle matched baseline implemented
- [x] one-frame uncertainty floor and |z|=2 rule fixed
- [x] development quality gate implemented
- [x] synthetic-video tracker regression added to CI
- [x] development gate executed on 10 real takes (10/10 quality-pass; median rope-length relative error 4.23%)
- [x] 45-degree validation executed on 10 real takes (10/10 quality-pass; median rope-length relative error 2.43%)
- [x] 90-degree final-test repeats opened only after lock (10/10 quality-pass; 220 confirmatory records)


### IRIS final-test freeze readiness
- [x] validation forensic supports freeze without retuning
- [x] truth-control correct rate = 1.0
- [x] placebo correct rate = 1.0
- [x] large-effect correct rate = 1.0
- [x] dose-direction sign rate = 1.0
- [x] dose-response Spearman = 0.974943
- [x] paired primary wins = 18, baseline wins = 0
- [x] frozen final-test configuration committed
- [x] final-test lock hashes code/config + validation forensic/evidence
- [x] final-test runner refuses accidental overwrite
- [x] reserved pendulum_90 split executed under valid lock


### IRIS locked final-test outcome
- [x] exact 10-video pendulum_90 population executed under the frozen lock
- [x] 10/10 final videos passed frozen quality gates
- [x] 220 final-test rows marked confirmatory
- [x] finite-amplitude probe strict accuracy = 90/110 = 81.82%
- [x] small-angle baseline strict accuracy = 40/110 = 36.36%
- [x] finite-amplitude SUPPORT = 40/50; VETO = 40/50; placebo = 10/10
- [x] small-angle SUPPORT = 0/50; VETO = 30/50; placebo = 10/10
- [x] paired finite-amplitude-only wins = 50; small-angle-only wins = 0; ties = 60
- [x] final median rope-length relative error = 16.81%
- [x] no post-final threshold/candidate/tracker retuning
- [ ] independent-sensor or physical-bench confirmation beyond same-video IRIS, if required by venue/reviewer


### Hostile-reviewer hardening
- [x] 36-attack reviewer threat matrix committed
- [x] video-clustered statistics implementation
- [x] strong direct-period and damped-nonlinear baseline implementation
- [x] GT-hidden proposal artifact with truth joined only after SHA closure
- [x] corruption robustness battery implementation
- [x] Metal-accelerated robustness preprocessing on macOS
- [x] AVFoundation decode + Metal corruption/resize/grayscale path avoids MJPEG retranscode
- [x] Metal clean-equivalence gate requires exact locked primary decision-vector reproduction
- [x] macOS CI builds and instantiates the native Metal preprocessing pipelines
- [x] proposal/probe overlap-dependence sweep implementation
- [x] measured-truth uncertainty sensitivity implementation
- [x] pinned official IRIS reference-baseline importer
- [x] evidence-driven rewrite storyboard/video generator
- [x] second-domain free-fall protocol/config/final population frozen
- [x] take-01 leakage explicitly forbidden in free-fall blind campaign
- [x] lock-gated free-fall final downloader
- [x] synthetic free-fall tracker CI regression
- [ ] execute full pendulum post-final hardening runner and record outcomes
- [ ] execute free-fall development/validation gate and record outcomes
- [ ] if validation passes, execute the nine-video locked free-fall final test
- [ ] update manuscript claims from actual hardening outputs, including failures

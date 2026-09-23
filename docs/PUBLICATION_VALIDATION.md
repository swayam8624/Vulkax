# Reality Probe Publication Validation

This is the operational guide for the validation extension introduced on
2026-09-23. It supplements, rather than rewrites, the frozen 2026-09-21 result.

## What is already implemented

The repository now contains:
- a canonical three-way SUPPORT/VETO/UNRESOLVED schema;
- a fixed primary |z| = 2 decision rule;
- D4V/OFC historical adapters;
- matched-method summaries;
- clustered bootstrap confidence intervals;
- risk/coverage and reliability tables;
- explicit failure-case output;
- placebo/negative-control analysis;
- dose-response and robustness analysis hooks;
- proposal/probe dependence metadata;
- transaction-gate harm/benefit accounting;
- sample-size/precision planning;
- a hash lock for final-test protocols;
- a measured-world protocol;
- a strict multi-dataset adapter contract.

## Public dataset acquisition and import

The publication-validation campaign now has an operational public-data path for
three complementary benchmarks:

- **GAUGE** — measured deformable motion and calibrated physical metadata. The
  default core profile downloads the 60 foam stretch/compression/shear trials used
  by the Vulkax deformable adapter. Previously analyzed shearing trials remain
  retrospective and are excluded from the new prospective world manifest.
- **IRIS** — 240 real 4K/60fps dynamics videos with independently measured physical
  parameters. The core profile downloads one take from every one of the 24 physical
  settings (8 classes x 3 settings), then splits by physical setting rather than by
  repeated take.
- **RGBench Cloth Sim-to-Real v1** — real segmented cloth point clouds, robot
  trajectories, camera calibration and garment meshes. The core profile selects one
  grasp/fold/fling capture from each of three predeclared garments spanning
  development, validation and final-test roles. The current v1 Hugging Face revision does not expose the upstream-documented `reference_results/` tree, so Vulkax does not depend on it.

Recommended command:

```bash
bash research/scripts/run_publication_validation.sh \
  --public-data-profile core
```

That command:
1. creates an isolated Python venv under `build/`;
2. installs `huggingface_hub`;
3. resolves and pins the exact Hugging Face revision of each dataset;
4. downloads only the core subset;
5. hashes every materialized file;
6. writes a dataset inventory and prospective world manifest;
7. imports GAUGE into SI-unit marker/driver CSV packages;
8. binds IRIS videos to independently measured parameter manifests;
9. validates/indexes RGBench calibration, joint streams and PCD headers;
10. generates the controlled trial plan;
11. runs the existing frozen D4V/OFC diagnostic analysis.

Use `--public-data-profile smoke` for a very small connectivity/integrity check,
or `--public-data-profile full` only when the complete multi-gigabyte releases
are actually required.

Generated data stay under `build/` and are not committed.


## Prospective GAUGE result and IRIS pendulum lane

The first new real-data validation-stage run used GAUGE stretching/compression.
It correctly abstained on every placebo case but resolved zero SUPPORT/VETO cases.
A saved-output forensic showed 100% correct truth ordering before thresholding, while
the median signal was only 0.00277x repeat variability and 0.11793x numerical
variability. GAUGE is therefore retained as an information-limit result and its
final repeats remain unopened.

The next real-data lane is IRIS single-pendulum video. Run:

```bash
bash research/scripts/run_iris_pendulum_validation.sh
```

That command:

1. pins the existing IRIS dataset revision;
2. downloads only `pendulum_20` and `pendulum_45` takes 01-10;
3. does **not** request additional `pendulum_90` final-test videos;
4. refreshes the public-data inventory and adapted manifests;
5. runs a deterministic classical motion tracker on the 20-degree development set;
6. requires at least 8/10 development takes to pass and <=20% median rope-length error;
7. only after that gate, processes the 45-degree validation set;
8. compares a finite-amplitude period probe against a small-angle baseline;
9. emits SUPPORT/VETO/UNRESOLVED truth controls, placebo, and dose-response rows;
10. merges the IRIS records with the existing publication-validation analysis.

The IRIS verifier uses real video period as evidence and the independently measured
rope length only for benchmark truth labels. It does not require CUDA or a learned
tracker.


## IRIS final-test freeze

The IRIS validation forensic supports freezing without retuning:

- truth-control correct rate: 1.0;
- placebo correct rate: 1.0;
- large-effect correct rate: 1.0;
- dose-direction sign rate: 1.0;
- dose-response Spearman: 0.974943;
- paired finite-amplitude wins vs small-angle: 18 vs 0;
- validation median rope-length relative error: 0.0242594.

The final-test method is therefore frozen in
`research/validation/iris_pendulum_final_test_v1.json`.

Run the reserved 90-degree final test only through:

```bash
bash research/scripts/run_iris_pendulum_final_test.sh
```

The command creates a hash lock **before** opening the remaining
`pendulum_90` videos. The lock covers the protocol, schema, frozen final-test
configuration, tracker/scoring implementation, downloader, dataset preparation
and adapter code, final summarizer, runner, and the validation records/forensic
that justified the freeze.

Any later modification to a locked input makes the final-test command abort.

The final run keeps the global `|z|=2` rule, tracker parameters, repair schedule,
and quality gates unchanged. It writes a descriptive final summary and does not
perform post-final readiness tuning.

## IRIS locked final-test result

The reserved 90-degree final split was executed under the frozen IRIS-specific
lock without changing the global `|z|=2` decision rule, tracker, candidate
schedule, quality gates, runtime, or ten-video final population.

Final result:
- 10/10 final videos passed the frozen quality gates;
- median period-inferred rope-length relative error: **16.81%**;
- finite-amplitude probe strict accuracy: **90/110 = 81.82%**;
- small-angle baseline strict accuracy: **40/110 = 36.36%**;
- finite-amplitude SUPPORT: **40/50**;
- finite-amplitude VETO: **40/50**;
- finite-amplitude placebo/UNRESOLVED: **10/10**;
- paired primary-only correct: **50**;
- paired baseline-only correct: **0**;
- ties: **60**;
- locked confirmatory records: **220**.

The 220 rows come from **10 physical videos** crossed with controlled repair cases
and two methods; they are not 220 independent physical experiments.

The canonical result is recorded in:
- `research/status/IRIS_PENDULUM_FINAL_TEST_RESULT_2026-09-23.md`;
- `research/results/IRIS_PENDULUM_FINAL_TEST_RESULT_2026-09-23.json`.

No post-final tuning is permitted. Any improved method must be reported as a new
post-final experiment.

## Reviewer-hardening campaign

The locked IRIS pendulum result is not modified by the reviewer-hardening campaign.

Run all local post-final attacks:

```bash
bash research/scripts/run_postfinal_reviewer_hardening.sh --profile full
```

This performs video-clustered statistics, strong direct-period and damped-nonlinear
baselines, a GT-hidden proposal→verification experiment, measured-truth uncertainty
sensitivity, proposal/probe dependence, corruption robustness, pinned official IRIS
reference imports, and deterministic evidence visuals.

On macOS the corruption-robustness stage now defaults to a native Apple-Silicon
fast path:

```text
AVFoundation / hardware video decode where available
→ Metal full-resolution corruption
→ Metal 640px resize + grayscale
→ raw grayscale frames
→ unchanged centroid / FFT / evidence scoring
```

It does **not** transcode temporary full-resolution MJPEG videos. Before any Metal
corruption result is accepted, the clean Metal path must reproduce every one of
the locked primary decisions for all ten final videos and agree with the frozen
observed period within 2%. A failed equivalence gate aborts the robustness run.

Force the legacy implementation only for debugging with:

```bash
python3 research/analysis/run_iris_postfinal_robustness.py --profile full --backend cpu
```

To additionally open the **development/validation** portion of the second blind
domain:

```bash
bash research/scripts/run_postfinal_reviewer_hardening.sh \
  --profile full \
  --with-freefall-validation
```

That command still cannot download the final `drop_150/02..10` videos.

If and only if the free-fall validation gate passes, the separate final command is:

```bash
bash research/scripts/run_iris_freefall_final_test.sh
```

The free-fall final downloader refuses to fetch those nine videos until the
domain-specific lock validates. `take 01` is permanently excluded from this blind
campaign because it existed in the original core profile.

## Fast diagnostic

If your existing D4V/OFC outputs are still under `build/`, this does not rerun
the expensive probes:

```bash
bash research/scripts/run_publication_validation.sh --diagnostic
```

If those outputs are absent, the script builds and runs only the two required
frozen probe targets.

## Force a fresh reproduction of the historical diagnostic

```bash
bash research/scripts/run_publication_validation.sh --refresh-probes
```

This reproduces the old evidence. It does not create new validation evidence.

## Plan new controlled trials

Create a world manifest such as:

```csv
dataset,scene,split
my_dataset,scene_001,development
my_dataset,scene_002,validation
my_dataset,scene_003,final_test
```

Then:

```bash
python3 research/analysis/generate_controlled_validation_plan.py \
  --worlds research/validation/world_manifest.csv \
  --profile full \
  --out build/publication-validation/trial_plan.csv
```

The planner emits truth controls, placebo/null controls, symmetric dose-response
trials, measurement/pose noise sweeps, missing-observation sweeps, and
proposal/probe dependence sweeps. It plans experiments only; a dataset/physics
adapter must execute them.

## Lock the final test

Before opening final-test results:

```bash
python3 research/analysis/freeze_publication_validation.py \
  --protocol research/validation/protocol_v1.json \
  --schema research/validation/record_schema_v1.json \
  --extra research/validation/world_manifest.csv \
  --require-clean \
  --out build/publication-validation/final_test_lock.json
```

Check it immediately before final analysis:

```bash
python3 research/analysis/freeze_publication_validation.py \
  --check build/publication-validation/final_test_lock.json
```

## Analyze new dataset records

Dataset adapters should output standardized records. Add any number of them:

```bash
bash research/scripts/run_publication_validation.sh \
  --lock build/publication-validation/final_test_lock.json \
  --extra build/dataset-a/validation_records.csv \
  --extra build/dataset-b/validation_records.csv
```

Main outputs:

```text
build/publication-validation/
  records.csv
  sample_size_plan.json
  analysis/
    summary.json
    method_summary.csv
    risk_coverage.csv
    reliability.csv
    paired_method_comparison.csv
    dose_response.csv
    robustness.csv
    negative_controls.csv
    calibration.csv
    failure_cases.csv
    transaction_utility.csv
```

## Scientific rule

An empty table is allowed. A negative result is allowed. Zero coverage is allowed.

What is not allowed is manufacturing rows for experiments that were never run,
lowering the final threshold because the final data did not resolve, or calling
retrospective D4V/OFC/GAUGE evidence prospective confirmation.

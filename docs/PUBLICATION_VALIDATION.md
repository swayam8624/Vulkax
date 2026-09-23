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

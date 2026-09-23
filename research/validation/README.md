# Publication Validation Extension

This directory is the reviewer-facing verification/validation extension created
after external feedback that the original Reality Probe manuscript had an
interesting concept but did not yet contain enough related-work coverage or
well-designed validation experiments for publication.

It is deliberately separated from the frozen 2026-09-21 research result.

## Scientific separation

There are now three evidence layers.

1. **Frozen historical evidence** — D0-D4V, OFC, and the GAUGE retrospective.
   Those results keep their original labels, thresholds, and negative outcomes.
2. **Diagnostic normalization** — the old D4V/OFC proposal tables can be converted
   to one three-way SUPPORT/VETO/UNRESOLVED record contract so methods can be
   compared with the same metrics. This is useful analysis, not new prospective
   evidence.
3. **Prospective publication validation** — newly generated controlled trials,
   external datasets, and eventually measured physical trials. Final-test data may
   be opened only after the protocol lock is created.

No script in this directory is allowed to turn retrospective results into
confirmatory evidence.

## Files

- `protocol_v1.json` — fixed decision semantics, experiment families, metrics,
  bootstrap unit, and final-test freeze rules.
- `record_schema_v1.json` — canonical per-method/per-trial record contract.
- `world_manifest.csv` — intentionally not committed with private or unavailable
  data. Generate it from the datasets actually used for the final campaign.
- `../analysis/normalize_publication_validation.py` — adapters for the frozen
  D4V/OFC tables plus already-standardized future records.
- `../analysis/analyze_publication_validation.py` — three-way accuracy,
  coverage/risk, bootstrap confidence intervals, threshold curves, reliability,
  negative-control, dose-response, and robustness summaries.
- `../analysis/generate_controlled_validation_plan.py` — deterministic trial
  planner for truth controls, placebos, dose response, noise, missingness, and
  channel-dependence sweeps.
- `../analysis/freeze_publication_validation.py` — hashes the protocol inputs
  before a final-test split is opened.
- `../scripts/run_publication_validation.sh` — one-command orchestrator.

## Operational public benchmark suite

The framework now has a downloader/importer rather than requiring manual dataset
assembly.

```bash
bash research/scripts/prepare_public_validation_data.sh --profile core
```

Core profile:
- GAUGE: 60 foam trials across stretching, compression and shearing;
- IRIS: 24 videos covering every class/setting combination with one repeated take;
- RGBench: representative grasp/fold/fling captures from `green_tshirt`,
  `grey_pleat_skirt`, and `white_shirt`, plus required garment meshes.

Every download is revision-pinned and SHA-256 indexed. The prepared campaign writes:

```text
build/publication-validation/public-data/
  dataset_inventory.csv
  world_manifest.csv
  dataset_truth_index.json
  preparation_report.json
  trial_plan.csv
  adapted/
    adapter_summary.json
    gauge/...
    iris/...
    rgbench/...
```

Scientific separation is enforced automatically: legacy GAUGE shearing is marked
retrospective and cannot enter the new prospective world manifest.

## Required interpretation

A decision is generated from a signed standardized score (z):

- `z >= +2` -> **support**
- `z <= -2` -> **veto**
- otherwise -> **unresolved**

The sign convention is fixed: positive evidence favors the proposed repair;
negative evidence favors vetoing it.

For controlled trials, the truth itself may be **unresolved**. This is important:
a verifier is wrong if it confidently asserts support or veto when the supplied
information is intentionally non-identifying.

## Recommended execution sequence

Development and smoke testing:

```bash
bash research/scripts/run_publication_validation.sh --diagnostic
```

Create a prospective trial plan from a dataset/scene manifest:

```bash
python3 research/analysis/generate_controlled_validation_plan.py \
  --worlds research/validation/world_manifest.csv \
  --profile full \
  --out build/publication-validation/trial_plan.csv
```

Before opening an untouched final-test split:

```bash
python3 research/analysis/freeze_publication_validation.py \
  --protocol research/validation/protocol_v1.json \
  --schema research/validation/record_schema_v1.json \
  --extra research/validation/world_manifest.csv \
  --out build/publication-validation/final_test_lock.json
```

Then run the final campaign using that lock and never tune the threshold against
final-test labels.

## What still requires real execution

The repository can define, validate, normalize, and statistically analyze the
protocol without new hardware. It cannot honestly manufacture evidence for trial
families that have not been executed. Placebo, dose-response, channel-dependence,
robustness, multi-dataset, and measured-world rows become scientific evidence only
after the relevant adapters/probes produce records conforming to the schema.


## Current prospective execution status — 2026-09-23

GAUGE stretching/compression validation has now executed. It is a preserved
information-limit result: controlled truth ordering was correct, but the available
trajectory signal was far below repeat and numerical variability, so final-test
repeats were not opened.

IRIS single-pendulum development, validation, and the locked 90-degree final test have now executed. The reproduction command for development/validation is:

```bash
bash research/scripts/run_iris_pendulum_validation.sh
```

The command first expands only the development/validation pendulum repeats, runs a
20-degree development gate, and opens the 45-degree validation videos only if that
gate succeeds. The separately frozen 90-degree final test subsequently executed
under the IRIS-specific lock: 10/10 videos passed, the finite-amplitude probe
achieved 90/110 strict correct decisions versus 40/110 for the small-angle baseline,
and the paired comparison was 50 primary-only wins, 0 baseline-only wins, 60 ties.
The 220 confirmatory rows arise from 10 physical videos and must be interpreted with
that clustering structure.

# Vulkax Phase I Publication Asset Lock

Status: **FROZEN ASSET SET**

The following assets define the Phase I paper-data/visual package. They are
generated deterministically from the canonical result ledgers or from the frozen
experiment outputs.

## Main result figures

| ID | Asset | Evidence role |
|---|---|---|
| F1 | `fig_ranking_agreement.svg` | D2b/D3 matched-method ranking comparison |
| F2 | `fig_standardized_separation.svg` | distance from the frozen mechanism-resolution reference |
| F3 | `fig_numerical_floor.svg` | numerical-floor reduction without observability recovery |
| F4 | `fig_d4v_proposals.svg` | 36 proposed repairs: 14 deceptive, 22 beneficial, 0 resolved |
| F5 | `fig_gauge_channel_contradiction.svg` | observation/marker versus longitudinal-mechanism disagreement |
| F6 | `fig_information_frontier.svg` | post-hoc signal amplification required to reach the frozen credibility reference |

Each SVG has a source CSV generated beside it.

## Main tables

- `table_stage_outcomes.csv`
- `table_claim_boundaries.csv`
- `table_information_frontier.csv`
- `research/paper_data/EXPERIMENT_MATRIX.csv`
- `research/paper_data/ABLATION_MATRIX.md`
- `research/results/DCS_FINAL_BENCHMARK_TABLE_2026-09-20.csv`
- `research/results/GAUGE_PAIRED_DIAGNOSTICS_2026-09-20.csv`

## Reproducibility / supplemental assets

- per-case D2 and D3 tables;
- D3 stencil tables;
- D4V proposal table;
- GAUGE per-trial retrospective table;
- captured-world certificate;
- system/compiler/GPU provenance;
- command logs;
- SHA256SUMS and artifact manifest;
- paper-reproduction validation result.

## Motion / system demonstration

The release-facing captured-world showcase remains the system demonstration path.
It is not used as quantitative scientific evidence.

A native-backend machine may generate a turntable/showcase sequence with:

```bash
./build/vulkax captured-world-run \
  build/captured-example/capture.vkcap \
  build/captured-world-showcase \
  m4 0.003 1 1 1 \
  Metal 0.08 0.01 0.02 12345 \
  --showcase studio_pedestal \
  --showcase-assets build/paper-showcase \
  --showcase-resolution 1920x1080 \
  --turntable 24
```

Use `Vulkan` on a Vulkan-capable Linux/NVIDIA system.

The motion asset is illustrative/system-facing. It must not be presented as
validation evidence.

## Asset generation

```bash
python3 research/analysis/generate_paper_assets.py \
  --results research/results/DCS_FINAL_RESULTS_2026-09-20.json \
  --out build/paper-figures
```

The complete one-command path generates and packages the same set:

```bash
./run_everything.sh --clean
```

## Freeze rule

Do not replace a frozen figure because a later experiment looks more favorable.

New evidence belongs to a new research phase unless it is purely a rendering or
typographic correction of the same underlying frozen data.

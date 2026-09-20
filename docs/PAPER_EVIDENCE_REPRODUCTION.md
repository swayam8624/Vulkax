# Vulkax Full Research / Paper-Evidence Reproduction

This guide reproduces the complete currently executed Vulkax research evidence
without writing manuscript prose.

Canonical branch:

`research/integration-20260920`

## Fresh clone

```bash
git clone --branch research/integration-20260920 --single-branch \
  https://github.com/swayam8624/Vulkax.git
cd Vulkax
```

## One command

```bash
bash run_everything.sh
```

The runner auto-selects:
- `Metal` on macOS;
- `Vulkan` on Linux when `vulkaninfo` is available;
- otherwise `none`.

To force a backend:

```bash
bash run_everything.sh --backend Metal
bash run_everything.sh --backend Vulkan
bash run_everything.sh --backend none
```

## Clean full reproduction

```bash
bash run_everything.sh --clean
```

## Useful reduced modes

Skip the long measured GAUGE retrospective:

```bash
bash run_everything.sh --skip-gauge
```

Skip the timing benchmark:

```bash
bash run_everything.sh --skip-performance
```

Include selected historical falsification probes:

```bash
bash run_everything.sh --exhaustive
```

## What the default run executes

1. capture system/compiler/GPU provenance;
2. syntax-check and self-test all reusable Python research tooling;
3. configure Release CMake with tests enabled;
4. build the entire repository;
5. run the complete CTest suite;
6. validate evidence schemas and release-facing claims;
7. exercise CLI failure contracts;
8. run native backend conformance when available;
9. generate and execute a deterministic captured-world example;
10. run controlled timing evidence;
11. reproduce D1 deceptive-repair positive control;
12. reproduce solver-native fixed-witness discovery;
13. reproduce automatic active-selection discovery;
14. reproduce frozen D2 validation;
15. reproduce D3 witness-space/adaptive-order discovery;
16. reproduce D4V pair-specific repair-veto discovery;
17. fetch the public GAUGE foam subset;
18. infer/validate the measured effective deforming span;
19. run the 20 definitive GAUGE forward simulations with live progress;
20. validate fresh numerical results against the frozen canonical ledger;
21. generate deterministic SVG/CSV paper figures and tables;
22. assemble a SHA-256-indexed paper-evidence bundle;
23. validate the final bundle manifest.

## Main outputs

### Evidence bundle

`build/paper-evidence/`

Contains:
- canonical committed result/protocol documents;
- freshly reproduced analysis outputs;
- command logs;
- system provenance;
- generated paper assets;
- reproduction-validation result;
- SHA-256 manifest/index.

Start with:

```text
build/paper-evidence/README.md
build/paper-evidence/manifest.json
build/paper-evidence/artifact_index.csv
```

### Paper assets

`build/paper-figures/`

Generated assets:
- `fig_ranking_agreement.svg`
- `fig_standardized_separation.svg`
- `fig_numerical_floor.svg`
- `fig_d4v_proposals.svg`
- `fig_gauge_channel_contradiction.svg`
- source CSV for every figure;
- stage-outcome table;
- claim-boundary table;
- `figure_manifest.json`.

### Raw experiment outputs

- `build/dcs-positive-control/`
- `build/dcs-solver-native/`
- `build/dcs-active-selection/`
- `build/dcs-d2-validation/`
- `build/dcs-d3-discovery/`
- `build/dcs-d4v-discovery/`
- `build/gauge-effective-span/`
- `build/gauge-dcs-retrospective/`
- `build/paper-captured-world-run/`
- `build/paper-performance/`

## Frozen reproduction expectations

The validator compares a fresh run against the committed canonical ledger.

Key expected outcomes:

- D1: 64/64 constructed deceptive repairs identified;
- D2 fixed/fresh: negative;
- D2 frozen resolved coverage: 0/16;
- D3 adaptive resolved coverage: 0/6;
- D3 selected order: k=2 on 6/6 worlds;
- D4V proposals: 36 total, 14 deceptive, 22 beneficial;
- D4V resolved coverage: 0%;
- GAUGE ordinary overlap preference: 10/10;
- GAUGE marker dark-field endpoint wins: 0/10;
- GAUGE longitudinal dark-field endpoint wins: 9/10.

The validator allows small declared floating-point tolerance but does not relax
categorical outcomes.

## GAUGE progress

The definitive retrospective is computationally heavy and executes 20 full forward
simulations. The script prints:

```text
[DCS] START material=soft trial=2
[DCS]   endpoint simulation...
[DCS]   endpoint DONE
[DCS]   overlap simulation...
[DCS]   overlap DONE
...
```

so a long CPU run should no longer appear hung.

## Cloud / GPU use

The default GAUGE MPM path is CPU-side, so a faster GPU alone does not accelerate
that existing solver substantially.

A rented Linux/NVIDIA system is still valuable for:
- Vulkan/NVIDIA cross-platform validation;
- Gaussian rendering/scaling;
- future GPU-parallel counterfactual execution;
- publication-quality rendering/video generation.

For final hardware experiments, always preserve:
- exact git commit;
- CPU/GPU model;
- driver/runtime versions;
- compiler/CMake versions;
- generated logs and evidence manifest.

The full runner records this provenance automatically.

## Research-integrity boundary

A successful reproduction means the frozen evidence reproduced.

It does **not** mean:
- DCS became a positive prospective verifier;
- negative D2/D3/D4V gates changed;
- GAUGE became prospective confirmation.

The current executed evidence remains:
- D2/D3/D4V negative;
- GAUGE retrospective/channel-specific;
- D5 fresh positive measured confirmation not executed.

Any future positive mechanism must use new untouched discovery/validation data and
then the already-implemented D5 replay path.

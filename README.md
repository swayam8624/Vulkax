<h1 align="center">Reality Probe</h1>

<h3 align="center">Counterfactual Physical Verification of Captured Worlds</h3>

<p align="center">
  Paper-facing research identity of the <code>VULKAX</code> repository.<br/>
  A captured world can fit what was observed and still be wrong about how the world behaves.
</p>

<p align="center">
  <a href="https://github.com/swayam8624/Vulkax/actions/workflows/paper-evidence-smoke.yml"><img src="https://github.com/swayam8624/Vulkax/actions/workflows/paper-evidence-smoke.yml/badge.svg?branch=main" alt="Paper evidence smoke"/></a>
  <a href="https://github.com/swayam8624/Vulkax/actions/workflows/paper-evidence-full.yml"><img src="https://github.com/swayam8624/Vulkax/actions/workflows/paper-evidence-full.yml/badge.svg?branch=main" alt="Full reproduction"/></a>
  <a href="https://github.com/swayam8624/Vulkax/actions/workflows/visualization-smoke.yml"><img src="https://github.com/swayam8624/Vulkax/actions/workflows/visualization-smoke.yml/badge.svg?branch=main" alt="Visualization smoke"/></a>
  <img src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus" alt="C++20"/>
  <img src="https://img.shields.io/badge/macOS-Metal-black?logo=apple" alt="Metal"/>
  <img src="https://img.shields.io/badge/Linux-Vulkan-A41E22?logo=vulkan" alt="Vulkan"/>
</p>

## Project status

The original research program is frozen at the 2026-09-21 scientific snapshot. A separate publication-validation extension began on 2026-09-23 to strengthen verification, validation, uncertainty accounting, and external testing without changing the frozen evidence.

The scientific snapshot is:

```text
tag:    paper-freeze-2026-09-21
commit: a9da8c0aa8689ebeea0d84baf95a74907659a837
```

The `main` branch contains later documentation and visualization work, but the scientific claims, thresholds, experiment labels, result ledgers, and evidence packages remain tied to the frozen snapshot.

The final research outcome is not that every candidate repair can be certified. The evidence now supports a more specific, conditional result:

> A repair can improve ordinary observational agreement while making an untouched physical target worse. Reality Probe treats acceptance of that repair as a separate SUPPORT / VETO / UNRESOLVED transaction. Whether that transaction is useful depends on the physical information carried by the verification channel.

The evidence ladder is deliberately mixed. DCS and known-force compliance expose information limits in frozen synthetic experiments. A repository-locked IRIS pendulum final test then supplies a positive measured result: the finite-amplitude probe makes 90/110 controlled decisions correctly across 10 fresh videos, versus 40/110 for the matched small-angle diagnostic baseline, with all 10 placebo cases left unresolved. A separately frozen IRIS free-fall replication subsequently fails untouched validation with 111.7% median acceleration relative error despite 5/5 low-level quality passes. That failure is retained, and its designated final split remains unopened.

The locked results remain reproducible. Reviewer-facing post-final analyses strengthen statistics, baselines, proposal realism, uncertainty, and robustness without replacing the frozen decisions.

## Publication-validation extension

The new validation framework lives under
[`research/validation/`](research/validation/README.md). It standardizes the old
D4V/OFC proposal ledgers for diagnostic comparison while keeping them explicitly
non-confirmatory, and defines a prospective locked protocol for new experiments.

Key entrypoint:

```bash
bash research/scripts/run_publication_validation.sh --diagnostic
```

Completed evidence and post-final diagnostic evidence are tracked separately.
The locked IRIS pendulum final result is never rewritten by later clustered,
baseline, uncertainty, proposal, dependence, or robustness analyses. The failed
free-fall validation is likewise preserved as a negative result rather than
recycled into confirmation.

## Explainer video

<p align="center">
  <a href="docs/readme_assets/reality_probe_explainer.mp4">
    <img src="https://raw.githubusercontent.com/swayam8624/Vulkax/main/docs/readme_assets/reality_probe_explainer_full.gif" width="760" alt="Reality Probe full 116-second mathematical explainer"/>
  </a>
</p>

<p align="center">
  <strong>Reality Probe, complete 116-second mathematical explainer</strong><br/>
  The full animation plays inline from deceptive repair through controlled probing, mechanism fingerprints, residual fields, standardized evidence, the information limit, and the final refusal decision.<br/>
  <sub>Click the animation for the full-resolution MP4.</sub>
</p>

## Visual explanation

<table>
<tr>
<td align="center" width="33%">
  <img src="https://raw.githubusercontent.com/swayam8624/Vulkax/main/docs/readme_assets/02_deceptive_repair.png" width="285" alt="Deceptive repair"/><br/>
  <sub><strong>Deceptive repair</strong><br/>ordinary held-out fit improves while an untouched physical target worsens</sub>
</td>
<td align="center" width="33%">
  <img src="https://raw.githubusercontent.com/swayam8624/Vulkax/main/docs/readme_assets/03_deception_map.png" width="285" alt="Deception map"/><br/>
  <sub><strong>Proposal landscape</strong><br/>36 fresh proposals show where observational and physical outcomes disagree</sub>
</td>
<td align="center" width="33%">
  <img src="https://raw.githubusercontent.com/swayam8624/Vulkax/main/docs/readme_assets/04_same_probe.png" width="285" alt="Same physical probe"/><br/>
  <sub><strong>Controlled intervention</strong><br/>truth and repair are asked the same physical question</sub>
</td>
</tr>
<tr>
<td align="center" width="33%">
  <img src="https://raw.githubusercontent.com/swayam8624/Vulkax/main/docs/readme_assets/06_fingerprint.png" width="285" alt="Mechanism fingerprint"/><br/>
  <sub><strong>Mechanism fingerprint</strong><br/>directional responses summarize how the world reacts</sub>
</td>
<td align="center" width="33%">
  <img src="https://raw.githubusercontent.com/swayam8624/Vulkax/main/docs/readme_assets/07_residual_field.png" width="285" alt="Mechanism residual field"/><br/>
  <sub><strong>Mechanism residual</strong><br/>the response difference exposes disagreement hidden by ordinary fit</sub>
</td>
<td align="center" width="33%">
  <img src="https://raw.githubusercontent.com/swayam8624/Vulkax/main/docs/readme_assets/11_information_limit.png" width="285" alt="Information limit"/><br/>
  <sub><strong>Information limit</strong><br/>the force channel improves the signal by 11.46x but still remains below the frozen decision threshold</sub>
</td>
</tr>
</table>

<details>
<summary><strong>Complete 12-frame visual atlas</strong></summary>

<br/>

<table>
<tr>
<td align="center"><img src="https://raw.githubusercontent.com/swayam8624/Vulkax/main/docs/readme_assets/01_question.png" width="260"/><br/><sub>01. Research question</sub></td>
<td align="center"><img src="https://raw.githubusercontent.com/swayam8624/Vulkax/main/docs/readme_assets/02_deceptive_repair.png" width="260"/><br/><sub>02. Deceptive repair</sub></td>
<td align="center"><img src="https://raw.githubusercontent.com/swayam8624/Vulkax/main/docs/readme_assets/03_deception_map.png" width="260"/><br/><sub>03. Proposal landscape</sub></td>
</tr>
<tr>
<td align="center"><img src="https://raw.githubusercontent.com/swayam8624/Vulkax/main/docs/readme_assets/04_same_probe.png" width="260"/><br/><sub>04. Controlled probe</sub></td>
<td align="center"><img src="https://raw.githubusercontent.com/swayam8624/Vulkax/main/docs/readme_assets/05_response_overlay.png" width="260"/><br/><sub>05. Response comparison</sub></td>
<td align="center"><img src="https://raw.githubusercontent.com/swayam8624/Vulkax/main/docs/readme_assets/06_fingerprint.png" width="260"/><br/><sub>06. Mechanism fingerprint</sub></td>
</tr>
<tr>
<td align="center"><img src="https://raw.githubusercontent.com/swayam8624/Vulkax/main/docs/readme_assets/07_residual_field.png" width="260"/><br/><sub>07. Residual field</sub></td>
<td align="center"><img src="https://raw.githubusercontent.com/swayam8624/Vulkax/main/docs/readme_assets/08_dcs_intuition.png" width="260"/><br/><sub>08. DCS intuition</sub></td>
<td align="center"><img src="https://raw.githubusercontent.com/swayam8624/Vulkax/main/docs/readme_assets/09_standardization.png" width="260"/><br/><sub>09. Standardized evidence</sub></td>
</tr>
<tr>
<td align="center"><img src="https://raw.githubusercontent.com/swayam8624/Vulkax/main/docs/readme_assets/10_signal_gain.png" width="260"/><br/><sub>10. Signal gain</sub></td>
<td align="center"><img src="https://raw.githubusercontent.com/swayam8624/Vulkax/main/docs/readme_assets/11_information_limit.png" width="260"/><br/><sub>11. Information limit</sub></td>
<td align="center"><img src="https://raw.githubusercontent.com/swayam8624/Vulkax/main/docs/readme_assets/12_conclusion.png" width="260"/><br/><sub>12. Final conclusion</sub></td>
</tr>
</table>

<p align="center"><img src="https://raw.githubusercontent.com/swayam8624/Vulkax/main/docs/readme_assets/storyboard.png" width="820" alt="Reality Probe storyboard"/></p>

Each still also has an editable SVG in `docs/readme_assets/`.
</details>

## What the project set out to build

VULKAX began as a systems project for turning captured scenes into persistent, editable, physically executable worlds.

A useful captured world needs more than a renderer. It needs a stable relationship between appearance and physical state, a way to replay and calibrate the physics, a way to propose local edits, and an independent way to decide whether an edit deserves to be committed.

That led to five engineering requirements:

1. preserve stable identity between appearance and physical representations;
2. replay captured behavior in a deterministic physical model;
3. separate fitting data from held-out evaluation data;
4. keep proposal generation separate from verification;
5. commit a rewrite only when the evidence supports it, otherwise roll it back.

The result is a C++20 system with versioned capture bundles, persistent Gaussian identity, APIC/MPM replay, fit-only material calibration, held-out replay, local influence estimation, adaptive rewrite proposals, atomic commit or rollback, native Metal and Vulkan rendering, measured-data ingestion, and machine-readable evidence records.

Vulkax 1.0 is the stable verified-rewritable-reality baseline for this study. The release contract is documented in [Vulkax 1.0](docs/RELEASE_1_0.md). The measured-data path is documented in [MEASURED_BENCHMARK_0_45.md](docs/MEASURED_BENCHMARK_0_45.md), and the one-command captured-world path is documented in [CAPTURED_WORLD_RUN_0_80.md](docs/CAPTURED_WORLD_RUN_0_80.md).

## Engineering journey

The implementation developed in stages. Each stage closed a specific gap in the end-to-end captured-world pipeline.

| Stage | Main result |
|---|---|
| 0.39 | Observation-robustness tests for calibration drift, influence stability, and adaptive-region overlap |
| 0.40 | Versioned capture evidence contract with SHA-256 payload identity, units, frame metadata, provenance, and uncertainty sidecars |
| 0.45 | Public DOT C2 measured-source benchmark with fit and held-out splits, calibrated replay, local proposal generation, and rollback |
| 0.50 | Native Vulkan and Metal Gaussian projection and raster/compositing with CPU-oracle regression |
| 0.60 | Unified verified rewrite transaction with typed edits, provenance, locality checks, and atomic rollback |
| 0.70 | Scale-safe Gaussian identity and reorder-safe selection, correspondence, filtering, and hierarchy queries |
| 0.80 | One-command captured-world execution with schema-versioned certificates and reproducible showcase output |
| 0.90 | Release hardening, CLI failure tests, schema validation, performance evidence, documentation audits, and cross-platform gates |
| 1.0 | Stable verified-rewritable-reality baseline |

The DOT C2 benchmark is important because it established the measured-data boundary of the system. It uses real measured trajectory geometry, while rest state, mass, rest volume, neutral Gaussian photometry, and some uncertainty terms remain explicit model proxies. The fitted material parameters are therefore model-conditioned effective values, not claimed material measurements.

The selected local rewrite in the DOT benchmark was rejected and rolled back. That rejection is part of the system result, not a failed demo.

## The research question that emerged

Once the end-to-end system was working, the harder question became clear.

Suppose a candidate physical repair lowers the ordinary held-out error:

```math
L_{\mathrm{obs}}(M_{\mathrm{repair}})
<
L_{\mathrm{obs}}(M_{\mathrm{baseline}})
```

That does not imply that the repair is physically more correct.

A model can match the observations better while becoming worse on a physical behavior that was not part of the fitting objective.

This project calls that failure mode a **deceptive physical repair**.

The research phase therefore shifted from building an editable executable world to asking a stricter question:

> What physical evidence is required before a repaired captured world can be trusted?

## Dark-Field Counterfactual Spectroscopy

The first research direction was Dark-Field Counterfactual Spectroscopy, or DCS.

For a physical world (M), let (F_M(u)) denote an observable response to an intervention (u). Instead of reading the raw response directly, DCS forms signed intervention contrasts designed to cancel response components shared by competing models.

A generic contrast has the form:

```math
\mathcal D_{\mu}[F]
=
\sum_i w_i F(u_i)
```

with cancellation constraints such as:

```math
\sum_i w_i = 0,
\qquad
\sum_i w_i u_i = 0.
```

A simple symmetric second-order witness is:

```math
F(+a)+F(-a)-2F(0)
```

and a mixed finite-amplitude witness is:

```math
F(A,B)-F(A,0)-F(0,B)+F(0,0).
```

The mathematical operators themselves are not presented as new. The research question is whether these mechanism-selective contrasts can be made useful for falsifying deceptive repairs in captured executable worlds.

## Experimental journey

The research program deliberately kept positive controls, discovery experiments, retrospective evidence, and fresh evaluation separate.

### D1: constructed positive control

The first task was to verify that the machinery could reject deliberately deceptive cases under a controlled construction.

Result:

```text
deceptive repairs tested:       64
correctly rejected:             64
median observation improvement: 60.88%
median witness degradation:     9.375x
```

This established implementation correctness under the positive-control construction. It did not establish prospective generalization.

### D2: frozen truth-world validation

D2 moved to 16 frozen truth worlds.

Result:

```text
truth worlds:                   16
resolved worlds:                 0
median standardized separation:  0.041578
reference decision magnitude:     2
```

The witness existed, but its standardized separation was far below the evidential scale required for a decision.

### D3: witness-space and adaptive-order study

D3 tested whether adaptive witness order and numerical treatment could recover useful separation.

Result:

```text
truth worlds:                         6
resolved worlds:                      0
adaptive DCS median separation:       0.0588583
median k=2 numerical RMS:             8.435e-7 m
median k=3 numerical RMS:             2.153e-7 m
```

The numerical floor was characterized, but the evidential separation remained too small.

### D4V: repair-veto discovery

D4V examined actual repair proposals.

Result:

```text
repair proposals:       36
deceptive:              14
beneficial:             22
resolved coverage:       0%
```

This was the point where the central limitation became difficult to dismiss. The method could describe mechanism-sensitive differences, but the available evidence did not support a reliable support or veto decision at the frozen threshold.

### GAUGE: retrospective measured contradiction

The public GAUGE foam-shearing benchmark supplied a measured retrospective case where different evidence channels disagreed.

```text
ordinary face + marker preference:  overlap 10/10
marker dark-field preference:       overlap 10/10
longitudinal dark-field preference: endpoint 9/10
```

The exploratory retrospective exact sign-test for the longitudinal channel gave `p = 0.02148`.

This result is kept explicitly retrospective because the metric conflict was already known before DCS was designed. It is evidence of a real channel contradiction, not fresh prospective confirmation.

### Locked measured confirmation and the stop rule

The generic confirmatory replay path includes frozen no-fit semantics, SUPPORT,
VETO, and UNRESOLVED outcomes, together with explicit uncertainty accounting.
The publication-validation extension then froze a separate real-video IRIS
pendulum protocol before opening the designated final media.

On 10 fresh `pendulum_90` videos, all 10 passed quality control. The
finite-amplitude period probe made 90/110 controlled decisions correctly
(81.82%), while the matched small-angle diagnostic baseline made 40/110
(36.36%). The primary probe was correct on 40/50 SUPPORT cases, 40/50 VETO
cases, and 10/10 UNRESOLVED placebo cases. Those 110 rows are nested controlled
cases inside 10 physical videos, not 110 independent physical experiments.

A second frozen equation-family replication on IRIS free-fall did not confirm
the result. Its untouched `drop_100/06..10` validation set passed low-level
quality on 5/5 videos but failed the physical recovery gate with 111.7% median
acceleration relative error. Retrospective rescue diagnostics were unable to
produce a robust target-free estimator, so that lane was closed and
`drop_150` remains unopened.

The stop rule is therefore active in both directions: successful locked evidence
is preserved as successful, and failed locked evidence is preserved as failed.

### Spatial localization and export

The spatial layer was completed alongside the verification experiments. It provides per-region dark-field residuals, standardized local residuals, resolved and unresolved region flags, mappings from physical particles to Gaussian or surface components, and PLY export for visualization.

This layer does not add a new verification claim. It makes the evidence spatially inspectable and connects solver-native quantities to the final paper visualizations.

## Orthogonal physical information

The D4V result left an important ambiguity. The failure could have been specific to the DCS construction, or it could have reflected a broader shortage of informative physical evidence.

To separate those possibilities, the project froze a new experiment before execution. The fitting and repair proposal pipeline was left unchanged, while a known-force compliance channel was added as an orthogonal source of information.

The protocol used:

- six new off-grid truth worlds;
- the same ordinary fitting and proposal-generation path;
- a known 40 N force applied to each top-layer particle;
- four force directions: +X, -X, +Y, and +Z;
- the same frozen support and veto reference of (|z| >= 2).

The fresh result was:

| Quantity | Kinematic DCS | Force compliance |
|---|---:|---:|
| Median absolute z | 0.04877 | 0.55863 |
| Maximum absolute z | 0.17279 | 1.31911 |
| Resolved coverage | 0% | 0% |
| Median additional signal needed to reach 2 | 41.01x | 3.58x |

The force channel increased the median standardized signal by **11.46x**.

That is a substantial gain in observability, but it still did not cross the frozen decision threshold. All 36 fresh proposals remained unresolved.

This changed the interpretation of the project. The main limitation was no longer best described as an unfinished implementation problem. It was an information problem.

## Final scientific result

The completed evidence supports a conditional systems claim rather than a
universal verifier.

First, deceptive physical repair is a real failure mode in the tested
captured-world setting: ordinary held-out improvement can disagree with an
untouched physical target.

Second, probe mathematics alone is not enough. Frozen DCS experiments can
expose mechanism-sensitive structure while still providing too little
standardized separation for a decision.

Third, changing the physical information channel matters. Known-force
compliance raises the median standardized evidence magnitude by **11.46x** on
fresh synthetic truth worlds, although all 36 proposals remain below the frozen
decision magnitude.

Fourth, a better-matched physical observable can produce useful measured-video
decisions. On the locked 10-video IRIS pendulum final set, the finite-amplitude
probe makes **90/110** controlled decisions correctly versus **40/110** for the
matched small-angle diagnostic baseline, with **10/10** placebo cases left
UNRESOLVED.

Finally, that success is not universal. The frozen IRIS free-fall replication
fails untouched validation with **111.7% median acceleration relative error**.
The failure is preserved, the validation split is permanently retrospective,
and the designated final split remains unopened.

The resulting claim is:

> Reality Probe provides an evidence-governed transaction for individual
> physical rewrites. When the separately reserved verification channel contains
> discriminating information, it can support or veto a rewrite; when it does
> not, the system should remain unresolved or retire the verification lane
> rather than manufacture certification.

## Frozen result summary

| Experiment | Main outcome |
|---|---|
| D1 positive control | 64 of 64 constructed deceptive repairs rejected |
| D2 validation | 16 truth worlds, 0 resolved, median standardized separation 0.041578 |
| D3 adaptive-order study | 6 truth worlds, 0 resolved, median adaptive separation 0.0588583 |
| D4V repair proposals | 36 proposals, 14 deceptive, 22 beneficial, 0% resolved coverage |
| GAUGE retrospective | ordinary and marker channels favored overlap 10/10; longitudinal channel favored endpoint 9/10 |
| Fresh force-compliance study | 36 proposals, 12 deceptive, 24 beneficial, 11.46x median signal gain, 0 resolved |
| Locked IRIS pendulum final | 10/10 quality-pass videos; finite-amplitude probe 90/110 correct vs small-angle baseline 40/110; 10/10 placebo abstentions |
| IRIS free-fall validation | 5/5 quality-pass videos but 111.7% median acceleration relative error; validation failed and final split remains unopened |

For the fresh force-compliance study:

```text
DCS median |z|:              0.048767
force median |z|:            0.558629
force maximum |z|:           1.319111
frozen decision magnitude:   2
resolved proposals:          0 / 36
```

The selected visualization case improves ordinary held-out error by about 14.93% while worsening the untouched physical target by about 9.03%. It is used only as a post-hoc visualization example and does not affect the aggregate statistics or thresholds.

## What is implemented

The final repository includes:

- versioned captured-world evidence bundles;
- persistent Gaussian and physical identity;
- reorder-safe selection, correspondence, filtering, and rollback;
- Gaussian appearance ingestion;
- nonlinear APIC/MPM replay;
- fit-only material calibration;
- held-out replay;
- observation robustness analysis;
- finite-difference and controlled adjoint influence paths;
- adaptive local rewrite proposals;
- evidence-derived atomic commit and rollback;
- native Metal and Vulkan Gaussian paths;
- measured DOT C2 benchmark support;
- DCS intervention and witness infrastructure;
- matched baseline experiments;
- GAUGE retrospective analysis;
- fresh orthogonal force-compliance evaluation;
- repository-locked IRIS pendulum final-test pipeline and result ledger;
- preserved failed IRIS free-fall validation and retrospective closure ledger;
- post-final clustered-statistics, baseline, proposal, uncertainty, dependence, and robustness tooling;
- confirmatory replay infrastructure with frozen no-fit semantics;
- spatial dark-field localization and PLY export;
- deterministic paper figures and tables;
- deterministic Reality Probe explainer assets;
- SHA-256-indexed evidence packaging;
- one-command research reproduction.

## What the project does not claim

The completed project does not claim that:

- DCS is a universal physical correctness certificate;
- DCS outperforms every matched raw or Fisher-style baseline;
- the tested force-compliance channel provides useful prospective repair-veto coverage at the frozen threshold;
- finite-amplitude pendulum physics or gravity estimation is itself novel;
- the 110 pendulum case rows are independent physical experiments;
- the same-video pendulum verification channel is an independent sensor modality;
- the failed free-fall lane provides positive confirmation;
- GAUGE is fresh confirmatory evidence;
- fitted DOT parameters are true material measurements;
- the Stanford Bunny used in visualization is the benchmark geometry;
- display-magnified deformation is the physical displacement magnitude;
- publication acceptance or novelty follows from implementation alone.

These boundaries are part of the final result, not unfinished work.

## Visualization and presentation

The paper-facing visualization layer is separate from the scientific freeze.

Scientific pixels come from frozen result data, deterministic solver replay, or clearly identified explanatory graphics. The Stanford Bunny is used only as a visualization carrier. Surface deformation is driven by interpolation of the frozen solver displacement field, and display deformation is typically magnified 400x so the response can be seen. Reported numerical quantities remain unscaled.

The main visual package includes:

| Asset | Purpose |
|---|---|
| Deceptive-repair hero | Shows the observational improvement and physical contradiction |
| Deception map | Places all 36 fresh force-compliance proposals in ordinary-improvement versus physical-target space |
| Diagnostic textures | Makes truth and repair deformation readable under the same probe |
| Mechanism fingerprints | Compares response signatures for +X, -X, +Y, and +Z |
| Mechanism residual field | Visualizes the surface magnitude of repair-to-truth response disagreement |
| Information-limit figure | Shows the force-channel gain relative to the frozen evidence threshold |
| Mathematical explainer | Presents the complete research argument as a 116-second deterministic vector film |

The explainer source is in [visualization/explainer](visualization/explainer/README.md).

## Reproduction

The canonical research branch is `main`.

From a fresh clone:

```bash
git clone https://github.com/swayam8624/Vulkax.git
cd Vulkax
./run_everything.sh --clean
```

Useful variants:

```bash
./run_everything.sh --backend Metal
./run_everything.sh --backend Vulkan
./run_everything.sh --backend none
./run_everything.sh --skip-gauge
./run_everything.sh --skip-performance
./run_everything.sh --exhaustive
```

The full run performs environment capture, build and tests, captured-world execution, D1 through D4V, the fresh force-compliance study, GAUGE retrospective analysis, information-frontier diagnostics, paper-asset generation, frozen-result validation, and evidence packaging.

The resulting evidence package is written to:

```text
build/paper-evidence/
```

The portable archive is written as:

```text
build/vulkax-paper-evidence-<commit>.tar.gz
```

Paper figures are generated under:

```text
build/paper-figures/
```

README and explainer media are generated from deterministic source code and published through the repository workflows.

For detailed reproduction instructions, see [PAPER_EVIDENCE_REPRODUCTION.md](docs/PAPER_EVIDENCE_REPRODUCTION.md).

## Build and test

### macOS and Linux

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DVULKAX_BUILD_TESTS=ON

cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### Windows

```powershell
cmake -S . -B build -DVULKAX_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Installation notes are in [INSTALL_0_90.md](docs/INSTALL_0_90.md). Performance methodology is in [PERFORMANCE_0_90.md](docs/PERFORMANCE_0_90.md).

## Repository guide

| Need | Document |
|---|---|
| Final research summary | [VULKAX_FINAL_RESEARCH_SUMMARY_2026-09-21.md](research/results/VULKAX_FINAL_RESEARCH_SUMMARY_2026-09-21.md) |
| Final result ledger | [VULKAX_FINAL_RESULTS_2026-09-21.json](research/results/VULKAX_FINAL_RESULTS_2026-09-21.json) |
| Research result index | [research/results/README.md](research/results/README.md) |
| Paper-data map | [research/paper_data/README.md](research/paper_data/README.md) |
| Force-compliance result | [ORTHOGONAL_FORCE_COMPLIANCE_RESULT_2026-09-21.md](research/results/ORTHOGONAL_FORCE_COMPLIANCE_RESULT_2026-09-21.md) |
| Locked IRIS pendulum final result | [IRIS_PENDULUM_FINAL_TEST_RESULT_2026-09-23.json](research/results/IRIS_PENDULUM_FINAL_TEST_RESULT_2026-09-23.json) |
| Failed free-fall validation | [IRIS_FREEFALL_V6_VALIDATION_RESULT_2026-09-24.json](research/results/IRIS_FREEFALL_V6_VALIDATION_RESULT_2026-09-24.json) |
| Free-fall retrospective closure | [IRIS_FREEFALL_V67_RETROSPECTIVE_CLOSURE_2026-09-24.json](research/results/IRIS_FREEFALL_V67_RETROSPECTIVE_CLOSURE_2026-09-24.json) |
| Novelty / claim positioning | [REALITY_PROBE_NOVELTY_MAP_2026-09-24.md](research/status/REALITY_PROBE_NOVELTY_MAP_2026-09-24.md) |
| DCS benchmark summary | [DCS_FINAL_BENCHMARK_SUMMARY_2026-09-20.md](research/results/DCS_FINAL_BENCHMARK_SUMMARY_2026-09-20.md) |
| Information frontier | [DCS_INFORMATION_FRONTIER_2026-09-20.md](research/results/DCS_INFORMATION_FRONTIER_2026-09-20.md) |
| Claim boundaries | [CLAIM_GUARD.md](research/literature/CLAIM_GUARD.md) |
| Limitations and stop rules | [DCS_LIMITATIONS_AND_KILL_CRITERIA.md](research/status/DCS_LIMITATIONS_AND_KILL_CRITERIA.md) |
| Mathematics-to-code audit | [DCS_MATH_IMPLEMENTATION_AUDIT_2026-09-21.md](research/status/DCS_MATH_IMPLEMENTATION_AUDIT_2026-09-21.md) |
| Vulkax 1.0 baseline | [RELEASE_1_0.md](docs/RELEASE_1_0.md) |
| Visual explainer | [visualization/explainer/README.md](visualization/explainer/README.md) |

## Final repository state

The normal working branch set is intentionally small:

```text
main
release/1.0.0
legacy/studio-v1-2026-08-10
```

Historical experimental endpoints were preserved before branch cleanup. Open pull requests and issues were brought to zero during the final repository pass.

The scientific freeze is immutable, the research evidence is reproducible, the negative results remain visible, and the visualization layer is derived from the frozen data rather than used to redefine it.

## Project completion

VULKAX and the Reality Probe research program have reached their intended endpoint for this study.

The systems baseline and the primary experimental sequence are complete. The
locked positive and negative findings are preserved, including the successful
IRIS pendulum final test and the failed IRIS free-fall validation. Post-final
reviewer hardening is secondary analysis only; it cannot replace those frozen
results.

The project is now in manuscript hardening, evidence packaging, and repository
cleanup rather than open-ended method rescue.


## License and third-party material

Original Reality Probe / VULKAX code, documentation, and project-authored source
assets are licensed under the Apache License 2.0. See [LICENSE](LICENSE) and
[NOTICE](NOTICE).

Datasets, public meshes, downloaded media, and other external assets retain their
own terms. The current attribution and redistribution boundaries are recorded in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

For paper writing, start with the
[SIGGRAPH manuscript dossier](research/paper_data/MANUSCRIPT_DOSSIER_SIGGRAPH.md)
and the [manuscript asset index](research/paper_data/MANUSCRIPT_ASSET_INDEX.csv).

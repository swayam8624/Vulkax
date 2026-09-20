# Benchmark candidates for the Vulkax discovery campaign

## Tier A — immediate priority

### GAUGE (2026)

**Use:** primary real-world physical-fidelity benchmark candidate, especially textiles and volumetric deformables.

- 22 controlled task families.
- ~1,560 repeated real motion-capture trials.
- rigid body, cable, textile, volumetric deformable regimes.
- measured/calibrated physical metadata and uncertainty annotations.
- textile stretch/bend/fling/friction tasks.
- foam stretch/compression/shear/twist/bend and cantilever tasks.
- code repository reports MIT license.
- dataset card reports MIT; verify each released asset before redistribution.

Why it matters: unlike DOT C2, GAUGE is explicitly built around measurement-grounded physical fidelity and provides quantities Vulkax currently lacks. It is also a **novelty threat**: “diagnosing simulator physics” alone is not enough after GAUGE.

### IRIS (ECCV 2026)

**Use:** WorldIR, identifiability, refusal and extrapolation benchmark for simple rigid/ODE dynamics.

- repository currently states 240 videos = 8 classes × 3 settings × 10 takes, 4K/60fps.
- paper abstract/search metadata reports 220 in one version; treat exact count as versioned dataset metadata and verify downloaded release.
- independently measured ground-truth parameters.
- equation-family identification, parameter recovery, identifiability, extrapolation, robustness.
- code: MIT.
- dataset: CC-BY-NC-4.0 according to repository; do not treat as unrestricted commercial data.

Why it matters: very clean real benchmark for whether Vulkax knows when parameters are identifiable and whether it should abstain/refuse.

## Tier B — direct deformable baselines / datasets

### M-PhyGs / Phlowers (CVPR 2026)

Real multi-material flowers. Estimates material segmentation, Young's modulus and density from sparse-view interaction video after Gaussian reconstruction. Strong direct baseline/threat for heterogeneous material inference.

### Cloth-Sim2Real benchmark

Real RGB-D/point-cloud cloth manipulation with three garments and system identification across MuJoCo, Bullet, Flex and SOFA. Useful for cross-solver/model-disagreement tests.

### Fabric material videos (ICCV 2013)

Public fabric videos with measured stiffness and area weight. Old but useful as independent real material-property evidence.

## Tier C — synthetic stress laboratories

### MOSIV (2026)

Synthetic multi-object system-identification dataset from Genesis with contact-rich interactions, ground-truth stiffness/friction/plasticity and multi-view video. Useful for contact/event and multi-object counterfactuals. Dataset is large (~68.5 GB), so use selective subsets first.

### MPMWorlds (2026)

2D MPM synthetic videos across deformables, fluids, kinetic objects and emitters. Useful for extrapolation and failure-analysis experiments.

### Existing Vulkax controlled worlds

Use for cheap exhaustive sweeps because every physical quantity is known and reproducible.

## DOT C2 — retain, but change its role

DOT C2 remains valuable **not** as a true material-recovery benchmark. Its role should be:

- real measured trajectory case study;
- model-adequacy stress test;
- rest-state/load/thickness/density ambiguity example;
- failed-rewrite forensic case;
- demonstration of honest refusal when truth is unavailable.

Do not compare inferred E against nonexistent DOT material ground truth.

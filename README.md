# VulkanEngine-MINI: BEACON Research Renderer

BEACON is a reproducible Vulkan research renderer for **Budget- and Error-Aware Adaptive Clustered
Lighting**. It dynamically allocates depth partitions per screen tile, chooses explicit or bitset
light lists per cluster, conservatively bounds omitted diffuse contribution, and controls quality
and hierarchy complexity from measured timing budgets.

The repository contains real Vulkan rendering paths, analytical ablations, deterministic scene
generation, raw benchmark output, exact-reference image comparison, generated figures, and a
compiled research paper.

## Research claim

BEACON studies the unified system, not the individual ingredients. Clustered lighting, bounded
many-light approximation, SSBOs, instancing, and grid-based sampling are established techniques.
The contribution evaluated here is their combination as a temporally stable adaptive hierarchy with
aggregate error accounting, per-cluster representation selection, and online budget feedback.

## Techniques

| CLI name | Implementation |
| --- | --- |
| `baseline` | Original ten-light UBO forward reference |
| `ssbo` | Exact SSBO forward lighting |
| `instanced` | SSBO lighting with model-grouped instanced draws |
| `cpu-clustered` | Fixed-grid analytical CPU baseline |
| `fixed-cluster-cost-model` | Fixed-grid analytical cost ablation |
| `gpu-clustered` | Compute-built fixed-grid Vulkan clustered forward |
| `adaptive-exact` | Vulkan adaptive depth hierarchy, no pruning |
| `adaptive-bounded` | Vulkan adaptive hierarchy with aggregate bounded pruning |
| `beacon` | Full Vulkan BEACON with hybrid encoding, hysteresis, and controller |

Adaptive modes begin with a 16 x 9 XY grid. Each tile independently selects 1, 2, 4, or 8 depth
leaves from local light pressure. Sparse leaves store explicit indices; dense leaves use bitsets.
The exact mode retains every intersecting light. Bounded modes sort conservative diffuse bounds and
remove lights only while the accumulated omitted bound remains within the configured cluster budget.

## Build

On macOS:

```bash
brew install cmake glfw glm
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
```

The Vulkan SDK must be discoverable by CMake. Vertex, fragment, and compute shaders are compiled to
SPIR-V by the `Shaders` target.

## Run

Interactive full BEACON:

```bash
build/LveEngine --technique beacon --scene repeated \
  --objects 1000 --lights 500 --light-distribution single-hotspot \
  --cluster-build-budget-ms 0.7 --lighting-budget-ms 3.0 --quality-error 0.005
```

Measured run with exact offscreen comparison and terminal progress:

```bash
build/LveEngine --benchmark --technique beacon \
  --scene repeated --objects 1000 --lights 500 \
  --light-distribution single-hotspot \
  --frames 300 --warmup-frames 120 \
  --show-light-billboards false --capture-reference true \
  --output beacon_results/beacon_hotspot
```

Use `--quiet` to suppress the progress bar. The benchmark writes `frames.csv`, `manifest.json`, and
`summary.json`; analytical runs also write `clusters.csv`.

## Reproduce experiments

```bash
scripts/run_beacon_matrix.sh build/LveEngine beacon_results_matrix
python3 scripts/generate_beacon_figures.py beacon_results_matrix docs/results/reproduced
```

The matrix covers uniform, hotspot, depth-stacked, and adversarial distributions from 10 to 2,000
lights and from 100 to 10,000 objects. Vulkan timestamp queries populate GPU pass timings when the
device supports them; otherwise the CSV identifies CPU fallback timing explicitly.

## Checked-in artifact results

The deterministic three-frame artifact-validation run in
[docs/results/final](docs/results/final) was captured on an Apple M2 Pro through MoltenVK. This
device reports timestamp queries unavailable, so these are CPU frame timings and must not be read as
GPU kernel timings.

| Technique | p50 frame ms | p95 frame ms | Offscreen PSNR | SSIM |
| --- | ---: | ---: | ---: | ---: |
| `gpu-clustered` | 24.3365 | 25.5247 | 99.0000 | 1.000000 |
| `adaptive-exact` | 20.2507 | 20.3952 | 99.0000 | 1.000000 |
| `adaptive-bounded` | 24.6537 | 25.9196 | 86.5998 | 1.000000 |
| `beacon` | 17.8998 | 18.3242 | 99.0000 | 1.000000 |

This compact run validates execution and image comparison. It is not a substitute for the full
repeated-trial matrix. The raw rows and regenerated summary are in
[summary.md](docs/results/final/figures/summary.md).

## Research outputs

- [Research paper (PDF)](docs/paper/beacon_research.pdf)
- [LaTeX source](docs/paper/beacon_research.tex)
- [Artifact status and limitations](docs/BEACON_STATUS.md)
- [Paper-style implementation report](docs/BEACON_RESEARCH_REPORT.md)
- [Raw final validation data](docs/results/final)
- [Generated SVG figures](docs/results/final/figures)

## Key source files

- `src/beacon/adaptive_vulkan_builder.cpp`: hierarchy, pruning, encoding, hysteresis, and metrics
- `src/systems/clustered_lighting_system.cpp`: fixed-grid compute dispatch
- `shaders/cluster_build.comp`: fixed GPU cluster assignment
- `shaders/simple_shader_adaptive.frag`: adaptive explicit/bitset traversal
- `src/beacon/offscreen_comparison.cpp`: rendered MSE, PSNR, and SSIM
- `src/beacon/gpu_profiler.cpp`: Vulkan timestamp query pool
- `scripts/run_beacon_matrix.sh`: deterministic experiment matrix
- `scripts/generate_beacon_figures.py`: dependency-free figure generation

## Scope

The formal approximation claim covers opaque, unshadowed diffuse point lighting with finite-radius
inverse-square attenuation. Specular, shadows, transparency, neural sampling, and reservoir methods
are excluded from the claim and discussed only as related or future research directions.

## Attribution and license

The renderer substrate derives from Brendan Galea's Little Vulkan Engine tutorial under the MIT
license. BEACON's research renderer, benchmarks, shaders, controller, measurements, and paper are
the work contained in this repository. See [LICENSE](LICENSE).

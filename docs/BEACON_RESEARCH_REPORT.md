# BEACON Research Report

## Thesis

BEACON evaluates whether a clustered forward renderer can adapt depth partitioning, light-list
representation, and diffuse-light approximation while targeting explicit construction-time,
lighting-time, and image-error budgets.

## Implemented system

The repository provides nine executable techniques. `baseline`, `ssbo`, and `instanced` establish
forward-rendering baselines. `cpu-clustered` and `fixed-cluster-cost-model` provide deterministic
analytical ablations. `gpu-clustered` builds a fixed grid in Vulkan compute. `adaptive-exact`,
`adaptive-bounded`, and `beacon` render through Vulkan using per-tile adaptive depth leaves.

Full BEACON combines:

- a coarse 16 x 9 XY partition with 1, 2, 4, or 8 independently selected depth leaves per tile;
- aggregate conservative bounds for omitted diffuse point-light contribution;
- explicit lists for sparse clusters and dense bitsets for high-occupancy clusters;
- overflow-safe representation selection;
- split/merge hysteresis, eight-frame minimum lifetime, and a 16-change frame limit;
- EWMA controller feedback from Vulkan timestamps or an explicitly labeled CPU fallback;
- exact SSBO offscreen reference captures with MSE, PSNR, and SSIM.

## Research isolation

`adaptive-exact` changes partitioning without dropping lights. `adaptive-bounded` adds pruning.
`beacon` adds temporal state and budget feedback. The fixed CPU and fixed GPU variants expose the
cluster-builder ablation, while `ssbo` remains the exact many-light image reference.

## Reproduction

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
scripts/run_beacon_matrix.sh build/LveEngine beacon_results_matrix
python3 scripts/generate_beacon_figures.py beacon_results_matrix docs/results/reproduced
```

Each Vulkan case writes frame-level CSV, a JSON manifest, and a JSON summary. Analytical cases also
write cluster-level CSV. The matrix uses deterministic seeds, warm-up frames, fixed resolutions,
uniform and skewed light distributions, and explicit measurement-group labels.

## Current artifact evidence

The checked-in compact validation dataset uses 20 objects and 100 hotspot lights on Apple M2 Pro
through MoltenVK. The device reports Vulkan timestamps unavailable, so these values are CPU frame
measurements:

| Technique | p50 ms | p95 ms | PSNR | SSIM |
| --- | ---: | ---: | ---: | ---: |
| Fixed GPU clustered | 24.3365 | 25.5247 | 99.0000 | 1.000000 |
| Adaptive exact | 20.2507 | 20.3952 | 99.0000 | 1.000000 |
| Adaptive bounded | 24.6537 | 25.9196 | 86.5998 | 1.000000 |
| Full BEACON | 17.8998 | 18.3242 | 99.0000 | 1.000000 |

This is artifact-validation evidence, not a cross-hardware performance claim. Raw rows are stored in
`docs/results/final`, and the generated summary and SVG figures are under its `figures` directory.

## Claim boundary

The formal bound applies to opaque, unshadowed diffuse inverse-square point lighting with finite
influence radius. The grid is world-aligned, SSIM is global luminance, and RGBA8 captures quantize
small differences. Shadows, specular bounds, transparency, reservoirs, and neural sampling are
outside the claim. These are declared scope boundaries rather than missing execution paths.

The full paper is available as `docs/paper/beacon_research.pdf`, with reproducible LaTeX source next
to it.

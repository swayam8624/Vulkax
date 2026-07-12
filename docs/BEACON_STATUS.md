# BEACON Research Artifact Status

BEACON is a complete experimental renderer for budget- and error-aware adaptive clustered point
lighting. Every declared `RenderTechnique` has an executable implementation. The two analytical
techniques produce model data; the remaining techniques render through Vulkan.

## Vulkan techniques

- `baseline`: original ten-light `GlobalUbo` reference.
- `ssbo`: exact forward lighting from an unbounded point-light storage buffer.
- `instanced`: SSBO lighting plus model-grouped instanced submission.
- `gpu-clustered`: fixed 16 x 9 x 24 compute-built light lists consumed by clustered forward shading.
- `adaptive-exact`: per-XY-tile adaptive depth leaves with exact candidate retention.
- `adaptive-bounded`: adaptive leaves plus aggregate conservative diffuse pruning.
- `beacon`: bounded adaptive clustering, explicit/bitset encoding, hysteresis, minimum lifetime,
  bounded structural changes, and budget-controller feedback.

## Analytical techniques

- `cpu-clustered`: deterministic fixed-grid CPU model and cluster dump.
- `fixed-cluster-cost-model`: named fixed-grid cost-model ablation.

## Measurement surfaces

- CPU frame and adaptive hierarchy-build time.
- Vulkan query-pool build and lighting timestamps when the selected device exposes timestamps.
- Active clusters, maximum list size, explicit/bitset distribution, overflows, evaluated/pruned
  samples, predicted omitted bound, split/merge counts, and churn.
- Offscreen exact-reference MSE, PSNR, and global luminance SSIM.
- Raw frame CSV, cluster CSV for model runs, JSON manifest/summary, and generated SVG figures.

## Scope and limitations

The formal bound covers unshadowed diffuse inverse-square point lighting with a finite influence
radius. Specular BRDFs, shadows, transparency, and visibility-aware bounds are outside the stated
claim. Adaptive hierarchy construction is deliberately CPU-side so it can be compared with the
fixed GPU compute builder. The tested Apple M2 Pro MoltenVK device reports timestamp queries as
unavailable; those runs report CPU fallback timing while timestamp-capable Vulkan devices use the
query-pool columns automatically.

The checked-in `docs/results/final` dataset is a deterministic artifact-validation run. The full
matrix script supplies the larger repeated-trial protocol used for research evaluation.

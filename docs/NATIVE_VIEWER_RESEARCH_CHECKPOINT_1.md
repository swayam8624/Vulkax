# Vulkax Native Viewer Research Checkpoint 1

## Status

This checkpoint defines the first stable, benchmarked native interactive Gaussian-viewer layer for Vulkax.

It is presentation/runtime infrastructure. It does **not** alter Vulkax scientific authority: captured-world state, Gaussian stable identity, MPM state, calibration, verification, evidence, certificates, proposal/commit/rollback semantics, and research outputs remain unchanged.

Checkpoint stack:

- native Metal interactive viewer and reusable scene layer;
- native anisotropic Gaussian projection with degree-0..3 SH support;
- conservative visibility + importance LOD;
- correctness-validated Metal retained-set sorting with deterministic CPU fallback;
- direct Gaussian PLY / OBJ / image authoring inputs;
- exact OBJ mesh Surface mode plus sampled Gaussian representation;
- explicit single-image 2.5D splat-card mode;
- stable-ID Vulkax-compatible Gaussian PLY export;
- repeatable native benchmark artifact.

## Runtime authority policy

GPU sorting is never trusted merely because the Metal kernels execute.

For every newly loaded scene/device session:

1. Vulkax starts in validation mode.
2. CPU and Metal ordering are both evaluated for three camera invalidations.
3. Exact retained source-index order and depth-key parity must pass.
4. The comparable end-to-end GPU candidate path is timed against the CPU reference path.
5. Metal becomes authoritative only when the median GPU/CPU time ratio is at most **0.95**.
6. Any parity failure, invalid timing, Metal failure, or slower median locks the session to deterministic CPU ordering.
7. A scene reload resets validation.

The inspector reports validation progress, active authority, visibility time, CPU reference time, Metal wall/device time, median performance ratio, and fallback reason.

This policy intentionally prefers a correct faster CPU path over a correct but slower GPU path.

## Benchmark methodology

Dedicated workflow: `.github/workflows/native-viewer-research-checkpoint.yml`

Executable: `vulkax_viewer_benchmark`

Schema: `vulkax.native_viewer.research_checkpoint.v1`

The benchmark generates deterministic Gaussian scenes at:

- 10,000 splats
- 50,000 splats
- 100,000 splats
- 200,000 splats

Depth order is a deterministic coprime modular permutation, so source order is not correlated with camera depth.

Both paths use the same camera, conservative frustum visibility, opacity filtering, and importance-LOD settings.

CPU baseline:

```text
visibility + importance LOD + back-to-front CPU sort
```

Hybrid candidate:

```text
visibility + importance LOD without final CPU sort
-> Metal depth keys
-> Metal bitonic retained-set sort
-> synchronization/readback/sanity validation
```

One warm-up precedes five measured repetitions. The report contains median and p95 values.

Correctness is hard-gated on exact CPU/GPU retained-order parity. Performance is recorded rather than compared with an arbitrary CI-wide fixed threshold; the interactive runtime performs its own device-local promotion decision.

## Measured checkpoint result

GitHub Actions run: `35507754675`

Artifact: `native-viewer-research-checkpoint` (artifact id `10604811721`)

Environment:

- GPU: **Apple Paravirtual device**
- OS: **macOS 26.6.2 (25G83)**
- repetitions: **5**

| Source | Retained | Parity | CPU full median | CPU cull/LOD median | Metal wall median | Metal device median | Hybrid median | CPU / Hybrid |
|---:|---:|:---:|---:|---:|---:|---:|---:|---:|
| 10,000 | 10,000 | yes | 0.647 ms | 0.063 ms | 1.696 ms | 0.872 ms | 1.763 ms | 0.367x |
| 50,000 | 50,000 | yes | 4.155 ms | 0.750 ms | 4.816 ms | 1.829 ms | 5.705 ms | 0.728x |
| 100,000 | 100,000 | yes | 9.085 ms | 2.789 ms | 10.310 ms | 3.018 ms | 13.097 ms | 0.694x |
| 200,000 | 200,000 | yes | 19.831 ms | 5.706 ms | 26.490 ms | 5.321 ms | 32.120 ms | 0.617x |

### Interpretation

The checkpoint establishes **correctness**, not a GPU speedup.

Metal ordering exactly matched the CPU reference at every measured size. However, the current synchronous `MetalGpuSorter` wrapper is slower end-to-end on the hosted Apple paravirtual GPU.

The difference between Metal device time and Metal wall time is important. The kernels themselves take roughly 0.87–5.32 ms across this range, while total Metal-sort wall time is 1.70–26.49 ms. Current overhead includes transient buffer allocation, command encoding, explicit synchronization, CPU readback, and retained-set sanity validation.

Therefore this checkpoint deliberately keeps the performance-aware CPU fallback. On this benchmark environment, Metal would **not** be promoted.

These values must not be extrapolated to an M2 Pro or another Apple GPU. The native viewer repeats the promotion test locally for the actual device and scene.

## Stable authoring contract

### Gaussian PLY

Native PLY import preserves stored Gaussian position, anisotropic scale, rotation, opacity, SH data, and stable Vulkax identity where present.

### OBJ

OBJ import:

- supports polygon fan triangulation;
- supports positive and negative position indices;
- preserves optional vertex colours;
- keeps the exact mesh triangle representation for Surface mode;
- area-samples deterministic surface points into anisotropic Gaussians aligned with triangle normals;
- uses identical before/after state because authored assets are not scientific rewrite results.

### Image

Single-image import is explicitly labelled `image_splat_card_2_5d`.

It preserves decoded RGB/alpha and aspect ratio and creates a flat anisotropic Gaussian card. It does not claim monocular 3D reconstruction or invent unseen depth.

### Export

The native viewer can export the current Gaussian state through Vulkax's authoritative 3DGS PLY serializer. The round-trip test verifies Gaussian count, stable identity, scale, opacity, and SH representation.

## Acceptance gates

This checkpoint is considered stable only when all of the following are green on the final head:

- full Vulkax CI;
- native Metal viewer macOS compile/link;
- embedded Metal shader compile;
- reusable viewer scene tests on Linux/macOS;
- OBJ/image conversion tests;
- canonical PNG decode orientation/RGBA/alpha test on macOS;
- stable-ID Gaussian PLY export round-trip;
- GPU depth-key runtime parity;
- GPU bitonic-sort runtime parity;
- reusable `MetalGpuSorter` parity;
- GPU sort session policy tests, including correct-but-slower fallback;
- shuffled-depth research benchmark at 10k/50k/100k/200k;
- uploaded JSON + Markdown benchmark artifact.

## Reproduce on a Mac

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVULKAX_BUILD_TESTS=ON
cmake --build build --target vulkax_viewer vulkax_viewer_benchmark \
  vulkax_viewer_scene_tests vulkax_viewer_asset_import_tests \
  --parallel "$(sysctl -n hw.logicalcpu)"

ctest --test-dir build -R '^vulkax_viewer_(scene|asset_import)$' --output-on-failure

mkdir -p build/research-checkpoint
./build/vulkax_viewer_benchmark \
  --sizes 10000,50000,100000,200000 \
  --repeats 5 \
  --json build/research-checkpoint/native_viewer_benchmark.json \
  --markdown build/research-checkpoint/native_viewer_benchmark.md
```

The local benchmark is more relevant for renderer-performance decisions than hosted-CI timings because the interactive viewer's performance authority is device-specific.

## Next research target

The benchmark identifies the next optimization target directly: remove synchronous wrapper overhead rather than optimizing the already-fast depth-key arithmetic in isolation.

The next stage should keep retained indices and sort buffers GPU-resident, reuse buffer capacity across frames, encode ordering work into the render command stream, avoid CPU readback after trust, and evaluate a tile/radix ordering strategy against the exact CPU reference.

That work belongs **after** this checkpoint because Checkpoint 1 provides the reproducible correctness and timing baseline against which those changes can be measured.

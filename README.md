# Vulkax

**Verified Rewritable Reality — a graphics and computational-physics research system for turning captured scenes into persistent, physically executable world models.**

Vulkax begins with a physical **Problem** or captured **World**, keeps appearance separate from solver discretization, and requires quantitative evidence before a result may be called verified.

```text
Captured World / Problem
        |
        v
appearance <-> semantics <-> physical representation
        |                      |
        +------ World Correspondence Graph
                               |
                               v
                     operator / solver plan
                               |
                               v
                    accelerated execution
                               |
                               v
                         verification
                               |
                +--------------+--------------+
                v              v              v
             analysis        inverse       optimization
                |
                v
       Operator Influence / scientific visualization
```

The research question is stronger than Gaussian editing: **can a reconstructed real scene be rewritten locally — geometry, material, constraints and eventually topology — while keeping appearance, physical state and provenance mutually consistent and quantitatively trustworthy?**

## Current research implementation — 0.35 development head

### Captured-world representation

- renderer-independent `GaussianCloud`;
- ASCII and binary-little-endian 3DGS PLY ingestion;
- 3DGS log-scale, opacity-logit, quaternion and spherical-harmonic data;
- semantic `WorldIR` entities, revisions and provenance;
- bidirectional `WorldCorrespondenceGraph` linking Gaussian support, semantic entities and generic physical DOFs;
- transactional local translation/material rewrites, rollback receipts and explicit unaffected-region drift.

### Native Gaussian rendering

Vulkax contains a first reference-quality anisotropic Gaussian renderer:

- full 3D covariance is projected into screen-space 2D covariance;
- each visible Gaussian becomes an oriented anisotropic ellipse, not a point sprite;
- spherical harmonics up to degree 3 are evaluated from stored 3DGS coefficients;
- splats are depth ordered and alpha composited;
- projection/covariance/SH/sorting remain on CPU as the numerical reference;
- raster/compositing have native Vulkan and Metal implementations;
- a tracked synthetic Gaussian scene and renderer regression test are included.

This is intentionally not yet the final high-throughput renderer. GPU projection, tile binning, radix sorting and scaling benchmarks come after the reference path is validated.

### Appearance <-> physics coupling

The first decoupled coupling implementation uses affine-reproducing moving least squares (MLS):

- Gaussian centers reproduce affine physical motion to numerical precision;
- a local affine deformation gradient is fitted from physical support points;
- Gaussian covariance is transported as `Sigma' = F Sigma F^T`;
- covariance is decomposed back to Gaussian orientation and log-scales;
- deformation is non-accumulating because every update maps from stored captured/rest geometry;
- forces applied at Gaussian interaction points transfer back to physical DOFs;
- tests require partition of unity, affine reproduction, force conservation and torque conservation.

This is the first concrete implementation of the central Vulkax hypothesis:

```text
best representation for appearance != best representation for physics
```

### Captured deformable replay and material identification

Vulkax now has an end-to-end captured-object research path instead of only solver-internal synthetic state:

- `particles.csv` defines stable physical particle IDs, rest positions, mass and rest volume;
- `observations.csv` binds tracked marker IDs to physical particle IDs over time with explicit `fit` / `validation` splits;
- the captured pose is initialized by fitting an affine map from **fit-split t=0 observations only**;
- the active Gaussian object is inverse-warped into rest space through MLS before simulation;
- an independent nonlinear APIC/MPM physical representation performs free elastic relaxation;
- observed marker positions are compared against the physical state on the exact solver timestep lattice;
- fit/validation RMS and maxima, initialization error, appearance round-trip error, conservation, energy drift, deformation determinant and MLS residuals are exported;
- material calibration grid-searches Young's modulus and Poisson ratio using **nonzero-time fit rows only**;
- held-out validation rows are reported after selection and cannot influence candidate ranking.

A deterministic dataset generator makes the complete path reproducible. Its ground truth is `E = 15000 Pa`, `nu = 0.30`, `dt = 1e-4 s`, with held-out markers.

```bash
./build/vulkax captured-deformable-generate-example build/captured-example

./build/vulkax captured-material-calibrate \
  build/captured-example/object.ply \
  build/captured-example/particles.csv \
  build/captured-example/observations.csv \
  build/captured-calibration \
  1e-4 0.08
```

The generated bundle contains:

```text
object.ply
particles.csv
observations.csv
truth.csv
```

Calibration writes:

```text
material_grid.csv
selected_samples.csv
selected_summary.csv
selected_evidence.csv
```

This deterministic example is a controlled regression, **not evidence that real-world material identification has been solved**. Real captured observations with measurement noise, imperfect correspondences and model discrepancy are still required.

### Existing computational foundation

The repository also contains typed SI dimensions, `ProblemIR`, operator graphs, solver/fidelity planning, numerical reference solvers, result certificates, goal-oriented adaptivity, inverse/optimization foundations, discrete adjoints, global and local Operator Influence foundations, geometry/voxelization, scientific visualization, camera/capture, Vulkan/Metal probing and native compute conformance.

## Research-integrity status

Release tests are compiled with assertions active. Enabling them exposed two previously hidden failures: an incorrect goal-oriented refinement expectation and shared-edge degeneracy in mesh voxelization. Both were fixed rather than suppressed.

The native Gaussian renderer and covariance-coupling path must continue to pass the Linux/macOS/Windows matrix and real Metal/Vulkan smoke tests before stronger validation claims are made. CI renders the tracked Gaussian scene through Vulkan on Linux and Metal on macOS.

The captured-deformable path has controlled synthetic replay and material-identification regressions, including held-out observations. It does **not** yet establish real-world material recovery, production-scale Gaussian rendering, topology rewriting, automatic semantic reconstruction, XR interaction, or publishable novelty.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVULKAX_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Inspect the tracked Gaussian scene:

```bash
./build/vulkax gaussian-info examples/synthetic_gaussian.ply
```

Render it on Apple Metal:

```bash
./build/vulkax gaussian-render examples/synthetic_gaussian.ply build/gaussian-metal.ppm Metal
```

Render it through Vulkan:

```bash
./build/vulkax gaussian-render examples/synthetic_gaussian.ply build/gaussian-vulkan.ppm Vulkan
```

On macOS:

```bash
open build/gaussian-metal.ppm
open build/gaussian-vulkan.ppm
```

A real 3DGS export can be inspected/rendered by replacing the example path with its `point_cloud.ply`.

Existing problem and backend commands remain:

```bash
./build/vulkax validate examples/rotating_mill.vkx
./build/vulkax inspect examples/rotating_mill.vkx
./build/vulkax plan examples/rotating_mill.vkx
./build/vulkax --probe-backends
./build/vulkax --conformance Metal
./build/vulkax --conformance Vulkan
```

## Architectural laws

1. A solver never owns presentation semantics.
2. A renderer never decides governing physics.
3. A problem is not a visualizer mode.
4. Appearance primitives and physical degrees of freedom are allowed to differ.
5. Physical values carry dimensions.
6. `VERIFIED` is earned by evidence; it is never a UI label.
7. Backend choice is capability-driven and cannot alter problem semantics.
8. A new feature must help solve a problem that was not hard-coded as a demo.
9. Research hooks such as operator attribution and world correspondence live in the core representation, not as post-hoc visualization hacks.
10. Integration of known techniques is not itself a novelty claim.

## Immediate research sequence

1. Keep validating the native Gaussian reference renderer and MLS covariance coupling on CI + Apple M2 Pro.
2. Move Gaussian projection/binning/sorting onto GPU while retaining the CPU oracle.
3. Add hierarchical Gaussian IDs/entity selection for million-splat scenes.
4. **Captured deformable baseline: implemented** for controlled Gaussian + APIC/MPM replay with explicit correspondence and evidence export.
5. **Transfer/conservation/error benchmarking: implemented foundation**, with real-capture evaluation still pending.
6. **Material identification: implemented controlled fit/held-out grid-search regression**, with noisy real observations and uncertainty analysis still pending.
7. Apply Operator Influence Fields to the same captured deformable object and verify local counterfactual predictions against reruns.
8. Add XR only once the physical/coupling mechanisms are quantitatively trustworthy.

See [`docs/REWRITABLE_REALITY.md`](docs/REWRITABLE_REALITY.md), [`docs/RESEARCH.md`](docs/RESEARCH.md), [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md), and [`docs/VISION.md`](docs/VISION.md).

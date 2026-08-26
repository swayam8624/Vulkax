# Vulkax

**Verified Rewritable Reality — a graphics and computational-physics research system for turning captured scenes into persistent, physically executable world models.**

Vulkax keeps captured appearance separate from solver discretization, connects both through explicit correspondence, and treats verification evidence as part of the system rather than a presentation layer.

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

The central research question is stronger than Gaussian editing: **can a reconstructed scene be rewritten locally — geometry, material and supported constraints — while keeping appearance, physical state and provenance mutually consistent and quantitatively trustworthy?**

## Current implementation — Vulkax 0.60

### Captured-world representation

- renderer-independent `GaussianCloud`;
- ASCII and binary-little-endian 3DGS PLY ingestion;
- 3DGS log-scale, opacity-logit, quaternion and spherical-harmonic data;
- semantic `WorldIR` entities, revisions and provenance;
- bidirectional `WorldCorrespondenceGraph` linking Gaussian support, semantic entities and generic physical DOFs;
- copy-then-commit world transactions with expected-revision, duplicate-ID and edit-precondition checks;
- typed local geometry, material and supported constraint-metadata rewrites;
- rollback receipts preserving appearance positions, material/constraint metadata, world revision and provenance;
- evidence-derived verified rewrite execution with affected/unaffected-region locality checks.

### Unified verified rewrite transactions

Vulkax 0.60 makes `verified` an evidence result rather than a caller-controlled flag. A transaction is structurally validated before mutation, applied to a candidate world, and committed atomically. If required post-commit evidence is absent or fails, the verified executor restores the prior world automatically.

Physical rewrites require a verifier that supplies a traceable artifact, a completed/passed physical rerun, and finite scalar error/tolerance evidence with `error <= tolerance`. Material rewrites additionally require a completed/passed independent oracle. Geometry rewrites must satisfy appearance-propagation and unaffected-region locality policy; entities with physical bindings also require a physical rerun.

0.60 includes one concrete solver-backed adapter for the controlled captured APIC/MPM Young's-modulus path. It derives the selected region from stable MPM-particle bindings and runs the retained finite-difference derivative reference, exact-magnitude nonlinear counterfactual, APIC reverse material adjoint and derivative comparison before the central transaction can commit.

Reproduce the public controlled transaction after generating the deterministic captured bundle:

```bash
./build/vulkax_captured_rewrite \
  build/captured-example/capture.vkcap \
  build/captured-verified-rewrite \
  m4 0.003 1 1 1 \
  43,44,47,48,59,60,63,64 \
  15000 0.30 0.08 0.01 0.02
```

The controlled CI case rewrites eight stable particles from `15000 Pa` to `15300 Pa` (+2%). On the Linux public gate the derived physical error is `6.52454608162e-05` against the controlled tolerance `0.25`, unaffected Gaussian positional drift is `0`, and the transaction commits without rollback. This is deterministic synthetic verification evidence, **not a measured real-object rewrite claim**.

See [`docs/VERIFIED_REWRITE_0_60.md`](docs/VERIFIED_REWRITE_0_60.md) for the evidence contract, rollback semantics and implementation boundary. Geometry and constraint transactions have the central verifier interface and regression coverage, but 0.60 does not yet claim a captured solver-specific geometry or constraint verifier.

## Scalable Gaussian execution

Vulkax retains a CPU numerical oracle and also contains native Gaussian projection paths for Vulkan and Metal.

The renderer currently provides:

- full 3D covariance projected into screen-space 2D covariance;
- oriented anisotropic Gaussian ellipses rather than point sprites;
- spherical harmonics up to degree 3 from stored 3DGS coefficients;
- native Vulkan and Metal raster/compositing paths;
- native Vulkan and Metal projection of the controlled Gaussian stream;
- projected visibility, extent and tile-bound evidence;
- deterministic stable far-to-near ordering of visible projected splats;
- deterministic CSR-style tile-reference construction;
- CPU-vs-native image comparison;
- public timing and memory scaling evidence.

The implementation boundary matters: **native projection is GPU-backed, while final stable ordering and the current CSR tile-reference construction remain CPU-side.** The 0.50 renderer milestone does not claim GPU radix sorting or a fully GPU-resident tile/bin/sort/composite pipeline.

Run the public scaling benchmark with:

```bash
./build/vulkax_gaussian_scaling \
  examples/synthetic_gaussian.ply \
  build/gaussian-scaling.csv \
  Vulkan 64 256 3 16
```

Use `Metal` on macOS.

The machine-readable evidence records splat/tile counts, projection and tile memory, CPU/native/total timings, image RMSE/PSNR, maximum channel difference, changed-pixel fraction and whether native projection was actually used.

CI validates the controlled 64/128/256-splat sweep with a combined image policy:

```text
maximum channel delta       <= 3 byte values
normalized RGBA RMSE        < 1e-4
changed-pixel fraction      < 1e-4
native projection           required
```

Timing is recorded but **speedup is not a pass criterion**. Linux CI currently uses Mesa `llvmpipe`, so its software Vulkan projection is slower than the CPU oracle on these tiny scenes. The repository does not turn that result into an acceleration claim.

See [`docs/GAUSSIAN_SCALING_0_50.md`](docs/GAUSSIAN_SCALING_0_50.md) for the exact evidence contract, observed CI values and limitations.

## Appearance ↔ physics coupling

The decoupled coupling path uses affine-reproducing moving least squares (MLS):

- Gaussian centers reproduce affine physical motion to numerical precision;
- local affine deformation is fitted from physical support points;
- Gaussian covariance is transported as `Sigma' = F Sigma F^T`;
- transported covariance is decomposed back to Gaussian orientation and log-scales;
- updates map from stored captured/rest geometry rather than accumulating deformation error;
- forces at Gaussian interaction points transfer back to physical DOFs;
- tests cover partition of unity, affine reproduction, force conservation and torque conservation.

The representation principle is explicit:

```text
best representation for appearance != best representation for physics
```

## Captured deformable replay and material identification

The controlled captured-object path contains:

- stable physical particle IDs with rest position, mass and rest volume;
- tracked marker observations bound to particle IDs over time;
- explicit `fit` and held-out `validation` splits;
- affine captured-pose initialization from fit-split `t=0` observations only;
- inverse appearance warp into rest space through MLS;
- nonlinear APIC/MPM free elastic relaxation;
- exact solver-lattice observation comparison;
- fit/validation RMS and maxima;
- initialization and appearance round-trip error;
- conservation, energy, deformation-determinant and MLS evidence;
- grid-search material calibration over Young's modulus and Poisson ratio using nonzero-time fit rows only.

Generate the deterministic controlled bundle:

```bash
./build/vulkax captured-deformable-generate-example build/captured-example
./build/vulkax captured-deformable-validate-bundle \
  build/captured-example/capture.vkcap
```

Calibrate material parameters:

```bash
./build/vulkax captured-material-calibrate \
  build/captured-example/object.ply \
  build/captured-example/particles.csv \
  build/captured-example/observations.csv \
  build/captured-calibration \
  1e-4 0.08
```

The controlled generator uses known regression truth (`E = 15000 Pa`, `nu = 0.30`, `dt = 1e-4 s`). It is a deterministic verification fixture, **not evidence that material identification on real captured objects has been solved**.

## Versioned capture evidence contract

`capture.vkcap` schema v1 records:

- relative payload paths;
- SHA-256 identity for appearance, particles, observations and uncertainty;
- SI units;
- coordinate frame and axis convention;
- solver timestep;
- `synthetic`, `measured` or `derived` source kind;
- human-readable source/provenance description.

`uncertainty.csv` supplies one component-wise position-uncertainty row per observation. The validator checks hashes, syntax, marker-to-particle identity, solver-lattice timestamps, physical mass/volume validity, initialization coverage, dynamic fit/validation coverage and uncertainty/trajectory consistency.

A valid hash proves **input identity**, not measurement authenticity or physical correctness.

Vulkax can also author the manifest around externally prepared payloads without modifying those payloads. The caller declares source kind and provenance; the tool does not infer that data are genuinely measured.

See [`docs/CAPTURE_BUNDLE_0_40.md`](docs/CAPTURE_BUNDLE_0_40.md).

## Captured material Operator Influence

The controlled captured path combines three deliberately separate layers:

1. an independent nonlinear finite-difference material-influence oracle;
2. a reverse-mode APIC/MPM material-scale derivative implementation;
3. an adjoint-guided adaptive spatial proposal layer that must still be checked by the nonlinear oracle.

The scalar objective is projected displacement of a selected tracked marker from `t=0` to a selected observation time.

The reverse kernel currently covers the controlled regime:

- pure APIC;
- zero external/gravity forcing;
- `boundaryCells == 0`;
- quadratic B-spline P2G/G2P dependence;
- affine velocity and deformation-gradient evolution;
- Neo-Hookean Kirchhoff stress;
- analytic derivative with respect to particle-local Young's-modulus scale.

One reverse trajectory produces a stable-ID `dJ/ds_p` field for all physical particles. Region derivatives are sums over their constituent particle gradients and are checked against the finite-difference reference.

The default adaptive proposal on the deterministic 64-particle fixture selects 10 particles in one connected region while retaining about `91.18%` of total absolute particle-gradient mass. Its derivative and nonlinear counterfactual checks are tight in that controlled case. That result is **not** a claim of automatic material segmentation or real-world influence localization.

Reproduce the full reference/adjoint/adaptive path:

```bash
./build/vulkax captured-material-influence \
  build/captured-example/object.ply \
  build/captured-example/particles.csv \
  build/captured-example/observations.csv \
  build/captured-influence \
  m4 0.003 1 1 1 \
  1e-4 15000 0.30 0.08 0.01 0.02
```

The finite-difference/nonlinear path remains the **verification oracle** after the adjoint is enabled.

## Synthetic observation robustness

The robustness command independently perturbs initial-pose and nonzero-time observations with deterministic component-wise noise, reruns material calibration and downstream influence/proposal analysis, and records stability evidence.

```bash
./build/vulkax captured-observation-robustness \
  build/captured-example/object.ply \
  build/captured-example/particles.csv \
  build/captured-example/observations.csv \
  build/captured-robustness \
  m4 0.003 1 1 1 \
  1e-6 1e-6 12345 1e-4 0.08
```

This is explicitly a **synthetic stress test**. The meaningful uncertainty scale for measured data must come from the real tracking/capture process.

See [`docs/OBSERVATION_ROBUSTNESS_0_39.md`](docs/OBSERVATION_ROBUSTNESS_0_39.md).

## Build

### macOS / Linux

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DVULKAX_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

On Linux, install a Vulkan development/runtime stack and `glslangValidator` to enable the Vulkan compute/render/projection CI-equivalent path.

### Windows

```powershell
cmake -S . -B build -DVULKAX_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The project remains C++20. The 0.60 milestone does not change the language standard.

## Useful graphics commands

Inspect a Gaussian PLY:

```bash
./build/vulkax gaussian-info examples/synthetic_gaussian.ply
```

Render with a native backend:

```bash
./build/vulkax gaussian-render \
  examples/synthetic_gaussian.ply \
  build/gaussian.ppm \
  Vulkan
```

or on macOS:

```bash
./build/vulkax gaussian-render \
  examples/synthetic_gaussian.ply \
  build/gaussian.ppm \
  Metal
```

Check backend discovery/conformance:

```bash
./build/vulkax --require-backend Vulkan
./build/vulkax --conformance Vulkan
```

Use `Metal` on macOS.

## Research-integrity status

What is currently supported by controlled evidence:

- captured/physical representations remain separate and explicitly coupled;
- deterministic captured replay and held-out material-calibration regressions;
- finite-difference/nonlinear material-influence oracle;
- controlled APIC material-scale reverse derivative checked against that oracle;
- adaptive material-region proposals checked by independent nonlinear reruns;
- deterministic synthetic observation-noise stress evidence;
- versioned capture bundle identity/validation contract;
- native Vulkan/Metal Gaussian projection with CPU-oracle image regression;
- machine-readable Gaussian scaling/memory/timing evidence;
- atomic unified rewrite transactions with evidence-derived verification and automatic rollback;
- one solver-backed controlled captured-material rewrite path that consumes fresh finite-difference, nonlinear and adjoint evidence.

What is **not** established yet:

- real measured deformable validation for the 0.45 milestone;
- real measured rewrite verification;
- captured solver-specific geometry and constraint verification beyond the generic transaction/verifier interface and controlled tests;
- production-scale Gaussian rendering performance;
- full GPU tile/bin/radix-sort/composite execution;
- general differentiable MPM through FLIP blending, boundary clamps and arbitrary forcing;
- automatic material segmentation from the adjoint field;
- topology surgery;
- publication novelty or broad real-world claims.

The measured-deformable milestone remains **external-data-blocked** until a genuine captured sequence with known scale, marker correspondence and measurement uncertainty exists. Synthetic or re-authored synthetic payloads are not allowed to satisfy it.

## Road to 1.0

The scoped roadmap is in [`docs/ROADMAP_1_0.md`](docs/ROADMAP_1_0.md).

After 0.60, the code-completable sequence is:

```text
0.70  scale-safe identity and selection
0.80  one-command controlled captured-world research demo
0.90  release hardening and documentation/performance audit
1.0   stable verified-rewritable-reality baseline
```

0.45 measured validation remains an independent evidence requirement rather than a reason to fabricate data or block unrelated code-only milestones.

## Development rules

- One milestone branch at a time.
- Version bumps occur only after the behavior/evidence candidate is green.
- Proposal and verification paths remain separate.
- Synthetic evidence stays labelled synthetic.
- Numerical gates are documented and tied to explicit baselines/requirements.
- The finite-difference/nonlinear oracle remains available after efficient derivative paths exist.
- Failed cases that expose genuine limitations are fixed or documented rather than silently suppressed.

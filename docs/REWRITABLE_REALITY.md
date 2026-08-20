# Vulkax research program: Verified Rewritable Reality

## Research question

Can a captured real-world scene become a persistent, editable, physically executable and scientifically
inspectable world model without sacrificing photorealistic appearance or numerical trustworthiness?

Vulkax treats this as a research program, not as a claim already proved.

## Representation principle

The central hypothesis is deliberately different from approaches that use one primitive for both rendering
and simulation:

```text
best representation for appearance != best representation for physics
```

Vulkax therefore maintains three coupled views of one world:

```text
Appearance world   <->   Semantic world   <->   Physical world
3D/4D Gaussians          entities/parts          FEM / MPM / DEM / CFD
```

The coupling object is the **World Correspondence Graph (WCG)**. It records which appearance primitives
belong to semantic entities and which physical degrees of freedom represent those entities. State-transfer
algorithms are evaluated independently of renderer and solver.

## Rewriting, not merely editing

An edit changes appearance. A rewrite changes executable world state. Examples include geometry,
material parameters, physical constraints, topology, lighting, observations and solver-relevant metadata.
Each rewrite is a transaction with provenance and an explicit affected support.

The first measurable invariant is **unaffected-region drift**:

```text
D_unaffected = max positional/appearance change outside the transaction support.
```

For operations that should be perfectly local, the target is exactly zero. For learned or optimization-based
rewrites, the metric becomes a quantitative measure rather than an aesthetic judgement.

## Research hypotheses

### H1 — Decoupled representation

A correspondence-based appearance/physics architecture can preserve rendering quality while allowing the
physical discretization to change independently for accuracy, adaptivity and solver choice.

### H2 — Persistent localized rewriting

A transaction can update the necessary appearance, semantic and physical state while measurably limiting
changes outside the requested region.

### H3 — Bidirectional state transfer

Appearance-to-physics interaction and physics-to-appearance deformation can be implemented with bounded
transfer error and explicit conservation evidence. The current affine MLS reference implementation enforces
partition of unity and affine reproduction, transports Gaussian covariance through the fitted deformation,
and tests force and torque conservation for point interactions. These are implementation invariants, not yet
a cross-scene research result.

### H4 — Real-to-verified-sim identification

Observed images/video/forces can identify physical parameters whose simulated response predicts held-out
real observations within reported uncertainty.

### H5 — Physics debugging

Operator Influence Fields can predict the effect of held-out local interventions on a user-selected observable
better per unit compute than brute-force finite differences.

## Required evaluation dimensions

No paper or project page should report only PSNR or screenshots. Depending on the experiment, record:

- PSNR / SSIM / LPIPS for appearance;
- depth, surface or Chamfer error for geometry;
- displacement, velocity, force and stress error for mechanics;
- conservation residuals where applicable;
- correspondence/state-transfer error;
- covariance/deformation-transfer error;
- unaffected-region drift;
- edit latency and recompile/re-solve cost;
- inverse-parameter error and confidence/identifiability;
- predicted-vs-actual counterfactual error;
- runtime, memory and scaling with Gaussian count / physical DOF;
- failure cases and the operating region where approximations remain valid.

## Current implementation boundary — Vulkax 0.20 development head

### Captured-world representation

- renderer-independent `GaussianCloud`;
- ASCII and binary-little-endian 3DGS PLY ingestion;
- semantic `WorldIR` with revision/provenance state;
- `WorldCorrespondenceGraph` linking Gaussians, entities and generic physical handles;
- transactional rigid translations and material-parameter rewrites;
- rollback receipts;
- explicit unaffected-region drift measurement.

### Reference Gaussian rendering

- projection of full 3D Gaussian covariance to oriented screen-space covariance;
- real anisotropic ellipse raster support rather than point sprites;
- spherical-harmonic evaluation through degree 3 when coefficients are available;
- back-to-front alpha compositing;
- native Vulkan and Metal raster/compositing paths;
- CPU projection/covariance/SH/depth-order reference path retained as an oracle;
- tracked synthetic PLY scene, CLI renderer and regression tests.

The reference renderer is an implementation milestone, not yet a performance contribution. GPU projection,
tile binning, sorting and large-scene benchmarks remain future work.

### Reference appearance/physics coupling

- affine-reproducing MLS support from Gaussian centers to independent physical points;
- partition-of-unity and affine-reproduction error measurements;
- non-accumulating center transfer from a captured/rest state;
- local weighted affine deformation fit;
- full Gaussian covariance transport `Sigma' = F Sigma F^T`;
- eigendecomposition back to Gaussian rotation/log-scale parameters;
- Gaussian-point force transfer back to physical DOFs;
- explicit net-force and net-torque conservation evidence;
- tests using a general affine transform with rotation/stretch/shear.

### Integrity controls

Release tests keep assertions enabled. Doing so exposed previously hidden adaptivity and voxelization failures;
both were fixed rather than suppressing their checks. CI builds Linux/macOS/Windows and explicitly smoke-tests
the tracked Gaussian scene through Vulkan on Linux and Metal on macOS.

## Non-claims

Vulkax does **not** yet claim:

- production-scale or state-of-the-art Gaussian rendering performance;
- MPM/FEM simulation of a captured real object;
- topology rewriting;
- automatic semantic reconstruction;
- inverse material identification from real video;
- XR interaction;
- superiority over PhysGaussian, VR-GS, GS-Verse, Real2Sim or other related methods;
- publishable novelty from integration alone.

## Immediate research milestones

1. Validate the current native renderer and covariance-coupling head on CI and Apple M2 Pro.
2. Move projection, tile binning and depth ordering to GPU while retaining the CPU oracle.
3. Add hierarchical Gaussian IDs and entity selection suitable for million-splat scenes.
4. Build an independent MPM/FEM representation for a captured deformable-object benchmark.
5. Measure bidirectional transfer error, conservation, unaffected drift, runtime and memory across resolution changes.
6. Add observation loss and identify material parameters from captured loading data; validate on held-out observations.
7. Add local material/geometry rewrites and quantify physical and visual side effects.
8. Apply Operator Influence Fields to the same captured object and validate predictions by full counterfactual reruns.
9. Add XR only after the above mechanisms are quantitatively trustworthy.

## Research discipline

- Integration of known methods is not itself novelty.
- Every new research claim requires a baseline and a falsifiable metric.
- Rendering primitives never determine solver discretization by architectural accident.
- Learned components may propose or accelerate; verification decides whether a result is trusted.
- Preserve failed cases and negative results.
- The flagship demo must be a consequence of the method, not a substitute for evaluation.

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
belong to semantic entities and which physical degrees of freedom represent those entities. State transfer
algorithms will be evaluated independently of the renderer and solver.

## Rewriting, not merely editing

An edit changes appearance. A rewrite changes the executable world state. Examples include geometry,
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
transfer error. Later work must test conservation of momentum/energy where those quantities are meaningful.

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
- unaffected-region drift;
- edit latency and recompile/re-solve cost;
- inverse-parameter error and confidence/identifiability;
- predicted-vs-actual counterfactual error;
- runtime, memory and scaling with Gaussian count / physical DOF;
- failure cases and the operating region where approximations remain valid.

## Current implementation boundary — Vulkax 0.18

The first research slice provides:

- renderer-independent `GaussianCloud`;
- ASCII and binary-little-endian 3DGS PLY ingestion;
- semantic `WorldIR` with revision/provenance state;
- `WorldCorrespondenceGraph` linking Gaussians, entities and generic physical handles;
- transactional rigid translations and material-parameter rewrites;
- rollback receipts;
- explicit unaffected-region drift measurement;
- tests for Gaussian ingestion, correspondence integrity and local rewrite invariance.

This does **not** yet claim Gaussian rendering, MPM coupling, topology editing, learned semantics, inverse material
identification from video, XR interaction or publishable novelty.

## Immediate next research milestones

1. Native Vulkan Gaussian renderer with Metal parity on Apple.
2. Hierarchical Gaussian IDs and entity selections suitable for million-splat scenes.
3. Surface/MPM embedding and measurable bidirectional transfer operators.
4. Captured deformable-object benchmark with known loading and held-out observations.
5. Material-parameter identification through differentiable simulation + Gaussian observation loss.
6. Local material/geometry rewrites with drift, transfer and physical-error measurements.
7. Operator Influence visualization on the same captured object.
8. XR only after the above mechanisms are quantitatively trustworthy.

## Research discipline

- Integration of known methods is not itself novelty.
- Every new research claim requires a baseline and a falsifiable metric.
- Rendering primitives never determine solver discretization by architectural accident.
- Learned components may propose or accelerate; verification decides whether a result is trusted.
- Preserve failed cases and negative results.
- The flagship demo must be a consequence of the method, not a substitute for evaluation.

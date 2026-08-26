# Vulkax roadmap to 1.0

This roadmap freezes the core project scope. Vulkax 1.0 is complete when one captured deformable object can move through the entire verified-rewritable-reality pipeline reproducibly:

```text
captured appearance + measured observations
            |
            v
stable appearance <-> physical correspondence
            |
            v
material calibration + uncertainty / robustness evidence
            |
            v
particle-level Operator Influence
            |
            v
adaptive local rewrite proposal
            |
            v
independent nonlinear verification
            |
            v
appearance update + provenance-preserving transaction
            |
            v
rendered result + machine-readable evidence bundle
```

The objective is a coherent, quantitatively defensible research system, not an unlimited simulation engine.

## Definition of done for 1.0

Vulkax 1.0 must satisfy all of the following.

1. **Captured input** — ingest a Gaussian appearance object, a stable physical-particle representation, and measured marker observations through a documented data contract.
2. **Correspondence** — preserve stable IDs from capture through physical replay, influence analysis, adaptive selection and rewrite evidence.
3. **Physical replay** — run the controlled APIC/MPM deformable path with explicit numerical evidence and deterministic regression coverage.
4. **Material identification** — calibrate material parameters using fit observations only and report held-out validation separately.
5. **Robustness** — quantify calibration and influence-field sensitivity to observation uncertainty instead of presenting one clean-fit number.
6. **Operator Influence** — expose particle-local material sensitivity and adaptive spatial proposals.
7. **Independent verification** — retain finite-difference and nonlinear counterfactual reruns as oracles for proposed material rewrites.
8. **Local rewrite transaction** — apply a verified local material/geometry/constraint rewrite through the world/correspondence representation with provenance and rollback evidence.
9. **Appearance consistency** — propagate verified physical changes back to Gaussian position/covariance without accumulating deformation error.
10. **Rendering** — render the rewritten Gaussian scene through the validated reference renderer and GPU-backed raster path.
11. **Benchmarkability** — provide deterministic benchmark commands plus at least one measured captured-object evidence bundle when external data is available.
12. **Release quality** — Linux, macOS and Windows CI green; assertions active in release tests; no known failing core tests; documentation states limitations precisely.

## Milestone sequence

### 0.39 — observation robustness — implemented

Purpose: make clean synthetic calibration/influence results uncertainty-aware before using measured data.

Deliverables:

- deterministic t=0 pose-noise and dynamic-observation-noise perturbations;
- repeated material calibration without validation leakage;
- Young's-modulus and Poisson-ratio selection drift;
- fit, held-out validation, initialization and appearance-roundtrip error;
- particle-adjoint cosine similarity and relative L2 field error;
- strongest-particle stability;
- adaptive-region particle overlap and retained absolute-gradient mass;
- CSV evidence and a public CLI stress command;
- controlled noise-sweep regression in CI.

Exit gate: satisfied on the controlled synthetic path. Zero-noise identity reproduces the clean baseline exactly; nonzero scenarios are finite, deterministic and evidence-producing. The controlled 1 micrometre result remains synthetic regression evidence rather than a measured tolerance claim.

### 0.40 — capture evidence contract and dataset validation — implemented

Purpose: remove assumptions hidden inside hand-authored CSV files.

Implemented deliverables:

- versioned `vulkax_capture 1` captured-deformable manifest;
- explicit SI units, coordinate frame, axis convention, solver timestep and source/provenance fields;
- SHA-256 identities for appearance, particles, observations and uncertainty payloads;
- one-row-per-observation position-uncertainty sidecar;
- validation for duplicate/invalid IDs, missing marker trajectories, off-lattice timestamps, impossible masses/volumes, unknown particle references and inconsistent dynamic fit/validation assignments;
- stable marker-to-particle correspondence checks across time;
- initialization and dynamic fit/held-out-validation coverage requirements;
- public `captured-deformable-validate-bundle` command that validates the bundle before simulation;
- deterministic generator emission of the same manifest/payload contract intended for measured data;
- Linux end-to-end corruption rejection plus the cross-platform captured-bundle regression.

Exit gate: satisfied for the contract itself and the deterministic controlled bundle. Malformed bundles covered by the regression fail with actionable errors, payload mutation is rejected by SHA-256 identity, and the generator emits a schema-valid bundle before the existing simulation pipeline runs. This does **not** satisfy the 0.45 measured-data requirement.

### 0.45 — measured deformable benchmark

Purpose: cross the boundary from controlled synthetic verification to actual captured evidence.

External requirement: a measured deformable sequence is required. Code alone cannot satisfy this milestone.

Minimum experiment:

- one soft/deformable object;
- known spatial scale and coordinate frame;
- Gaussian/point-based captured appearance;
- at least four initialization markers and multiple dynamic markers;
- explicit fit/validation marker split;
- repeated or uncertainty-labelled measurements if practical.

Evaluation:

- replay error;
- calibrated material stability;
- held-out validation error;
- observation-noise robustness sweep around measured uncertainty;
- influence-field stability;
- at least one proposed local material rewrite checked by a nonlinear rerun.

Exit gate: repository language may claim only what the measured evidence actually supports.

### 0.50 — scalable Gaussian execution

Purpose: remove the largest remaining graphics-system gap while retaining the CPU numerical oracle.

Deliverables:

- GPU projection of Gaussian means/covariances;
- tile/bin assignment;
- deterministic or validated depth ordering strategy;
- GPU-friendly compositing path;
- CPU-vs-GPU image regression metrics;
- timing and memory benchmarks across increasing splat counts;
- graceful fallback to the reference renderer.

Exit gate: GPU output remains within documented image-error tolerances of the reference path and scaling evidence is published in machine-readable form.

### 0.60 — unified verified rewrite transaction

Purpose: turn isolated research commands into the central product/research operation.

Required rewrite classes for 1.0:

- local material coefficient rewrite;
- local geometric translation/deformation rewrite;
- constraint/boundary-condition metadata rewrite where supported by the physical path.

Transaction requirements:

- stable target IDs;
- precondition validation;
- provenance record;
- affected/unaffected-region evidence;
- rollback receipt;
- physical rerun where the rewrite changes physics;
- appearance propagation through correspondence;
- verification status derived from evidence, never set manually.

Topology surgery is explicitly deferred beyond 1.0.

### 0.70 — scale-safe identity and selection

Purpose: make the world/correspondence layer usable beyond tiny tracked examples.

Deliverables:

- hierarchical Gaussian/entity IDs;
- selection groups independent of transient array order;
- correspondence lookup tests under reorder/filter operations;
- scalable bounding hierarchy or equivalent spatial lookup;
- benchmark of selection/correspondence operations on large synthetic clouds.

Exit gate: stable identity survives serialization, filtering and rewrite transactions.

### 0.80 — one-command end-to-end research demo

Purpose: demonstrate the actual Vulkax thesis instead of a collection of subcommands.

Target command concept:

```text
vulkax captured-world-run <bundle> <output-dir> [settings]
```

The run should produce:

- validated input manifest;
- calibration table and selected material;
- held-out replay evidence;
- robustness summary;
- particle influence field;
- adaptive proposals;
- independent verification for the selected rewrite;
- transaction/provenance receipt;
- rewritten Gaussian scene;
- before/after renders;
- a concise result certificate indexing every artifact.

Exit gate: a clean checkout can reproduce the controlled demo through one documented command.

### 0.90 — release hardening

Deliverables:

- warning/error cleanup in code touched by the 1.0 path;
- CLI help and validation consistency;
- reproducible benchmark scripts;
- versioned evidence schemas;
- documentation audit against actual implementation;
- performance report for the principal path;
- failure-case tests;
- installation/build instructions for macOS, Linux and Windows.

Exit gate: no README claim depends on an unimplemented core mechanism.

### 1.0 — stable verified-rewritable-reality baseline

Release only after:

- exact release commit passes the full OS matrix;
- post-tag/release smoke test passes;
- controlled benchmark artifacts are reproducible;
- measured benchmark status is clearly reported (completed if data exists, otherwise explicitly external-data-blocked rather than simulated away);
- limitations and deferred work are explicit.

## Explicitly deferred beyond 1.0

The following may be valuable research directions but are not allowed to block the core release:

- topology cutting/fracture/remeshing as a rewrite primitive;
- XR interaction;
- automatic semantic reconstruction;
- generalized differentiable MPM through FLIP blending, boundary clamps and arbitrary forcing;
- broad multiphysics Operator Influence claims;
- production-scale distributed reconstruction;
- automatic material segmentation claims from adjoints alone;
- publication/novelty claims unsupported by comparative experiments.

## Development rules until 1.0

1. One milestone branch at a time.
2. A milestone is not version-bumped until its exact candidate passes CI.
3. Feature proposals and verification remain separate code/evidence paths.
4. Synthetic robustness is labelled synthetic; measured evidence is labelled measured.
5. Numerical thresholds come from explicit baselines or physical requirements, not convenient values chosen after failures.
6. The finite-difference/nonlinear oracle remains available even after efficient adjoint paths exist.
7. Failed cases stay visible as evidence or tests when they reveal a real limitation.
8. New features outside this roadmap require replacing an existing 1.0 item rather than silently expanding scope.

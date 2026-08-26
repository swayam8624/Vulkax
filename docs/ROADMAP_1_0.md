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

### 0.50 — scalable Gaussian execution — implemented

Purpose: remove the largest remaining graphics-system gap while retaining the CPU numerical oracle.

Implemented deliverables:

- native Vulkan and Metal projection of Gaussian means/covariances and projected extents;
- native projected tile bounds plus deterministic CSR-style tile-reference evidence;
- stable far-to-near depth ordering of the returned projected stream;
- native Vulkan and Metal Gaussian raster/compositing paths;
- CPU-vs-native image regression metrics;
- public `vulkax_gaussian_scaling` timing/memory benchmark across increasing splat counts;
- explicit native/fallback evidence, with the public benchmark refusing to call a fallback run native;
- schema-aware CSV validator used by Linux and macOS CI.

Implementation boundary: final stable ordering and CSR tile-reference construction remain CPU-side. 0.50 does not claim GPU radix sorting or a fully GPU-resident tile/bin/sort/composite pipeline.

Exit gate: satisfied on the controlled 64/128/256-splat CI sweep. Native projection is required. The combined image gate is maximum channel delta `<= 3`, normalized RGBA RMSE `< 1e-4`, and changed-pixel fraction `< 1e-4`. Timing is published but speedup is not a correctness requirement. See `docs/GAUSSIAN_SCALING_0_50.md`.

### 0.60 — unified verified rewrite transaction — implemented

Purpose: turn isolated research commands into the central product/research operation.

Implemented transaction classes:

- local geometry translation/deformation metadata through `TranslateEntity`;
- local material coefficient rewrites through `SetMaterialParameter`;
- supported constraint/boundary-condition metadata rewrites through `SetConstraintParameter`.

Implemented transaction/evidence semantics:

- stable semantic target IDs and physical/appearance correspondence preconditions;
- expected-revision checking and duplicate transaction-ID rejection;
- copy-then-commit atomicity so a later invalid edit cannot leave a partial world mutation;
- provenance records and rollback receipts covering appearance positions, material maps, constraint maps, revision and provenance;
- affected/unaffected-region locality evidence;
- verification status derived from evidence rather than caller state;
- automatic rollback when required physical, oracle, locality or appearance-propagation evidence fails;
- a physical verifier interface used by geometry/material/constraint transactions according to their correspondence requirements;
- machine-readable transaction evidence/summary CSV output.

Concrete controlled physical adapter:

- `makeCapturedMaterialRewriteVerifier` binds a local `young_modulus` transaction to stable MPM-particle IDs;
- the requested rewrite magnitude must match the nonlinear verification perturbation exactly, preventing evidence reuse for a different material change;
- the verifier runs the retained finite-difference derivative reference, separate nonlinear counterfactual, controlled APIC reverse material adjoint, and adjoint-vs-reference comparison;
- the public `vulkax_captured_rewrite` command consumes the versioned captured bundle and emits transaction plus physical-oracle artifacts;
- the controlled CI path verifies an eight-particle `15000 Pa -> 15300 Pa` (+2%) rewrite with no unaffected-position drift and no rollback.

Implementation boundary: 0.60 establishes the generic geometry and constraint verifier/rollback semantics and controlled regression coverage, but only the captured **material** rewrite currently has a concrete solver-backed APIC/MPM verifier adapter. A captured solver-specific geometry or constraint verifier is not claimed. Topology surgery remains deferred beyond 1.0.

Exit gate: satisfied for the central transaction semantics and controlled material path. The feature head passed 42 tests on Linux, macOS and Windows; Linux additionally passed the public manifest-to-solver-to-verified-transaction end-to-end gate. See `docs/VERIFIED_REWRITE_0_60.md`.

### 0.70 — scale-safe identity and selection — implemented

Purpose: make appearance identity, selection and correspondence independent of transient Gaussian vector order.

Implemented deliverables:

- composite 32-bit namespace + 32-bit local `GaussianId` stored on each splat;
- transient `GaussianIndexView` for stable-ID to current-index resolution, with invalid/duplicate-ID rejection;
- deterministic source-order fallback IDs for legacy PLY files without Vulkax identity properties;
- explicit `vulkax_id_namespace` / `vulkax_id_local` PLY persistence for durable Vulkax identity;
- `WorldCorrespondenceGraph` appearance bindings migrated from vector indices to stable Gaussian IDs;
- transaction touched sets, position snapshots, rollback and unaffected-position drift migrated to stable IDs;
- durable named selection groups containing stable IDs only;
- ID-preserving filtering plus explicit `pruneMissingGaussians` for correspondence graphs after filtering;
- stable-ID AABB query results reusing the existing `GaussianHierarchy` rather than adding a second BVH;
- reorder/filter/serialization/rewrite regressions;
- public `vulkax_gaussian_identity_benchmark` plus schema validator.

Controlled scale evidence: Linux CI validates 4,096, 16,384 and 65,536 synthetic splats. At each size it requires identity lookup, selection membership, semantic correspondence and hierarchy-query membership to survive a complete storage reversal. The validator also checks nonempty bounded query/selection counts, exactly 8 bytes of explicit stable-ID payload per splat, increasing sample sizes/payload and finite non-negative timing fields. Timing is evidence-only; no performance threshold is used.

Implementation boundary: fallback IDs for legacy PLY are deterministic only relative to that file's source vertex order and are not a global uniqueness guarantee across unrelated legacy clouds. 0.70 does not add a distributed/UUID identity service or replace the existing semantic `EntityId` scheme.

Exit gate: satisfied on the controlled path. The functional candidate passed 43 tests on Linux, macOS and Windows, Linux passed the public 4K→65K identity benchmark validator, and existing captured-world/Vulkan/Metal gates remained green. See `docs/GAUSSIAN_IDENTITY_0_70.md`.

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
2. A milestone is not version-bumped until its exact behavior/evidence candidate passes CI; release-only metadata is then validated again on the exact release head.
3. Feature proposals and verification remain separate code/evidence paths.
4. Synthetic robustness is labelled synthetic; measured evidence is labelled measured.
5. Numerical thresholds come from explicit baselines or physical requirements, not convenient values chosen after failures.
6. The finite-difference/nonlinear oracle remains available even after efficient adjoint paths exist.
7. Failed cases stay visible as evidence or tests when they reveal a real limitation.
8. New features outside this roadmap require replacing an existing 1.0 item rather than silently expanding scope.

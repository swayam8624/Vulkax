# Vulkax 1.0 — stable verified-rewritable-reality baseline

Vulkax 1.0 freezes the research and product baseline established by the 0.39–0.90 milestones. It does **not** introduce a new physical model or reinterpret earlier evidence. The release packages the implemented captured-world data contract, stable identity/correspondence, APIC/MPM replay and calibration, robustness analysis, Operator Influence, adaptive proposals, evidence-derived transactions, native Gaussian rendering, measured DOT C2 evaluation, and the reproducible visual showcase into one stable baseline.

## Baseline contract

The 1.0 principal path is:

```text
versioned captured bundle
    -> stable appearance/physical identity
    -> fit-only material calibration
    -> held-out replay evidence
    -> observation-uncertainty stress analysis
    -> particle-local Operator Influence
    -> adaptive local rewrite proposal
    -> nonlinear + finite-difference/adjoint verification
    -> atomic commit or rollback with provenance
    -> Gaussian appearance output
    -> native research render
    -> presentation-only showcase
    -> machine-readable certificate
```

A completed run is not the same as an accepted rewrite. Controlled synthetic evidence contains a verified local material rewrite. The real DOT C2 measured-source benchmark produces a candidate rewrite that fails the independent derivative-oracle contract and is therefore rejected and rolled back. Both outcomes are part of the 1.0 contract.

## Evidence carried into 1.0

- Capture bundle: `vulkax_capture` schema v1 with SHA-256 payload identity, SI units, frame metadata, source classification and uncertainty sidecar.
- Captured-world certificate: `vulkax_captured_world_run` schema v2.
- Showcase manifest: `vulkax_showcase` schema v1.
- Showcase asset lock: `vulkax_showcase_assets` schema v1.
- Controlled Gaussian identity evidence through 65,536 synthetic splats.
- Native Vulkan/Metal projection and rendering with CPU-oracle image regression.
- Controlled APIC material adjoint checked against finite difference and nonlinear counterfactuals.
- Real DOT C2 measured-source benchmark with 225 stable correspondences, 675 observations and a held-out split.

See `schemas/evidence_registry.json` and `scripts/validate_evidence_registry.py` for the release-facing schema registry.

## Measured benchmark boundary

The DOT C2 benchmark remains exactly as documented in `docs/MEASURED_BENCHMARK_0_45.md`:

- measured trajectory geometry is real DOT data;
- the Vulkax physical rest/reference state, mass/rest-volume model, neutral Gaussian photometry and uncertainty scale are explicit proxies;
- selected `7500 Pa` / `nu = 0.45` values are model-conditioned effective parameters, not material measurements;
- held-out dynamic RMS is approximately `0.004417317099 m`;
- the selected measured local rewrite is rejected by the complete verifier and rolled back.

1.0 does not turn that rejection into a success claim. It treats refusal to commit insufficiently verified measured-data edits as a core system property.

## Graphics and showcase boundary

The one-command path can render through Vulkan or Metal and can generate deterministic `studio_pedestal` / `cloth_showcase` presentation outputs including hero, detail, contact-sheet and turntable views. Pinned CC0 presentation assets are hash validated.

The showcase is presentation-only. Props/environment imagery do not enter research evidence, and the stored Gaussian spherical-harmonic appearance is not claimed to be physically relit by the HDRI.

The scalable Gaussian implementation has native GPU projection and native raster/compositing. Final stable ordering and the current CSR tile-reference construction remain CPU-side; 1.0 does not claim a fully GPU-resident tile/bin/radix-sort/composite pipeline.

## Rewrite boundary

The central transaction layer supports typed geometry, material and supported constraint-metadata edits with atomicity, provenance and rollback. The concrete captured solver-backed verifier in 1.0 is the APIC/MPM local Young's-modulus path. Geometry and constraint edits have the generic verifier interface and controlled regression coverage, but no captured solver-specific geometry/constraint verifier is claimed.

## Release gates

The exact 1.0 release candidate must pass:

1. normal Linux, macOS and Windows CI/CTest;
2. evidence-schema registry validation and release-claim audit;
3. CLI failure-contract and principal-path benchmark generation on Linux, macOS and Windows;
4. controlled one-command Vulkan, Metal and Windows no-render runs;
5. canonical real DOT C2 measured benchmark;
6. real DOT C2 one-command Vulkan cloth showcase;
7. the dedicated 1.0 release smoke workflow.

After merge, the exact `main` commit must pass the same applicable gates. Tag `v1.0.0` is then required to trigger the tag smoke run; release closure is not claimed until that tagged smoke succeeds.

## Explicitly deferred beyond 1.0

- topology cutting, fracture and remeshing;
- XR interaction;
- automatic semantic reconstruction;
- generalized differentiable MPM through FLIP blending, boundary clamps and arbitrary forcing;
- broad multiphysics Operator Influence claims;
- production-scale distributed reconstruction;
- automatic material segmentation claims from adjoints alone;
- distributed/global UUID allocation for unrelated legacy Gaussian clouds;
- fully GPU-resident Gaussian tile/bin/radix-sort execution;
- validated shell/cloth constitutive material recovery from DOT C2;
- publication/novelty claims unsupported by comparative experiments.

The 1.0 release is therefore a stable, reproducible **verified-rewritable-reality research baseline**, not a claim that every future graphics, simulation or reconstruction problem is solved.

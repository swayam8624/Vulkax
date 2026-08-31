# Vulkax roadmap to 1.0

This roadmap freezes the 1.0 scope. Vulkax 1.0 is the stable baseline for a captured deformable object to traverse:

```text
captured appearance + measured observations
            |
            v
stable appearance <-> physical correspondence
            |
            v
fit-only material calibration + held-out replay
            |
            v
robustness + particle Operator Influence
            |
            v
adaptive local rewrite proposal
            |
            v
independent nonlinear / derivative verification
            |
            v
atomic commit or rollback + provenance
            |
            v
Gaussian output + native render + certificate
            |
            v
presentation-only visual showcase
```

The objective is a coherent, quantitatively defensible research system, not an unlimited simulation engine.

## Definition of done for 1.0

1. **Captured input** — versioned Gaussian/particle/observation/uncertainty contract with stable identity and provenance.
2. **Correspondence** — stable Gaussian and physical IDs survive reorder/filter/rewrite operations.
3. **Physical replay** — deterministic APIC/MPM captured replay with explicit numerical evidence.
4. **Material identification** — fit-only material selection with held-out validation reported separately.
5. **Robustness** — observation perturbation stress evidence without relabelling synthetic perturbations as measured noise.
6. **Operator Influence** — particle-local sensitivity and adaptive proposals.
7. **Independent verification** — finite-difference, nonlinear counterfactual and controlled adjoint comparison remain separate from proposal generation.
8. **Local rewrite transaction** — atomic evidence-derived commit/rollback with provenance.
9. **Appearance consistency** — verified physical state changes can propagate through appearance correspondence without identity drift.
10. **Rendering** — CPU oracle plus native Vulkan/Metal Gaussian paths.
11. **Measured evidence** — at least one real measured deformable trajectory traverses the benchmark with measured/proxy boundaries explicit.
12. **Presentation** — reproducible visual showcase remains separate from research evidence.
13. **Release quality** — Linux/macOS/Windows release gates, release-facing schema audit, controlled benchmark, measured benchmark, showcase and tag smoke.

## Milestone sequence

### 0.39 — observation robustness — implemented

Established deterministic observation-perturbation stress evidence, calibration drift metrics, particle-influence stability and adaptive-region overlap on the controlled captured fixture. The controlled perturbation distribution remains synthetic.

See `docs/OBSERVATION_ROBUSTNESS_0_39.md`.

### 0.40 — capture evidence contract and dataset validation — implemented

Established `vulkax_capture` schema v1, SHA-256 payload identity, SI units, coordinate-frame metadata, source classification, uncertainty sidecars and pre-simulation trajectory/data validation. Hash identity is not measurement-authenticity proof.

See `docs/CAPTURE_BUNDLE_0_40.md`.

### 0.45 — measured deformable benchmark — implemented

Integrated the public CC0 DOT C2 sequence as a reproducible real measured-source benchmark:

- 225 stable measured 3D correspondences;
- 675 observations, with 585 fit and 90 held-out validation rows;
- explicit measured / derived / model-proxy / literature-proxy / limitation provenance;
- fit-only 28-candidate material calibration;
- held-out replay error;
- measured-source perturbation stress analysis;
- finite-difference and APIC-adjoint material influence;
- adaptive local proposal;
- one +2% local material rewrite checked by nonlinear and independent derivative evidence;
- automatic rollback when the complete verifier contract is not met;
- permanent measured-evidence validator and CI artifact.

Recorded result:

```text
model-conditioned effective E        7500 Pa
model-conditioned nu                 0.45
fit dynamic RMS                      0.004390821778 m
held-out validation RMS              0.004417317099 m
adaptive regions                     8
adaptive particles                   182 / 225
retained absolute-gradient mass      0.9480125633
selected measured rewrite            rejected
rollback                             performed
```

DOT does not provide the stress-free state, loads, density, thickness, mass or material ground truth required to call those fitted values true cloth properties. The current zero-force volumetric APIC proxy is not claimed to be a validated cloth constitutive model.

See `docs/MEASURED_BENCHMARK_0_45.md`.

### 0.50 — scalable Gaussian execution — implemented

Established native Vulkan/Metal Gaussian projection and raster/compositing, deterministic projected ordering/tile evidence, CPU-vs-native image regression, and public timing/memory scaling evidence. Final stable ordering and current CSR tile construction remain CPU-side; no fully GPU-resident radix-sort/tile pipeline is claimed.

See `docs/GAUSSIAN_SCALING_0_50.md`.

### 0.60 — unified verified rewrite transaction — implemented

Established atomic copy-then-commit transactions, expected-revision and duplicate-ID guards, material/geometry/constraint-metadata edit types, provenance/rollback receipts, locality checks, and verification status derived from evidence. The concrete captured solver-backed verifier is the APIC/MPM local Young's-modulus path; captured solver-specific geometry/constraint verification is not claimed.

See `docs/VERIFIED_REWRITE_0_60.md`.

### 0.70 — scale-safe identity and selection — implemented

Established composite stable Gaussian IDs, transient ID→index views, explicit ID-bearing PLY persistence, reorder-safe correspondence/transactions/rollback, durable selections, ID-preserving filtering, explicit correspondence pruning and stable-ID hierarchy queries. Controlled structural evidence reaches 65,536 synthetic splats.

See `docs/GAUSSIAN_IDENTITY_0_70.md`.

### 0.80 — one-command end-to-end research demo — implemented

Established public `vulkax captured-world-run`, schema-v2 certificates, controlled Vulkan/Metal/Windows-no-render execution, real DOT C2 one-command execution, and deterministic presentation-only `studio_pedestal` / `cloth_showcase` outputs with pinned/hash-validated CC0 assets.

A completed run remains distinct from a verified rewrite. The real DOT rewrite is rejected and rolled back while the overall measured run remains complete.

See `docs/CAPTURED_WORLD_RUN_0_80.md`.

### 0.90 — release hardening — implemented

Established release-facing CLI failure regressions, evidence schema registry, documentation claim audit, cross-platform principal-path timing evidence, installation instructions, performance report, warning/error cleanup on the principal path, and release-gate wiring for controlled, measured and showcase workflows.

Required hardening artifacts include:

- `docs/INSTALL_0_90.md`;
- `docs/RELEASE_HARDENING_0_90.md`;
- `docs/PERFORMANCE_0_90.md`;
- `schemas/evidence_registry.json`;
- `scripts/benchmark_captured_world_run.py`;
- `scripts/test_release_cli_failures.py`;
- `scripts/validate_evidence_registry.py`.

### 1.0 — stable verified-rewritable-reality baseline — implemented

1.0 freezes the implemented research, measured-evidence, rendering, showcase and release-hardening contracts above. It does not add a new constitutive model or reinterpret the DOT result.

Release candidate gates:

- exact release SHA passes normal Linux/macOS/Windows CI;
- release claim audit and evidence registry validation pass;
- CLI failure contract and principal-path evidence generation pass on all three OSes;
- controlled one-command Vulkan/Metal/Windows-no-render gates pass;
- canonical DOT C2 measured benchmark passes;
- DOT C2 one-command Vulkan cloth showcase passes;
- dedicated 1.0 release smoke passes.

After merge, the exact `main` SHA must pass the applicable gates. The `v1.0.0` tag must then trigger and pass the tag smoke before release closure is declared complete.

See `docs/RELEASE_1_0.md`.

## Explicitly deferred beyond 1.0

- topology cutting, fracture and remeshing;
- XR interaction;
- automatic semantic reconstruction;
- generalized differentiable MPM through FLIP blending, boundary clamps and arbitrary forcing;
- broad multiphysics Operator Influence claims;
- production-scale distributed reconstruction;
- distributed/global UUID allocation for unrelated legacy Gaussian clouds;
- fully GPU-resident Gaussian tile/bin/radix-sort/composite execution;
- automatic material segmentation claims from adjoints alone;
- validated shell/cloth constitutive recovery from DOT C2;
- publication/novelty claims unsupported by comparative experiments.

## Development and release rules

1. Proposal and verification remain separate paths.
2. Synthetic evidence stays labelled synthetic; measured, derived and proxy quantities stay distinct.
3. Numerical thresholds are documented and tied to explicit baselines/requirements.
4. Finite-difference/nonlinear oracles remain available after efficient derivative paths exist.
5. Failed meaningful cases remain visible as evidence.
6. Release version changes are validated on their exact SHA.
7. A Git tag is not treated as successful until the tag-triggered smoke workflow passes.

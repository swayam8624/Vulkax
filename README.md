# Vulkax

**Verified Rewritable Reality — a graphics and computational-physics research system for turning captured scenes into persistent, physically executable world models.**

Vulkax keeps appearance, semantics and physical discretization separate, connects them through explicit stable correspondence, and treats verification evidence, provenance and rollback as part of the system rather than presentation metadata.

## Current implementation — Vulkax 1.0

**Vulkax 1.0 is the stable verified-rewritable-reality baseline.** The release freezes the implemented 0.39–0.90 research path without adding new physical claims. See [`docs/RELEASE_1_0.md`](docs/RELEASE_1_0.md) for the exact baseline, release gates, measured-data boundary and deferred work.

```text
captured appearance + observations
          |
          v
stable appearance <-> physical identity
          |
          v
fit-only material calibration
          |
          v
held-out replay + robustness evidence
          |
          v
particle Operator Influence
          |
          v
adaptive local proposal
          |
          v
nonlinear + derivative verification
          |
    +-----+-----+
    |           |
  commit      rollback
    |           |
    +-----+-----+
          v
appearance/provenance transaction
          |
          v
native render + certificate
          |
          v
presentation-only showcase
```

The project remains C++20.

## Baseline capabilities

- renderer-independent Gaussian appearance with ASCII/binary 3DGS PLY ingestion;
- persistent composite `GaussianId` values independent of vector order;
- stable selection, semantic correspondence and reorder-safe transaction rollback;
- versioned captured-deformable evidence bundles with SHA-256 payload identity and uncertainty sidecars;
- affine MLS appearance↔physics coupling;
- nonlinear APIC/MPM captured replay and fit-only material calibration;
- held-out validation and deterministic observation-perturbation stress evidence;
- finite-difference material-influence oracle plus controlled APIC reverse material derivatives;
- adaptive spatial rewrite proposals that remain separate from verification;
- atomic geometry/material/constraint-metadata transaction semantics with provenance and rollback;
- concrete solver-backed verification for the captured local Young's-modulus path;
- native Vulkan and Metal Gaussian projection/raster paths with CPU-oracle image regression;
- schema-versioned run certificates and release-facing evidence registry;
- a real measured DOT C2 benchmark with explicit measured/derived/proxy provenance;
- a deterministic visual showcase with pinned/hash-validated CC0 presentation assets.

## One-command captured-world research + showcase — 0.80

The principal public workflow is:

```bash
./build/vulkax captured-world-run \
  build/captured-example/capture.vkcap \
  build/captured-world-run \
  m4 0.003 1 1 1 \
  Metal 0.08 0.01 0.02 12345 \
  --showcase studio_pedestal \
  --showcase-assets build/demo-assets \
  --showcase-resolution 1280x720 \
  --turntable 12
```

Use `Vulkan` on a Vulkan-capable build or `none` when producing research evidence without a native render dependency.

A completed research run is **not** synonymous with a verified rewrite. `certificate.json` records `run_status` separately from the rewrite verdict. A rewrite may verify and commit, or it may be rejected and automatically rolled back while the overall research run remains complete.

The optional showcase provides deterministic hero/detail/contact-sheet/turntable outputs through the `studio_pedestal` and `cloth_showcase` presets. Presentation props and the pinned CC0 environment asset are excluded from research evidence. Stored Gaussian spherical-harmonic appearance is **not** claimed to be physically relit by the HDRI.

See [`docs/CAPTURED_WORLD_RUN_0_80.md`](docs/CAPTURED_WORLD_RUN_0_80.md).

## Measured deformable benchmark — 0.45

Vulkax includes a reproducible real measured-source benchmark using the public CC0 DOT C2 cloth sequence. The workflow pins the source archive by Dataverse identity and MD5, imports 225 stable measured 3D correspondences into SI units, and records whether every introduced quantity is measured, derived, a model proxy, a literature proxy, or unavailable.

Current measured-source result:

```text
stable measured correspondences       225
observations                           675
fit / held-out rows                    585 / 90
model-conditioned effective E          7500 Pa
model-conditioned nu                   0.45
fit dynamic RMS                        0.004390821778 m
held-out dynamic RMS                   0.004417317099 m
adaptive regions                       8
adaptive particles                     182 / 225
retained absolute-gradient mass        0.9480125633
selected measured rewrite              rejected
rollback                               performed
```

The selected measured +2% local stiffness rewrite passes its configured nonlinear linearization tolerance but fails the independent derivative-oracle contract, so Vulkax rejects and rolls it back. That is retained as evidence rather than rewritten as a success.

DOT does not provide a certified stress-free C2 state, loads, density, thickness, mass or material ground truth. The Vulkax physical reference/rest-volume quantities and fitted stiffness are therefore model-conditioned proxies, not recovered true cloth properties.

See [`docs/MEASURED_BENCHMARK_0_45.md`](docs/MEASURED_BENCHMARK_0_45.md).

## Stable Gaussian identity — 0.70

Each splat has a composite 32-bit namespace + 32-bit local ID. `GaussianIndexView` resolves IDs to current storage positions and rejects invalid or duplicate IDs. Vulkax-authored PLY persists IDs; legacy PLY receives deterministic source-order fallback IDs, which are **not** a global uniqueness guarantee across unrelated files.

The public structural benchmark validates stable identity, selection, correspondence and hierarchy membership through complete storage reversal at 4,096, 16,384 and 65,536 synthetic splats:

```bash
./build/vulkax_gaussian_identity_benchmark \
  build/gaussian-identity-scaling.csv \
  4096 65536 3 32
python3 scripts/validate_gaussian_identity.py build/gaussian-identity-scaling.csv
```

See [`docs/GAUSSIAN_IDENTITY_0_70.md`](docs/GAUSSIAN_IDENTITY_0_70.md).

## Verified rewrite transactions — 0.60

`verified` is derived from evidence rather than caller state. Transactions are structurally validated, applied atomically, and rolled back when required physical/oracle/locality/appearance evidence is absent or fails.

The concrete captured solver-backed adapter supports local `young_modulus` rewrites on stable MPM-particle regions and checks fresh finite-difference, nonlinear counterfactual and APIC-adjoint evidence. Geometry and supported constraint-metadata edits have the central verifier interface and controlled regression coverage, but 1.0 does not claim a captured solver-specific geometry or constraint verifier.

See [`docs/VERIFIED_REWRITE_0_60.md`](docs/VERIFIED_REWRITE_0_60.md).

## Scalable Gaussian execution — 0.50

Vulkax retains a CPU numerical oracle and native Vulkan/Metal Gaussian projection and raster/compositing paths. The public scaling evidence records image agreement, projection/tile memory and timing:

```bash
./build/vulkax_gaussian_scaling \
  examples/synthetic_gaussian.ply \
  build/gaussian-scaling.csv \
  Vulkan 64 256 3 16
python3 scripts/validate_gaussian_scaling.py build/gaussian-scaling.csv
```

Use `Metal` on macOS.

The implementation boundary is explicit: native projection and raster/compositing are GPU-backed, while final stable ordering and current CSR tile-reference construction remain CPU-side. 1.0 does not claim GPU radix sorting or a fully GPU-resident tile/bin/sort/composite pipeline.

See [`docs/GAUSSIAN_SCALING_0_50.md`](docs/GAUSSIAN_SCALING_0_50.md).

## Build and test

### macOS / Linux

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DVULKAX_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### Windows

```powershell
cmake -S . -B build -DVULKAX_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Detailed dependency and backend-probe instructions remain in [`docs/INSTALL_0_90.md`](docs/INSTALL_0_90.md); 1.0 does not change those platform requirements.

## Release validation

Release-facing contracts can be checked with:

```bash
python3 scripts/validate_evidence_registry.py .
python3 scripts/audit_release_claims.py . --expected-project-version 1.0.0
python3 scripts/test_release_cli_failures.py --executable build/vulkax
python3 scripts/benchmark_captured_world_run.py --executable build/vulkax --iterations 3 --backend none
```

The principal-path timing report is evidence-only; hosted runners are not treated as performance-comparable systems. See [`docs/PERFORMANCE_0_90.md`](docs/PERFORMANCE_0_90.md) and [`docs/RELEASE_HARDENING_0_90.md`](docs/RELEASE_HARDENING_0_90.md).

## Research-integrity status

Supported by current evidence:

- deterministic controlled captured replay/calibration/robustness/influence regressions;
- stable capture and Gaussian identity contracts;
- finite-difference/nonlinear verification retained alongside efficient derivative paths;
- atomic evidence-derived transactions and automatic rollback;
- native Vulkan/Metal Gaussian image regression;
- real measured DOT C2 trajectory ingestion, fit-only calibration, held-out replay, influence analysis and rejected/rolled-back rewrite evidence;
- one-command research/certificate/showcase orchestration.

Not established by 1.0:

- true material-property recovery for DOT C2;
- a certified stress-free DOT state, measured loads, density, thickness or mass;
- a calibrated per-C2 measurement-noise distribution;
- native 3DGS photometric reconstruction of DOT C2;
- validated shell/cloth constitutive physics for that benchmark;
- successful commitment of the current measured DOT rewrite;
- captured solver-specific geometry/constraint verification;
- globally allocated UUID identity across unrelated legacy clouds;
- production-scale/distributed reconstruction;
- fully GPU-resident Gaussian tile/bin/radix-sort execution;
- general differentiable MPM through FLIP blending, boundary clamps and arbitrary forcing;
- automatic material segmentation from adjoints;
- topology surgery;
- publication novelty or broad real-world claims.

## Release history and roadmap

The scoped milestone ledger is in [`docs/ROADMAP_1_0.md`](docs/ROADMAP_1_0.md). Detailed evidence documents remain versioned by the milestone that established each contract. 1.0 is a stability/release boundary over those implemented mechanisms, not a rewrite of their evidence.

## Development rules

- Proposal and verification remain separate paths.
- Synthetic evidence stays labelled synthetic.
- Measured-source evidence keeps measured, derived and proxy quantities distinct.
- Numerical gates are tied to documented baselines/requirements.
- Finite-difference/nonlinear oracles remain available after efficient derivative paths exist.
- Failed cases that expose genuine limitations remain visible rather than being suppressed.

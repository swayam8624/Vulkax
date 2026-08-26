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

## Current research implementation — 0.40 development head

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

./build/vulkax captured-deformable-validate-bundle \
  build/captured-example/capture.vkcap

./build/vulkax captured-material-calibrate \
  build/captured-example/object.ply \
  build/captured-example/particles.csv \
  build/captured-example/observations.csv \
  build/captured-calibration \
  1e-4 0.08
```

The generated controlled directory contains:

```text
capture.vkcap
object.ply
particles.csv
observations.csv
uncertainty.csv
truth.csv
```

`truth.csv` is synthetic regression truth and is deliberately outside the capture manifest. It is not a required measured-data payload.

Calibration writes:

```text
material_grid.csv
selected_samples.csv
selected_summary.csv
selected_evidence.csv
```

This deterministic example is a controlled regression, **not evidence that real-world material identification has been solved**. Real captured observations with measurement noise, imperfect correspondences and model discrepancy are still required.

### Versioned capture evidence contract

Vulkax 0.40 wraps the existing captured-deformable payloads in a versioned evidence contract instead of relying on undocumented assumptions in hand-authored CSV files.

`capture.vkcap` schema v1 records:

- relative paths for appearance, physical particles, observations and observation uncertainty;
- SHA-256 identity for each of those four payloads;
- explicit SI payload units (`m`, `kg`, `s`);
- coordinate-frame and axis-convention declarations;
- the solver timestep used to interpret observation timestamps;
- a `synthetic`, `measured` or `derived` source kind plus a human-readable provenance description.

`uncertainty.csv` contains exactly one `marker_id,time,sigma_x,sigma_y,sigma_z` row for every observation. The public validator checks payload hashes and syntax, stable marker-to-particle identity, solver-lattice timestamps, physical mass/volume validity, initialization coverage, nonzero-time fit and held-out validation coverage, observation/uncertainty matching, complete marker trajectories and consistent nonzero-time fit/validation roles.

The controlled generator emits the same schema intended for measured data and immediately round-trips it through the loader. Linux CI then validates it through the public command, deliberately mutates `observations.csv`, and requires the unchanged manifest to fail with a SHA-256 mismatch before the established calibration/influence/robustness pipeline proceeds.

A valid hash establishes **exact input identity**, not measurement truth or authenticity. Source metadata are declarations; 0.40 does not prove that a tracker was calibrated correctly, that an internally consistent marker correspondence is physically correct, or that the current material model is adequate for a real object. See [`docs/CAPTURE_BUNDLE_0_40.md`](docs/CAPTURE_BUNDLE_0_40.md).

### Captured material Operator Influence: oracle + adjoint + adaptive proposals

The captured deformable representation combines a nonlinear finite-difference material-influence oracle, a reverse-mode APIC/MPM derivative implementation, and an adjoint-guided spatial proposal layer:

- every MPM particle carries an optional Young's-modulus scale keyed by its stable particle ID; the default is exactly `1.0`, preserving the homogeneous solver path;
- constitutive forces and elastic-energy evidence use the same local stiffness coefficient field;
- the scalar observable is the displacement of a selected tracked marker from `t=0` to a selected observation time, projected onto a requested direction;
- the public CLI retains non-empty rest-space octants as the fixed regression partition;
- each reference region is perturbed independently with a central finite difference to estimate `dJ / d(scale)`;
- every reference region is then rerun at a **different** perturbation magnitude so first-order counterfactual predictions are checked against an independent nonlinear simulation rather than the derivative samples themselves;
- the discrete adjoint differentiates the actual controlled APIC path in reverse through P2G, grid mass normalization/update, APIC G2P, quadratic B-spline weight/gradient dependence, affine-velocity update, deformation-gradient evolution and Neo-Hookean Kirchhoff stress;
- the constitutive derivative with respect to each particle-local Young's-modulus scale is analytic;
- one reverse trajectory produces `dJ/ds_p` for every physical particle, preserving stable particle IDs in the exported field;
- a region adjoint derivative is the sum of its constituent particle gradients and is compared directly against the finite-difference oracle;
- the adjoint exports the minimum distance of the baseline trajectory from a quadratic B-spline stencil knot so the current piecewise-smooth derivative condition remains visible in the evidence.

The current reverse kernel deliberately matches the controlled captured benchmark: **pure APIC, zero external/gravity forcing and `boundaryCells == 0`**. Reverse rules for FLIP blending, boundary clamps and general forcing have not yet been claimed or validated.

#### Adjoint-guided adaptive material regions

The adaptive path removes the octant partition from *proposal selection* without removing it as a regression oracle:

1. rank stable-ID particles deterministically by `|dJ/ds_p|`;
2. take the smallest ranked set reaching a requested cumulative absolute-gradient fraction;
3. also retain particles above a requested fraction of the strongest particle gradient;
4. estimate characteristic rest-space spacing from the median nearest-neighbor particle distance;
5. connect selected particles inside `adjacency_multiplier * spacing` and form disjoint spatial components;
6. rank components by captured absolute-gradient mass and enforce an explicit region budget;
7. record both candidate and surviving proposal mass so truncation cannot be hidden;
8. re-aggregate the already-computed particle adjoint onto the proposed regions without another reverse trajectory;
9. **independently rerun every proposed region through the finite-difference derivative oracle and the separate nonlinear counterfactual perturbation.**

A proposal is therefore only a prioritization mechanism. It does not become verified merely because the adjoint selected it.

On the deterministic 64-particle captured regression with the default adaptive settings (`90%` cumulative absolute-gradient target, `5%` relative peak threshold, `1.05x` adjacency radius, maximum 8 regions), the proposal is sharply localized:

```text
physical particles                         64
proposed particles                         10  (15.625% of the body)
connected adaptive regions                  1
captured sum |dJ/ds_p| fraction       0.9118020453
characteristic rest spacing              0.12
adjacency radius                         0.126
adaptive adjoint/reference abs error     2.876101728e-14
adaptive adjoint/reference rel error     5.457032793e-10
adaptive nonlinear counterfactual error  6.286064567e-05
```

Thus, in this **controlled synthetic case only**, 10 of 64 particles capture about **91.18%** of the total absolute material-influence field and form one spatially connected candidate region. The nonlinear first-order prediction error is about **0.0063%**, while the adjoint derivative agrees with the independent finite-difference oracle to essentially numerical precision. These numbers are a deterministic regression result, not evidence that the same localization quality will hold on noisy real captures.

Reproduce the reference, adjoint and adaptive verification paths after generating the captured example:

```bash
./build/vulkax captured-material-influence \
  build/captured-example/object.ply \
  build/captured-example/particles.csv \
  build/captured-example/observations.csv \
  build/captured-influence \
  m4 0.003 1 1 1 \
  1e-4 15000 0.30 0.08 0.01 0.02
```

The final arguments above mean:

```text
dt = 1e-4
Young's modulus = 15000 Pa
Poisson ratio = 0.30
grid cell size = 0.08
central-difference stiffness-scale step = 0.01
independent verification stiffness-scale delta = 0.02
```

Optional arguments can override the adaptive cumulative-gradient target, relative particle-gradient threshold, adjacency multiplier and region budget.

The influence command writes:

```text
influence.csv
counterfactual.csv
adjoint_influence.csv
particle_adjoint.csv
derivative_comparison.csv
adaptive_proposal_summary.csv
adaptive_influence.csv
adaptive_counterfactual.csv
adaptive_adjoint_influence.csv
adaptive_derivative_comparison.csv
baseline_samples.csv
baseline_summary.csv
baseline_evidence.csv
```

`influence.csv` remains the nonlinear finite-difference octant oracle. `counterfactual.csv` records baseline, first-order prediction, independent rerun result, absolute error and relative linearization error. `particle_adjoint.csv` exposes the stable-ID particle-local reverse-mode field, `adjoint_influence.csv` aggregates that field onto the fixed reference regions, and `derivative_comparison.csv` records reference-versus-adjoint error. The `adaptive_*` files separately preserve proposal coverage, proposed-region derivatives, independent nonlinear reruns and adjoint/reference comparisons.

The finite-difference path remains the **verification oracle**, not an implementation detail to delete after the adjoint passes. The adjoint establishes an efficient derivative path and a useful region-prioritization signal for this controlled APIC regime; none of these controlled results establishes real-world material influence experimentally, general differentiable MPM, production-scale performance, automatic semantic segmentation, or publishable novelty by itself.

### Captured observation robustness

Vulkax 0.39 adds a deterministic synthetic uncertainty-stress path around material identification and the downstream particle influence field. Initial-pose observations (`t = 0`) and dynamic observations (`t > 0`) can be perturbed independently with reproducible component-wise RMS scales and stable seeds. Every scenario reruns calibration, the selected-material adjoint and adaptive proposal analysis; validation observations remain held out from material ranking.

The public command is:

```bash
./build/vulkax captured-observation-robustness \
  build/captured-example/object.ply \
  build/captured-example/particles.csv \
  build/captured-example/observations.csv \
  build/captured-robustness \
  m4 0.003 1 1 1 \
  1e-6 1e-6 12345 1e-4 0.08
```

The command writes `robustness.csv` plus `scenarios.csv`, keeps the physical grid fixed from the clean bundle, and evaluates five deterministic non-clean cases (`pose_half`, `pose_full`, `dynamic_half`, `dynamic_full`, `combined`) beside the clean baseline.

For the controlled 64-particle regression at a maximum **1 micrometre component-noise scale** for both initial and dynamic observations, the Linux public-path evidence is:

```text
baseline E                                  15000 Pa
baseline nu                                 0.30
maximum E relative drift                    0
maximum nu absolute drift                   0
minimum particle-influence cosine           0.9999999994
maximum particle-influence relative L2      0.0003332153535
strongest-particle stability                5 / 5 scenarios
minimum adaptive-particle Jaccard            1.0
```

So, for **this synthetic case and this perturbation only**, the discrete material choice stayed unchanged, the maximum particle-field change was about `0.0333%` in relative L2 norm, and both the strongest particle and adaptive selected-particle membership stayed stable. This is regression evidence, not a claim about real tracker/camera tolerance. The meaningful uncertainty scale for a measured object must come from that capture process.

CI runs the robustness command twice with identical inputs and requires byte-identical evidence files, exact clean-baseline identity, finite numeric outputs and a measurable downstream response to nonzero observation perturbation. See [`docs/OBSERVATION_ROBUSTNESS_0_39.md`](docs/OBSERVATION_ROBUSTNESS_0_39.md) for the evidence contract and limitations.

### Existing computational foundation

The repository also contains typed SI dimensions, `ProblemIR`, operator graphs, solver/fidelity planning, numerical reference solvers, result certificates, goal-oriented adaptivity, inverse/optimization foundations, generic discrete-adjoint foundations, global and local Operator Influence foundations, geometry/voxelization, scientific visualization, camera/capture, Vulkan/Metal probing and native compute conformance.

## Research-integrity status

Release tests are compiled with assertions active. Enabling them exposed two previously hidden failures: an incorrect goal-oriented refinement expectation and shared-edge degeneracy in mesh voxelization. Both were fixed rather than suppressed.

The native Gaussian renderer and covariance-coupling path must continue to pass the Linux/macOS/Windows matrix and real Metal/Vulkan smoke tests before stronger validation claims are made. CI renders the tracked Gaussian scene through Vulkan on Linux and Metal on macOS.

The captured-deformable path has controlled synthetic replay, material-identification, local-material influence and observation-robustness regressions, including held-out observations. The material-influence path retains its nonlinear finite-difference/rerun oracle, checks the APIC material-scale adjoint against it region by region, and verifies adjoint-proposed spatial material regions through fresh nonlinear reruns. The robustness path separately perturbs initialization and dynamic observations with deterministic noise and records calibration/influence/proposal stability while keeping the clean discretization fixed. The 0.40 capture contract now hashes the exact appearance/particle/observation/uncertainty inputs and rejects malformed trajectory/evidence bundles through a public validator before simulation. CI requires a nonzero particle-level field, localized adaptive proposals retaining at least 90% of the controlled absolute-gradient mass, tight adjoint/reference agreement, bounded nonlinear counterfactual error, deterministic robustness evidence, exact clean-baseline identity and capture-bundle corruption rejection.

This is still controlled synthetic verification: it does **not** establish real-world material recovery/influence, real sensor/tracker robustness, authenticated measurement provenance, physically correct correspondences merely because IDs are internally consistent, derivative correctness through unimplemented FLIP/boundary branches, robustness to model discrepancy, production-scale Gaussian rendering, topology rewriting, automatic semantic reconstruction, XR interaction, or publishable novelty.

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

The project is scope-frozen to the path in [`docs/ROADMAP_1_0.md`](docs/ROADMAP_1_0.md). The next core milestones are:

1. **0.39 observation robustness — implemented on the controlled synthetic path**, with deterministic pose/dynamic perturbations and calibration/influence/adaptive-region evidence.
2. **0.40 capture evidence contract and dataset validation — implemented**, with versioned manifest, SI/frame/time/provenance metadata, observation uncertainty, exact input hashes and pre-simulation bundle rejection.
3. **0.45 measured deformable benchmark — external measured data required**; the synthetic generator cannot satisfy this milestone.
4. **0.50 scalable Gaussian execution** — move projection/binning/order/compositing toward GPU scale while retaining the CPU image oracle and publishing scaling evidence.
5. **0.60 unified verified rewrite transaction** — connect verified material/geometry/supported-constraint changes to provenance, rollback, physical rerun and Gaussian appearance propagation.
6. **0.70 scale-safe identity and selection** — make stable correspondence survive large-cloud reorder/filter/serialization operations.
7. **0.80 one-command captured-world research demo** — emit the complete evidence bundle, verified rewrite and before/after renders from one command.
8. **0.90 release hardening**, followed by the exact **1.0** release gate.

Topology surgery, XR, automatic semantic reconstruction, generalized differentiable MPM and broad multiphysics Operator Influence claims are explicitly deferred beyond 1.0 unless they replace, rather than expand, an existing core milestone.

See [`docs/ROADMAP_1_0.md`](docs/ROADMAP_1_0.md), [`docs/CAPTURE_BUNDLE_0_40.md`](docs/CAPTURE_BUNDLE_0_40.md), [`docs/OBSERVATION_ROBUSTNESS_0_39.md`](docs/OBSERVATION_ROBUSTNESS_0_39.md), [`docs/REWRITABLE_REALITY.md`](docs/REWRITABLE_REALITY.md), [`docs/RESEARCH.md`](docs/RESEARCH.md), [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md), and [`docs/VISION.md`](docs/VISION.md).

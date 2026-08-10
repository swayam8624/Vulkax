# Roadmap

## Phase 0 — Contracts

- [x] freeze v1 separately
- [x] typed SI dimensions/quantities bootstrap
- [x] ProblemIR bootstrap
- [x] residual operator decomposition
- [x] structural validation
- [x] deterministic problem hash
- [x] verification certificate bootstrap
- [x] backend capability/selection policy
- [ ] canonical serialization/schema migration
- [ ] geometry, observations, contacts, couplings, constitutive models
- [ ] solver/discretization plan IR

Exit condition: a physical problem can be serialized, validated, hashed, and transformed without any
demo-specific mode.

## Phase 1 — Vulkan execution substrate

- runtime Vulkan loader/device discovery
- compute/graphics queue capability model
- resource/memory allocator with budget reporting
- shader/pipeline cache
- timestamps and deterministic telemetry
- headless compute path
- backend conformance kernels

Exit condition: generic operator kernels run on Vulkan without solver-specific renderer coupling.

## Phase 2 — Scientific visualization substrate

- field resource registry
- particle renderer with GPU culling/LOD and impostors
- scalar slices/contours/volume/iso-surfaces
- vector glyphs/streamlines/pathlines
- tensor lenses
- clipping, probing, legends, synchronized comparison
- GPU-generated geometry for level sets/iso-surfaces

Exit condition: solvers expose fields; visualization chooses lenses without solver branches.

## Phase 3 — Granular/DEM forcing vertical

- GPU broad phase/spatial hashing
- Hertz-Mindlin/contact model foundation
- friction/rolling/restitution
- rotating arbitrary geometry
- 100k -> 1M+ particle scaling study
- impact/contact statistics and parameter sweeps

Exit condition: a rotating-mill problem is specified through ProblemIR and solved without a BallMill mode.

## Phase 4 — Hyperelastic inverse vertical

- nonlinear FEM/MPM choice justified by evaluation
- constitutive model family interface
- nonlinear/linear solver stack
- differentiable/adjoint sensitivities
- calibration from experimental curves/full-field data
- uncertainty and sequential experiment design

## Phase 5 — Aerodynamics forcing vertical

- geometry/domain preprocessing
- incompressible flow family
- adaptive/sparse representation
- drag/lift observables
- convergence and conservation certificate
- parameter/design optimization

## Phase 6 — Operator Influence Fields

- discrete intervention semantics
- adjoint interface
- spatiotemporal influence accumulation
- interactive counterfactual prediction
- CFD/FEM/DEM evaluation suite
- paper-quality finite-difference/counterfactual validation

No phase is allowed to add a top-level demo mode to make progress look faster.

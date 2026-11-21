# Vulkax source reading order

> **Deep implementation tutorial:** [MPM/DEM solvers, inverse fitting and verified world rewrites](SIMULATION_TO_VERIFIED_WORLD_TUTORIAL.md) explains the active function call path, numerical assumptions, failure conditions and evidence checks. It complements the [linked source reading order](SOURCE_READING_ORDER.md) rather than claiming research completeness.

This guide is a **learning route through implemented components**, not a list of claims that every experiment succeeds. The [README](../README.md), [architecture](ARCHITECTURE.md), [research overview](RESEARCH.md), and [CMakeLists](../CMakeLists.txt) explain the repository's intended scope and compiled targets. Source lives under `include/vulkax/` (interfaces) and `src/` (implementations). Read header and matching implementation together.

## 1. Understand the simulation foundation

1. [core/math.hpp](../include/vulkax/core/math.hpp), [core/units.hpp](../include/vulkax/core/units.hpp), and [numerics/dense.hpp](../include/vulkax/numerics/dense.hpp) — numerical representation and units.
2. [problem/problem_ir.hpp](../include/vulkax/problem/problem_ir.hpp), [document.hpp](../include/vulkax/problem/document.hpp), [validation.hpp](../include/vulkax/problem/validation.hpp) — problem representation, inputs, and validation.
3. [geometry/mesh.hpp](../include/vulkax/geometry/mesh.hpp) and [voxelize.hpp](../include/vulkax/geometry/voxelize.hpp) — geometric discretization.
4. [backend/backend.hpp](../include/vulkax/backend/backend.hpp) and [backend/probe.hpp](../include/vulkax/backend/probe.hpp) — platform capabilities and optional compute backends.
5. [planning/solver_plan.hpp](../include/vulkax/planning/solver_plan.hpp), [operators/operator_graph.hpp](../include/vulkax/operators/operator_graph.hpp), and [execution/experiment.hpp](../include/vulkax/execution/experiment.hpp) — planned execution and operator orchestration.

## 2. Implemented physics and sensitivities

6. [solvers/diffusion.hpp](../include/vulkax/solvers/diffusion.hpp), [fem.hpp](../include/vulkax/solvers/fem.hpp), [hyperelastic_fem.hpp](../include/vulkax/solvers/hyperelastic_fem.hpp) — progress from simple numerical PDEs to deformable solids.
7. [solvers/mpm.hpp](../include/vulkax/solvers/mpm.hpp), [solvers/dem.hpp](../include/vulkax/solvers/dem.hpp) and [dem_broadphase.hpp](../include/vulkax/solvers/dem_broadphase.hpp) — particles, grid coupling and collision candidates.
8. [coupling/mpm_gaussian.hpp](../include/vulkax/coupling/mpm_gaussian.hpp), [coupling/mls_embedding.hpp](../include/vulkax/coupling/mls_embedding.hpp) — relation between simulation state and represented geometry.
9. [autodiff/discrete_adjoint.hpp](../include/vulkax/autodiff/discrete_adjoint.hpp), [mpm_adjoint.hpp](../include/vulkax/autodiff/mpm_adjoint.hpp), [inverse/hyperelastic.hpp](../include/vulkax/inverse/hyperelastic.hpp) — gradients, inverse problems, calibration and differentiability assumptions.

## 3. Render and observe a world

10. [render/camera.hpp](../include/vulkax/render/camera.hpp), [gaussian.hpp](../include/vulkax/render/gaussian.hpp), [gaussian_projection.hpp](../include/vulkax/render/gaussian_projection.hpp), [gaussian_tiles.hpp](../include/vulkax/render/gaussian_tiles.hpp) — visual projection and Gaussian representation.
11. [world/world_ir.hpp](../include/vulkax/world/world_ir.hpp), [world/video_observation.hpp](../include/vulkax/world/video_observation.hpp), [world/correspondence_graph.hpp](../include/vulkax/world/correspondence_graph.hpp) — represent a measured world and correspondence to observations.
12. [world/reality_loop.hpp](../include/vulkax/world/reality_loop.hpp), [world/transaction.hpp](../include/vulkax/world/transaction.hpp), [world/verified_rewrite.hpp](../include/vulkax/world/verified_rewrite.hpp) — proposed corrections and controlled revision boundaries.

## 4. Reproduce experiments and assess claims

13. [research/deformable_world.hpp](../include/vulkax/research/deformable_world.hpp), [captured_reality_loop.hpp](../include/vulkax/research/captured_reality_loop.hpp) and [captured_verified_rewrite.hpp](../include/vulkax/research/captured_verified_rewrite.hpp) — research-specific workflows beyond baseline simulation.
14. [experiment/design.hpp](../include/vulkax/experiment/design.hpp), [verify/convergence.hpp](../include/vulkax/verify/convergence.hpp), [verify/result_certificate.hpp](../include/vulkax/verify/result_certificate.hpp) — controls, numerical convergence and evidence commitments.
15. [Tests](../tests/), [scripts](../scripts/), [research](../research/), [paper](../paper/), [visualization](../visualization/) — distinguish tests and scripts from recorded scientific outcomes. Read [paper evidence reproduction](PAPER_EVIDENCE_REPRODUCTION.md) before interpreting paper charts.

## How layers interact

**Simulation:** problem IR → validation → solver plan → numerical discretization/solver → output state → measured verification.

**Observed reality:** capture/observation → correspondence graph → world representation → proposed rewrite transaction → replay/probe → verification certificate. A smaller residual is not automatically a physically correct rewrite.

**Rendering:** simulation/world geometry → Gaussian or mesh representation → camera projection/tiling → rendered observation. Visual quality and physical consistency are separate measurements.

**Research:** experiment design and fixed inputs → model/solver baseline → controlled perturbation → metrics and convergence → reproducible evidence. Keep exploratory and held-out evidence separate.

## Exercises

1. Read one solver header and implementation; locate its time integration and boundary conditions.
2. Trace a captured video input into world representation and note where calibration assumptions enter.
3. Compare an observed image residual against a physical constraint or held-out perturbation.
4. Identify all seeds, configuration files, datasets and revisions needed to replay one reported result.
5. Explain why an experiment certificate is not itself proof of broad generalization.

This guide is a curated first pass; it does **not** assert that every research hypothesis, graphics feature or GPU backend is complete.


## Expanded study trail — solver mathematics, image evidence and verified rewrites

**Numerical reference:** [Diffusion](../src/solvers/diffusion.cpp) derives an explicit timestep stability limit from grid spacing/diffusivity. [Linear FEM](../src/solvers/fem.cpp) assembles tetrahedral stiffness, while [Hyperelastic FEM](../src/solvers/hyperelastic_fem.cpp) handles nonlinear deformation and singular reference matrices. The [DEM broadphase](../src/solvers/dem_broadphase.cpp) generates conservative neighbor candidates; only the later DEM collision/response pipeline decides real contacts. Learn why stability, convergence and contact correctness are separate tests.

**World edits with reversal:** [WorldIR](../src/world/world_ir.cpp) defines stable entity, observation and parameter identities → [WorldCorrespondenceGraph](../src/world/correspondence_graph.cpp) connects physical and Gaussian identities → [Transaction](../src/world/transaction.cpp) validates edits and records prior state → [CapturedVerifiedRewrite](../src/research/captured_verified_rewrite.cpp) applies bounded material rewrites subject to evidence checks. A transaction's existence proves neither scientific benefit nor successful validation.

**Captured-data fit:** [CapturedMaterialCalibration](../src/research/captured_material_calibration.cpp) evaluates candidate material parameters against captured movement data. Distinguish calibration/training samples from genuinely held-out verification and report the measured error on both.

**Rendering and verification:** [Camera](../src/render/camera.cpp) supplies time-dependent views → [GaussianProjectedRaster](../src/render/gaussian_projected_raster.cpp) dispatches visible splats to explicitly built Metal or Vulkan backends → [ImageMetrics](../src/render/image_metrics.cpp) compares RGBA render outputs. [ResultCertificate](../src/verify/result_certificate.cpp) combines finite measured criteria and trust states. Better PSNR is not by itself a more physically accurate world; use solver/observation evidence too.

### Questions worth tracing

1. Why does explicit diffusion reject dt beyond a stability threshold?
2. How do FEM linear and hyperelastic constitutive models differ, and which material range makes their comparison meaningful?
3. How is a Gaussian stable ID mapped back to its physical world entity after a transaction?
4. What happens if a requested renderer backend was not included in the build?
5. Which verification criteria were measured on held-out captured observations versus reused calibration data?

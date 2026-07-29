# Vulkax Physics Studio Roadmap

## Product decision

Vulkax is now being developed as **Vulkax Physics Studio**: a native desktop editor that turns
symbolic equations and simulation graphs into reproducible GPU visualizations and cinematic
exports. Atlas, GeoBEACON, and the checked Connaught Place application remain in the repository as
preserved research artifacts. They are no longer the product headline or the dependency path for
the editor.

The studio's research question is deliberately narrower than a generic renderer claim:

> Can an equation-aware runtime allocate simulation resolution, visualization quality, and render
> work to meet measurable numerical, visual, and frame/export budgets?

This is inspired by the scientific discipline behind the imagery in *Interstellar*, not a claim to
replicate DNEG's DNGR production renderer. The eventual relativity preset will explicitly identify
which parts are real-time approximations and which are offline reference calculations.

## What exists today

| Area | Evidence | Status |
| --- | --- | --- |
| Vulkan/BEACON renderer | `LveEngine`, benchmark infrastructure, profiler, shaders | Preserved research foundation |
| GeoBEACON city demo | Connaught Place plus London, Tokyo, and Midtown data | Preserved legacy application |
| Atlas globe and navigation | `atlas_*` libraries and tools | Preserved legacy experiment |
| Equation core | `vulkax_equation`, parser, AST evaluation, deterministic preset catalog | Implemented |
| Typed physics IR | dimensions, scalar/vector fields, placements, validated operators, explicit solver-pass lowering, and executable scalar register programs | Implemented foundation |
| Portable compute lowering | one canonical scalar IR hash, CPU interpreter, constant folding/CSE, GLSL/SPIR-V and MSL emission, and Vulkan/Metal wave agreement | Implemented for scalar fields |
| Vulkan field compute | persistent editor-owned Wave Field and Gray-Scott executors, GPU timestamp query, CPU image-provider readback, and headless agreement | Implemented |
| Vulkan dynamic simulation | wave, two-field Gray-Scott, and two-pass N-body velocity-Verlet solvers with CPU/GPU agreement | Implemented |
| Simulation references | deterministic wave, Gray-Scott, and softened N-body velocity-Verlet graphs | Implemented foundation |
| Qt editor | `Vulkax Physics Studio.app`, parameterized dynamic graphs, live preview, project I/O, timeline, PNG/sequence export | Implemented |
| Physics benchmark | `vulkax-equations`, raw CSV and JSON output | Implemented |
| Relativity reference | adaptive 3D Schwarzschild CPU reference, CPU Kerr/Carter reference, fixed-step Vulkan agreement check, and 3D orbital-plane Metal viewport trace | Implemented foundation |
| Direct macOS GPU viewport | SwiftUI/Metal `CAMetalLayer` application, private RGBA16F radiance, HDR presentation, and temporal accumulation | Implemented |
| Direct Vulkan presentation | GLFW/MoltenVK compute-to-swapchain presenter: device-local RGBA16F storage image, Wave, Schwarzschild, and Kerr compute modes, progressive accumulation, graphics sampling, timestamp queries, finite-frame smoke mode, and export-only linear EXR capture | Implemented foundation |
| GPU volume visual | 64 x 96 x 64 staggered MAC velocity, density/temperature, obstacle, pressure/divergence, curl, RK2/MacCormack transport, two-level multigrid projection, GPU CFL substeps, and HDR ray marching | Implemented research foundation |
| Adaptive preview budget | EWMA timing/error controller connected to editor preview scale | Implemented foundation |

The equation benchmark is an analytical CPU reference. It does not claim GPU timing, numerical PDE
accuracy, or an interactive editor. Its purpose is to give every future GPU implementation a
deterministic input/output baseline.

All C++ test targets explicitly undefine `NDEBUG`, including in a Release build, so `assert`-based
numerical, parser, and contract checks execute in CI and local CTest runs.

Reaction-diffusion exposes diffusion, feed, and kill controls; N-body exposes central mass,
orbiter mass, and softening. Editing any dynamic parameter rebuilds the deterministic state at the
current timeline time. `physics_studio_dynamic_project_smoke` saves and reloads a modified
reaction-diffusion project to verify parameter and timeline reconstruction.

## Phases and acceptance gates

### Phase 0: Preserve and clean

- Keep the Atlas and GeoBEACON executables, test assets, papers, and CLI names unchanged.
- Present Atlas as `legacy/experimental` in product-facing documentation.
- Keep the city demo as a separate runnable entry point; do not route the editor through it.

Gate: existing CTest coverage remains green and the equation core has its own build target.

### Phase 1: Desktop editor shell

- Add Qt 6 Quick/QML desktop app for macOS and Windows.
- Provide a Vulkan viewport, project files, dockable equation/preset/parameter/timeline panels,
  keyboard shortcuts, and a profiler panel.
- Make the UI host `vulkax_equation`; no duplicated parser in QML or app code.

Gate: a user can open a preset, change a parameter, save/reopen a project, and see the viewport
update at 60 Hz.

Current evidence: `physics_studio_smoke`, `physics_studio_ui_smoke`, and
`physics_studio_sequence` CTest checks pass. The Qt chrome requests Vulkan RHI on an interactive
launch. Wave Field and Gray-Scott use a persistent compute-only Vulkan context with device,
pipelines, ping-pong buffers, command buffer, fence, and timestamp query retained for the editor
session; it reads the scalar fields into the existing Qt image provider. Gray-Scott retains the CPU
simulation only as a live solver-agreement reference. The compute context is not yet shared with
Qt Quick RHI, and scalar, particle, and lensing previews remain visibly labelled CPU/reference paths.

### Phase 2: Equation and graph compiler

- Extend scalar expressions into typed scalar/vector/field AST nodes with source spans.
- Add validation, dimensional metadata, parameter ranges, graph dependencies, and diagnostics.
- Emit both CPU reference evaluators and validated GLSL compute source from one canonical AST.

Gate: every shipped equation has CPU/GPU agreement within a declared tolerance and malformed input
produces a location-aware diagnostic instead of a shader crash.

Current evidence: the wave AST lowers into a backend-neutral scalar register program after typed
output-field validation. The same canonical IR hash executes through the CPU interpreter, emits
GLSL compiled by `glslangValidator` for Vulkan, and emits runtime-compiled MSL for native Metal.
The checked Apple M2 Pro runs report Vulkan `max_error < 1e-5` and Metal
`max_error = 1.16e-06` against the analytical reference. Coupled equations, stencil operators,
boundary lowering, and agreement across every shipped equation remain separate gates.

### Phase 3: GPU simulation runtime

- Implement GPU fields, ping-pong storage images/buffers, timestep scheduling, boundary conditions,
  deterministic seeds, and GPU timestamp zones.
- Start with wave propagation, reaction-diffusion, particle gravity, and vector-field advection.
- Add reference solvers and convergence tests before prioritizing visual effects.

Gate: convergence/error reports exist for each solver, no in-flight resource destruction occurs,
and output is reproducible from a saved project and seed.

Current evidence: deterministic CPU wave and Gray-Scott graph references and generated compute
kernel contracts are implemented and tested. The wave graph has a 64x64 Vulkan ping-pong
dispatch/readback comparison with zero measured error for the checked 32-step run. The two-field
Gray-Scott graph has a separate 64x64 Vulkan dispatch/readback comparison for 64 steps with
`max_error = 1.19e-07` and a `6.73471 ms` summed Vulkan timestamp on Apple M2 Pro. Both results
are headless correctness checks. Wave Field and Gray-Scott now have persistent editor GPU paths;
N-body editor ownership remains runtime work.

The editor also ships a deterministic N-body orbit preset using a softened, center-of-mass-frame
velocity-Verlet reference. Its 4,800-step unit test bounds energy drift below two percent and its
24-frame PNG export is verified to contain changing frames. GPU particle integration and rendering
have a checked two-pass Vulkan implementation with `max_error = 5.45e-06` after 128 steps on
Apple M2 Pro. Persistent editor particle buffers and instanced rendering remain separate work.

### Phase 4: Cinematic visualization and export

- Add transfer functions, volume/surface/particle/vector visualization materials, HDR tone mapping,
  camera paths, timelines, image sequences, and EXR/PNG export.
- Separate real-time preview quality from offline export quality and record the configuration in the
  export manifest.

Gate: a project produces deterministic frame sequences and a self-contained manifest that can be
used to regenerate them.

Current evidence: PNG sequence export and `sequence_manifest.json` are implemented and exercised
headlessly. The macOS bundle also exports the current preview as a linear half-float OpenEXR file
through the native OpenEXR runtime; the CTest check validates its OpenEXR header. The source is a
linearized display preview, not an HDR simulation buffer. HDR accumulation, camera paths, and
director tooling remain out of this first export implementation.

The current paper-style implementation report is
[`docs/paper/vulkax_physics_studio.pdf`](paper/vulkax_physics_studio.pdf). It records these
boundaries explicitly and is regenerated from `vulkax_physics_studio.tex`.

### Phase 5: Relativity flagship

- Build a documented Schwarzschild lensing preset first, with a ray-integration reference mode.
- Add Kerr transport only with explicit horizon, symmetry, spin-asymmetry, and convergence tests.
- Treat film-grade ray-bundle rendering as an offline research path, never as an unqualified
  real-time claim.

Gate: camera trajectories, units, numerical integrator, precision, and reference comparisons are
all exposed in the project and report.

Current evidence: the native Metal viewport has an interactive Schwarzschild visual with an
RK4 null-ray approximation, progressive HDR accumulation, explicit capture, and a Doppler-style
thin-disk heuristic. The C++ reference module also provides an adaptive 3D Schwarzschild
geodesic integrator and a Vulkan fixed-step compute agreement check. A separate C++ Kerr reference
uses the Carter-separated first-order equations and verifies the horizon, Schwarzschild-limit
symmetry, central capture, spin-induced prograde/retrograde asymmetry, and step refinement. Its
five-ray central-difference reference computes a source-space Jacobian, singular values,
magnification, shear, principal orientation, and caustic-risk classification, with differential,
integrator-refinement, and Schwarzschild-symmetry checks. The direct Vulkan Kerr mode uses the same
conserved-quantity formulation, progressive jittered HDR, and symmetric `+/-x`, `+/-y`
differential rays to derive an anisotropic source footprint. This remains a finite-difference
bundle rather than full Jacobi/geodesic-deviation propagation, spectral transfer, or a film-grade renderer.

### Phase 5B: Buoyant Smoke Example

- Solve velocity advection, pressure projection, density/temperature transport, buoyancy, and
  vorticity confinement on a deterministic grid.
- Render the resulting density and temperature fields without claiming a 3D volume solution.

Gate: deterministic replay, finite state, bounded divergence, visible density transport, and a
native export sequence.

Current evidence: the buoyant-smoke numerical suite verifies deterministic replay, finite
density/temperature, bounded divergence, and plume rise. The editor preset renders the same 2D
incompressible reference and has a checked varying PNG sequence export. The direct Metal target
executes an interactive 3D staggered MAC grid: scalar fields are cell-centred, velocity components
use separate face textures, velocity backtracing is RK2, density and temperature use limited
MacCormack correction, and the two-level pressure V-cycle respects solid obstacles. It also stores
curl and pre/post-projection divergence before HDR volume ray marching. A GPU maximum-velocity
reduction computes the Courant number and selects one or two fixed-upper-bound command-stream
substeps, avoiding an interactive CPU synchronization point. The stress verification measured
CFL 0.779541, selected two 0.025-second substeps, and reduced divergence L2 from 0.688996 to
0.008427. Its low-CFL counterpart measured 0.086629, selected one 0.016667-second substep, and
reduced divergence from 0.243419 to 0.004010. Both cases retain the multigrid-versus-40-Jacobi
ablation, verify finite scalar/curl/radiance ranges, and reproduce identical half-float output
signatures in independent dispatch sequences. Combustion chemistry, deeper adaptive multigrid, sparse bricks, and
film-production validation remain outside this implementation.

### Phase 6: Adaptive research controller

- Apply BEACON-style budgeting to simulation resolution, mesh/volume resolution, sampling density,
  temporal accumulation, and export time.
- Measure numerical error separately from visual error and controller predictions.
- Compare fixed-quality, screen-space-only, numerical-only, and full equation-aware policies.

Gate: raw frame-level results regenerate every figure, negative cases are reported, and every
quality claim identifies its metric and baseline.

Current evidence: \`vulkax-quality-benchmark\` evaluates the canonical quantum-wavepacket AST at a
fixed exact grid and a controller-selected grid, writing \`quality_frames.csv\` and
\`quality_summary.json\`. On the checked Apple M2 Pro run at a 1 ms target, adaptive preview changed
quality 13 times and reduced p50 analytical sampling time from \`9.55 ms\` to \`1.19 ms\`, while the
measured resampling MSE rose to \`0.00162\`. This is a CPU analytical-preview result, not a GPU,
perceptual, or rendered-image quality claim.

The native editor now computes the same type of fixed-grid sampling MSE for analytical presets on
each preview frame and feeds it into the EWMA controller. PDE, particle, and lensing paths report
zero only as an explicit unmeasured state; their zero must not be interpreted as image agreement.

## First executable workflow

```bash
cmake -S . -B build-vulkax -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-vulkax --target vulkax_equations EquationCoreTests -j 8
ctest --test-dir build-vulkax -R equation_core --output-on-failure
build-vulkax/vulkax-equations --frames 180 --samples 512 \
  --output docs/results/physics_studio_current
```

The result directory contains `preset_runs.csv` and `summary.json`. The runner labels the data as
`cpu_analytic_reference`; it must never be mixed with Vulkan GPU timing results.

## Architecture boundary

```text
Equation text/presets -> AST + diagnostics -> CPU reference evaluator
                                      |                 |
                                      v                 v
                                GLSL compiler -> GPU simulation fields
                                                        |
                                                        v
Qt/QML editor -> project/timeline -> Vulkan visualization -> realtime preview/export
                                                        |
                                                        v
                                         research metrics + reproducible manifests
```

## Deferred scope

No feature is silently implied by this roadmap. The following are explicitly outside the first
editor release: Atlas expansion, routing, Android, game gameplay, proprietary map imagery,
neural simulation, strict general-relativistic film parity, distributed rendering, and cloud
accounts. They may become separate projects only after the editor's numerical and rendering
foundations are verified.

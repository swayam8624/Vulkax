# Vulkax Physics Studio

Vulkax is transitioning into a native desktop physics-visualization editor: equations and
simulation graphs become reproducible GPU visualizations, real-time previews, and cinematic
exports. The active product direction is **Vulkax Physics Studio**, not a globe/navigation
application.

This repository deliberately keeps the earlier Vulkan research work intact:

- **BEACON** is the clustered-lighting research renderer and benchmark substrate.
- **GeoBEACON** is the OpenStreetMap-based Connaught Place city/digital-twin experiment.
- **Atlas** is a preserved experimental globe/navigation stack.

They remain runnable regression baselines and source material for the editor's future adaptive
research controller. They are not presented as unfinished requirements for Physics Studio.

The complete, honest phase plan is [Vulkax Physics Studio Roadmap](docs/VULKAX_PHYSICS_STUDIO_ROADMAP.md).

## Run Physics Studio

On macOS, the product editor is the native SwiftUI application with a direct `MTKView`; it is not
a local web server and it does not route interactive frames through `QImage`. Select a scalar
preset or edit the equation directly, press **Compile**, and every symbol other than `x`, `y`, `z`,
and `t` becomes a live parameter. The shared equation core exposes conservative range/singularity
analysis. Native compilation creates or restores a device-keyed Metal compute pipeline
asynchronously while the previous valid pipeline remains active. Project open/save, parameter changes, playback,
reset, and timeline scrubbing all feed that GPU viewport directly.

Preset changes are revision-gated: a newly extracted parameter block is never submitted to an older
asynchronously compiled pipeline. The scene sidebar and top toolbar both expose **Add 3D Object**, and
OBJ files can be dropped directly onto the viewport. Saving a project copies the active obstacle into
a sibling `.assets` directory and stores a relative path, so the `.vxp` can be moved with its assets.

```bash
scripts/vulkax_macos.sh physics
```

Interactive scalar, Schwarzschild, and volume frames remain in private floating-point GPU textures
until presentation. The renderer uses three frames in flight, command-buffer completion telemetry,
resize-safe drawable resources, and no per-frame fence wait or mapped readback. Vulkan and Metal
consume the same versioned `VulkaxFrameRequest` ABI and report the same capabilities/telemetry
structures. This verifies a custom equation plus `.vxp` project round trip on the GPU:

```bash
scripts/vulkax_physics_metal.sh --native-dynamic-equation-project-gpu-smoke
```

The Qt application remains available as the cross-platform compatibility, deterministic export,
and CI surface. Its image provider is not the canonical interactive renderer:

```bash
scripts/vulkax_macos.sh physics-qt
```

The portable direct-presentation reference is separate from the Qt editor and uses the Vulkan
swapchain without a Qt surface or CPU image bridge:

```bash
scripts/vulkax_macos.sh physics-vulkan
scripts/vulkax_macos.sh physics-vulkan --smoke
scripts/vulkax_macos.sh physics-vulkan --black-hole
scripts/vulkax_macos.sh physics-vulkan --black-hole --smoke
scripts/vulkax_macos.sh physics-vulkan --black-hole --smoke \
  --output build/direct-vulkan-captures/schwarzschild.exr
scripts/vulkax_macos.sh physics-vulkan --kerr --spin 0.8
scripts/vulkax_macos.sh physics-vulkan --kerr --spin 0.8 --smoke \
  --output build/direct-vulkan-captures/kerr.exr
scripts/vulkax_macos.sh physics-vulkan --volume
```

The interactive command dispatches a compute shader into a device-local `RGBA16F` image, samples
that image in the graphics pass, tone maps it, and presents the result to the Vulkan drawable.
`--smoke` hides the window and presents three frames for deterministic macOS/MoltenVK validation.
When timestamp queries are supported, it also prints measured Vulkan compute and compute-to-render
GPU durations for the final presented frame.

`--volume` now advances the same persistent Vulkan staggered-MAC resources used by the numerical
validation runner. Each presented frame executes GPU CFL selection, buoyancy, curl/vorticity,
two-level pressure correction, obstacle-aware projection, RK2/MacCormack transport, density
hierarchy construction, self-shadowed volume marching, and a direct radiance-buffer-to-HDR-image
pass. The swapchain path no longer renders the separate procedural density field.

`--black-hole` selects the direct Vulkan Schwarzschild mode. It uses an orbital-plane RK4 trace,
capture/escape classification, equatorial disk crossings, and the same direct HDR compute-to-present
path. Its bounded 512-pixel-wide compute extent keeps this research preview interactive.
`--kerr` selects the Carter-separated Kerr null-geodesic mode. `--spin` accepts a dimensionless
spin strictly between -1 and 1. A persistent GPU queue adaptively integrates central geodesics and
curvature-driven Jacobi bundles, compacts active rays with indirect count/scan/scatter dispatches,
and writes a source-footprint field consumed by the direct HDR Kerr shader. This replaces the four
finite-difference neighbor traces while preserving root-refined disk intersections, six-band
redshifted thermal transfer, opacity, corona scattering, and progressive accumulation. Interactive
sessions use a 256-pixel-wide preview; deterministic and offline runs retain the 384-pixel profile.
`--output` performs an export-only transfer after the final frame and writes the untouched linear
half-float radiance to OpenEXR. The exporter rejects non-finite frames or captures lacking both
shadow and luminous radiance; interactive frames still perform no readback.

Build the compatibility target directly with:

```bash
cmake -S . -B build-vulkax -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-vulkax --target physics_studio_app -j 8
open "build-vulkax/Vulkax Physics Studio.app"
```

The editor's headless sequence job is useful for reproducible exports:

```bash
QT_QPA_PLATFORM=offscreen \
"build-vulkax/Vulkax Physics Studio.app/Contents/MacOS/Vulkax Physics Studio" \
  --export-sequence docs/results/my_wave_sequence --frames 120
```

The native macOS viewport and its deterministic GPU checks can also be run directly:

```bash
scripts/vulkax_physics_metal.sh
scripts/vulkax_physics_metal.sh --black-hole-smoke
scripts/vulkax_physics_metal.sh --volume-smoke
scripts/vulkax_physics_metal.sh --native-gpu-smoke
scripts/vulkax_physics_metal.sh --native-black-hole-gpu-smoke
scripts/vulkax_physics_metal.sh --native-volume-gpu-smoke
scripts/vulkax_physics_metal.sh --native-imported-mesh-gpu-smoke \
  tests/fixtures/airflow_cube.obj
scripts/vulkax_physics_metal.sh --native-dynamic-equation-project-gpu-smoke
```

This target presents through `CAMetalLayer`. Scalar equations, the interactive Schwarzschild
visual, and the 3D volume smoke visual execute in Metal compute with private GPU textures.

The three `--native-*-gpu-smoke` commands are deterministic verification modes. They dispatch
the actual wave, progressive Schwarzschild, and volume kernels. The volume test uses separate
`u`, `v`, and `w` face textures, RK2 velocity backtracing, obstacle-aware divergence, a two-level
pressure V-cycle, face projection, MacCormack density/temperature transport, curl generation, and
HDR ray marching. A GPU maximum-speed reduction selects one or two bounded CFL substeps without
interactive CPU readback; both branches are verified. On the checked Apple M2 Pro run, the stress
case measured `CFL=0.779541`, selected two `0.025 s` substeps, and reduced divergence L2 from
`0.688996` to `0.008427`. The low-CFL case measured `0.086629`, selected one `0.016667 s` substep,
and reduced divergence from `0.243419` to `0.004010`. The GPU test also runs a 40-iteration
fine-grid Jacobi ablation, verifies multigrid has the lower residual, and reproduces each half-float
radiance signature in a second independent dispatch. Only these tests use shared textures for a minimal result readback;
interactive frames remain GPU-resident until presentation.

In Volume Smoke mode, **Import obstacle** accepts a closed OBJ mesh. The editor normalizes and
uploads its triangles, voxelizes the moving mesh into the obstacle texture on Metal, samples the
pressure field on its triangles, and advances its rigid-body state on the GPU. The inspector exposes
position, Euler rotation, nonuniform scale, mass, linear/angular velocity, and diagonal inertia.
Metal and Vulkan transform vertices with the same quaternion convention, integrate force and torque,
and impose `linear velocity + angular velocity cross radius` on fluid faces touching the moving mesh.
Static floor cells remain zero-velocity boundaries. Version 6 project files persist the complete body
configuration and package the OBJ beside the project. Versions 1 through 5 still load with default
body values. Before upload, the importer rejects invalid indices, degenerate triangles, open boundaries,
non-manifold edges, and inconsistent winding with explicit defect counts. The checked cube test reports
12 triangles, 2,598 occupied cells, nonzero GPU-computed
translation, and normalized rotational integration. The portable Vulkan MAC test provides the
corresponding OBJ, voxelization, moving-boundary, force/torque, and body-advance path.

## Equation Reference Runner

The shared `vulkax_equation` core provides the parser, canonical AST evaluator, deterministic
preset catalog, and GLSL compute-shader contract emitter. It ships scalar wave, gravity-potential,
quantum wavepacket, electromagnetic-pulse, reaction-diffusion seed, and N-body orbit fields.

```bash
cmake -S . -B build-vulkax -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-vulkax --target vulkax_equations EquationCoreTests -j 8
ctest --test-dir build-vulkax -R equation_core --output-on-failure
build-vulkax/vulkax-equations --frames 180 --samples 512 \
  --output docs/results/physics_studio_current
```

The runner writes raw `preset_runs.csv` and `summary.json`, explicitly labelled
`cpu_analytic_reference`. These are the correctness inputs for the upcoming GPU compiler and
simulation runtime; they are not performance claims.

The paper-style implementation report and compiled PDF are available at
[vulkax_physics_studio.tex](docs/paper/vulkax_physics_studio.tex) and
[vulkax_physics_studio.pdf](docs/paper/vulkax_physics_studio.pdf).

CTest executables explicitly retain C++ assertions even when the surrounding build is Release, so
their numerical and contract checks are not compiled out by `NDEBUG`.

## Legacy research applications

The working Connaught Place app remains the default legacy demo:

```bash
scripts/vulkax_macos.sh doctor
scripts/vulkax_macos.sh test
scripts/vulkax_macos.sh app
```

It provides offline city search and routing for the checked OSM dataset. Central London, Tokyo,
and Midtown Manhattan are still selected with `scripts/vulkax_macos.sh london`, `tokyo`, and
`nyc`. Atlas's non-default globe experiment is available through
`scripts/vulkax_macos.sh atlas`.

Existing BEACON/GeoBEACON CLI identifiers, datasets, papers, and CTest coverage are retained.
See [the legacy operating guide](docs/RUNNING_VULKAX.md) for those commands and
[the Atlas architecture](docs/ATLAS_ARCHITECTURE.md) for the preserved globe stack.

## Current status

| Component | State |
| --- | --- |
| Equation parsing, conservative interval/singularity analysis, AST evaluation, built-in presets, raw analytical results | Implemented |
| Qt 6 macOS editor, parameterized dynamic graphs, project I/O, timeline, PNG/sequence export | Implemented |
| Executable scalar Physics IR with CPU interpreter, common-subexpression elimination, constant folding, GLSL/MSL emission, and Vulkan/Metal wave agreement | Implemented foundation |
| Scalar evolution/stencil IR with Laplacian and directional gradients, explicit time stepping, open/periodic/fixed boundaries, CPU oracle, generated SPIR-V/MSL, and Vulkan agreement | Implemented foundation |
| Coupled scalar evolution IR with cross-field stencils, simultaneous old/new state semantics, per-field boundaries, generated SPIR-V/MSL, and CPU/Vulkan Gray-Scott agreement | Implemented foundation |
| Persistent editor Wave Field and Gray-Scott Vulkan compute, plus headless wave/N-body CPU/GPU agreement | Implemented |
| Linear OpenEXR preview export | Implemented |
| Typed physics IR, explicit equation-defined pass graphs, reflected resource layouts, automatic Vulkan layout/pool materialization, persistent pipeline artifacts, and executable scalar-field programs | Implemented foundation |
| Direct macOS Metal viewport with GPU-resident HDR radiance and presentation | Implemented |
| Direct Vulkan/MoltenVK compute-to-swapchain field presenter with no CPU image bridge | Implemented and smoke-tested on Apple M2 Pro |
| Direct Vulkan/MoltenVK Schwarzschild GPU preview with timestamped compute-to-present path | Implemented foundation and smoke-tested on Apple M2 Pro |
| Direct Vulkan Schwarzschild linear HDR OpenEXR capture with content validation | Implemented and tested |
| CPU Kerr/Carter reference with null-constraint telemetry, curvature-driven Jacobi/geodesic-deviation transport, root-refined multi-intersection events, finite-thickness disk/corona transfer, adaptive central Vulkan ray, progressive HDR, and measured EXR quality | Implemented research foundation and smoke-tested on Apple M2 Pro |
| GPU active-ray integrate/scan/scatter compaction with indirect next-dispatch generation | Implemented and Vulkan-tested on Apple M2 Pro |
| Wave-field direct Vulkan HDR OpenEXR export | Implemented |
| Schwarzschild CPU reference, Vulkan fixed-step compute check, and interactive Metal 3D orbital-plane visual | Implemented foundation |
| Schwarzschild thin-disk lensing and 2D buoyant-smoke equation suites | Implemented and tested |
| Interactive GPU 3D MAC smoke with staggered face velocity, RK2/MacCormack transport, obstacle-aware two-level multigrid projection, curl, temperature, and volume ray marching | Implemented research foundation |
| Portable Vulkan MAC projection, GPU CFL, solid obstacles, curl/vorticity, two-level pressure correction, RK2/MacCormack transport, density hierarchy, self-shadowed HDR volume march, timestamps, and persisted pipeline cache | Implemented foundation |
| Imported-mesh airflow coupling with persisted transforms, quaternion rotation, Metal/Vulkan voxelization, GPU pressure force/torque, angular integration, moving boundary velocities, editor controls, and reflected graph passes | Implemented single-body research path |
| Linear-HDR EXR MSE, PSNR, global SSIM, maximum error, temporal difference, 1/4/16-sample convergence, and golden-image gates | Implemented |
| Adaptive preview quality controller with live analytical MSE and raw benchmark | Implemented foundation |

The Interstellar reference is an inspiration for visual rigor, not a claim of parity with DNEG's
DNGR production renderer. Vulkax will state whether a visualization is analytical, numerically
validated, real-time approximate, or offline reference.

## Equation Examples

Run the two checked example suites after building:

```sh
ctest --test-dir build -L black_hole_example --output-on-failure
ctest --test-dir build -L buoyant_smoke_example --output-on-failure
ctest --test-dir build -R 'vulkan_generated_coupled_stencil_compute|vulkan_mac_projection_compute' --output-on-failure
```

The checked Schwarzschild thin-disk suite renders a black shadow from captured Schwarzschild null
rays, maps escaped rays using the RK4 deflection reference, and samples an inclined emissive
accretion annulus with a documented Doppler-brightness heuristic. Separately, the direct Vulkan
Kerr mode integrates Carter-separated rays and differential neighbours. The CPU event solver uses
root-refined horizon/equatorial crossings and front-to-back finite-thickness transfer; the GPU
central ray uses adaptive full-step/two-half-step control while differential rays use a cheaper
conserved-potential schedule. Separately, the C++ reference transports two screen-basis Jacobi
fields with the Kerr Riemann tensor and tests its curvature oracle. The interactive filtering
footprint remains the faster five-ray finite-difference estimator; Vulkax does not claim
DNGR-equivalent production convergence.

The checked buoyant-smoke suite solves a deterministic 2D stable-fluid chain: semi-Lagrangian
advection, pressure projection for near-zero divergence, temperature/density buoyancy, and
vorticity confinement. Separately, the native Metal target contains an interactive 3D staggered
MAC-grid smoke foundation with private face-velocity, density, temperature, pressure, divergence,
obstacle, and curl fields plus HDR ray marching. It is not a combustion or film-production solver.
The portable Vulkan test validates staggered face indexing, GPU maximum-speed/CFL selection,
solid boundaries, curl/vorticity, a fine/coarse/fine pressure sequence, midpoint-RK2 backtracing,
bounded MacCormack transport, and a max-density hierarchy. Its volume marcher uses empty-space
skipping, light transmittance, self-shadowing, and a bounded multiple-scattering term. On the
checked 80-step M2 Pro run it selected `dt=0.0091313` at `CFL=0.7`, reduced divergence from
`0.072966` to `0.0233146`, measured curl `1.90616`, and completed in `923.289 ms`. The native
Metal viewport provides live presentation; the portable Vulkan suite writes its computed HDR
radiance to a deterministic validation capture. A separate direct Vulkan `--volume` mode ray
marches a GPU-resident procedural density field directly into the swapchain with empty-space
skipping, transmittance, and self-shadowing; the complete Vulkan MAC solver remains its validated
compute/capture executable rather than sharing that presenter's resource set.

## Build prerequisites

The legacy Vulkan programs require CMake, GLFW, GLM, nlohmann-json, SQLite, CURL, Vulkan, and
`glslangValidator`. On macOS:

```bash
brew install cmake ninja glfw glm nlohmann-json sqlite curl vulkan-loader glslang qtbase qtdeclarative qtsvg
```

Use a new build directory when changing CMake generators. For example, if an existing `build/`
was configured with Unix Makefiles, do not reconfigure it with Ninja; use `build-vulkax/` as shown
above.

## Attribution

Vulkax code is MIT. The renderer substrate derives from Brendan Galea's Little Vulkan Engine under
MIT. The preserved OpenStreetMap data and derived GeoBEACON city data carry separate ODbL notices;
see `data/connaught_place/LICENSE-ODbL.md` and [OpenStreetMap copyright](https://www.openstreetmap.org/copyright).

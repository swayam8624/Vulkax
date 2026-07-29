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

The native Qt 6 editor is available on macOS as a regular application, not a local web server.
It starts in live playback: select a preset, edit its equation, press **Compile and extract controls**,
and every symbol other than `x`, `y`, `z`, and `t` becomes a bounded live parameter. Sliders update
the running preview while they are dragged; playback, pause, reset, and timeline scrubbing work
without exporting frames. The macOS interface is composited through Metal while the numerical
compute executor remains Vulkan, avoiding a Qt Vulkan-RHI crash in the interactive UI. Wave Field
and Gray-Scott reaction-diffusion use a persistent
editor-owned Vulkan compute executor with timestamp queries and CPU readback into the Qt image
provider. Other scalar, particle, and lensing previews retain their explicitly labelled CPU/reference paths.

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
```

The interactive command dispatches a compute shader into a device-local `RGBA16F` image, samples
that image in the graphics pass, tone maps it, and presents the result to the Vulkan drawable.
`--smoke` hides the window and presents three frames for deterministic macOS/MoltenVK validation.
When timestamp queries are supported, it also prints measured Vulkan compute and compute-to-render
GPU durations for the final presented frame.

`--black-hole` selects the direct Vulkan Schwarzschild mode. It uses an orbital-plane RK4 trace,
capture/escape classification, equatorial disk crossings, and the same direct HDR compute-to-present
path. Its bounded 512-pixel-wide compute extent keeps this research preview interactive.
`--kerr` selects the Carter-separated Kerr null-geodesic mode. `--spin` accepts a dimensionless
spin strictly between -1 and 1. The shader traces a central ray plus symmetric `+/-x` and `+/-y`
differential rays, uses their escaped source coordinates as a first-order filtering footprint, and
progressively accumulates jittered samples in the same device-local HDR image. Disk crossings use a
six-band redshifted thermal model with Kerr orbital frequency rather than the Schwarzschild mode's
image-space beaming term.
`--output` performs an export-only transfer after the final frame and writes the untouched linear
half-float radiance to OpenEXR. The exporter rejects non-finite frames or captures lacking both
shadow and luminous radiance; interactive frames still perform no readback.

```bash
scripts/vulkax_macos.sh physics
```

Or build it directly:

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

For the direct native macOS viewport, which bypasses the Qt image bridge, run:

```bash
scripts/vulkax_physics_metal.sh
scripts/vulkax_physics_metal.sh --black-hole-smoke
scripts/vulkax_physics_metal.sh --volume-smoke
scripts/vulkax_physics_metal.sh --native-gpu-smoke
scripts/vulkax_physics_metal.sh --native-black-hole-gpu-smoke
scripts/vulkax_physics_metal.sh --native-volume-gpu-smoke
```

This target presents through `CAMetalLayer`. Wave, the interactive Schwarzschild visual, and the
3D volume smoke visual execute in Metal compute with private GPU textures. The Qt/Vulkan editor
remains the cross-platform authoring and reproducible export application.

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
| Equation parsing, AST evaluation, built-in presets, raw analytical results | Implemented |
| Qt 6 macOS editor, parameterized dynamic graphs, project I/O, timeline, PNG/sequence export | Implemented |
| Executable scalar Physics IR with CPU interpreter, common-subexpression elimination, constant folding, GLSL/MSL emission, and Vulkan/Metal wave agreement | Implemented foundation |
| Scalar evolution/stencil IR with Laplacian and directional gradients, explicit time stepping, open/periodic/fixed boundaries, CPU oracle, generated SPIR-V/MSL, and Vulkan agreement | Implemented foundation |
| Coupled scalar evolution IR with cross-field stencils, simultaneous old/new state semantics, per-field boundaries, generated SPIR-V/MSL, and CPU/Vulkan Gray-Scott agreement | Implemented foundation |
| Persistent editor Wave Field and Gray-Scott Vulkan compute, plus headless wave/N-body CPU/GPU agreement | Implemented |
| Linear OpenEXR preview export | Implemented |
| Typed physics IR, dimension/type validation, explicit solver-pass lowering, and executable scalar-field programs | Implemented foundation |
| Direct macOS Metal viewport with GPU-resident HDR radiance and presentation | Implemented |
| Direct Vulkan/MoltenVK compute-to-swapchain field presenter with no CPU image bridge | Implemented and smoke-tested on Apple M2 Pro |
| Direct Vulkan/MoltenVK Schwarzschild GPU preview with timestamped compute-to-present path | Implemented foundation and smoke-tested on Apple M2 Pro |
| Direct Vulkan Schwarzschild linear HDR OpenEXR capture with content validation | Implemented and tested |
| CPU Kerr/Carter reference with null-constraint telemetry, 12-band disk transfer, validated five-ray source-Jacobian bundle, direct Vulkan symmetric differential bundle and six-band disk transfer, progressive HDR, and EXR validation | Implemented research foundation and smoke-tested on Apple M2 Pro |
| Wave-field direct Vulkan HDR OpenEXR export | Implemented |
| Schwarzschild CPU reference, Vulkan fixed-step compute check, and interactive Metal 3D orbital-plane visual | Implemented foundation |
| Schwarzschild thin-disk lensing and 2D buoyant-smoke equation suites | Implemented and tested |
| Interactive GPU 3D MAC smoke with staggered face velocity, RK2/MacCormack transport, obstacle-aware two-level multigrid projection, curl, temperature, and volume ray marching | Implemented research foundation |
| Portable Vulkan staggered-MAC projection, RK2/MacCormack scalar transport, source injection, and HDR volume ray march with timestamping and CPU projection-oracle agreement | Implemented foundation |
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
Kerr mode integrates Carter-separated rays and differential neighbours; neither path claims full
Jacobi-matrix geodesic deviation or film-production radiative transfer.

The checked buoyant-smoke suite solves a deterministic 2D stable-fluid chain: semi-Lagrangian
advection, pressure projection for near-zero divergence, temperature/density buoyancy, and
vorticity confinement. Separately, the native Metal target contains an interactive 3D staggered
MAC-grid smoke foundation with private face-velocity, density, temperature, pressure, divergence,
obstacle, and curl fields plus HDR ray marching. It is not a combustion or film-production solver.
The portable Vulkan test validates the same staggered face indexing through buoyancy, divergence,
80 Jacobi pressure iterations, and projection. It then performs midpoint-RK2 scalar backtracing,
bounded MacCormack density/temperature correction, persistent source injection, and a 96-sample
HDR volume ray march into a storage buffer. On the checked M2 Pro one-step run, it reduced
divergence from `0.0193272` to `0.00216895`, matched the independent CPU projection oracle within
`5.89e-07`, produced finite luminance in `[0.005008, 0.409979]`, and measured `2.45863 ms` for the
recorded Vulkan command stream. The Vulkan path does not yet include the Metal path's two-level
multigrid projection, GPU CFL controller, obstacle/curl passes, or direct swapchain volume display.

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

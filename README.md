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
| Scalar AST-to-GLSL compiler and CPU wave/reaction-diffusion graph references | Implemented |
| Persistent editor Wave Field and Gray-Scott Vulkan compute, plus headless wave/N-body CPU/GPU agreement | Implemented |
| Linear OpenEXR preview export | Implemented |
| HDR accumulation, cameras, and director tooling | Next export/runtime gate |
| Validated Schwarzschild ray reference and reference-guided lensing preview | Implemented foundation |
| Schwarzschild thin-disk lensing and 2D buoyant-smoke equation suites | Implemented and tested |
| Adaptive preview quality controller with live analytical MSE and raw benchmark | Implemented foundation |

The Interstellar reference is an inspiration for visual rigor, not a claim of parity with DNEG's
DNGR production renderer. Vulkax will state whether a visualization is analytical, numerically
validated, real-time approximate, or offline reference.

## Equation Examples

Run the two checked example suites after building:

```sh
ctest --test-dir build -L black_hole_example --output-on-failure
ctest --test-dir build -L buoyant_smoke_example --output-on-failure
```

The checked Schwarzschild thin-disk suite renders a black shadow from captured Schwarzschild null
rays, maps escaped rays using the RK4 deflection reference, and samples an inclined emissive
accretion annulus with a documented Doppler-brightness heuristic. It is not a Kerr or ray-bundle
renderer.

The checked buoyant-smoke suite solves a deterministic 2D stable-fluid chain: semi-Lagrangian
advection, pressure projection for near-zero divergence, temperature/density buoyancy, and
vorticity confinement. It is not a 3D volume-rendered fire system.

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

# Vulkax Native Metal Viewer MVP

The native viewer is the desktop continuation of the self-contained WebGL viewer. It consumes the same authoritative captured-world outputs but renders them in a real macOS Metal window.

## Architecture

`vulkax_viewer_common` is cross-platform presentation/debugging infrastructure. It loads:

- `appearance/before.ply` and `appearance/rewritten.ply` through Vulkax's existing Gaussian loader;
- optional physical `particles.csv`;
- `influence/selected_rewrite_region.csv`;
- a deterministic outer-shell surface only when physical particles form a complete Cartesian lattice.

It never mutates any of these files. The native frontend is therefore downstream of the scientific state rather than part of the rewrite/evidence contract.

The macOS frontend is `vulkax_viewer`, implemented with AppKit + MetalKit and no third-party UI/windowing dependency.

## Native Gaussian rendering

The Metal splat path is intentionally more faithful than the WebGL point-sprite MVP.

For each Gaussian it uploads:

- center position;
- XYZ linear scales;
- quaternion orientation;
- SH-DC-derived RGB color;
- opacity.

The vertex shader rotates the three scaled Gaussian basis vectors, projects them through the camera, constructs the 2D screen covariance `C = A A^T`, eigen-decomposes that covariance, and draws a camera-facing ellipse along the two screen-space eigenvectors. The fragment shader evaluates a three-sigma Gaussian falloff.

Transparent splats currently use a CPU back-to-front index sort. Sorting is marked dirty by camera/state changes rather than performed blindly on every stationary frame.

## Visibility and large-scene groundwork

`include/vulkax/viewer/visibility.hpp` adds a reusable visibility/LOD stage for both Metal and the future Vulkan frontend. It performs:

1. minimum-opacity rejection;
2. conservative three-sigma sphere/frustum culling;
3. projected-footprint importance ranking when the visible set exceeds a splat budget;
4. back-to-front depth ordering of the retained set.

The budget stage deliberately selects first and depth-sorts second so LOD does not destroy conventional alpha-compositing order. Regression tests cover behind-camera rejection, side-frustum rejection, opacity rejection, importance budgeting, and final depth order.

The utility is part of the reusable viewer layer now; replacing the Metal frontend's current all-splat dirty sort with this visibility result is the next performance integration step.

## Viewer modes

- **Hybrid** — physical surface + Gaussian appearance + physical particles.
- **Splats** — Gaussian appearance only.
- **Surface** — derived physical shell only.
- **Particles** — physical MPM particles only.

Before/verified-after switching changes the authoritative Gaussian state. A material rewrite that did not move Gaussian centers is not given invented motion; the selected physical rewrite particles/surface vertices remain the visual explanation.

## Interaction

- left drag: orbit;
- right or middle drag: pan;
- wheel/trackpad scroll: zoom;
- drag a `.ply` file directly onto the Metal viewport: load that Gaussian asset;
- `R`: reset camera;
- `1`–`4`: Hybrid / Splats / Surface / Particles;
- `B` / `A`: Before / Verified After;
- `H`: rewrite highlight;
- `G`: ground grid;
- Space: auto orbit.

The native inspector exposes the same mode/state controls plus splat scale, opacity, exposure, a live FPS readout, scene/rewrite statistics, and a standalone Gaussian PLY open dialog.

## Build and run on macOS

The simplest path after a captured-world run already exists is:

```bash
bash scripts/open_native_viewer.sh
```

It configures/builds only the required native viewer target and then launches the existing run. Optional explicit run/particle paths are accepted:

```bash
bash scripts/open_native_viewer.sh \
  build/captured-world-run \
  build/captured-example/particles.csv
```

Manual build/run:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVULKAX_BUILD_TESTS=ON
cmake --build build --target vulkax_viewer --parallel "$(sysctl -n hw.logicalcpu)"

./build/vulkax_viewer \
  --run build/captured-world-run \
  --particles build/captured-example/particles.csv
```

Open an authored/exported Gaussian asset directly:

```bash
./build/vulkax_viewer --ply path/to/asset_vulkax_splats.ply
```

## Current boundary

This PR establishes the reusable native scene model, desktop interaction, anisotropic Metal Gaussian renderer, native asset loading, FPS diagnostics, and shared visibility/LOD selection logic. It is not yet the final production 3DGS backend.

The next renderer stages are:

- wire the common visibility/LOD result into Metal draw counts;
- replace CPU `O(N log N)` transparency sorting with GPU depth/tile/radix work for genuinely large clouds;
- move from the current projected-axis covariance approximation to the exact perspective Jacobian form;
- add view-dependent higher-order SH color;
- add native before/after comparison and capture tooling;
- add a Vulkan desktop frontend reusing `vulkax_viewer_common`.

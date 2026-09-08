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

Transparent splats use a CPU back-to-front index sort. Sorting is marked dirty by camera/state changes rather than performed blindly on every stationary frame.

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
- `R`: reset camera;
- `1`–`4`: Hybrid / Splats / Surface / Particles;
- `B` / `A`: Before / Verified After;
- `H`: rewrite highlight;
- `G`: ground grid;
- Space: auto orbit.

The native inspector exposes the same mode/state controls plus splat scale, opacity, exposure, and a standalone Gaussian PLY open dialog.

## Build and run on macOS

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

This PR establishes the native scene model, desktop interaction and Metal anisotropic splat renderer. It is not yet the final production 3DGS backend. Future renderer work can add GPU radix/tile sorting, exact perspective covariance/Jacobian handling, SH view-dependent color, visibility culling, LOD, and the Vulkan window frontend while reusing `vulkax_viewer_common`.

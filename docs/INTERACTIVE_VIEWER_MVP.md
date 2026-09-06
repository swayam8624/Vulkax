# Vulkax Interactive Viewer MVP

The interactive viewer turns a completed Vulkax captured-world run into a self-contained WebGL2 scene that can be opened directly in a desktop browser. It is a presentation/debugging layer: it never modifies Gaussian PLY evidence, physical/rewrite outputs, calibration data, certificates, or stable identity data.

## Current MVP

The generated viewer contains four live modes:

- **Hybrid** — presentation surface + authoritative Gaussian splats + physical particles.
- **Splats** — the Gaussian appearance world rendered as real-time Gaussian point sprites.
- **Surface** — a shaded physical shell when a regular particle lattice is available.
- **Particles** — the physical MPM particle set with the selected rewrite region highlighted.

Interaction:

- left mouse drag: orbit;
- right mouse drag: pan;
- wheel: zoom;
- double click: refocus/reset camera;
- `R`: reset camera;
- optional auto-orbit;
- before/verified-after state switch;
- rewrite-region highlight toggle;
- ground grid toggle;
- splat-size, opacity, and exposure controls;
- PNG capture.

The viewer is fully self-contained: no CDN, framework, font, JavaScript package, or network request is required at runtime.

## Build a viewer from a run

For the canonical controlled example:

```bash
python3 scripts/build_interactive_viewer.py \
  build/captured-world-run \
  --particles-csv build/captured-example/particles.csv

open build/captured-world-run/render/interactive/viewer.html
```

The generated HTML embeds the data needed to display that run, so `file://` opening works without running a local web server.

## Asset import

The right-side inspector can load local ASCII `.ply` and `.obj` files with the browser File API. Imported assets are normalized into viewer space and displayed as live splats. This is deliberately viewer-only: it does not author or overwrite a Vulkax capture bundle.

The MVP treats OBJ vertices as splat seeds. Face-aware mesh-to-Gaussian sampling, glTF/GLB ingestion, and image/video reconstruction belong to the next authoring/import phase.

## Rendering model

Gaussian mode uses WebGL2 `POINTS` with a Gaussian radial falloff in the fragment shader. Point size is perspective-scaled from the stored Gaussian scale. This is intentionally a stable MVP rather than a full production 3DGS tile/sort pipeline; the native Vulkan/Metal renderer can later adopt the same interaction and scene model.

When physical particles form a complete regular Cartesian lattice, the generator derives only the six outer shell surfaces, triangulates them deterministically, and colors vertices in the selected rewrite region orange. If topology is not known, the surface mode remains unavailable rather than inventing a scientific surface.

## Scientific boundary

The viewer distinguishes authoritative and presentation data:

- authoritative: before/rewritten Gaussian centers and appearance properties, physical particles, selected rewrite-region IDs;
- presentation: derived shell triangles, studio lighting, grid, exposure, camera, point-sprite falloff.

A verified material rewrite can legitimately leave Gaussian centers unchanged. The viewer therefore never fabricates displacement: it keeps the geometry fixed and highlights the selected physical rewrite region.

## Smoke test

```bash
python3 scripts/build_interactive_viewer.py --self-test
```

The self-test verifies PLY parsing, particle-lattice shell construction, rewrite-region ingestion, and generation of a self-contained HTML file.

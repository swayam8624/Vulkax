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

## One-command launch

From the Vulkax repository after a captured-world run already exists:

```bash
bash scripts/open_interactive_viewer.sh
```

The launcher uses `build/captured-world-run` and, when available, `build/captured-example/particles.csv`. On macOS it opens the generated viewer automatically.

## Build a viewer manually

For the canonical controlled example:

```bash
python3 scripts/build_interactive_viewer_app.py \
  build/captured-world-run \
  --particles-csv build/captured-example/particles.csv

open build/captured-world-run/render/interactive/viewer.html
```

`build_interactive_viewer.py` is the core scene-data/HTML compiler. `build_interactive_viewer_app.py` is the public hardened launcher: it applies Retina-safe point sizing, explicit DOM bindings for stable Chrome/Safari behavior, and the production local-file importer.

The generated HTML embeds the data needed to display that run, so `file://` opening works without running a local web server.

## Asset import

The right-side inspector can click or drag/drop local `.ply` and `.obj` files through the browser File API. Imported assets are normalized into viewer space and displayed as live splats. This is deliberately viewer-only: it does not author or overwrite a Vulkax capture bundle.

Supported PLY paths include:

- ASCII PLY;
- binary little-endian PLY;
- binary big-endian PLY;
- XYZ-only point clouds;
- RGB vertex colors;
- 3D-Gaussian-style `f_dc_0..2`, `scale_0..2`, and `opacity` fields.

PLY scales are normalized with scene extent instead of being left in source units. Large point clouds are deterministically reduced to a 250,000-point browser budget rather than blindly allocating unbounded WebGL buffers.

OBJ import understands polygon faces and positive/negative face indices, triangulates the faces, and deterministically surface-samples the triangles into splats. OBJ files with only vertex records are still accepted as point clouds. Vertex colors are used when the exporter stores them directly on `v` records.

The importer reads files as `ArrayBuffer`, so binary PLY is not corrupted through text decoding. Drag/drop calls the same import routine directly rather than trying to synthesize and assign a `DataTransfer` object to the hidden file input, which is unreliable across browsers.

The current browser importer intentionally stops at PLY/OBJ. glTF/GLB ingestion and image/video reconstruction belong to the next authoring/import phase.

## Rendering model

Gaussian mode uses WebGL2 `POINTS` with a Gaussian radial falloff in the fragment shader. Point size is perspective-scaled from the stored Gaussian scale and clamped by the browser/GPU point-size implementation limit. The hardened launcher keeps the scale multiplier in world-space territory so Retina displays do not collapse the scene into giant point sprites.

This is intentionally a stable MVP rather than a full production 3DGS tile/sort pipeline; the native Vulkan/Metal renderer can later adopt the same interaction and scene model.

When physical particles form a complete regular Cartesian lattice, the generator derives only the six outer shell surfaces, triangulates them deterministically, and colors vertices in the selected rewrite region orange. If topology is not known, the surface mode remains unavailable rather than inventing a scientific surface.

## Scientific boundary

The viewer distinguishes authoritative and presentation data:

- authoritative: before/rewritten Gaussian centers and appearance properties, physical particles, selected rewrite-region IDs;
- presentation: derived shell triangles, studio lighting, grid, exposure, camera, point-sprite falloff.

A verified material rewrite can legitimately leave Gaussian centers unchanged. The viewer therefore never fabricates displacement: it keeps the geometry fixed and highlights the selected physical rewrite region.

## Smoke tests

```bash
python3 scripts/build_interactive_viewer.py --self-test
python3 scripts/build_interactive_viewer_app.py --self-test
node scripts/viewer_importers_test.js
```

The importer regression test covers RGB ASCII PLY, binary little-endian Gaussian PLY, OBJ face sampling, negative OBJ indices, malformed inputs, and deterministic downsampling. CI additionally generates a persistent hardened viewer fixture, validates the HTML contract, and runs `node --check` on the generated inline JavaScript.

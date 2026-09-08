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

`build_interactive_viewer.py` is the core scene-data/HTML compiler. `build_interactive_viewer_app.py` is the public hardened launcher: it applies Retina-safe point sizing, explicit DOM bindings for stable Chrome/Safari behavior, and the production local-file importers.

The generated HTML embeds the data needed to display that run, so `file://` opening works without running a local web server.

## Asset import

The right-side inspector can click or drag/drop local `.ply`, `.obj`, `.glb`, `.gltf`, `.png`, `.jpg/.jpeg`, and `.webp` assets through the browser File API. Imported assets are normalized into viewer space and displayed as live splats. This is deliberately viewer-only: it does not author or overwrite a Vulkax capture bundle.

### PLY

Supported PLY paths include:

- ASCII PLY;
- binary little-endian PLY;
- binary big-endian PLY;
- XYZ-only point clouds;
- RGB vertex colors;
- 3D-Gaussian-style `f_dc_0..2`, `scale_0..2`, and `opacity` fields.

PLY scales are normalized with scene extent instead of being left in source units. Large point clouds are deterministically reduced to a 250,000-point browser budget rather than blindly allocating unbounded WebGL buffers.

### OBJ

OBJ import understands polygon faces and positive/negative face indices, triangulates the faces, and deterministically surface-samples the triangles into splats. OBJ files with only vertex records are still accepted as point clouds. Vertex colors are used when the exporter stores them directly on `v` records.

### GLB / glTF 2.0

The dependency-free glTF path supports:

- GLB 2.0 with embedded JSON/BIN chunks;
- `.gltf` JSON with base64/data-URI buffers;
- `.gltf` plus external `.bin` sidecars when the `.gltf` and `.bin` are selected or dropped together;
- active-scene traversal and node matrix/TRS transforms;
- indexed and non-indexed mesh primitives;
- `TRIANGLES`, `TRIANGLE_STRIP`, `TRIANGLE_FAN`, and `POINTS` primitive modes;
- `POSITION` and `COLOR_0` accessors;
- material `baseColorFactor` tint/alpha;
- deterministic triangle surface sampling into display splats.

Texture images are not sampled in this MVP, so textured glTF meshes use their vertex colors and/or material base color. Skins and animation playback are not applied yet. Sparse accessors and Draco/meshopt-compressed primitives are rejected with explicit diagnostics rather than silently rendering corrupted geometry.

For a `.gltf` with an external buffer, select both files together in the picker or drag them together onto the importer:

```text
model.gltf
model.bin
```

### Images: presentation 2.5D splat card

A single image does not contain enough information for honest 3D reconstruction, so the current image path does **not** pretend to recover a 3D scene. Instead it converts the visible pixels into a color/alpha-preserving Gaussian-style splat card:

```text
PNG / JPEG / WebP
        ↓
browser image decode
        ↓
RGBA pixels
        ↓
budget-aware spatial sampling
        ↓
aspect-preserving XY splats at z = 0
        ↓
interactive WebGL Gaussian view
```

The image card:

- preserves source aspect ratio;
- preserves RGB and alpha;
- ignores fully/mostly transparent pixels;
- deterministically reduces large images to an 80,000-splat browser budget;
- is explicitly marked `presentation-only` / `image_2.5d` in importer metadata.

Orbiting the camera therefore reveals that the result is a plane. True image/video → 3D Gaussian reconstruction is a separate future pipeline requiring depth/multi-view inference, camera poses, or an actual 3DGS optimization stage.

The importer reads all binary formats as `ArrayBuffer`, so binary PLY and GLB are not corrupted through text decoding. Drag/drop calls the same package import routine directly rather than trying to synthesize and assign a `DataTransfer` object to the hidden file input, which is unreliable across browsers.

## Rendering model

Gaussian mode uses WebGL2 `POINTS` with a Gaussian radial falloff in the fragment shader. Point size is perspective-scaled from the stored Gaussian scale and clamped by the browser/GPU point-size implementation limit. The hardened launcher keeps the scale multiplier in world-space territory so Retina displays do not collapse the scene into giant point sprites.

This is intentionally a stable MVP rather than a full production 3DGS tile/sort pipeline; the native Vulkan/Metal renderer can later adopt the same interaction and scene model.

When physical particles form a complete regular Cartesian lattice, the generator derives only the six outer shell surfaces, triangulates them deterministically, and colors vertices in the selected rewrite region orange. If topology is not known, the surface mode remains unavailable rather than inventing a scientific surface.

## Scientific boundary

The viewer distinguishes authoritative and presentation data:

- authoritative: before/rewritten Gaussian centers and appearance properties, physical particles, selected rewrite-region IDs;
- presentation: derived shell triangles, studio lighting, grid, exposure, camera, point-sprite falloff;
- imported external assets: presentation-only viewer data, never written into a Vulkax capture bundle by this MVP;
- single-image imports: explicitly 2.5D cards, not reconstructed 3D evidence.

A verified material rewrite can legitimately leave Gaussian centers unchanged. The viewer therefore never fabricates displacement: it keeps the geometry fixed and highlights the selected physical rewrite region.

## Smoke tests

```bash
python3 scripts/build_interactive_viewer.py --self-test
python3 scripts/build_interactive_viewer_app.py --self-test
node scripts/viewer_importers_test.js
node scripts/viewer_gltf_importer_test.js
node scripts/viewer_image_importer_test.js
```

The importer regression suites cover RGB ASCII PLY, binary little-endian Gaussian PLY, OBJ face sampling, negative OBJ indices, malformed inputs, deterministic downsampling, embedded-buffer glTF, external `.gltf + .bin`, GLB 2.0, indexed triangles, triangle strips, image aspect preservation, alpha filtering, and image point-budget downsampling. CI additionally generates a persistent hardened viewer fixture, validates the HTML contract, and runs `node --check` on the generated inline JavaScript.

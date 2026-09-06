# Vulkax 1.0.x Surface Proxy Showcase

The Gaussian PLYs remain the authoritative appearance evidence. The surface-proxy renderer is a presentation-only layer that makes sparse captured worlds readable without changing the evidence files.

For dense square appearance lattices, the renderer uses the Gaussian ordering directly. For sparse appearance captures such as the canonical 5-Gaussian / 64-particle example, pass the captured `particles.csv`; the renderer then builds the outer shell of the regular physical MPM lattice and uses that as the continuous presentation surface.

The renderer estimates normals, performs depth-tested triangle rasterization, adds restrained studio shading, a soft contact shadow, rewrite-region accents, and a turntable. If the authoritative Gaussian centers do not move during a verified material rewrite, the surface geometry remains unchanged and the selected physical rewrite region is highlighted instead of inventing deformation.

It never overwrites the Gaussian PLYs, raw PPM evidence, rewrite evidence, calibration outputs, certificates, or stable identity data.

## Canonical synthetic example

After `captured-deformable-generate-example` and `captured-world-run` have produced their outputs:

```bash
python3 scripts/render_surface_proxy.py build/captured-world-run \
  --particles-csv build/captured-example/particles.csv \
  --width 1280 \
  --height 720 \
  --turntable 12

open build/captured-world-run/render/surface_proxy/surface_gallery.html
```

The `--particles-csv` path is strongly recommended for the canonical example because its appearance layer contains only five Gaussians while the physics body contains a regular `4 x 4 x 4` particle lattice.

## Generic appearance-only use

If a capture already contains a dense square Gaussian lattice, the renderer can operate without a particle CSV:

```bash
python3 scripts/render_surface_proxy.py build/captured-world-run \
  --width 1280 \
  --height 720 \
  --turntable 12
```

For sparse non-lattice appearance clouds, the presentation fallback is a conservative convex-hull proxy. This fallback is explicitly reported in `manifest.json` and in the gallery metadata.

## Outputs

```text
render/surface_proxy/
  surface_before.png
  surface_after.png
  surface_gallery.html
  manifest.json
  turntable/frame_000.png ...
```

`manifest.json` records the proxy source (`physical` or `appearance`), inferred topology, turntable count, Gaussian displacement, selected rewrite-particle count, and that raw evidence was not modified.

## Validation

The renderer has no third-party Python dependencies. Its built-in self-test covers both sparse non-lattice appearance data and the regular `4 x 4 x 4` physical-particle path:

```bash
python3 scripts/render_surface_proxy.py --self-test
```

# Vulkax 1.0.x Surface Proxy Showcase

The Gaussian PLYs remain the authoritative appearance evidence. This presentation layer reads `appearance/before.ply` and `appearance/rewritten.ply`, derives a cloth-like surface proxy from the stable row-major lattice, estimates normals, and renders a shaded beauty view with depth, a soft contact shadow, corner markers, and a turntable.

It never overwrites the Gaussian PLYs, raw PPM evidence, rewrite evidence, calibration outputs, certificates, or stable identity data.

## Run

After `captured-world-run` has produced a run directory:

```bash
python3 scripts/render_surface_proxy.py build/captured-world-run \
  --width 1280 \
  --height 720 \
  --turntable 12

open build/captured-world-run/render/surface_proxy/surface_gallery.html
```

Outputs are written under:

```text
render/surface_proxy/
  surface_before.png
  surface_after.png
  surface_gallery.html
  manifest.json
  turntable/frame_000.png ...
```

## Validation

The renderer has no third-party Python dependencies. Run its built-in smoke test with:

```bash
python3 scripts/render_surface_proxy.py --self-test
```

## Current limitation

The stable proxy currently expects a square row-major capture lattice. If a future captured world is not a square lattice, the script stops instead of inventing topology. That is deliberate: presentation geometry must be derived from a known stable ordering, not guessed.

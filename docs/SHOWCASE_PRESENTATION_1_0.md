# Vulkax 1.0 presentation layer

Vulkax 1.0 keeps its research evidence and its presentation surface deliberately separate. The native Gaussian renderer and the captured-world certificate remain the source of truth; the showcase turns those results into artifacts that are easier to inspect, present and share.

## Stable presentation contract

A showcase run continues to emit the frozen PPM artifacts used by the 1.0 evidence workflow. The presentation revision adds browser-ready derivatives beside them:

```text
render/showcase/
├── gallery.html
├── summary_card.svg
├── contact_sheet.ppm
├── contact_sheet.png
├── hero_before.ppm / hero_baseline.ppm
├── hero_before.png / hero_baseline.png
├── hero_after.ppm / hero_rollback.ppm
├── hero_after.png / hero_rollback.png
├── closeup_*.ppm
├── closeup_*.png
├── showcase_manifest.json
└── turntable/
    ├── frame_000.ppm
    ├── frame_000.png
    └── ...
```

For a rejected rewrite, the presentation explicitly labels and shows the rollback state rather than making the failed candidate look successful.

## Gallery

Open `render/showcase/gallery.html` directly in a browser. It is self-contained except for the local PNG/SVG artifacts generated in the same showcase directory and requires no network connection, CDN, JavaScript framework or external font.

The gallery contains:

- a Vulkax 1.0 status header with scene, backend and rewrite verdict;
- the generated summary card;
- side-by-side initial/final hero views;
- a framed comparison plate;
- detail views when closeups are enabled;
- a scrub-able deterministic turntable of the final committed or rollback state.

## PNG output

`writePng` is implemented inside the stable renderer utility layer without adding libpng or zlib as build/runtime dependencies. It writes RGBA PNG files using a standards-compliant zlib stream composed of stored DEFLATE blocks, with PNG CRC-32 and zlib Adler-32 checksums.

The measured showcase CI does more than check file extensions or signatures: it parses the PNG chunks, concatenates IDAT data, decompresses it with Python's standard-library `zlib`, checks scanline dimensions and verifies the expected filter layout.

This choice favors deterministic, dependency-free output over maximum PNG compression. PPM remains available when minimal encoding overhead matters; PNG is the presentation format.

## Presentation grade

Only PNG derivatives receive the mild display grade. It applies restrained saturation/contrast separation and a small vignette/corner attenuation so the showcase reads more cleanly on modern displays. Raw PPM evidence is not graded.

The showcase manifest records this distinction through `presentation_revision`, `presentation_png`, `browser_gallery` and `presentation_grade` fields while retaining showcase schema version 1 for backward-compatible consumers.

## Decorative scene geometry

`studio_pedestal` and `cloth_showcase` remain presentation presets, not reconstructed physical geometry. The revised floor is denser and lower contrast, and the studio pedestal uses a smoother radial Gaussian layout. These splats are still explicitly excluded from research evidence and do not change the captured world, correspondence graph, physical discretization, rewrite transaction or verifier result.

The pinned HDR environment asset remains metadata/reference only. Vulkax does not claim that stored Gaussian spherical-harmonic appearance is physically relit by that asset.

## Stability boundary

This presentation revision intentionally does **not** change:

- material calibration;
- APIC/MPM replay;
- Operator Influence;
- rewrite proposal generation;
- nonlinear or derivative verification;
- commit/rollback semantics;
- Gaussian stable identity;
- native renderer evidence images outside the showcase;
- captured-world certificate semantics.

The result is therefore a 1.0.x-quality visual polish of the existing stable model rather than a new research milestone or a new physical claim.

# Vulkax

> [!IMPORTANT]
> **Vulkax v1 is frozen.** Its exact pre-reset state is preserved on
> [`legacy/studio-v1-2026-08-10`](../../tree/legacy/studio-v1-2026-08-10) at commit
> `ff760f8f85683947e61e5ea66b99785d9994e913`.
>
> This repository is now a **reproducible v1 foundation and source archive**. The next Vulkax
> architecture is being designed as a problem-driven, solver-independent computational-physics
> system and will be developed separately rather than extending the v1 Studio abstractions.

## What this repository contains

Vulkax v1 accumulated several substantial research and engineering systems. They remain here because
they are useful numerical, GPU, rendering, import, validation, and reproducibility foundations—not
because their product architecture should constrain the next system.

Reusable foundations include:

- canonical equation parsing and typed scalar/vector/tensor compute IR,
- Vulkan and native Metal compute/presentation work,
- numerical field, fluid, particle, rigid-body, and relativity experiments,
- glTF/GLB/OBJ scene import and PBR rendering foundations,
- deterministic project/capture infrastructure including native 4K HEVC output,
- validation, sanitizers, numerical regression tests, and cross-platform CI,
- preserved BEACON, GeoBEACON, and Atlas research baselines.

The historical native Physics Studio, direct Vulkan presenter, Qt compatibility application, and
legacy research applications are still buildable from this tree. See
[`docs/RUNNING_VULKAX.md`](docs/RUNNING_VULKAX.md), [`docs/STATUS.md`](docs/STATUS.md), and the exact
legacy branch for the full v1 operating surface.

## What is frozen

The following v1 concepts are **legacy product abstractions** and should not be extended into the next
architecture:

- hard-coded Wave / Schwarzschild / Volume Smoke visualizer modes,
- visualization mode as the owner of simulation semantics,
- equation-string-only medium inference,
- obstacle-centric scene semantics,
- demo-specific renderer/solver branches,
- the monolithic native Studio state model,
- BEACON / GeoBEACON / Atlas as product identities.

They may remain useful test fixtures, numerical references, or research examples. They are not the
north-star API.

## Vulkax Next direction

The next system starts from a **physical problem**, not a demo or visualizer:

```text
Problem
  -> physical model
  -> mathematical/operator model
  -> discretization + solver plan
  -> accelerated execution
  -> verification
  -> analysis / inverse / optimization
  -> scientific visualization
```

The new architecture will center on typed problem/operator representations, dimensional correctness,
solver-independent verification, reproducible result certificates, and a backend-capability layer.
Vulkan is the primary cross-platform GPU target; Metal is a first-class Apple backend; OpenGL may be
used as a compatibility/fallback renderer. Backend selection belongs to runtime capability discovery,
not to demo-specific compile-time assumptions.

The research north star is not merely *what happened?* but also *why did the governing physics produce
this observable?*—including operator-level attribution and counterfactual physical analysis.

## Development policy for v1

New product features should not be added to the v1 Studio. Changes to this repository should be limited
to preservation, reproducibility, security, build portability, correctness fixes, and extraction of
well-tested reusable foundations.

Generated frame sequences, movies, EXRs, raw dumps, and similar run output should be published as CI
artifacts or releases rather than committed to Git. Small manifests, numerical summaries, fixtures,
and deliberately curated research figures may remain versioned.

For the exact freeze boundary, preserved capabilities, and known issues, see
[`docs/V1_FREEZE.md`](docs/V1_FREEZE.md).

## License and attribution

Vulkax code is MIT. The renderer substrate includes work derived from Brendan Galea's Little Vulkan
Engine under MIT. Preserved OpenStreetMap-derived data has separate ODbL attribution; see the notices
under `data/`.

# Vulkax architecture map

Vulkax is one repository with one active product and several preserved research baselines. The repository is intentionally not presented as four separate products.

## Active product

**Vulkax Physics Studio** is the product surface under active development. It turns equations, solver graphs and imported scene geometry into reproducible GPU visualizations, live previews and deterministic exports.

Its core implementation is split by responsibility:

- `src/vulkax/equation/` owns scalar expression parsing, canonical ASTs and equation compilation.
- `src/vulkax/physics/` owns the typed Physics IR, resource reflection, pipeline artifacts and executable solver/shader lowering.
- `src/vulkax/sim/` owns simulation systems such as particles, buoyant smoke, fluid/rigid coupling and sparse storage.
- `src/vulkax/relativity/` owns Schwarzschild/Kerr numerical references and transfer calculations.
- `src/vulkax/research/` owns adaptive-quality and experiment-oriented controllers.
- `src/vulkax/editor/` is the cross-platform Qt compatibility/editor surface.
- `apps/VulkaxPhysicsStudioMac/` is the native macOS SwiftUI/Metal product surface.

The native Metal application and the direct Vulkan presenter are backend implementations of the same product direction; they are not separate engines.

## GPU/runtime boundary

The modern Vulkax subsystems increasingly own their own Vulkan/Metal execution paths, but the direct Vulkan presenter still reuses parts of the older `lve_*` Vulkan wrapper for window, swapchain and basic resource duties. Treat that layer as a compatibility substrate under migration, not as the architectural center of Vulkax.

New GPU infrastructure should live under the Vulkax namespaces and should not introduce new dependencies from modern code back into tutorial-era naming or source-relative resource assumptions.

## Preserved research baselines

**BEACON** is the clustered-lighting renderer/benchmark substrate. It remains useful for GPU regression, benchmark methodology and historical research comparison.

**GeoBEACON** is the checked OpenStreetMap city/digital-twin experiment built on that renderer.

**Atlas** is the preserved globe/navigation stack. Its code is primarily under `src/atlas/` and its architecture is documented separately in `docs/ATLAS_ARCHITECTURE.md`.

These systems remain runnable because they provide regression and research value. New Physics Studio requirements should not be implemented inside Atlas/BEACON simply because an old executable already owns a window or Vulkan device.

## Repository ownership rule

When adding code, choose the narrowest owner:

1. reusable equation/physics/simulation/relativity logic belongs in the corresponding `src/vulkax/*` library;
2. backend-specific GPU execution belongs in a Vulkax GPU/runtime layer or the native app backend;
3. editor-only UI belongs in the editor/app surface;
4. Atlas/BEACON/GeoBEACON should change only for preserved-baseline fixes, explicit shared migrations or their own experiments;
5. generated files belong in the build/output tree unless they are deliberate checked test/reference assets.

This ownership rule is the migration path away from the repository's historical “root CMake plus `lve_*` knows everything” shape.

## Build and validation map

The build is being decomposed into `cmake/Vulkax*.cmake` modules so dependency discovery, shader generation, application packaging and the CTest registry are not all encoded in the root build file.

Validation is intentionally layered:

- CTest covers deterministic CPU/numerical contracts and Vulkan agreement tests;
- Linux/Lavapipe CI protects portable Vulkan behavior and headless compatibility surfaces;
- native macOS CI protects SwiftUI/Metal product behavior;
- sanitizer and static-analysis jobs protect host-side lifetime/undefined-behavior errors;
- visual/HDR gates protect reproducible research outputs.

See `docs/STATUS.md` for the detailed implementation ledger and `docs/VULKAX_PHYSICS_STUDIO_ROADMAP.md` for forward-looking product work.

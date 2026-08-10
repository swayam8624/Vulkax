# Vulkax Physics Studio — Scene, Models, Media, Cameras and Capture

This guide describes the native macOS Studio workflow built on top of the Vulkax numerical/GPU runtime. The editor deliberately separates **what the user sees** from **what the solver consumes** so imported assets, visualization choices, materials and cinematic output do not silently change the underlying numerical model.

## Scene workspace

The native Studio uses a scene-first three-pane layout:

- **Scene**: visualization mode, inferred simulation medium, imported entities and camera presets.
- **Viewport**: live Metal simulation/rendering, scene geometry, camera interaction and timeline transport.
- **Inspector**: equation/solver controls, selected-object role/proxy settings, camera director controls and cinematic capture.

Changing presentation state such as the camera does not modify the equation or simulation state. It only resets progressive visual accumulation where needed.

## Equation-aware simulation medium

Vulkax can suggest the domain on which an equation should be visualized. Current categories include 2D surface, 3D volume, particle set, rigid body, vector field, relativistic ray bundle, trajectory/ODE and abstract field.

Inference returns confidence and human-readable reasons. **Auto is a recommendation, not a physical truth.** The Inspector always permits a manual override, and the override is persisted in the `.vxp` project.

## Importing cars, props and scene geometry

Use **Add Model** or drop a supported model into the viewport.

### First-class Vulkax formats

The native asset pipeline directly supports:

- **OBJ** for geometry,
- **glTF 2.0 (`.gltf`)**,
- **GLB (`.glb`)**.

The glTF/GLB path supports static scene/node transforms, indexed and non-indexed triangle primitives, normals, UVs, multiple primitives/materials, embedded/local buffers, embedded/local images, base-color textures/factors, metallic-roughness textures/factors, tangent-space normal maps with normal scale, and emissive factors.

Vulkax reconstructs a tangent frame from world-position and UV derivatives for normal mapping, so static vehicle/prop assets do not require a separate tangent preprocessing pass.

Current first-class glTF boundaries are intentionally explicit: sparse accessors, skeletal animation, skinning, morph targets and non-triangle primitive modes are not treated as supported production features.

### macOS Model I/O compatibility formats

For formats outside OBJ/glTF/GLB, the native macOS app asks `MDLAsset.canImportFileExtension` at runtime. If the installed macOS framework reports the extension as importable, Vulkax uses Model I/O as a **static-geometry compatibility adapter**, flattens the result through a temporary OBJ representation, and feeds it into the normal Vulkax mesh/proxy pipeline.

The picker includes common compatibility candidates such as PLY, STL, USD-family assets, Alembic and FBX. This does **not** mean every macOS release guarantees every format. In particular, FBX is accepted only when the runtime capability check succeeds. The compatibility path does not promise preservation of FBX animation, rigs or material semantics; convert important production assets to glTF/GLB for the native PBR path.

### Visual mesh versus simulation proxy

Downloaded car, architectural and production meshes are often open, non-manifold, self-intersecting or unnecessarily dense. Feeding that exact topology into voxelization/contact is fragile. Vulkax therefore stores two representations:

1. **Visual mesh** — original imported topology, normals, UVs and materials used by the scene renderer.
2. **Simulation proxy** — closed geometry used by voxelization, collision/contact and fluid coupling.

A valid closed manifold can use `Render Mesh` as its simulation proxy. Otherwise Vulkax can use a conservative closed bounds proxy. The visible car remains the original asset while the solver works against the stable proxy. Both representations consume the same rigid-body transform, so rendered geometry follows solver-driven motion.

### Scene roles

Entities can be assigned roles such as Visual, Collider, Fluid Obstacle, Source, Probe or Domain Surface. A visual-only prop can exist in a shot without participating in the equation. Simulation roles determine whether a physics proxy is consumed by the solver.

## PBR scene rendering

Imported glTF/GLB geometry is rendered in a separate Metal scene pass over the simulation result. Current material support includes:

- sRGB base-color texture and factor,
- metallic-roughness texture using glTF's B/G channels plus scalar factors,
- tangent-space normal texture and normal scale,
- emissive factor,
- GGX normal distribution,
- Smith geometry term,
- Schlick Fresnel,
- direct key light, environment-like ambient/rim response and Studio exposure.

Material data is render-only. Texture complexity cannot change voxelization or contact behavior.

## Camera controls

The viewport camera is the real render camera rather than a UI-only orbit value. Current controls include orbit, pan, dolly, camera presets, explicit position/target/up, vertical FOV and exposure. Volume rendering and visual scene meshes consume the same camera.

## Director camera track

Use **Add / Update Camera Key** at the current playhead position to create a director track. Keys persist position, target, up vector, FOV and exposure in the project file. The renderer interpolates the same track in interactive playback and deterministic offline capture.

A capture with no camera keys uses the camera pose selected when recording begins.

## Deterministic 4K cinematic capture

The native Studio capture path is an offline renderer, **not screen recording or viewport upscaling**.

Choose 1920×1080 or 3840×2160, 24/30/60 fps and duration. During capture Vulkax:

1. snapshots the camera/director-track state,
2. renders the simulation at requested output resolution,
3. advances simulation time with exact `1 / fps` cadence,
4. renders PBR scene geometry using the same camera,
5. renders directly into Metal-backed CoreVideo pixel buffers,
6. writes deterministic HEVC `.mov` frames through `AVAssetWriter`.

The cinematic smoke test creates a short movie, reopens it and verifies the actual video track is 3840×2160.

## Project format

Current native projects use `.vxp` format version **9**. The loader remains backward-compatible with versions 1–9. Version 9 persists the camera director track in addition to equation/runtime graph, parameters, medium override, scene entities, role/proxy configuration, rigid-body transforms, camera and capture settings.

For `.gltf` assets, project packaging also copies safe local buffer/image sidecars. Data URIs remain embedded; remote URI fetching and directory traversal are rejected. GLB stays self-contained.

## Equation/runtime generation

The native equation editor calls the canonical C++ equation parser through a small C ABI. Swift no longer owns a second lexer/parser implementation. The bridge supplies parameter ordering, diagnostics, a canonical AST hash and Metal source for the live native path.

The core Physics IR includes executable scalar, vector and dense 3×3 tensor compute programs. Tensor fields are represented as nine canonical scalar component programs with shared domain/parameters, CPU evaluation, stable hashing and GLSL/MSL emission.

Vulkax also has a generated transport/diffusion planner for a deliberately bounded equation family:

`d(field)/dt = -v·grad(field) + D laplacian(field) + source`

It lowers into the existing scalar stencil IR and computes explicit advection-CFL and forward-Euler diffusion timestep limits. This is useful solver generation, but it is **not** presented as a universal symbolic PDE solver.

## Native verification modes

Representative native checks include:

```bash
cd apps/VulkaxPhysicsStudioMac
swift build -c release

.build/release/VulkaxPhysicsStudioMac --native-gpu-smoke
.build/release/VulkaxPhysicsStudioMac --native-volume-gpu-smoke
.build/release/VulkaxPhysicsStudioMac \
  --native-imported-mesh-gpu-smoke ../../tests/fixtures/airflow_cube.obj
.build/release/VulkaxPhysicsStudioMac \
  --native-scene-mesh-gpu-smoke ../../tests/fixtures/triangle_pbr.gltf
.build/release/VulkaxPhysicsStudioMac \
  --native-scene-mesh-gpu-smoke ../../tests/fixtures/modelio_triangle.ply
.build/release/VulkaxPhysicsStudioMac --native-camera-track-smoke
.build/release/VulkaxPhysicsStudioMac --native-cinematic-capture-smoke
.build/release/VulkaxPhysicsStudioMac --native-dynamic-equation-project-gpu-smoke
```

Permanent CI also exercises Linux Vulkan/Lavapipe, native macOS Metal and Windows UCRT64 product builds. The direct Vulkan presenter can force validation layers in Release CI and is exercised across Wave, Schwarzschild, Kerr and Volume modes.

## Reproducibility and claim boundaries

Vulkax keeps four concepts separate:

- **numerical state** — values produced by the solver,
- **visual representation** — transfer function / material / ray-marched interpretation,
- **scene geometry** — visual assets and simulation proxies,
- **camera/capture** — framing and encoding.

An attractive 4K frame is not evidence of numerical accuracy. A low numerical residual is not evidence of film-production convergence. Research outputs should continue to distinguish analytical reference, numerical error, cross-backend agreement and visual/image-space metrics.

## Current limitations

The Studio is a research/engineering editor rather than a production DCC package. Important current boundaries include:

- first-class glTF is static triangle geometry; skinning/morph/animation and sparse accessors are not implemented;
- compatibility formats through Model I/O are static-geometry fallback paths, not native material/animation importers;
- arbitrary visual topology may use a simplified simulation proxy;
- generated transport/diffusion covers a bounded explicit equation family, not arbitrary PDE inference;
- the direct Vulkan presentation boundary still wraps parts of the preserved LVE swapchain substrate internally;
- there is no production CFD/combustion, DNGR-equivalent convergence, distributed render farm or Blender/Unreal-class scene-authoring claim.

Those boundaries are intentional: visual polish and import convenience sit on top of testable numerical/runtime contracts rather than hiding uncertainty behind screenshots.
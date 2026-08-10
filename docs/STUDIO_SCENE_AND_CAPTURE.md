# Vulkax Physics Studio — Scene, Models, Media, Cameras and Capture

This guide describes the native macOS Studio workflow added on top of the numerical/GPU runtime. It deliberately separates **what the user sees** from **what the solver consumes** so imported assets, visualization choices and cinematic output do not silently change the underlying numerical model.

## Scene workspace

The native Studio uses a scene-first three-pane layout:

- **Scene**: visualization mode, inferred simulation medium, imported entities and camera presets.
- **Viewport**: the live Metal simulation/render view, camera interaction and timeline transport.
- **Inspector**: equation/solver controls, selected-object properties, camera settings and cinematic capture.

The editor keeps the simulation graph and the presentation state separate. Changing the camera does not modify the equation or simulation state; it resets only progressive visual accumulation where required.

## Equation-aware simulation medium

Vulkax can suggest the medium/domain on which an equation should be visualized. Current categories are:

- 2D surface
- 3D volume
- particle set
- rigid body
- vector field
- relativistic ray bundle
- trajectory / ODE
- abstract field

Inference returns a confidence score and human-readable reasons. **Auto is a recommendation, not a physical truth.** An equation alone may be compatible with several physical interpretations, so the Inspector always permits a manual override. The selected override is persisted in the `.vxp` project.

## Importing cars, props and other geometry

Use **Add Model** or drop an `.obj` file into the viewport. The imported object is retained as a **visual mesh** even when its topology is not suitable for numerical simulation.

This is important for real-world assets. Downloaded car, architectural and production models are often open, non-manifold, self-intersecting or unnecessarily dense. Feeding that topology directly into a voxel/contact solver is fragile. Vulkax therefore separates:

1. **Visual mesh** — the original imported geometry rendered by the Studio scene pass.
2. **Simulation proxy** — the closed geometry used by voxelization, collision/contact and fluid coupling.

If an OBJ is already a valid closed manifold, `Render Mesh` can be used directly as its proxy. Otherwise the default is a closed bounds proxy. The original car/prop remains the thing you see; the solver operates on the stable proxy.

### Scene entity roles

An entity can be assigned a role such as Visual, Collider, Fluid Obstacle, Source, Probe or Domain Surface. A visual-only entity can exist in a shot without participating in the equation. A simulated object consumes the same rigid transform in the renderer and the numerical runtime, so the visible model follows the body moved by the solver.

Current model import is intentionally conservative: **OBJ is the supported general scene format today**. glTF/GLB, FBX, materials and skeletal assets are future asset-pipeline work; the Studio does not claim support for them yet.

## Camera controls

The viewport camera is a real render camera rather than a UI-only orbit value. Current controls are:

- left drag — orbit
- Shift + left drag or right drag — pan
- scroll / trackpad magnify — dolly
- camera presets — Perspective, Front, Top, Isometric and Close-up
- Inspector — position, target, vertical FOV and exposure

Volume rendering consumes this camera directly. Visual scene meshes use the same camera projection, so imported models and simulated radiance stay registered in both the interactive viewport and cinematic capture.

## Camera keyframes

When the camera-track feature is enabled, use **Add Camera Key** in the Camera inspector at the current timeline time. Camera keys store position, target, up vector, FOV and exposure. The renderer uses deterministic smooth interpolation between keys; offline capture follows the same track.

A capture with no camera keys uses the camera pose that was active when Record was pressed.

## Deterministic 4K cinematic capture

The native Studio's capture path is an offline renderer, **not screen recording**.

Choose:

- 1920×1080 or 3840×2160 (4K UHD)
- 24, 30 or 60 fps
- duration
- the desired camera pose / camera track

Press **Record** and choose a `.mov` destination. During capture Vulkax:

1. snapshots the requested camera/camera-track state,
2. renders the simulation at the requested output resolution,
3. advances simulation time using an exact `1 / fps` timestep,
4. composites the visual scene-mesh pass using the same camera,
5. renders directly into Metal-backed CoreVideo pixel buffers,
6. writes HEVC video through `AVAssetWriter` with deterministic presentation timestamps.

The interactive viewport remains separate; capture does not simply stretch the on-screen image to 4K.

### Native verification modes

The repository contains deterministic checks for the relevant paths:

```bash
cd apps/VulkaxPhysicsStudioMac
swift build -c release

.build/release/VulkaxPhysicsStudioMac --native-gpu-smoke
.build/release/VulkaxPhysicsStudioMac --native-volume-gpu-smoke
.build/release/VulkaxPhysicsStudioMac \
  --native-imported-mesh-gpu-smoke ../../tests/fixtures/airflow_cube.obj
.build/release/VulkaxPhysicsStudioMac \
  --native-scene-mesh-gpu-smoke ../../tests/fixtures/airflow_cube.obj
.build/release/VulkaxPhysicsStudioMac --native-cinematic-capture-smoke
.build/release/VulkaxPhysicsStudioMac --native-camera-track-smoke
```

The cinematic writer smoke produces a short HEVC `.mov`, reopens it as an AV asset and verifies that the video track is actually 3840×2160.

## Reproducibility and claim boundaries

Vulkax separates four ideas that should not be conflated:

- **numerical state** — values produced by the solver,
- **visual representation** — transfer function / mesh / ray-marched interpretation,
- **scene geometry** — imported visual assets and their simulation proxies,
- **camera/capture** — how those results are framed and encoded.

An attractive 4K frame is not evidence of numerical accuracy. Likewise, a low numerical residual does not imply perceptual or film-production convergence. The research/benchmark paths record their measurement class explicitly and should continue to distinguish analytical reference, numerical error, cross-backend agreement and visual/image-space metrics.

## Current limitations

The Studio is a research/engineering editor rather than a production DCC package. Important current boundaries include:

- general imported scene assets: OBJ today;
- arbitrary visual topology may use a simplified simulation proxy;
- no claim of production CFD/combustion fidelity;
- no DNGR-equivalent Kerr production convergence claim;
- no distributed render farm or cloud scene service;
- no material/texture/skeletal asset pipeline comparable to Blender or Unreal yet.

Those limitations are intentional: imported models and cinematic output should sit on top of a testable numerical/runtime foundation rather than hide uncertainty behind visual polish.

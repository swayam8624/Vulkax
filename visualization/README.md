# VULKAX Visualization Layer

This directory is the presentation layer for the frozen VULKAX paper story. It is intentionally separated from the solver, experiment runners, thresholds, candidate generation, and frozen result ledgers.

**No AI-generated image is a scientific submission asset in this pipeline.** Paper-facing evidence figures are deterministic vector graphics generated from the frozen VULKAX result ledger. The cinematic "Reality Inspector" is a procedural Blender scene generated from code and is explicitly marked as a schematic storyboard unless a frame is driven by exported solver state.

## Scientific boundary

The visualization layer may read frozen evidence. It may not change experiments, thresholds, force amplitude, truth worlds, candidate grids, labels, or scientific decisions.

The immutable scientific snapshot remains:

```text
tag     paper-freeze-2026-09-21
commit  a9da8c0aa8689ebeea0d84baf95a74907659a837
ledger  research/results/VULKAX_FINAL_RESULTS_2026-09-21.json
```

`visualization/scripts/verify_frozen_evidence.py` fails closed if the headline values used by the graphics no longer match the frozen ledger. In a Git checkout it also resolves the immutable tag and checks those same values against the ledger stored at the tagged commit.

## One command

```bash
./visualization/render_all.sh
```

This writes deterministic SVG assets to:

```text
build/visualization/
```

The first three assets are:

```text
fig_information_frontier.svg
fig_evidence_story.svg
fig_reality_inspector_storyboard.svg
```

The information-frontier figure is paper evidence: it reads the fresh force-compliance result directly and visualizes the frozen `|z|=2` reference, DCS median/max, force-compliance median/max, the 11.46× median information gain, and the fact that all proposals remained unresolved.

The evidence-story figure is a compact paper/slide summary of D1, D2, D3, D4V, OFC, and GAUGE.

The Reality Inspector figure is deliberately labelled a **schematic storyboard**. Its mechanism drawing is explanatory, not a fabricated measurement. Every number printed on it comes from the frozen result ledger.

## Procedural cinematic scene

If Blender is installed:

```bash
./visualization/render_all.sh --blender
```

This runs `visualization/blender/reality_inspector_scene.py`.

The scene uses only procedural geometry and materials: no downloaded mesh, no generated image, and no hidden asset dependency. It creates the visual language for the eventual SIGGRAPH-style video: a captured object, an apparently improved repair, a physical probe, ghosted counterfactual response, a scanning plane, and the `REFUSE` outcome.

The current Blender scene is intentionally a storyboard scene. It must remain labelled `SCHEMATIC` in publication/video output until its animated geometry is replaced or driven by exported VULKAX solver states. A cinematic explanatory deformation must never be mistaken for measured or simulated evidence.

## Planned evidence-driven cinematic upgrade

The next visual integration point is the existing run output:

```text
build/dcs-d4v-discovery/proposals.csv
build/orthogonal-force-compliance/proposals.csv
build/gauge-dcs-retrospective/per_trial.csv
```

The final cinematic renderer should select a representative case deterministically, export the actual baseline/repair/truth state trajectories from VULKAX, and feed those trajectories into the same Blender scene. At that point the geometry motion can be labelled `solver output` rather than `schematic`.

Until then, use the vector figures as scientific evidence and the Reality Inspector only as an explanatory teaser/storyboard.

## Figure classes

`visualization/manifest.json` records whether each surface is `paper_evidence`, `paper_summary`, or `schematic_storyboard`. Any future asset must declare one of these roles before being treated as publication-ready.

The governing principle is simple:

> make the presentation spectacular without allowing presentation code to manufacture evidence.

## Solver-state-driven Reality Inspector

The schematic layer now has a strict upgrade path to **actual solver motion**.

Build and replay the exact frozen APIC → PIC hero case:

```bash
cmake -S . -B build-vis -DCMAKE_BUILD_TYPE=Release -DVULKAX_BUILD_TESTS=OFF
cmake --build build-vis --target vulkax_visualization_trajectory_probe --parallel

./build-vis/vulkax_visualization_trajectory_probe build/visualization-trajectory

python3 visualization/scripts/render_solver_trajectory.py \
  --trajectory-dir build/visualization-trajectory \
  --out build/visualization
```

The exporter refits the two candidates with the original frozen calibration procedure and **fails closed** unless it reproduces the exact held-out and untouched-target errors stored for truth world 5. Only after that guard passes does it export synchronized particle trajectories for the four frozen 40 N force directions.

The exported state is explicitly:

```text
raw_solver_state_before_synthetic_observation_noise
```

so the cinematic renderer can distinguish physical solver state from the noisy observation model used to compute experimental statistics.

Generated evidence-facing surface:

```text
build/visualization/fig_solver_state_force_trajectories.svg
```

It overlays truth APIC, fitted baseline APIC and fitted repair PIC particle states and top-layer centroid trails for all four force directions. No deformation is hand-authored in this figure.

### Mechanism X-ray and Blender animation

Natural-scale particle motion is intentionally tiny, so an additional X-ray surface renders **candidate-minus-truth residual vectors with an explicit display magnification**:

```bash
python3 visualization/scripts/render_solver_residual_xray.py \
  --trajectory-dir build/visualization-trajectory \
  --out build/visualization
```

Default display magnification is `700×`. The raw numerical values printed beside it remain unscaled.

The same raw trajectory package can drive a procedural Blender scene:

```bash
blender -b -P visualization/blender/solver_trajectory_scene.py -- \
  --trajectory-dir build/visualization-trajectory \
  --direction px \
  --motion-scale 400 \
  --output build/visualization/vulkax_solver_reality_inspector
```

Add `--render` after the Blender-script separator arguments to render H.264. The in-scene title permanently states the display motion magnification so a presentation render cannot be mistaken for natural-scale deformation.

## One-command full local visual build

On macOS, Blender does **not** need to be on shell `PATH`. Both visualization entrypoints automatically detect the standard app bundle:

```text
/Applications/Blender.app/Contents/MacOS/Blender
```

To generate every current visual surface and all four solver-driven Blender scenes:

```bash
./visualization/run_full_visualization.sh
```

To additionally render the four solver-driven H.264 videos:

```bash
./visualization/run_full_visualization.sh --render-videos
```

Optional overrides:

```bash
export BLENDER_BIN="/Applications/Blender.app/Contents/MacOS/Blender"
export VULKAX_MOTION_SCALE=400
./visualization/run_full_visualization.sh
```

## Aesthetic paper hero — Stanford Bunny + real VULKAX field

The primary paper-facing 3D scene now uses the Stanford Bunny as a **visualization carrier**. It is not claimed to be the frozen benchmark object. The surface is warped by trilinear interpolation of the exported 4x4x4 VULKAX MPM displacement field for truth APIC, baseline APIC and repair PIC.

The asset is downloaded from the Stanford 3D Scanning Repository by:

```bash
./visualization/assets/fetch_stanford_bunny.sh
```

The download is cached under `build/assets/stanford-bunny/`; an attribution file and SHA-256 are written beside it. Publication images must credit the Stanford Computer Graphics Laboratory.

The default full build renders a 2560x1440 hero still:

```text
build/visualization/vulkax_bunny_hero_px.png
build/visualization/vulkax_bunny_hero_px.blend
```

The composition is:

```text
01 OBSERVE      baseline APIC as scanned/wireframe state + solver lattice
02 REPAIR       polished PIC candidate that improves ordinary held-out error
03 INTERROGATE  PIC repair + truth ghost + residual field + physical probe
```

All displayed deformation is solver-driven. `VULKAX_MOTION_SCALE` is only a declared display magnification.

For highest-quality stills on Apple Silicon, after the Eevee preview succeeds:

```bash
VULKAX_AESTHETIC_ENGINE=cycles \
VULKAX_MOTION_SCALE=400 \
./visualization/run_full_visualization.sh
```

The Blender script requests Metal for Cycles when available.


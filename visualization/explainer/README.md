# Reality Probe — a scientific explainer

A 116-second silent film about deceptive physical repair and information-limited
verification. The film is designed to stand on its own without narration.

## Chosen medium

A deterministic Python vector scene engine renders both editable SVG figures and
antialiased movie frames. Pillow supplies typography/rasterization; NumPy handles
read-only interpolation; FFmpeg encodes H.264. These tools were already available
on this machine. There are no new repository dependencies or changes to CMake,
solver code, experiment runners, thresholds, or frozen results.

This medium gives exact control over mathematical correspondences, repeatable
rendering, editable publication assets, and fast preview iteration. The main
physical object is **the actual 4 × 4 × 4 MPM solver body**, with a rest-coordinate
grid. No Stanford Bunny is used in this film; there is no substitute benchmark
geometry or public mesh attribution requirement. The earlier cinematic plates
and film are not reused.

## Deliverables

The default output directory is `build/reality-probe-explainer/`:

- `reality_probe_explainer.mp4`: 2560 × 1440, 30 fps, 116 s, silent, H.264.
- `reality_probe_preview.mp4`: 960 × 540, 15 fps, same complete story.
- `index.html`: local chapter player; open in a browser.
- `figures/`: twelve editable SVGs and matching 1920 × 1080 PNGs.
- `storyboard.png`: a twelve-frame review sheet.
- `source-data/`: exact copies of the ledger, proposal table, hero row, and replay.
- `editable-source/`: renderer, configuration, player, and tests.
- `provenance.json`: input SHA-256 hashes and presentation transformations.
- `chapters.json`: exact scene start times.
- `narration.srt`: optional timed narration/caption script.
- `VALIDATION.md`: executed checks and limitations.

The original source is `visualization/explainer/`. It must run inside this checkout
(or a copy retaining the repository data paths). The exported editable source also
finds the checkout by walking its parent directories. The movies, SVGs, PNGs, and
HTML player are independently portable. Fonts are not redistributed.

## Storyboard

| Time | Scientific purpose | Visual reasoning / source |
|---|---|---|
| 0–20 s | Define deceptive repair | Observation links (schematic), solver rest body, real hero ordinary/target error bars |
| 20–31 s | Locate the failure in the population | All 36 proposals move to their measured positions; selected hero is identified |
| 31–49 s | Ask an unconstrained physical question | Actual +X replay, two material grids, truth/repair overlay; grid shrinks into the first response column |
| 49–64 s | Build a mechanism fingerprint | Four actual probe replays and six-component centroid response columns; grids expand for comparison |
| 64–77 s | Isolate disagreement spatially | Trilinear residual field, common four-direction normalization, independently verified raw response RMS |
| 77–85 s | Explain mechanism selection | Clearly schematic second difference cancels constant and linear trends |
| 85–96 s | Explain standardized evidence | Error improvement over combined uncertainty; DCS median moves to force median |
| 96–109 s | Reveal the information limit | Evidence axis expands to the fixed threshold; all 36 evidence magnitudes, medians, maximum, selected hero, unresolved outcome |
| 109–116 s | State the research conclusion | Appearance agreement does not imply physical verification |

Transitions combine meaningful motion (deforming, overlaying, contracting into
columns, expanding, changing an evidence axis) with brief pauses. Short dissolves
are used at changes of representation. No rotating camera, particles, glow,
pedestals, or cinematic audio.

## Frozen data and interpretation

Scientific freeze: `paper-freeze-2026-09-21`, commit
`a9da8c0aa8689ebeea0d84baf95a74907659a837`.

Inputs:

- `research/results/VULKAX_FINAL_RESULTS_2026-09-21.json`
- `visualization/data/ofc_proposals_visualization_2026-09-21.csv`
- `visualization/data/hero_case_ofc_2026-09-21.json`
- `visualization/data/OFC_VISUALIZATION_DATA.md`
- `research/benchmarks/ORTHOGONAL_FORCE_COMPLIANCE_PROTOCOL_2026-09-21.md`
- `visualization/probes/frozen_hero_trajectory_probe.cpp`
- fresh replay under `build/reality-probe-explainer/replay/`

The experiment is **synthetic**. Its 36 proposals contain **12 deceptive and 24
beneficial repairs**. This is the fresh OFC experiment, not the earlier D4V
population. The post-hoc hero selection chooses the most negative force-progress
z among deceptive proposals: truth world 5, APIC baseline → PIC repair. This is
not an assertion that PIC is generally worse than APIC.

The ordinary metric is held-out kinematic RMS error. The untouched target is a
stronger, unused kinematic target, not the force response. The proposal is selected
by ordinary error before hidden target labels and force evidence are consulted.
No causal claim about a particular transfer scheme is invented.

## Equations and transformations

The deception map uses the documented exact percentage transformations:

```text
ordinary improvement = 100 (baseline_holdout − repair_holdout) / baseline_holdout
target change         = 100 (repair_target − baseline_target) / baseline_target
```

The rest grid samples the exported displacement using trilinear interpolation.
Particle IDs are x fastest, then y, then z. Solver x is screen horizontal; y is
screen vertical; z is a fixed oblique depth projection. Display coordinates are:

```text
screen_x = center_x + size (solver_x + 0.32 solver_z)
screen_y = center_y − size (solver_y + 0.16 solver_z)
display_state = rest + 400 (solver_state − rest)
```

The **400× magnification is display only**. Intermediate video frames linearly
interpolate adjacent synchronized solver snapshots; they do not resimulate or
retune physics. The bottom remains fixed. The four forces remain 40 N **per top
particle** in +X, −X, +Y, +Z. Opposite X probes are not independent basis axes.

The six-component response is `[top_dx, top_dy, top_dz, inner_dx, inner_dy,
inner_dz]`. Response-column animation uses actual interpolated centroid responses.
The fingerprint notation `Φ = [r(+X) r(−X) r(+Y) r(+Z)]` is an explanatory arrangement
of the existing response bundle, not an invented dense linear compliance model.

The field is `D(x,p) = ||u_repair(x,p) − u_truth(x,p)||`. All four panels share the
maximum final per-particle residual over all four directions. The surface is a
front-face trilinear presentation; the reported raw RMS is computed over the six
compliance components. This spatial field is **not** the DCS statistic.

The DCS second-difference diagram is explicitly **schematic**. It illustrates the
order-2 moment-annihilation idea: weights `[1, −2, 1]` cancel constants and linear
terms. The implemented comparator uses an optimized order-2 stencil on a 3×3
intervention lattice. The diagram is not a measured response or the actual chosen
stencil. No second-difference values are reported as experimental evidence.

The force statistic is exactly:

```text
z_force = [e(B,Y) − e(R,Y)] /
          sqrt(sigma_meas² + sigma_repeat² + sigma_num,B² + sigma_num,R²)
```

Here `e` is bundle RMS; numerical scales compare nominal and half-dt bundles;
measurement and repeat scales are each frozen at 2e-5 m. The film's phrase
“combined uncertainty” abbreviates this denominator. DCS uses the same progress
structure with stencil-propagated observation uncertainty and witness-space
numerical refinement.

The evidence plot uses **absolute** z magnitudes, while the decision remains signed:
`z >= +2` supports; `z <= −2` vetoes; otherwise unresolved. All 36 remain unresolved.
The **11.4550585×** headline is the ratio of aggregate medians, not the median of
per-proposal ratios. The selected hero ratio is about **11.704349×**; the raw +X
response RMS ratio is about **12.0748×**. These three quantities are not conflated.
The hero signed force z is −1.3181063; the aggregate maximum magnitude is 1.3191113.
Stronger evidence does not establish prospective certification.

## Art direction

Typography: system Georgia for mathematical/editorial headings, Arial for labels;
Linux fallback uses system DejaVu. SVG text remains live/editable. Colors and title
are centralized in `reality_probe.json`: warm neutral `#f5f3ed`, ink `#24343b`, truth
cyan `#147f96`, repair orange `#cf6635`, restrained refusal red `#ae3c3b`.
Color is paired with labels and mark shapes. The deception map uses equal-size
circles for beneficial and diamonds for deceptive proposals. No sizes imply an
unlabeled metric. The scientific precision is retained in the packaged sources;
on-screen numbers are rounded for readability.

## Reproduce

Use an existing Python environment with NumPy and Pillow, plus FFmpeg on PATH.
On this machine the installed environment is:

```bash
FILM_PY=/Users/swayamsingal/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/bin/python3
```

One-command regeneration from the repository root:

```bash
bash visualization/explainer/render_explainer.sh final
```

Equivalent individual steps:

```bash
cmake --build build-vis --target vulkax_visualization_trajectory_probe --parallel 4
./build-vis/vulkax_visualization_trajectory_probe build/reality-probe-explainer/replay
"$FILM_PY" visualization/explainer/render.py --mode check
"$FILM_PY" visualization/explainer/test_explainer.py
"$FILM_PY" visualization/explainer/render.py --mode stills
"$FILM_PY" visualization/explainer/render.py --mode preview
"$FILM_PY" visualization/explainer/render.py --mode final
cp visualization/explainer/review.html build/reality-probe-explainer/index.html
cp visualization/explainer/README.md build/reality-probe-explainer/README.md
```

`--width`, `--fps`, `--start`, and `--end` permit short test exports. For a fresh
checkout, configure `build-vis` using the command in `visualization/README.md`.
The renderer refuses mismatched frozen medians/maxima, labels/counts, hero
selection, missing states, force protocol drift, unfixed bottom particles, or
inconsistent response summaries. Its tests additionally verify interpolation and
check every half-second SVG for finite geometry and title-safe text.

The final movie is encoded from 3200×1800 antialiased frames to 2560×1440 at 30 fps,
CRF 17, yuv420p, fast-start MP4. No AI-generated pixels are used.

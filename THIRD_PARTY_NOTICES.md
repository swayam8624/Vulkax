# Third-Party Notices

This file records third-party datasets and assets referenced or fetched by the
Reality Probe / VULKAX repository. The project license in `LICENSE` applies to
original project code, documentation, and project-authored visualization source.
It does not replace the licenses or usage terms of third-party material.

## Stanford Bunny

- Source: Stanford University Computer Graphics Laboratory, Stanford 3D Scanning Repository
- Repository page: https://graphics.stanford.edu/data/3Dscanrep/
- Fetcher: `visualization/assets/fetch_stanford_bunny.sh`
- Repository status: fetched into `build/assets/stanford-bunny/`; the source mesh is not part of the core project license
- Project use: publication and research visualization carrier only
- Required paper framing: the bunny is not the frozen benchmark geometry; its displayed deformation is driven by the exported VULKAX solver displacement field
- Attribution: publication images using the bunny must credit the Stanford Computer Graphics Laboratory
- Distribution note: the Stanford repository terms, not Apache-2.0, govern the mesh

## Poly Haven studio_small_03_1k

- Source: Poly Haven
- Asset: `studio_small_03_1k`
- Author recorded in the lock file: Greg Zaal
- Source page: https://polyhaven.com/a/studio_small_03
- License: CC0 1.0
- Lock file: `assets/demo/showcase_assets.lock.json`
- Repository status: fetched into `build/demo-assets/`; not committed as research evidence
- Project use: deterministic presentation environment only, not physical evidence

## DOT Deformable Object Tracking Dataset, sequence C2

- Dataset: Deformable Object Tracking Dataset (DOT)
- Dataset DOI: `10.13021/ORC2020/XXLVXM`
- Selected file PID: `doi:10.13021/orc2020/XXLVXM/ZVZHVR`
- Selected archive: `C02.zip`
- License recorded by the benchmark provenance: CC0 1.0
- Importer: `scripts/import_dot_c2.py`
- Repository status: the source archive is downloaded and checksum-verified during reproduction; it is not committed
- Project use: measured-source benchmark
- Important boundary: VULKAX adds explicit derived/model-proxy quantities and does not relabel them as DOT measurements

## GAUGE Dataset

- Dataset: GAUGE: A Measurement-Grounded Benchmark for Physical Fidelity in Simulation Engines and Video World Models
- Public dataset page: https://huggingface.co/datasets/InternRobotics/GAUGE-Dataset
- Fetcher: `research/scripts/fetch_gauge_foam_subset.py`
- Dataset license displayed by the public dataset at the time of the frozen audit: MIT
- Repository status: fetched for measured retrospective analysis; not bundled into the repository
- Project use: foam stretching/compression/shearing measured trajectories and metadata
- Redistribution note: re-check asset-level terms before redistributing downloaded GAUGE files

## Wikimedia Commons development videos

These sources are retained for post-1.0 development and are not part of the frozen
Reality Probe paper evidence. They are fetched by
`scripts/fetch_reality_video_assets.py` and governed by their source licenses.

| Asset | Attribution | License |
|---|---|---|
| Bouncing Ball | ScienceCOLA | CC-BY-3.0 |
| Bouncy ball 240fps | GolhaMedia | CC-BY-SA-4.0 |
| Exercise-ball rebound video | Rhetos | CC-BY-SA-4.0 |

The exact source pages, checksums, dimensions, and intended evidence scopes are
recorded in `assets/reality/video_sources.lock.json`.

## Fonts and local software

The deterministic explainer uses system fonts and does not redistribute font files.
Blender, FFmpeg, Python, NumPy, Pillow, Vulkan/Metal tooling, and other external
software remain governed by their respective upstream licenses.

## Publication checklist

Before a public paper, project page, supplementary archive, or ACM Digital Library
deposit:

1. keep Stanford Bunny attribution in any figure that uses it;
2. do not imply that the bunny is benchmark geometry;
3. keep DOT and GAUGE dataset citations and license boundaries explicit;
4. preserve CC-BY and CC-BY-SA attribution for any Wikimedia footage that is ever included;
5. distinguish fetched presentation assets from research evidence;
6. include this notice file, or equivalent attribution text, with redistributed project bundles.

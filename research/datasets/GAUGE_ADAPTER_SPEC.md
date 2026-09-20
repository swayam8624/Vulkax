# GAUGE → Vulkax deformable evidence adapter

Public GAUGE release inspected 2026-09-20.

The dataset currently exposes separate `metadata/deformable/*.json` task descriptions and `data/deformable/<task>/json/<material>/<trial>.json` measured trajectories.

For foam stretching/compression/shearing, the public metadata records two foam materials. The observed metadata reports soft foam mass 0.0066 kg, density 13.2 kg/m³, Young's modulus 86410 Pa, Poisson ratio 0.23; hard foam mass 0.0131 kg, density 26.2 kg/m³, Young's modulus 99372 Pa, Poisson ratio 0.34. Trials report 30 FPS, translation unit `mm`, a driven base trajectory and per-marker XYZ trajectories.

The adapter:
- performs no network requests;
- hashes the supplied source files;
- converts trajectory translations to metres explicitly;
- emits `markers.csv`, optional `driver.csv`, and `manifest.json`;
- labels provenance as `measured`;
- preserves measured material metadata instead of turning it into fitted Vulkax truth.

Primary first experiments:
1. foam stretching as fit/calibration evidence;
2. foam compression/shearing as **different physical interventions** for transfer testing;
3. compare Vulkax inferred material parameters with measured GAUGE metadata;
4. test whether a refusal/repair policy learned synthetically remains sensible on repeated real trials.

Source dataset: https://huggingface.co/datasets/InternRobotics/GAUGE-Dataset
License displayed by dataset: MIT. Re-check asset-level terms before redistribution.

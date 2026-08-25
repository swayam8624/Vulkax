# Vulkax 0.39 — captured observation robustness

Vulkax 0.39 adds a deterministic **synthetic observation-uncertainty stress harness** around the captured deformable material-identification and Operator Influence pipeline.

This milestone does **not** claim real-capture robustness. It establishes the experiment and evidence path that measured data can later use without changing the analysis logic.

## What is perturbed

The harness distinguishes two observation-error classes:

- **initial-position noise** applies only to `t = 0` marker observations and therefore stresses captured-pose initialization / correspondence propagation;
- **dynamic-position noise** applies only to `t > 0` observations and therefore stresses material candidate ranking and held-out replay evidence.

Noise is component-wise, bounded, zero-mean and generated from a SplitMix64-derived deterministic hash. The requested scale is the population RMS scale of each Cartesian component. The hash-based construction avoids standard-library-dependent random-distribution behavior and is reproducible across supported platforms.

The physical grid is constructed once from the clean bundle and reused by every perturbed scenario. A robustness result therefore does not conflate observation perturbation with a changing MPM discretization.

## Evidence produced per scenario

Every scenario reruns material calibration and the downstream material-influence analysis and records:

- selected Young's modulus and Poisson ratio;
- Young's-modulus relative drift and Poisson-ratio absolute drift from the clean baseline;
- dynamic fit RMS and held-out validation RMS;
- initialization-fit RMS and Gaussian appearance round-trip RMS;
- particle-adjoint cosine similarity against the clean field;
- particle-adjoint relative L2 field error;
- strongest stable particle ID and whether it matches the clean baseline;
- minimum quadratic B-spline stencil-knot margin;
- adaptive region count and particle count;
- adaptive retained absolute-gradient fraction;
- adaptive selected-particle Jaccard overlap against the clean proposal.

The validation split is never used to rank material candidates.

## Public command

Generate the controlled bundle first, then run:

```bash
./build/vulkax captured-deformable-generate-example build/captured-example

./build/vulkax captured-observation-robustness \
  build/captured-example/object.ply \
  build/captured-example/particles.csv \
  build/captured-example/observations.csv \
  build/captured-robustness \
  m4 0.003 1 1 1 \
  1e-6 1e-6 12345 1e-4 0.08
```

Arguments after the objective direction are:

```text
maximum initial-position component RMS noise
maximum dynamic-position component RMS noise
base seed
timestep
grid cell size
```

The command generates five deterministic non-clean scenarios in addition to the baseline:

```text
pose_half
pose_full
dynamic_half
dynamic_full
combined
```

It writes:

```text
robustness.csv
scenarios.csv
```

`scenarios.csv` preserves the exact perturbation scales and derived seeds. `robustness.csv` preserves the inference and influence metrics.

## Controlled regression result

For the deterministic 64-particle captured regression, using maximum initial and dynamic component-noise scales of `1e-6`, base seed `12345`, `dt = 1e-4`, and grid cell size `0.08`, the Linux public-path run measured:

```text
baseline Young's modulus                    15000 Pa
baseline Poisson ratio                      0.30
maximum Young's-modulus relative drift      0
maximum Poisson-ratio absolute drift        0
minimum particle-influence cosine           0.9999999994
maximum particle-influence relative L2      0.0003332153535
strongest-particle stability                5 / 5 scenarios
minimum adaptive-particle Jaccard            1.0
```

Thus, **for this controlled synthetic dataset and this particular 1 micrometre component-noise stress only**, the discrete material grid selected the same `(E, nu)` candidate in all scenarios, while the particle-local influence field changed by at most about `0.0333%` in relative L2 norm. The strongest particle and adaptive selected-particle membership remained unchanged.

These numbers are regression evidence, not a physical measurement-noise tolerance claim. The useful uncertainty scale for a real object must come from that capture process and its measurement instrumentation.

## CI contract

The Linux end-to-end gate runs the same public command twice with identical arguments and requires:

- byte-identical `robustness.csv` outputs;
- byte-identical `scenarios.csv` outputs;
- exact clean-baseline material and influence identity;
- no numeric NaN or infinity tokens;
- at least one non-clean scenario to produce a nonzero particle-influence field change.

The complete C++ regression suite remains cross-platform and is required on Linux, macOS and Windows.

## What 0.39 does not establish

0.39 does not establish:

- real-camera or tracker noise robustness;
- robustness to correspondence-ID mistakes or marker swaps;
- robustness to missing observations or irregular timestamps;
- model-discrepancy robustness when the real object is not represented by the controlled Neo-Hookean APIC model;
- continuous material-parameter uncertainty beyond the current discrete candidate grid;
- general adjoints for FLIP blending, boundary clamps or arbitrary forcing.

Those limitations motivate the versioned capture/evidence contract and measured deformable benchmark in the 1.0 roadmap.

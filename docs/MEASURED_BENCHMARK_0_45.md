# Vulkax 0.45 — measured deformable benchmark

Vulkax 0.45 crosses the project boundary from deterministic synthetic regression to a reproducible pipeline driven by **real measured deformable-object motion**.

The benchmark uses the public **Deformable Object Tracking Dataset (DOT)**, sequence **C2**, published by Li et al. through the George Mason University Dataverse:

- dataset DOI: `10.13021/ORC2020/XXLVXM`
- related paper: *Textureless Deformable Object Tracking with Invisible Markers*, ICCP 2024
- selected archive persistent ID: `doi:10.13021/orc2020/XXLVXM/ZVZHVR`
- selected archive: `C02.zip`
- pinned MD5: `0f347c3f95ed2def9fd81ba5236955b1`
- dataset license: CC0 1.0

DOT provides real-world deformable-object videos, reconstructed geometry and tracked 2D/3D correspondences. Vulkax does **not** reinterpret all DOT fields as physical measurements. The importer records an explicit provenance class for every quantity introduced by the benchmark.

## What is measured and what is not

The distinction below is part of the benchmark contract.

| Quantity | Vulkax provenance class | Meaning |
|---|---|---|
| C2 3D correspondence positions | measured | DOT camera-coordinate tracked 3D geometry |
| frame 11 initialization positions | measured | measured state used as Vulkax `t=0` |
| frames 16 and 21 dynamic positions | measured | later measured checkpoints used for fit/validation |
| Gaussian center geometry | measured | centers are the frame-11 measured correspondences |
| millimetres-to-metres conversion | explicit conversion | source geometric values are converted by `0.001` before Vulkax ingestion |
| characteristic spacing | derived from measurement | median horizontal/vertical neighbour spacing in the reference grid |
| frame 1 as physical reference | model proxy | an earlier measured state, **not** asserted to be stress free |
| density | model proxy | `1000 kg/m^3`; DOT does not supply material density for C2 |
| thickness | model proxy | `0.1 ×` measured correspondence spacing; not a DOT thickness measurement |
| mass/rest volume | model proxy | derived from proxy density, thickness and measured spacing |
| Gaussian photometry | model proxy | neutral SH-DC appearance; no 3DGS photometric reconstruction claim |
| `0.26 mm` perturbation scale | literature proxy | stress-test scale based on the DOT paper's reported point-to-ray alignment experiment; not a per-C2 statistical sigma |
| Young's modulus / Poisson ratio | model-conditioned fit | parameters selected inside the Vulkax volumetric APIC proxy |
| material ground truth | unavailable | DOT C2 does not supply stress-free state, loads, thickness, mass or Young's modulus |

Accordingly, the selected material parameters below are **not measurements of the cloth's true constitutive properties**.

## Reproducible input construction

The benchmark downloads the pinned `C02.zip`, verifies its MD5, and imports 225 stable tracked points.

The selected timing is:

```text
DOT source rate              60 Hz
reference frame              1   (model proxy; earlier measured state)
initialization frame         11  (measured t=0)
dynamic frame                16  (83.333 ms after initialization)
dynamic frame                21  (166.667 ms after initialization)
```

The resulting Vulkax bundle contains:

```text
225 physical particles
225 measured Gaussian centers
675 marker observations
675 uncertainty-sidecar rows
585 fit observations
 90 held-out validation observations
```

At each nonzero-time checkpoint, 180 stable markers are assigned to fit and 45 to validation. All 225 initialization rows are fit rows because they define the captured initial pose.

The measured grid has characteristic spacing approximately:

```text
0.00499424151 m
```

Measured motion relative to frame 11 is nontrivial:

```text
frame 16 RMS displacement from frame 11 = 0.00246742697 m
frame 21 RMS displacement from frame 11 = 0.00531704040 m
```

## Numerical model used for this benchmark

Vulkax presently evaluates the DOT cloth through its existing zero-external-force volumetric APIC/MPM proxy. That is intentionally **not** presented as a validated cloth model.

Settings used by the reproducible workflow:

```text
transfer                       APIC
solver dt                      1 / 4800 s = 0.00020833333333333335 s
grid cell size                 0.012 m
source observation rate        60 Hz
APIC substeps / source frame   80
finite-difference scale step   0.01
rewrite scale delta            0.02
objective marker               dot_c2_113
objective time                 1 / 6 s
objective direction            +z
```

The first measured experiment attempted `dt = 1/1200 s` and produced an inverted deformation during calibration. That failed run was not hidden or converted into a passing threshold. The final benchmark tightens integration to `1/4800 s`; measured observation times remain unchanged.

## Calibration and held-out replay

The 28-candidate material grid is selected using **fit-split nonzero-time observations only**. Held-out validation is not used for candidate ranking.

Observed result from the reproducible Linux measured-source workflow:

```text
selected model-conditioned Young's modulus    7500 Pa
selected model-conditioned Poisson ratio      0.45
fit dynamic samples                            360
fit dynamic RMS                                0.004390821778 m
held-out validation samples                    90
held-out validation RMS                        0.004417317099 m
initialization affine-fit RMS                  0.0009106961364 m
appearance round-trip RMS                      5.703779315e-12 m
```

Interpretation:

- fit and held-out errors are close, so this particular split does not show an obvious fit/validation collapse;
- the approximately `4.4 mm` dynamic RMS error is large relative to the approximately `5.0 mm` measured neighbour spacing;
- the selected `nu = 0.45` lies at the high end of the tested grid;
- therefore these results are evidence about **the current Vulkax model's adequacy on the measured trajectory**, not evidence that the real cloth material has been recovered.

## Measured-source robustness stress test

The source trajectory is measured. The perturbations themselves are deterministic bounded synthetic stress tests around those measurements, using the literature-derived `0.26 mm` scale rather than a claimed per-sequence noise distribution.

Across five perturbed scenarios plus the clean baseline:

```text
maximum selected-E relative change          0
maximum selected-nu absolute change         0
minimum particle influence cosine           0.9984812323
maximum particle influence relative L2      0.1165661521
strongest-particle stability                5 / 5
minimum adaptive-particle Jaccard            0.9381443299
```

This supports the narrow statement that, under this deterministic stress scale, the selected grid candidate and strongest influence location are stable. It does **not** establish a calibrated sensor-noise model for C2.

## Operator Influence and adaptive proposal

Using the selected model-conditioned material candidate:

```text
reference regions                            6 rest-space octants
adaptive regions                             8
adaptive proposed particles                  182 / 225
adaptive retained absolute-gradient mass     0.9480125633
adaptive characteristic spacing              0.004787263416 m
adaptive adjacency radius                    0.005026626587 m
maximum reference derivative magnitude       0.000100253791
maximum reference nonlinear relative error   0.3409944885
maximum adjoint absolute derivative error    1.615077257e-07
maximum adjoint relative derivative error    0.002593474212
adaptive max nonlinear relative error         0.1319945905
adaptive max adjoint relative error           0.001007878174
```

The adjoint remains a proposal/analysis path. Finite difference and separate nonlinear reruns remain the verification oracles.

## Local rewrite verdict

For the explicit rewrite check, the workflow selects the rest-space reference region with the largest absolute finite-difference derivative. On the recorded run that is:

```text
region                     octant_3
particle count             31
reference derivative       0.00010025379101052945
baseline Young's modulus   7500 Pa
requested Young's modulus  7650 Pa
requested scale delta      +2%
```

The verified transaction result is:

```text
status                       rejected
nonlinear relative error     0.0994435887658
nonlinear tolerance          0.25
unaffected position drift    0
rollback performed           yes
```

The nonlinear counterfactual therefore satisfies the configured `25%` linearization-error limit. However, the central material verifier also requires the independent adjoint-vs-finite-difference derivative oracle to satisfy:

```text
absolute derivative error <= 1e-8
relative derivative error <= 5e-3 when |reference derivative| > 1e-7
```

The independent derivative oracle does not satisfy the complete verifier contract for this selected measured region, so the transaction is **rejected and rolled back**. This is a successful verification-system outcome: measured data produced a candidate rewrite, the independent evidence was insufficient, and Vulkax refused to commit it.

The permanent `scripts/validate_measured_dot_c2.py` validator recomputes this verdict from the physical counterfactual and derivative-comparison artifacts instead of trusting the transaction status string alone.

## Reproduction

The repository workflow `.github/workflows/measured-dot-c2.yml` is the canonical measured benchmark. Its source archive is pinned by DOI/file identity and checksum.

The same pipeline can be reproduced locally on a Linux/macOS checkout with network access to the public Dataverse archive:

```bash
mkdir -p build/dot-download
curl -L --fail --retry 3 \
  'https://dataverse.orc.gmu.edu/api/access/datafile/:persistentId/?persistentId=doi:10.13021/orc2020/XXLVXM/ZVZHVR' \
  -o build/dot-download/C02.zip

echo '0f347c3f95ed2def9fd81ba5236955b1  build/dot-download/C02.zip' | md5sum -c -

python3 scripts/import_dot_c2.py build/dot-download/C02.zip build/dot-c2
python3 scripts/validate_dot_c2_import.py build/dot-c2
```

Then follow the calibration / robustness / influence / rewrite commands in `.github/workflows/measured-dot-c2.yml`. Finish by validating the complete evidence tree:

```bash
python3 scripts/validate_measured_dot_c2.py \
  build/dot-c2 \
  build/dot-c2/measured_benchmark_summary.csv
```

CI uploads the resulting `build/dot-c2` evidence tree as an artifact. The downloaded source ZIP is not committed to the repository.

## What 0.45 does and does not establish

0.45 establishes that Vulkax can reproducibly take a real measured deformable trajectory through:

```text
pinned real source
    -> SI/provenance import
    -> capture-bundle validation
    -> fit-only material calibration
    -> held-out replay
    -> measured-source perturbation stress test
    -> finite-difference + adjoint influence
    -> adaptive proposal
    -> nonlinear + independent derivative verification
    -> commit or rollback verdict
```

0.45 does **not** establish:

- true cloth Young's modulus or Poisson ratio recovery;
- a certified stress-free rest state;
- measured density, thickness, mass or loads for DOT C2;
- a calibrated per-C2 uncertainty distribution;
- a native 3DGS reconstruction of C2 photometry;
- validated shell/cloth constitutive physics;
- successful commitment of the measured local rewrite.

The measured evidence currently supports the stronger engineering claim that Vulkax can expose model mismatch and can refuse a measured-data rewrite when its independent verification contract is not met.

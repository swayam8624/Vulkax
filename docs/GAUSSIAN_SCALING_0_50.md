# Vulkax 0.50 — scalable Gaussian execution evidence

Vulkax 0.50 moves Gaussian projection from a CPU-only reference implementation to native Vulkan and Metal execution while retaining the CPU path as the numerical oracle.

This milestone is intentionally narrower than a production 3DGS renderer. It establishes a measurable, cross-platform execution path and publishes its remaining CPU work instead of describing it as fully GPU resident.

## What executes natively

For Vulkan and Metal, the native projection path computes per-Gaussian screen-space data from the stored 3DGS representation, including:

- projected mean;
- projected covariance / anisotropic ellipse parameters;
- visibility and projected extent;
- tile bounds used by the scalable evidence path.

The projected stream is converted into the same raster representation used by the reference renderer. Vulkan and Metal then use their native Gaussian raster/compositing paths.

## What remains CPU-side

0.50 does **not** claim an end-to-end GPU radix-sort/tile-compositor pipeline.

The current implementation deliberately retains:

- stable far-to-near ordering of the returned visible splats on CPU;
- deterministic CSR-style tile-reference construction on CPU from native projected tile bounds;
- benchmark orchestration and CPU-oracle rendering on CPU.

This keeps the ordering semantics deterministic and independently inspectable while the native projection result is checked against the established oracle. GPU radix sorting and a fully GPU-resident per-tile compositor remain future renderer work rather than being implied by the 0.50 label.

## Public benchmark

A clean build exposes:

```bash
./build/vulkax_gaussian_scaling \
  examples/synthetic_gaussian.ply \
  build/gaussian-scaling.csv \
  Vulkan 64 256 3 16
```

On macOS, use `Metal` instead of `Vulkan`.

Arguments are:

```text
vulkax_gaussian_scaling <point_cloud.ply> <output.csv>
                        [Vulkan|Metal]
                        [min-splats]
                        [max-splats]
                        [levels]
                        [tile-size]
```

The source cloud is repeated deterministically to produce the requested splat counts. This is a controlled scaling regression; it is not a claim about the statistics or visual complexity of a production reconstruction.

## Machine-readable evidence schema

The CSV contains one row per requested splat count with these fields:

```text
input_splats
visible_splats
tile_references
max_splats_per_tile
projection_input_bytes
projection_output_bytes
tile_reference_bytes
cpu_projection_ms
native_projection_ms
scalable_total_ms
max_channel_difference
rmse
psnr_db
changed_pixel_fraction
used_native_projection
fallback_reason
```

Timing is reported as evidence. **No speedup threshold is used as a correctness gate.** Runner hardware and driver implementation matter, and the Linux CI path currently uses Mesa `llvmpipe`, a software Vulkan implementation.

## Image-agreement policy

`scripts/validate_gaussian_scaling.py` validates the CSV by column name rather than by positional shell parsing.

For the controlled 64/128/256-splat regression, the release gate requires:

- native projection was actually used;
- positive visible/tile/memory evidence;
- projection input bytes increase with splat count;
- all timing and global-error values are finite and non-negative;
- maximum individual channel difference `<= 3` byte values;
- normalized RGBA RMSE `< 1e-4`;
- changed-pixel fraction `< 1e-4`;
- positive infinity for PSNR is accepted only when RMSE is exactly zero.

The combined policy is intentional. A native floating-point implementation can produce a sparse quantization difference at an ellipse edge while remaining globally indistinguishable from the double-precision CPU oracle. The local ceiling therefore remains small while the two global limits are much stricter.

## Observed CI evidence

### Linux / Vulkan software path

The GitHub Linux runner exposes:

```text
Vulkan | llvmpipe (LLVM 20.1.2, 256 bits)
```

On the controlled sweep used to establish the 0.50 gate, representative evidence was:

| splats | tile refs | input bytes | max channel delta | RMSE | changed fraction |
|---:|---:|---:|---:|---:|---:|
| 64 | 12,478 | 4,096 | 3 | 1.82685e-5 | 1.30208e-5 |
| 128 | 24,871 | 8,192 | 1 | 7.07537e-6 | 4.34028e-6 |
| 256 | 49,778 | 16,384 | 0 | 0 | 0 |

Representative native projection times on that software Vulkan runner were about `1.1–1.4 ms`, compared with roughly `0.03–0.07 ms` for CPU projection on these tiny scenes. That is **not acceleration evidence**; it is expected to be dominated by software-driver and setup overhead. 0.50 therefore makes no Vulkan speedup claim from CI.

### macOS / Metal path

The macOS CI runner used an Apple Paravirtual Metal device. For the same controlled 64/128/256-splat sweep, CPU and scalable images were byte-identical in the observed run (`max_channel_difference = 0`, `RMSE = 0`, `changed_pixel_fraction = 0`). Native projection was used for every row.

These values are regression evidence for the tracked controlled scene and CI environment. They are not a performance characterization of Apple silicon generally.

## Fallback behavior

The library can report a projection fallback when the requested native projection path is unavailable. The public scaling benchmark is stricter: it refuses to emit a successful native benchmark when fallback was used, preserving the distinction between a CPU fallback and native evidence.

## Research-integrity boundary

0.50 supports the following claims:

- Vulkan and Metal execute Gaussian projection natively on the controlled path;
- the resulting rendered images are checked against the CPU numerical oracle;
- deterministic tile-reference and depth-order evidence is available;
- timing and memory scaling evidence is machine-readable;
- the tested 64/128/256-splat native paths satisfy the documented image-agreement policy in CI.

0.50 does **not** establish:

- a production-scale 3DGS renderer;
- GPU radix sorting;
- a fully GPU-resident tile bin/sort/composite pipeline;
- a universal performance advantage over the CPU path;
- real-capture rendering quality at large reconstruction scale.

The CPU oracle remains part of the codebase specifically so later acceleration work can continue to be checked against an independent reference.
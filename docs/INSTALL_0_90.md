# Vulkax 0.90 build and installation guide

This document records the supported source-build path used for Vulkax release hardening. Vulkax is currently built directly with CMake; it does not yet install a system package or SDK.

## Common requirements

- CMake 3.25 or newer.
- A C++20 compiler.
- Python 3 for evidence validators, benchmark importers and release-hardening scripts.
- Git when cloning the repository.

The core project can compile without Vulkan. Native Vulkan compute/render paths are enabled only when CMake finds Vulkan and `glslangValidator`. On Apple platforms, the Metal and Foundation frameworks are required and are linked by CMake automatically.

## macOS

Recommended toolchain:

- recent macOS with Xcode or Xcode Command Line Tools;
- AppleClang with C++20 support;
- CMake 3.25+;
- Python 3.

Configure, build and test:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DVULKAX_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Probe the native Metal backend:

```bash
./build/vulkax --require-backend Metal
./build/vulkax --conformance Metal
```

Run the release-facing captured-world CLI regression:

```bash
python3 scripts/test_release_cli_failures.py \
  --executable build/vulkax
```

## Linux

The normal CI release path uses Ubuntu 24.04 and installs:

```bash
sudo apt-get update
sudo apt-get install -y libvulkan-dev mesa-vulkan-drivers glslang-tools
```

A C++20 compiler, CMake 3.25+ and Python 3 are also required.

Configure, build and test:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DVULKAX_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Probe Vulkan and run compute conformance:

```bash
./build/vulkax --require-backend Vulkan
./build/vulkax --conformance Vulkan
```

Mesa `llvmpipe` is acceptable for CI correctness coverage. Its timing must not be presented as discrete-GPU performance evidence.

## Windows

Recommended toolchain:

- Windows 11 or a current supported Windows environment;
- Visual Studio 2022 Build Tools or Visual Studio 2022 with the Desktop development with C++ workload;
- CMake 3.25+;
- Python 3.

Configure, build and test with a multi-config generator:

```powershell
cmake -S . -B build -DVULKAX_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

The cross-platform captured-world research path can be exercised without requiring a native render backend:

```powershell
.\build\Release\vulkax.exe captured-deformable-generate-example build\captured-example
.\build\Release\vulkax.exe captured-world-run build\captured-example\capture.vkcap build\captured-world-run m4 0.003 1 1 1 none 0.08 0.01 0.02 12345
python scripts\validate_captured_world_run.py build\captured-world-run --expected-backend none --expected-rewrite verified --expected-showcase none
```

A Vulkan SDK/runtime may be installed separately when native Vulkan development is desired. The base Windows correctness gate does not require Vulkax to claim a native renderer.

## Deterministic controlled release benchmark

After a successful build, run the principal-path wall-clock report without native rendering:

```bash
python3 scripts/benchmark_captured_world_run.py \
  --executable build/vulkax \
  --iterations 3 \
  --backend none
```

On Windows with a multi-config build:

```powershell
python scripts\benchmark_captured_world_run.py --executable build\Release\vulkax.exe --iterations 3 --backend none
```

The benchmark writes a CSV and JSON summary. Timing is evidence-only and is not a correctness threshold because CI hosts and software GPU implementations are not performance-comparable.

## Showcase assets

The optional showcase path uses a pinned asset lock. Fetch and validate it explicitly:

```bash
python3 scripts/fetch_demo_assets.py \
  assets/demo/showcase_assets.lock.json \
  build/demo-assets
python3 scripts/validate_demo_assets.py \
  assets/demo/showcase_assets.lock.json \
  build/demo-assets
```

Asset download is intentionally not performed by CMake configure/build.

## Release-hardening checks

Run the non-hardware-specific hardening checks from the repository root:

```bash
python3 scripts/validate_evidence_registry.py .
python3 scripts/audit_release_claims.py .
```

Then run the normal CTest suite and the CLI failure regression. Exact release candidates must still pass the repository's full GitHub Actions OS matrix before version advancement.

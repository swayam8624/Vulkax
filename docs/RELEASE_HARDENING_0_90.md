# Vulkax 0.90 release hardening

Vulkax 0.90 does not expand the research thesis. It hardens the implemented 0.80 captured-world path so the eventual 1.0 baseline can be released without stale claims, ambiguous CLI behavior or unversioned evidence contracts.

The frozen 0.90 behavior/evidence candidate was `dddaaae7008520fff7494e52700758dd280dfe23`. It remained versioned as `0.80.0` until the complete behavior gate was green. The release head is therefore allowed to advance to `0.90.0`, but it must pass the same gates again on its exact SHA before merge to `main`.

## Scope

The roadmap deliverables for 0.90 are:

1. warning/error cleanup in code touched by the 1.0 path;
2. CLI help and validation consistency;
3. reproducible benchmark scripts;
4. versioned evidence schemas;
5. documentation audit against actual implementation;
6. performance report for the principal path;
7. failure-case tests;
8. installation/build instructions for macOS, Linux and Windows.

No 0.90 hardening change is allowed to reinterpret the 0.45 measured result, turn a rejected rewrite into a pass, remove the finite-difference/nonlinear oracle, or claim physical HDRI relighting that the Gaussian renderer does not implement.

## CLI hardening

`captured-world-run` exposes a dedicated `--help` path and validates release-facing argument errors before launching expensive research work where possible.

The failure regression covers:

- missing required arguments;
- unknown render backend;
- zero objective direction;
- unknown showcase preset;
- showcase-specific options without `--showcase`;
- showcase requested while rendering is disabled;
- reuse of a non-empty output directory.

Run:

```bash
python3 scripts/test_release_cli_failures.py --executable build/vulkax
```

On a multi-config Windows build use `build/Release/vulkax.exe`.

## Evidence schema registry

`schemas/evidence_registry.json` records the release-facing schema identifiers and versions that must remain compatible with their validators:

- captured bundle: `vulkax_capture`, version 1;
- captured-world certificate: `vulkax_captured_world_run`, version 2;
- showcase manifest: `vulkax_showcase`, version 1;
- showcase asset lock: `vulkax_showcase_assets`, version 1.

Run:

```bash
python3 scripts/validate_evidence_registry.py .
```

The registry does not replace the artifact-specific validators. It prevents release documentation and validator code from silently drifting to different schema identities.

## Principal-path performance report

The controlled principal path is benchmarked with the deterministic captured-deformable fixture:

```bash
python3 scripts/benchmark_captured_world_run.py \
  --executable build/vulkax \
  --iterations 3 \
  --backend none
```

The default benchmark disables native rendering so the report can run on Linux, macOS and Windows without conflating orchestration cost with host-specific GPU availability. Optional native backend/showcase runs can be recorded separately.

Outputs:

- `build/release-hardening-performance/captured_world_performance.csv`;
- `build/release-hardening-performance/captured_world_performance_summary.json`;
- one complete evidence directory per iteration.

The JSON summary uses schema `vulkax_captured_world_performance` version 1. Wall-clock timing is evidence-only. There is no pass/fail speed threshold because GitHub-hosted runners, Mesa software Vulkan and developer hardware are not performance-comparable.

The frozen behavior candidate produced successful one-iteration hosted-runner observations on Linux, macOS ARM and Windows. See `docs/PERFORMANCE_0_90.md` for the exact values and interpretation boundary.

## Documentation audit

Run:

```bash
python3 scripts/audit_release_claims.py .
```

For the 0.90 release head the audit requires:

- CMake project version `0.90.0`;
- README current implementation label `Vulkax 0.90`;
- the implemented 0.80 one-command path to remain documented accurately;
- the completed 0.90 hardening milestone to be removed from the remaining-milestone list;
- 0.90 install, schema, benchmark, performance-report and failure-test artifacts to exist;
- the roadmap to mark 0.80 and 0.90 implemented while retaining the 1.0 boundary.

To reproduce the historical pre-bump behavior-candidate audit explicitly, use:

```bash
python3 scripts/audit_release_claims.py . --expected-project-version 0.80.0
```

## Build instructions

See `docs/INSTALL_0_90.md` for platform-specific source-build, backend-probe, test and release-hardening commands.

## Warning cleanup policy

Warnings on the principal 1.0 path are fixed when the correction is local and semantics-preserving. 0.90 does not hide correctness problems by globally disabling diagnostics. Platform API aggregate-initialization diagnostics are handled locally only where required fields are explicitly initialized or zero-initialized according to the API contract.

The release matrix remains compiled with the repository's normal warning levels (`/W4` on MSVC; `-Wall -Wextra -Wpedantic -Wconversion -Wshadow` on non-MSVC for `vulkax_core`). Dense legacy numerical kernels are not rewritten solely to erase benign diagnostics when doing so would introduce disproportionate numerical risk.

## Behavior-candidate acceptance

The frozen behavior/evidence candidate `dddaaae7008520fff7494e52700758dd280dfe23` satisfied:

- normal Linux, macOS and Windows CI;
- release claim audit and evidence registry validation;
- Linux, macOS and Windows CLI failure regression;
- Linux, macOS and Windows controlled performance evidence generation;
- controlled captured-world one-command demo on Vulkan, Metal and Windows no-render;
- canonical DOT C2 measured benchmark;
- measured DOT one-command Vulkan cloth showcase.

That candidate therefore authorized the `0.80.0 -> 0.90.0` version advance. It did **not** authorize skipping a second full check after the metadata/documentation change.

## Release-head exit gate

0.90 is mergeable only when all of the following hold on the exact `0.90.0` release head:

- normal CTest/CI suite green on Linux, macOS and Windows;
- release claim audit green;
- evidence registry validation green;
- CLI failure regression green on Linux, macOS and Windows;
- controlled performance evidence produced successfully on Linux, macOS and Windows;
- captured-world one-command CI green on Vulkan, Metal and Windows no-render;
- canonical DOT C2 measured benchmark green;
- measured DOT one-command showcase green;
- no README claim depends on an unimplemented core mechanism.

Only after the exact release head satisfies these gates should `feat/release-hardening-0-90` merge to `main`.

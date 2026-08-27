# Vulkax 0.90 release hardening

Vulkax 0.90 does not expand the research thesis. It hardens the already implemented 0.80 captured-world path so the eventual 1.0 baseline can be released without stale claims, ambiguous CLI behavior or unversioned evidence contracts.

The 0.90 behavior candidate intentionally remains versioned as `0.80.0` until the exact candidate passes CI. Only then should `CMakeLists.txt` advance to `0.90.0`, followed by a second exact-head CI pass.

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

The registry does not replace the artifact-specific validators. It prevents the release documentation and validator code from silently drifting to different schema identities.

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

## Documentation audit

Run:

```bash
python3 scripts/audit_release_claims.py .
```

The audit checks that:

- the CMake project version matches the expected pre-release version;
- README identifies the currently released implementation as 0.80 while 0.90 is still a candidate;
- the merged 0.80 one-command path is documented as implemented rather than future work;
- stale 0.70 release labels are removed;
- 0.90 install, schema, benchmark and failure-test artifacts exist;
- the roadmap still contains the release-hardening and 1.0 boundaries.

For the final 0.90 release-head recheck, invoke the audit with:

```bash
python3 scripts/audit_release_claims.py . --expected-project-version 0.90.0
```

## Build instructions

See `docs/INSTALL_0_90.md` for platform-specific source-build, backend-probe, test and release-hardening commands.

## Warning cleanup policy

Warnings on the principal 1.0 path should be fixed when the correction is local and semantics-preserving. 0.90 must not hide a correctness problem by globally disabling diagnostics. Platform API aggregate-initialization diagnostics may be handled locally only when all required fields are explicitly initialized or zero-initialized according to the API contract.

The release matrix remains compiled with the repository's normal warning levels (`/W4` on MSVC; `-Wall -Wextra -Wpedantic -Wconversion -Wshadow` on non-MSVC for `vulkax_core`).

## Exit gate

0.90 is ready for version advancement only when all of the following hold on the exact behavior candidate:

- normal CTest suite green;
- release claim audit green;
- evidence registry validation green;
- CLI failure regression green;
- controlled performance report produced successfully;
- captured-world one-command CI green;
- canonical DOT C2 measured benchmark green;
- measured DOT one-command showcase green;
- Linux, macOS and Windows normal CI green;
- no README claim depends on an unimplemented core mechanism.

After those pass, bump `0.80.0 -> 0.90.0`, rerun the same gates on the exact release head, then merge to `main`.

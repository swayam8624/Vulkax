# Vulkax 0.90 principal-path performance evidence

This report records reproducible wall-clock evidence for the Vulkax 0.90 release-hardening principal path. The numbers are observations from GitHub-hosted runners, not performance guarantees and not a pass/fail correctness threshold.

## Candidate measured

Behavior/evidence candidate:

```text
dddaaae7008520fff7494e52700758dd280dfe23
```

GitHub Actions workflow:

```text
Vulkax 0.90 release hardening
run 33033788212
```

The behavior candidate passed the release claim/schema contract and the Linux, macOS and Windows hardening jobs before this report was written.

## Benchmark command

Each operating-system job built a Release CLI and ran:

```text
scripts/benchmark_captured_world_run.py --iterations 1 --backend none
```

The benchmark creates the same deterministic controlled captured-deformable bundle on each host and executes the public `captured-world-run` path with:

- requested render backend: `none`;
- showcase: disabled;
- robustness seed: `12345`;
- one evidence-producing iteration;
- complete run evidence retained for audit.

The summary schema is `vulkax_captured_world_performance` version 1. Timing uses Python `time.perf_counter` around end-to-end orchestration.

## Observed hosted-runner results

| Host | Observed wall time | Result |
| --- | ---: | --- |
| GitHub Ubuntu 24.04 runner | `0.681872167 s` | completed, rewrite verified, no rollback |
| GitHub macOS 15 ARM runner | `0.445301334 s` | completed, rewrite verified, no rollback |
| GitHub Windows Server 2025 runner | `2.689947100 s` | completed, rewrite verified, no rollback |

The Windows summary identified its platform as `Windows-2025Server-10.0.26100-SP0` with Python `3.12.10`.

## Interpretation boundary

These values must **not** be used to claim that one operating system or backend is faster than another. GitHub-hosted runner hardware, virtualization, scheduler load, filesystem behavior and toolchains are not controlled to make the three rows performance-comparable.

The benchmark intentionally requests backend `none`; it measures the controlled captured-world research/orchestration path without adding host-specific GPU availability or presentation rendering. Native Vulkan/Metal performance is covered by the separate Gaussian scaling evidence and is not inferred from this table.

There is deliberately no timing threshold in CI. A release-hardening run passes when it produces finite, structurally valid performance evidence and a complete scientifically valid captured-world evidence directory, not when it beats an arbitrary stopwatch target.

## Reproduction

From a Release build:

```bash
python3 scripts/benchmark_captured_world_run.py \
  --executable build/vulkax \
  --iterations 3 \
  --backend none
```

On a multi-config Windows build:

```powershell
python scripts/benchmark_captured_world_run.py `
  --executable build/Release/vulkax.exe `
  --iterations 3 `
  --backend none
```

The release-hardening CI uses one iteration to control CI cost. Local investigations may use additional iterations, but any comparison must report the host and build configuration and must not silently mix hosted-runner measurements with developer-machine measurements.

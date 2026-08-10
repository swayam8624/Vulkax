# Vulkax Next

**A problem-driven, self-verifying computational-physics system.**

Vulkax Next is a clean architectural reset. It does not begin with a visualizer mode, a demo, or a
renderer. It begins with a physical **Problem** and compiles that problem into a reproducible numerical
experiment.

```text
Problem
  -> physical model
  -> operator model
  -> discretization / solver plan
  -> accelerated execution
  -> verification
  -> analysis / inverse / optimization
  -> scientific visualization
```

The initial code intentionally contains no Wave, Smoke, Schwarzschild, Car, or Ball-Mill special mode.
Those may become example problems later; none is allowed to become an architectural branch.

## Bootstrap status

The first foundation establishes:

- typed SI dimensions and quantities,
- `ProblemIR` for domains, fields, residual operators, materials, boundaries, objectives, accuracy,
  and compute budgets,
- structural validation and deterministic problem hashing,
- an operator graph designed for future operator-level attribution,
- conservative result certificates with Preview / Converging / Verified / Rejected trust states,
- a capability-scored backend policy with Vulkan first cross-platform, Metal first-class on Apple, and
  OpenGL as compatibility fallback,
- dependency-free C++20 builds on Linux, macOS, and Windows.

No renderer or numerical solver is being smuggled into the architecture before these contracts are
stable.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/vulkax
```

On multi-config generators, run the executable from the selected configuration directory.

## Architectural laws

1. A solver never owns presentation semantics.
2. A renderer never decides governing physics.
3. A problem is not a visualizer mode.
4. Physical values carry dimensions.
5. `VERIFIED` is earned by evidence; it is never a UI label.
6. Backend choice is capability-driven and cannot alter problem semantics.
7. A new feature must help solve a problem that was not hard-coded as a demo.
8. Research hooks such as operator attribution live in the core representation, not as post-hoc hacks.

Read [`docs/VISION.md`](docs/VISION.md), [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md), and
[`docs/RESEARCH.md`](docs/RESEARCH.md) before adding physics code.

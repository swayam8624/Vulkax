# Vulkax

**Verified Rewritable Reality — a research system for turning captured scenes into persistent, physically executable world models.**

Vulkax is a problem-driven, self-verifying computational-physics and graphics research platform. It does
not begin with a visualizer mode or a hard-coded demo. It begins with a physical **Problem** or captured
**World**, keeps appearance separate from solver discretization, and requires numerical evidence before a
result may be called verified.

```text
Captured World / Problem
        |
        v
appearance <-> semantics <-> physical representation
        |                      |
        +------ World Correspondence Graph
                               |
                               v
                     operator / solver plan
                               |
                               v
                    accelerated execution
                               |
                               v
                         verification
                               |
                +--------------+--------------+
                v              v              v
             analysis        inverse       optimization
                |
                v
       Operator Influence / scientific visualization
```

The research thesis is deliberately stronger than "Gaussian editing": can a reconstructed real scene be
rewritten locally—geometry, material, constraints and eventually topology—while keeping photorealistic
appearance, physical state and provenance mutually consistent and quantitatively trustworthy?

## Current implementation — 0.18

The existing computational foundation includes typed SI dimensions, `ProblemIR`, operator graphs, solver
planning, numerical reference solvers, verification/result certificates, Vulkan/Metal capability probing,
native compute conformance and scientific rendering foundations.

The first **Rewritable Reality** slice adds:

- renderer-independent `GaussianCloud` storage;
- ASCII and binary-little-endian 3DGS PLY ingestion;
- stable decoding helpers for 3DGS log-scales, opacity logits and quaternion normalization;
- semantic `WorldIR` entities with material metadata, revisions and provenance;
- a bidirectional `WorldCorrespondenceGraph` from Gaussian support to semantic entities and generic
  physical degrees of freedom;
- transactional local entity translation and material-parameter rewriting;
- rollback receipts;
- an explicit **unaffected-region drift** metric;
- tests for Gaussian ingestion, mapping integrity, rewrite locality and rollback;
- Release test builds with assertions kept active;
- restored Linux/macOS/Windows CI.

This does **not** yet claim a Gaussian renderer, MPM/FEM-to-Gaussian coupling, topology rewriting,
automatic semantic reconstruction, inverse material identification from video, XR interaction, or
publishable novelty.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVULKAX_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

On a 3DGS scene exported as `point_cloud.ply`:

```bash
./build/vulkax gaussian-info /path/to/point_cloud.ply
```

Existing physical-problem commands remain:

```bash
./build/vulkax validate examples/rotating_mill.vkx
./build/vulkax inspect examples/rotating_mill.vkx
./build/vulkax plan examples/rotating_mill.vkx
./build/vulkax --probe-backends
./build/vulkax --conformance Metal
./build/vulkax --conformance Vulkan
```

## Architectural laws

1. A solver never owns presentation semantics.
2. A renderer never decides governing physics.
3. A problem is not a visualizer mode.
4. Appearance primitives and physical degrees of freedom are allowed to differ.
5. Physical values carry dimensions.
6. `VERIFIED` is earned by evidence; it is never a UI label.
7. Backend choice is capability-driven and cannot alter problem semantics.
8. A new feature must help solve a problem that was not hard-coded as a demo.
9. Research hooks such as operator attribution and world correspondence live in the core representation,
   not as post-hoc visualization hacks.
10. Integration of known techniques is not itself a novelty claim.

## Research program

Read:

- [`docs/REWRITABLE_REALITY.md`](docs/REWRITABLE_REALITY.md) — captured-world hypotheses, metrics and
  immediate milestones;
- [`docs/RESEARCH.md`](docs/RESEARCH.md) — Operator Influence Fields and closed-loop experiment design;
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) — problem/solver architecture;
- [`docs/VISION.md`](docs/VISION.md) — broader system direction.

The next implementation milestone is a native Vulkan Gaussian renderer with a Metal path on Apple, followed
by measurable Gaussian-to-physical correspondence/transfer on a captured deformable-object benchmark.

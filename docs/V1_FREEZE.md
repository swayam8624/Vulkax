# Vulkax v1 freeze

Vulkax v1 was frozen on **2026-08-10** before the architectural reset.

## Immutable preservation point

- Branch: `legacy/studio-v1-2026-08-10`
- Commit: `ff760f8f85683947e61e5ea66b99785d9994e913`

That branch is the canonical source for reproducing the exact final v1 state. Cleanup on `main` must
never be used as a reason to rewrite or reinterpret what v1 contained.

## Why v1 is frozen

V1 accumulated real numerical and GPU work, but the product abstraction became demo-driven. The native
Studio ultimately revolved around a small set of special visualizer modes, and solver, scene, and UI
semantics became coupled to those modes. Continuing to add physics families to that structure would
increase feature count without creating a genuinely general computational-physics system.

The reset therefore preserves the **implementation assets** while rejecting the **product abstraction**.

## Foundations worth preserving or extracting

The following areas are candidates for reuse after independent validation and interface cleanup:

- canonical C++ equation parser and C ABI bridge,
- scalar, vector, tensor, stencil, and transport IR work,
- numerical reference solvers and regression fixtures,
- Vulkan device/compute/resource/runtime utilities,
- native Metal compute and presentation techniques,
- shader compilation and runtime-resource staging,
- static scene import and visual/simulation proxy separation,
- glTF/GLB/OBJ parsing and PBR material handling,
- deterministic camera/capture infrastructure,
- Kerr/Schwarzschild numerical experiments,
- MAC/grid/fluid experiments,
- particle and rigid-body experiments,
- cross-platform CI, sanitizers, clang-tidy, and numerical validation patterns.

Reuse is not automatic. Code moves into the next system only after its contract fits the new
problem/operator architecture and its tests travel with it.

## Legacy concepts that must not define Vulkax Next

- `VisualizerMode` or equivalent hard-coded physics/demo mode enums,
- Wave/Smoke/Black-Hole branches as top-level architecture,
- importing geometry implicitly choosing a simulation type,
- renderer code owning governing-physics semantics,
- solver code owning visualization presentation,
- equation-text heuristics as the authoritative physical model,
- obstacle-only scene terminology as a universal object model,
- the monolithic native `PhysicsModel` pattern,
- BEACON, GeoBEACON, Atlas, or Little Vulkan Engine naming as the product identity.

Historical examples can remain examples. They cannot become privileged architectural cases.

## Known v1 issue at freeze

The final v1 hardening cycle had exposed a real Vulkan validation problem around direct-presentation
pipeline/render-pass compatibility during swapchain recreation. A generation-based pipeline rebuild
was being developed and compiled, but the final four-mode validation pass still reported at least one
validation-layer failure. The WIP migration workflow and patch script used during that repair are
removed from the cleaned foundation; the exact unfinished state remains preserved on the legacy branch.

This issue must not be hidden by disabling validation. If the direct v1 presenter is maintained, it
should be fixed as a correctness task or left explicitly archived.

## What Vulkax Next is trying to become

The new system should compile a **problem into a numerical experiment**, not compile a demo selection
into a shader.

A future problem representation should be able to describe geometry/domains, unknown and known fields,
governing operators, constitutive models, materials, initial and boundary conditions, contacts,
couplings, observations, objectives, constraints, accuracy requirements, and compute budget. Solver
planning, accelerated execution, verification, inverse analysis, optimization, and visualization then
operate on that representation.

The first demanding vertical problems are intended to be:

1. GPU granular/DEM simulation of a rotating mill,
2. hyperelastic inverse material identification and experiment design,
3. vehicle aerodynamics with verification and optimization.

These are not three new hard-coded modes. They are integration tests for whether the abstractions are
general enough to solve unfamiliar physical problems.

## Renderer/backend policy for the next system

- Vulkan: primary cross-platform GPU backend and first implementation target.
- Metal: first-class Apple backend when it provides the strongest native path.
- OpenGL: compatibility/fallback rendering path where useful.
- DirectX: intentionally out of scope.

The runtime should score available backends by required capabilities and platform/device quality rather
than use a simplistic product-mode switch. Numerical problem semantics must remain backend-independent.

## Repository policy after the freeze

This v1 repository accepts preservation-oriented work: correctness, security, portability,
reproducibility, tests, and extraction of reusable components. New Vulkax product development belongs
in the new architecture repository.

Generated movies, frame sequences, EXRs, raw buffers, and similar run output belong in CI artifacts or
releases. Git should retain source, fixtures, manifests, compact numerical summaries, and deliberately
curated evidence.

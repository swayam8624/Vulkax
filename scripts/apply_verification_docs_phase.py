from pathlib import Path
import runpy

# Consume any still-present phase scripts first so documentation describes the actual assembled tree.
for script in ['scripts/apply_problem_runner_migration.py','scripts/apply_verticals_phase.py','scripts/apply_gpu_dem_phase.py','scripts/apply_opengl_phase.py','scripts/apply_research_phase.py']:
    p=Path(script)
    if p.exists(): runpy.run_path(str(p),run_name='__main__')
for stale in ['scripts/apply_problem_runner_migration.py','scripts/apply_verticals_phase.py','scripts/apply_gpu_dem_phase.py','scripts/apply_opengl_phase.py','scripts/apply_research_phase.py']:
    p=Path(stale)
    if p.exists():p.unlink()

Path('include/vulkax/verify/manufactured.hpp').write_text(r'''#pragma once
#include <cstddef>
#include <vector>
namespace vulkax::verify {
struct ManufacturedSample{std::size_t resolution{};double spacing{};double l2Error{};};
struct ManufacturedConvergence{std::vector<ManufacturedSample>samples;double observedOrder{};};
[[nodiscard]] ManufacturedConvergence verifyDiffusionManufactured(const std::vector<std::size_t>&resolutions,double diffusivity,double finalTime,double safety=0.35);
}
''')
Path('src/verify/manufactured.cpp').write_text(r'''#include "vulkax/verify/manufactured.hpp"
#include "vulkax/numerics/grid.hpp"
#include "vulkax/solvers/diffusion.hpp"
#include <cmath>
#include <numbers>
#include <stdexcept>
namespace vulkax::verify {
ManufacturedConvergence verifyDiffusionManufactured(const std::vector<std::size_t>&res,double alpha,double finalTime,double safety){if(res.size()<2||alpha<=0||finalTime<=0||safety<=0||safety>=1)throw std::invalid_argument("invalid manufactured diffusion verification");ManufacturedConvergence out;for(auto n:res){if(n<6)throw std::invalid_argument("manufactured resolution too small");const double h=1.0/static_cast<double>(n-1);numerics::ScalarGrid3D g(n,n,n,{h,h,h});for(std::size_t z=0;z<n;++z)for(std::size_t y=0;y<n;++y)for(std::size_t x=0;x<n;++x){const double X=x*h,Y=y*h,Z=z*h;g.at(x,y,z)=std::sin(std::numbers::pi*X)*std::sin(std::numbers::pi*Y)*std::sin(std::numbers::pi*Z);}const double dtMax=solvers::diffusionStableDt(g,alpha);const std::size_t steps=(std::size_t)std::ceil(finalTime/(safety*dtMax));const double dt=finalTime/static_cast<double>(steps);for(std::size_t s=0;s<steps;++s)solvers::advanceDiffusion(g,alpha,dt,1);const double decay=std::exp(-3.0*std::numbers::pi*std::numbers::pi*alpha*finalTime);double sum=0;std::size_t count=0;for(std::size_t z=1;z+1<n;++z)for(std::size_t y=1;y+1<n;++y)for(std::size_t x=1;x+1<n;++x){const double exact=decay*std::sin(std::numbers::pi*x*h)*std::sin(std::numbers::pi*y*h)*std::sin(std::numbers::pi*z*h);const double e=g.at(x,y,z)-exact;sum+=e*e;++count;}out.samples.push_back({n,h,std::sqrt(sum/static_cast<double>(count))});}const auto&a=out.samples[out.samples.size()-2];const auto&b=out.samples.back();out.observedOrder=std::log(a.l2Error/b.l2Error)/std::log(a.spacing/b.spacing);return out;}
}
''')
Path('tests/manufactured_tests.cpp').write_text(r'''#include "vulkax/verify/manufactured.hpp"
#include <cassert>
#include <cmath>
int main(){auto r=vulkax::verify::verifyDiffusionManufactured({9,13,17},0.04,0.025,0.3);assert(r.samples.size()==3);assert(r.samples[2].l2Error<r.samples[1].l2Error);assert(r.samples[1].l2Error<r.samples[0].l2Error);assert(std::isfinite(r.observedOrder));assert(r.observedOrder>1.5);return 0;}
''')

p=Path('CMakeLists.txt');t=p.read_text()
for old in ['project(Vulkax VERSION 0.22.0 LANGUAGES CXX)','project(Vulkax VERSION 0.21.0 LANGUAGES CXX)','project(Vulkax VERSION 0.20.0 LANGUAGES CXX)','project(Vulkax VERSION 0.19.0 LANGUAGES CXX)','project(Vulkax VERSION 0.18.0 LANGUAGES CXX)']:t=t.replace(old,'project(Vulkax VERSION 0.23.0 LANGUAGES CXX)')
if 'src/verify/manufactured.cpp' not in t:t=t.replace('src/verify/convergence.cpp src/verify/result_certificate.cpp','src/verify/convergence.cpp src/verify/manufactured.cpp src/verify/result_certificate.cpp')
# Append test name before foreach close using whichever current tail exists.
if 'manufactured)' not in t:
    import re
    t=re.sub(r'(foreach\(test_name IN ITEMS[^\n]*)(\))',lambda m:m.group(1)+' manufactured'+m.group(2),t,count=1)
p.write_text(t)

Path('docs/STATUS.md').write_text(r'''# Vulkax status

Vulkax is being rebuilt as a problem-driven computational-physics and research system. The governing rule is that a feature is only marked implemented when there is executable code and a regression/CI gate for its stated claim.

## Implemented and tested

- C++20 `ProblemIR`: domains, scalar/vector/tensor fields, residual operators, materials, boundary conditions, objectives, accuracy requirements, compute budgets, typed SI parameters, and reproducible data-resource references.
- Human-editable `.vkx` problem documents with validation, stable hashes, inspection, planning, and an end-to-end `run` command for connected verticals.
- Runtime backend discovery and policy selection. Vulkan is primary. Metal is a native Apple backend. OpenGL is a runtime-probed fallback and only advertises compute features when the actual context supports them.
- Real Vulkan and Metal compute conformance against a CPU reference.
- Real Vulkan and Metal offscreen scientific particle rendering, deterministic camera tracks, and fixed-FPS frame-sequence capture.
- Scientific visualization data products: perceptual color maps, scalar isosurfaces, vector-field streamlines, and particle instances.
- OBJ triangle ingestion, topology diagnostics, visual normalization, and closed-mesh voxelization for physics domains.
- Dense numerical primitives, structured scalar grids, explicit diffusion, convergence estimation, and manufactured-solution convergence verification.
- Result certificates with Preview / Converging / Verified / Rejected trust states.
- Fidelity/solver planning and empirical pilot-tournament selection.
- CPU reference solvers: linear tetrahedral FEM, compressible Neo-Hookean nonlinear tetrahedral FEM, DEM with spatial-hash broad phase, rotating-drum mechanics, 2D incompressible projection, and a staggered 3D MAC reference with voxel solids.
- Native Vulkan/Metal all-pairs GPU DEM reference kernels cross-checked against the CPU reference where the backend is available.
- Differentiable trajectory foundation: discrete adjoint propagation using finite-differenced local Jacobians, with full-rerun finite-difference gradient verification.
- Operator Influence research prototypes: static local influence/counterfactuals plus time-dependent operator-attribution for decomposed evolution operators.
- Goal-oriented refinement scoring based on local error times observable influence.
- Hyperelastic constitutive fitting/ranking foundations, inverse optimization utilities, next-experiment selection, and sequential hypothesis-discriminating experiment design.

## Reference verticals

### Granular rotating mill
`.vkx` operating parameters drive a deterministic CPU spatial-hash DEM reference. The run produces a result certificate and, when a native renderer exists, Vulkan/Metal scientific frames. The native GPU DEM kernel is separately cross-checked; the end-to-end mill runner is not yet the scalable GPU mill solver.

### Hyperelastic solid
A nonlinear compressible Neo-Hookean tetrahedral reference solves equilibrium with Newton iterations and line search. It produces stress/energy/deformation evidence and a result certificate. This is a small dense reference solver, not a production sparse/GPU nonlinear FEM package.

### Aerodynamics
An imported watertight OBJ becomes a voxel solid inside a staggered 3D MAC domain. The current reference advances convection/diffusion/projection, estimates pressure force, and produces streamline-based scientific output. This is a correctness/research reference, not validated industrial CFD, LES, or a turbulence-model replacement.

## Not yet claimed as complete

- million-particle GPU spatial-hash/sort/scan DEM;
- production sparse/GPU FEM and robust contact;
- high-Reynolds validated vehicle CFD / LES/RANS;
- full backend-independent arbitrary-kernel compiler from governing PDE syntax;
- native triangle/volume renderer parity with the particle renderer;
- OpenGL scientific-render fallback (runtime probing exists first);
- glTF/USD asset ingestion in the rebuilt core;
- production video/EXR pipeline in the rebuilt core;
- a polished cross-platform desktop Studio;
- statistically validated paper-level novelty claims for Operator Influence Fields.

Those are explicit future engineering/research tasks; they are not hidden behind demo names or marketing claims.
''')

Path('README.md').write_text(r'''# Vulkax

**A problem-driven computational-physics system for simulation, verification, inverse problems, optimization, scientific visualization, and research into explainable physics.**

Vulkax is not organized around hard-coded visualizer modes. A project describes a physical problem: domains, fields, governing operators, material data, boundary conditions, measurements, objectives, accuracy requirements and resource budgets. The runtime turns that description into an executable numerical experiment and keeps the simulation evidence separate from the way it is rendered.

## Core idea

```text
Problem document (.vkx)
        ↓
validated ProblemIR + SI dimensions
        ↓
operator graph / objectives
        ↓
solver + fidelity planning
        ↓
numerical experiment
        ↓
verification / result certificate
        ↓
analysis · inverse · optimization · visualization
```

The primary GPU API is **Vulkan**. On Apple platforms Vulkax can use **Metal** as a native backend. **OpenGL** is treated as a fallback capability and is runtime-probed instead of assumed from the operating-system name.

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DVULKAX_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

On macOS, install the normal CMake/Vulkan dependencies if you want Vulkan in addition to native Metal. The core and Metal path do not require a CUDA GPU.

## Inspect the machine

```bash
./build/vulkax --probe-backends
./build/vulkax --conformance Vulkan
./build/vulkax --conformance Metal   # macOS
```

The conformance command dispatches real native compute and compares its result with the CPU reference.

## Work with problems

```bash
./build/vulkax validate examples/rotating_mill.vkx
./build/vulkax inspect  examples/rotating_mill.vkx
./build/vulkax plan     examples/rotating_mill.vkx
```

A `.vkx` document is diff-friendly text. Physical numbers carry seven SI base-dimension exponents, so operating parameters participate in dimensional checks and in the stable problem hash.

## Run a connected vertical

```bash
./build/vulkax run examples/rotating_mill.vkx \
  --output build/mill-run \
  --frames 8 \
  --width 1280 \
  --height 720
```

The run directory contains a machine-readable result certificate and deterministic scientific frames when a native headless renderer is available.

Other reference problem documents include:

```text
examples/rubber_inverse.vkx
examples/car_aerodynamics.vkx
```

See [`docs/STATUS.md`](docs/STATUS.md) for the exact maturity boundary of each vertical.

## Result trust

Vulkax deliberately separates *a picture exists* from *the numerical result is trustworthy*.

```text
PREVIEW → CONVERGING → VERIFIED
                   ↘ REJECTED
```

A `ResultCertificate` records problem/solver hashes, backend/device identity, verification criteria, convergence or uncertainty evidence where available, and explanatory notes. A result is not promoted to `Verified` just because the solver terminated.

## Scientific visualization

The rebuilt renderer/visualization stack currently includes native Vulkan and Metal offscreen particle rendering, perceptual color maps, streamlines, generated scalar isosurfaces, deterministic camera tracks, and frame-sequence capture. Geometry and fields remain independent from solver implementations so the same physical state can be viewed through different scientific lenses.

## Research direction: physics debugging

Traditional simulation tells us **what happened**. Vulkax is also being built to ask **which physical mechanism caused the observable we care about**.

For a residual decomposition

```text
R(u) = R₁(u) + R₂(u) + ... + Rₖ(u)
```

Vulkax's Operator Influence work computes sensitivities of a chosen observable to local/operator interventions, then checks counterfactual predictions against full re-solves. The repository contains both a static local prototype and a time-dependent decomposed-evolution prototype. This is active research code; the project does not yet claim a proved universal causal interpretation or a publication result.

## Repository discipline

There is no `VisualizerMode`, no hidden “car mode”, and no solver that is allowed to depend on a UI panel. Vertical examples must use reusable problem/domain/operator/solver abstractions. CPU references stay in the tree even after GPU implementations arrive because they serve as correctness oracles.

## Legacy preservation

The pre-reset application is preserved on the branch:

```text
legacy/studio-v1-2026-08-10
```

The current `main` is the new problem-driven architecture.

## License

MIT, except third-party datasets/assets where their own notices apply.
''')

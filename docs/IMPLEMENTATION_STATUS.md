# Vulkax Next implementation status

This ledger separates implemented foundations from future scale claims.

## Implemented and tested

- problem-centric IR, typed units, operator graph, structural validation and stable hashing;
- runtime backend probing and capability-based selection;
- backend-independent ComputeIR with a CPU numerical oracle;
- real Vulkan compute execution and real Metal compute execution of the same ComputeIR contract;
- expression parsing, evaluation, symbolic differentiation and dimensional inference;
- structured scalar/vector/tensor fields and differential operators;
- CG, RK4, Newton and CFL numerical primitives;
- CPU/reference DEM with spatial hashing, nonlinear normal contact, damping, Coulomb-limited tangential friction and rotating cylindrical boundaries;
- Vulkan DEM step with atomic uniform-grid binning, bounded 27-cell neighbor search, read-only input/write-only output integration and explicit bin-capacity overflow reporting;
- tetrahedral compressible Neo-Hookean FEM reference dynamics and synthetic uniaxial material calibration;
- 2D staggered-MAC incompressible pressure projection and scalar advection reference;
- ProblemIR-driven solver-family planning with explicit verification evidence;
- discrete Operator Influence Fields and counterfactual objective-delta prediction;
- marching-tetrahedra scalar-field iso-surface extraction for regenerating 3D geometry;
- Richardson convergence estimation and evidence-backed Preview/Converging/Verified result certificates;
- multi-fidelity execution ladders that stop only after measured uncertainty satisfies the requested tolerance;
- weighted fitting/model selection for Neo-Hookean, Mooney-Rivlin and three-term Yeoh uniaxial responses;
- next-experiment recommendation based on disagreement between surviving material hypotheses;
- bounded scalar and multivariate numerical optimization;
- reusable scientific colormaps, scalar-slice rendering, particle sphere-splat reference rendering, PPM image export and OBJ geometry export;
- `vulkax-lab` headless research client that writes deterministic granular, incompressible-field, material-identification and implicit-field-to-geometry experiment outputs; Linux CI publishes a complete generated suite as a workflow artifact.

## Not yet claimed

The current Vulkan DEM path establishes the scalable spatial-hash/contact architecture but still uses host-visible coherent buffers and recompiles its two kernels per invocation; it is not yet a production persistent/device-local million-particle runtime. Vulkax Next also does not yet claim production CFD turbulence/aerodynamics, implicit production FEM, differentiable inverse mechanics, a complete PDE adjoint system, adaptive mesh refinement, or a finished interactive Vulkan scientific renderer. Those require dedicated performance work, convergence studies and benchmark evidence before they move into the implemented section.

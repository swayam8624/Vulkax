# Vulkax Next implementation status

This file distinguishes implemented foundations from future scale claims.

## Implemented and tested

- problem-centric IR, typed units, operator graph, structural validation and stable hashing;
- runtime backend probing and capability-based selection;
- backend-independent ComputeIR with a CPU numerical oracle;
- real Vulkan compute execution and real Metal compute execution of the same ComputeIR contract;
- expression parsing, evaluation, symbolic differentiation and dimensional inference;
- structured scalar/vector/tensor fields and differential operators;
- CG, RK4, Newton and CFL numerical primitives;
- CPU/reference DEM with spatial hashing, nonlinear normal contact, damping, Coulomb-limited tangential friction and rotating cylindrical boundaries;
- tetrahedral compressible Neo-Hookean FEM reference dynamics and synthetic uniaxial material calibration;
- 2D staggered-MAC incompressible pressure projection and scalar advection reference;
- ProblemIR-driven solver family planning with explicit verification evidence;
- discrete Operator Influence Fields and counterfactual objective-delta prediction;
- marching-tetrahedra scalar-field iso-surface extraction for regenerating 3D geometry.

## Not yet claimed

The reference implementations above are correctness substrates, not production-scale solvers. In particular, Vulkax Next does not yet claim million-particle GPU DEM, production CFD turbulence/aerodynamics, implicit production FEM, differentiable inverse mechanics, a full adjoint PDE system, adaptive mesh refinement, or a finished interactive scientific renderer. Those require dedicated GPU algorithms, convergence studies and benchmark evidence before they move into the implemented section.

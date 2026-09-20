# Baseline families required before any flagship claim

## Visual inverse physics
- PAC-NeRF
- gradSim
- MonoPhysics
- IRIS baselines
- ProJo4D where representation/physics recovery overlaps

## Gaussian physical simulation
- PhysGaussian
- PhysDreamer / DreamPhysics where relevant
- PhysFlow
- GASP
- i-PhysGaussian
- PUGS for physical-property understanding

## Differentiable simulation
- DiffTaichi
- ChainQueen
- Warp/Taichi differentiable MPM implementations where an experiment requires them

## Scientific-method baselines
- fit loss only
- held-out replay only
- finite-difference oracle
- full nonlinear rerun
- random intervention
- maximum-motion intervention
- maximum-gradient intervention
- Jacobian/Fisher-style optimal design
- fixed rewrite magnitude
- fixed verification tolerance
- multi-resolution/convergence control

A Vulkax result must not compare only against Vulkax 1.0.

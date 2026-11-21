# Vulkax — from simulated motion to independently verified world changes

This chapter complements [SOURCE_READING_ORDER.md](SOURCE_READING_ORDER.md) by following actual solver, inference, rendering and revision code. A fast simulation, a convincing visualization, a low residual and a verified physical edit are **four different achievements**. The source separates them, and the experiments should too.

## 1. Two particle-based physics algorithms solve different problems

The [MPM implementation](../src/solvers/mpm.cpp) uses Material Point Method: deforming material is represented by particles, while a temporary grid computes forces and momentum transfer. The core driver `stepMpm` follows this sequence:

1. Validate positive, finite `dt`, interpolation settings and grid/material parameters.
2. Transfer particle mass, momentum, and stress-derived forces onto nearby grid nodes using `particleToGridMpm` and its interpolation stencil.
3. Compute grid velocities from node momentum and mass; add forces/gravity over `dt` and apply grid boundary conditions.
4. Transfer updated grid motion back to the particles, blending the requested PIC/FLIP/APIC variants.
5. Advance particle positions/deformation and compute diagnostics in `MpmStepEvidence`.

The grid is an algorithmic intermediary—not the persistent world model. A particle's deformation gradient describes local material distortion; a Gaussian used to render a particle-based object is a **different** representation with different invariants.

The [DEM driver](../src/solvers/dem.cpp) represents bodies as individual particles with radii and contacts. Its `advanceDem` currently examines every unordered particle pair in a nested loop, applies forces for positive overlap, then integrates and handles wall collisions. That direct pair loop is (O(n^2)), even though other repository modules provide optional broadphase strategies. An acceleration algorithm is not actually used until the executed call path invokes it.

**Exercise:** choose ten spheres. Compare the number of possible pairs with the number of true contacts, and explain how a broadphase would reduce candidate work without skipping exact-contact tests.

## 2. Solver correctness includes conservation, stability and convergence

[`totalMpmMass`](../src/solvers/mpm.cpp) and `totalMpmMomentum` provide reference diagnostics. [`measureDem`](../src/solvers/dem.cpp) reports energy and contact overlap. Such counters are valuable but not interchangeable: energy may dissipate due to damping; overlap may decrease while a physically incorrect force field grows; a solver may remain stable while converging to the wrong solution.

For a controlled mesh/grid refinement experiment, [`estimateThreeLevelConvergence`](../src/verify/convergence.cpp) compares coarse, medium and fine values at a known refinement ratio. It rejects zero/degenerate differences and a nonmonotone sequence. This is a **numerical** extrapolation check, not an automatic proof of physical model validity.

Keep the experimental design, timestep, boundary conditions, material constants, particle population, solver options and units fixed when comparing algorithms. Report errors and physical invariants alongside runtime, not instead of them.

## 3. Physics state is not visual state

[`buildGaussianRasterBatch`](../src/render/gaussian.cpp) projects Gaussian cloud parameters into screen-space primitives using camera transforms and covariance. A renderer then composites the resulting batch. [`renderGaussianCloudHeadless`](../src/render/gaussian.cpp) explicitly dispatches to compiled Vulkan or Metal backends; it throws when the requested implementation was not included and rejects the unimplemented OpenGL branch.

This means GPU-backend selection, successful image generation and physically correct motion are separately testable. Gaussian covariance/appearance reconstruction can improve a screenshot without improving the underlying material model.

**Exercise:** keep a saved physics trajectory fixed while varying Gaussian appearance parameters. Which image metrics can change? Which physical state observables must remain identical?

## 4. The reality loop fits a hypothesis, not a universal truth

[`WorldIR`](../include/vulkax/world/world_ir.hpp) holds candidate world parameters, stable entities and observations. A forward model predicts observations from that hypothetical world. [`fitWorldHypothesis`](../src/world/reality_loop.cpp) compares the predicted measurements with those explicitly labeled for fitting.

It estimates a residual Jacobian (J) using finite parameter perturbations. Its damped least-squares step has the conceptual structure

[
(J^mathsf{T}J+lambda D)Delta p = -J^mathsf{T}r,
]

where (D) is a diagonal scale derived from the normal matrix. The implementation grows damping when a linear solve fails or no backtracked step reduces the objective, and shrinks it after an accepted step. Candidate parameter values are clamped to their declared beliefs/ranges. It records an optimization trace, including rejected attempts.

Crucially, the fitting residual and **validation-role residual** are computed separately. A final fit objective is not a held-out quality score, and a small loss does not establish that the inferred stiffness, density or other material parameters are physically unique.

[`rankParameterSensitivity`](../src/world/reality_loop.cpp) estimates how strongly local predictions respond to perturbing each parameter. [`analyzeLocalIdentifiability`](../src/world/reality_loop.cpp) examines the scaled Jacobian's spectral/numerical rank to detect weak or nonidentifiable directions. A model may fit observations well yet admit many parameter combinations producing essentially the same motion.

**Exercise:** create two correlated material parameters and fit only one type of marker motion. Explain how the Jacobian can become rank-deficient, and which new observation or controlled perturbation might distinguish the parameters.

## 5. Captured motion and split identity must survive import

[`appendCapturedObservations`](../src/research/captured_reality_loop.cpp) imports measured marker positions into WorldIR. Each stable observation ID contains marker, particle, time and data split. The code rejects empty datasets, missing referenced entities, duplicate observation IDs and invalid uncertainty settings.

An observation at time zero may be marked as initialization; later observations are labeled fitting or validation according to their capture split. `CapturedMpmForwardModel::operator()` reads proposed material values from WorldIR, runs captured free-relaxation simulation, and returns predicted marker positions in a physical SI observation space.

The evidence question is not simply whether the prediction curve overlaps the training data. It is **which data were used to choose the parameters**, which were held out, and whether the forward model was evaluated under an independent perturbation.

## 6. A world edit must pass a reversible verification gate

[`executeVerifiedRewrite`](../src/world/verified_rewrite.cpp) receives a world, correspondence graph, proposed transaction, policy and an optional physical verifier. It follows a deliberately stricter sequence than simply calling an edit method:

1. Validate the transaction and required propagation/physical/oracle preconditions.
2. If allowed, tentatively apply the transaction, recording a receipt so the prior world can be reconstructed.
3. Compare unaffected Gaussian positions against the policy's maximum drift and check required appearance propagation.
4. If a physical rerun is required, collect its completion, observable error, tolerance, traceable artifact and result.
5. If an independent oracle is required, verify its completion and success.
6. **Commit only if all required gates pass.** Otherwise roll back with the receipt and return the rejection reason and evidence record.

The two exceptional paths are distinct: a transaction rejected before application never changes the world; a transaction rejected after application must roll back. The return status, `rollbackPerformed`, and `worldCommitted` describe that difference.

[`writeVerifiedRewriteEvidenceCsv`](../src/world/verified_rewrite.cpp) makes those gates inspectable as a machine-readable record. This is a record of evaluated evidence—not a substitute for reviewing the actual physical rerun or its independent oracle.

**Exercise:** propose three modifications: one with excessive unrelated-region drift, one passing locality but failing the physical tolerance, and one that passes all required checks. Inspect the candidate world before/after and explain which states must be identical after rollback.

## 7. Reporting standards

For every numerical or visual claim, record the code revision, build flags and selected backend, dataset and split, random seed, scene, physical units, solver and discretization parameters, measurement definition and exact reproduction command. [ResultCertificate](../src/verify/result_certificate.cpp), [experiments](../src/experiment/) and [reproduction notes](PAPER_EVIDENCE_REPRODUCTION.md) should be read beside the resulting data.

The appropriate hierarchy of evidence is: **well-formed inputs → numerical/physical invariants → controlled experimental comparison → held-out validation → independent replay**. Attractive images and impressive frame rates provide valuable observations, but cannot replace these checks.

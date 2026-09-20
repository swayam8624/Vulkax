# Vulkax flagship problem/solution lock — DCS

Date: 2026-09-20
Status: **LOCKED FOR IMPLEMENTATION**
Supersedes: CAR-v1 / generic adequacy routing.

## Why the previous lock was superseded

The definitive GAUGE finite-support experiment triggered its own kill condition.
Finite support improved both ordinary held-out metrics on 10/10 repeats while
worsening the preregistered longitudinal mechanism on 0/10 repeats.

That result showed that "diagnose confidence and route repairs" was still too
generic. The harder problem is that an apparently successful repair can become
**more observationally convincing while becoming less physically correct**.

## Locked problem

> How can an executable captured-world system falsify a candidate physical repair
> that improves ordinary held-out observations by compensating with the wrong
> mechanism, especially when exact physical parameters are unknown?

## Locked solution family

**Dark-Field Counterfactual Spectroscopy (DCS).**

DCS constructs signed ensembles of physical interventions whose algebraic moments
annihilate response terms below a chosen mechanism order. Candidate worlds are then
compared on the residual dark-field response rather than on the large common motion
they already explain.

Core objects:
1. annihilating intervention stencils;
2. counterfactual cumulants;
3. mechanism order of contact / deception depth;
4. automatic disagreement-maximizing stencil synthesis in the lower-order nullspace;
5. numerical witness flow before physical interpretation;
6. deceptive-repair accept/refuse logic.

## Algorithmic thesis

Given feasible interventions u_i and model responses F_m(u_i):

1. form the lower-order moment matrix A_k containing all monomials of total degree < k;
2. restrict signed experiment weights w to null(A_k);
3. form a between-model response disagreement operator B;
4. solve for the dominant projected direction:

   w* = argmax_{w in null(A_k), ||w||=1} w^T B w;

5. physically execute or evaluate the signed ensemble;
6. compare measured and predicted dark-field witnesses;
7. increase mechanism order only if lower orders do not separate the models;
8. refuse physical interpretation if the witness does not survive numerical fidelity checks.

The initial implementation uses a deterministic projector + power iteration. More
constrained/safe optimization may replace the solver without changing the locked
problem.

## Novelty boundary

DCS does **not** claim novelty for:
- Taylor/Volterra response expansions;
- higher-order FRFs;
- finite differences;
- Möbius inversion;
- cumulants;
- metamorphic testing;
- active model discrimination;
- Fisher/Jacobian experiment design;
- renormalization.

The candidate novelty is their new captured-world repair role:

> actively synthesize signed physical intervention ensembles that suppress the
> lower-order/common response of competing reconstructed worlds, expose the first
> irreducible mechanism order at which they diverge, reject observationally
> successful but physically deceptive repairs, and require the witness to survive
> numerical-resolution tests before a physical rewrite is trusted.

## Locked implementation sequence

D0 deterministic mathematical controls.
D1 constructed deceptive-repair positive control.
D2 solver-native MPM discovery + fresh validation.
D3 automatic intervention/stencil synthesis and baselines.
D4 retrospective GAUGE mechanism study (motivation only).
D5 fresh real confirmatory benchmark.
D6 captured Gaussian spatial dark-field localization.

## Kill conditions

DCS is demoted if:
- deterministic annihilation controls fail;
- realistic noise destroys the separation advantage;
- fresh solver-native tests show no advantage over ordinary residual/model disagreement;
- automatic DCS acquisition does not improve over raw maximum-output separation at matched cost;
- claimed witnesses do not survive numerical refinement;
- no fresh measured-domain deceptive repair can be detected prospectively;
- a direct prior method is found with the complete captured-world deceptive-repair formulation.

## Paper gate

Paper prose may begin once:
- D0/D1 pass;
- D2 produces reproducible solver-native evidence or a clearly documented negative result;
- the benchmark/limitations/novelty boundaries are frozen.

Flagship claims must wait for D5 fresh measured validation.

# DCS novelty threat map — 2026-09-20

This is a claim guard, not a novelty declaration.

## Threats that are already occupied

### Higher-order nonlinear response / Volterra / higher-order FRFs
Established structural-dynamics literature measures second/third/higher-order
frequency response functions and uses them for nonlinear parameter identification.

Representative:
- Tomlinson & Billings, *Higher Order Frequency Response Functions in Nonlinear
  System Identification* (1991).
- Lin & Ng, *Higher-order FRFs and their applications to the identifications of
  continuous structural systems with discrete localized nonlinearities* (2018).
- Lin & Ng, *A new method for the accurate measurement of higher-order frequency
  response functions of nonlinear structural systems*.
- Teloli et al., Bayesian identification of higher-order FRFs for bolted-joint
  structures (2021).

Consequence:
DCS cannot claim novelty for exposing higher-order response or using higher-order
response to identify nonlinear mechanisms.

### Active model discrimination
Optimal separating inputs for competing nonlinear/uncertain models are established.

Representative:
- stochastic nonlinear MPC with active model discrimination (2017);
- input design for nonlinear model discrimination via affine abstraction (2018);
- Niu, Shen & Yong, *Active model discrimination: A multi-stage approach with
  adaptive partitions* (Automatica, 2026);
- Ni et al., *Safe, Real-Time Active Model Discrimination and Fault Diagnosis for
  Nonlinear Systems via Differentiable Reachability* (2026).

Consequence:
DCS cannot claim novelty for choosing an input that maximizes model disagreement.

### Metamorphic testing
Scientific software can be tested by relations among outputs from related inputs
when an oracle is unavailable.

Representative:
- Lin, Simon & Niu, *Exploratory Metamorphic Testing for Scientific Software*.

Consequence:
DCS cannot claim novelty for testing simulators through multi-input/output
relations.

### Lie brackets / intervention nonclosure
2026 causal-discovery work explicitly constructs intervention-induced response
fields and uses Lie/Frobenius bracket nonclosure as a diagnostic of latent/unmodeled
structure.

Representative:
- Mahadevan, *Latent Confounded Causal Discovery via Lie Bracket Geometry*,
  arXiv:2606.19610.

Consequence:
counterfactual holonomy / noncommutativity alone is not a safe flagship claim.

### Renormalization language
Renormalization-group ideas have already appeared in system identification.

Representative:
- Wang, Yu & Zhang, *Improved system identification with Renormalization Group*,
  ISA Transactions 53(5), 2014.

Consequence:
DCS numerical scale-flow is a verification guard only, not a novelty claim.

### Möbius/cumulant decomposition
Möbius inversion and cumulant-like higher-order interaction decompositions are
classical and appear in causal/interventional analysis.

Consequence:
counterfactual cumulants are a mathematical component, not the paper headline.

## Surviving narrow claim target

The current search has not found a direct method combining all of:

1. a captured/executable visual-physical world;
2. a **candidate repair** that is already preferred by ordinary held-out metrics;
3. synthesis of a **signed physical intervention ensemble** constrained to
   annihilate all response monomials below mechanism order k;
4. optimization of that ensemble specifically inside the lower-order nullspace to
   expose disagreement between otherwise observationally similar repaired worlds;
5. adaptive reporting of the first separating **mechanism order of contact**;
6. rejection of a repair when the measured dark-field witness worsens despite
   ordinary metric improvement;
7. a numerical-fidelity survival test before interpreting that witness physically;
8. gating of a requested physical rewrite rather than merely identifying system
   parameters.

This conjunction is the candidate DCS contribution.

## Reviewer attacks to prepare for

### "This is just a finite difference / Volterra kernel."
Required response must be empirical and algorithmic:
- show automatic nullspace synthesis;
- compare against fixed finite-difference stencils;
- compare against direct higher-order response estimation;
- show deceptive-repair detection rather than parameter identification.

### "This is just active model discrimination."
Required:
- matched acquisition-cost comparison against raw maximum-output separation and
  AMD/OED;
- cases where raw outputs are near-identical but lower-order-annihilated responses
  separate;
- measured repair acceptance/rejection, not merely model index recovery.

### "This is metamorphic testing."
Required:
- DCS relations are not assumed known invariants;
- the signed witness is **synthesized from competing physical worlds** under moment
  annihilation constraints;
- measured witness magnitude is a mechanism signal, not simply pass/fail against a
  predefined software relation.

### "Higher-order measurements are noisy and expensive."
Required:
- explicit stencil coefficient noise gain;
- SNR curves;
- cost-aware experiment selection;
- stop at first separating order;
- report cases where DCS abstains.

### "The GAUGE example was used to invent the method."
Agree. Treat GAUGE fixture overlap as discovery/motivation only.
The flagship claim requires a fresh measured confirmatory domain.

## Novelty status

Current status:
**promising but unproven**.

The mathematical primitives are mostly established.
The novelty burden rests on the captured-world deceptive-repair problem formulation,
lower-order-nullspace experiment synthesis, mechanism-order/deception-depth
interpretation, numerical witness survival, and rewrite decision.

Run another full-text novelty sweep immediately before paper submission.

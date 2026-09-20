# Vulkax exhaustive discovery matrix

Current hypothesis count: **360**.

The database deliberately crosses five representation families, ten uncertainty/failure sources, twelve evidence-producing interventions, ten diagnostics and eight decision outcomes. The resulting hypotheses are **search candidates**, not claims.

## Dimensions

**Representations:** Gaussian splat world; MPM particle field; surface marker graph; WorldIR object graph; hybrid Gaussian-MPM world.

**Uncertainty/failure sources:** measurement noise; unknown rest state; unknown boundary load; time-discretization error; grid-resolution error; model-form mismatch; material heterogeneity; contact-event uncertainty; camera-scale uncertainty; correspondence uncertainty.

**Evidence-producing interventions:** mixed deformation; localized poke; boundary displacement; release constraint; known force pulse; shear deformation; bending deformation; contact relocation; friction change; attachment relocation; targeted camera move; extra marker placement.

**Diagnostics:** held-out residual; unseen-intervention residual; smallest sensitivity singular value; parameter posterior spread; timestep refinement disagreement; solver-family disagreement; energy-balance defect; force-balance defect; event-regime crossing; spatial influence concentration.

**Decision outcomes:** rewrite acceptance; rewrite refusal; next-evidence request; model-class escalation; numerical-resolution escalation; material partition proposal; trust-radius contraction; counterfactual verification.

## Escalation rule

A hypothesis stays cheap until it passes all of:
1. no immediate novelty collision,
2. deterministic correctness control,
3. no target leakage,
4. effect larger than numerical uncertainty,
5. improvement over fit-only and held-out-only baselines,
6. disjoint intervention validation.

Only then does it earn real-data or GPU-scale compute.

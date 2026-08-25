# Research program

Vulkax is a platform; the platform itself is not automatically a publishable novelty claim. Research
contributions must be isolated, formalized, compared with the state of the art, and evaluated against
counterfactual or experimental evidence.

## Research track A: Operator Influence Fields

Let a discretized physical system be written as a residual decomposition

```text
R(u) = R1(u) + R2(u) + ... + Rk(u) = 0.
```

For a user-selected observable `J`, introduce a local intervention on each mechanism:

```text
R(u, a) = sum_k (1 + a_k(x,t)) R_k(u).
```

The central quantity is the operator influence field

```text
A_k(x,t) = dJ / da_k(x,t).
```

With an adjoint `lambda` satisfying the appropriate discrete adjoint system, a local contribution has the
form, up to discretization/sign convention,

```text
A_k ~ -lambda^T R_k.
```

The intended question is not “where is advection large?” but “where and when did the advection mechanism
influence the final quantity I care about?”

### Evaluation requirement

Pretty fields are not sufficient. For many held-out interventions `Delta a`, predict

```text
Delta J_predicted ~ sum_k integral A_k Delta a_k dx dt
```

then rerun the nonlinear simulation and compare against `Delta J_actual`. Report predictive error,
runtime, memory, and the region in which the first-order approximation is valid. Compare against finite
differences and relevant adjoint/sensitivity baselines.

Adjoint-guided region selection must follow the same discipline. A high-influence region selected from
`|A|` is a **proposal**, not evidence that the proposed rewrite is correct. Region construction should
record how much influence mass was retained or discarded, preserve stable physical IDs/provenance, and
remain deterministic for fixed inputs. Every proposed intervention used as evidence must then be checked
with a derivative oracle and an independent nonlinear rerun that was not used to select the region.

The controlled captured-material regression currently exercises this protocol with particle-local
Young's-modulus coefficients: a reverse APIC/MPM pass proposes spatial material regions from stable-ID
particle gradients, while central finite differences and separate nonlinear counterfactual perturbations
remain the verification oracle. This is a regression foundation only; the next evidence requirement is
measured captured deformables with observation noise, correspondence error and model discrepancy.

The first serious paper should demonstrate the formulation across more than one physics family rather
than hide a CFD-specific method behind generic language.

## Research track B: closed-loop experiment design

For inverse material characterization, maintain uncertainty over model family `M` and parameters `theta`.
Choose the next realizable experiment `d` to maximize expected information gain subject to manufacturing
and testing constraints:

```text
d* = argmax_d I(M, theta ; Y | d).
```

The contribution target is sequential joint model discrimination and parameter identification—not merely
fitting another hyperelastic model.

## Research discipline

- Never claim novelty because multiple known techniques were integrated.
- Never use an LLM-generated solver result without deterministic numerical verification.
- Keep mathematical claims separate from product claims.
- Build evaluation protocols before hero demos.
- Preserve failed cases; they are part of the research evidence.

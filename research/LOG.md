# Vulkax research log

## 2026-09-20 — Discovery campaign initialized

**Control:** v1.0.0 → `2e7c306d5275718438e6c658d208f60edec52e53`

**Current main:** `9860f20f01673f176f457983388a419266b63449` (presentation-only PR #53 after control)

**Discovery substrate:** PR #59 head `d48dbded55136e729d6ca8b03538cc93d57e62cc`

### Hypothesis
The strongest near-term research family is not Gaussian physics itself. It is the separation of:
1. fit quality,
2. identifiability,
3. held-out replay,
4. counterfactual transfer,
5. numerical validity,
6. verification/refusal.

### Action
Created isolated `research/discovery` branch. Seeded live audit, prior-art threat matrix, hypothesis database and cheap synthetic falsification workflow.

### Interpretation
PhysGaussian/PAC-NeRF/PhysDreamer/PhysFlow/PUGS/GASP/EMPM/MonoPhysics/i-PhysGaussian make representation+physics or inverse-fitting-only claims unsafe as a Vulkax flagship. Mechanics literature also makes generic identifiability/OED claims unsafe. The candidate gap must involve the verified-rewrite loop itself.

### Next falsification
Run cheap probes. Kill any claim that reduces to a known identifiability or optimal-design result. Escalate only mechanisms where Vulkax's persistent-world + intervention + independent-verification semantics create a measurable difference.

## 2026-09-20 — Cheap probes passed; first evidence recorded

**Branch/SHA:** research/discovery @ `0b62018c23c3cc7da49eb3c0fe0c12cfff238cb9`

**Workflow:** run `35509097733`, success.

### Outcome
All first cheap probes compiled and passed their integrity checks. The most important synthetic warning signal is a deliberately misspecified spring model with 0.136% same-regime held-out error but 20.94% error after a larger unseen intervention (154.36× degradation).

Timestep bias, finite trust radius and certificate FAR/FRR tradeoffs were also observed in controlled synthetic probes. These are instrumentation/falsification results, not publication claims.

### Interpretation
Held-out replay under the same experiment is now treated as a weak gate. Every serious inverse-physics claim must include an unseen-intervention/counterfactual transfer test.

Generic identifiability is also demoted as novelty: 2026 Physics-from-Video work and IRIS make this a direct prior-art area.

### Next falsification
Move the same questions into Vulkax's actual nonlinear APIC/MPM + Neo-Hookean solver, intentionally introduce model and timestep mismatch, re-fit stiffness, and measure whether apparent fit quality survives a new deformation.

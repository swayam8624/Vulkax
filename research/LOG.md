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

## 2026-09-20 — Solver-integrated model/numerics confounding confirmed

**Branch/SHA:** research/discovery @ `4127023ecba81532a368b8a2249968d84d041c9a`

**Workflow:** run `35509364792`, success.

### Experiment
Actual Vulkax nonlinear APIC/MPM + Neo-Hookean. Synthetic truth E=15 kPa, nu=0.35. Fit model intentionally forced to nu=0.08 and allowed to compensate through E.

### Outcome
E moved to 17.5 kPa (+16.67%). Early fit RMS was 8.543 μm; later same-intervention RMS 65.427 μm; unseen-deformation RMS 122.249 μm.

Independent timestep-refit sweep moved E from 15.0 kPa at dt=1e-4 to 15.75 kPa at dt=5e-4 (+5%) while fitting the same fine-dt truth.

### Interpretation
The mechanism survives escalation from an analytic toy into the Vulkax solver: physical-parameter estimates can absorb both model and numerical error. This remains a known general phenomenon, so novelty must come from an operational verified-rewrite policy, not from merely observing bias.

The truth run's ~9.73% maximum mechanical-energy drift is recorded as a correctness risk. No counterfactual certificate may ignore solver convergence/dissipation.

### Next falsification
Run an actual-solver intervention-design positive test for E/nu observability, then build a many-case certificate dataset to test whether held-out error, identifiability, nonlinear trust error, numerical uncertainty and model disagreement predict unseen-intervention failure.


## 2026-09-20 — Active physical experiment-design positive control passed

**Branch/SHA:** research/discovery @ `eb90005917593f47d25974621bf3ae518c794160`
**Workflow:** `35509650146`, success.

Weak synthetic deformation + 20 μm deterministic measurement noise fit E=16.5 kPa and nu=0.425 instead of 15 kPa / 0.35. A finite-difference sensitivity search selected a mixed deformation with condition number 4.46 versus 28.15 for repeating the weak experiment. Adding that observation recovered the exact grid truth. This is enabling machinery, not novelty evidence.

### Next falsification
Generate a multi-case solver-integrated counterfactual dataset and test whether any pre-truth evidence signal predicts unsafe unseen-intervention error. If held-out, identifiability, numerical refinement and model/scheme disagreement fail to concentrate errors, kill or redesign the certificate direction.


## 2026-09-20 — One-shot certificate fails; held-out ranking survives

**Branch/SHA:** research/discovery @ `6f2c67801d7e0467fe1fefd376e50d738f261fb5`
**Workflow:** `35509956517`, success.

### Outcome
108 solver-integrated cases: 22 safe / 86 unsafe at the 10% target-counterfactual criterion. Held-out RMS ranked failures strongly (AUC 0.936), but the safest 25% still had 29.6% unsafe cases. The untrained multi-signal composite underperformed held-out alone (AUC 0.780). Raw fit RMS was anti-informative (AUC 0.281).

### Decision
The simple one-shot certificate is **not sufficient** and is retained as a negative result. Do not tune a threshold on this same dataset and call it solved.

### Next falsification
Close the loop. Refuse weak-evidence cases, choose the deformation with highest material sensitivity, acquire new evidence, re-fit, then test a *different* target deformation. Determine which failure classes repair (parameter ambiguity) and which persist (numerical/model-class inadequacy).

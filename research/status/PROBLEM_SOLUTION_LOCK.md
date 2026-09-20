# Vulkax locked research problem and solution — 2026-09-20

## Working title

**Falsify, Repair, Re-Verify: Intervention-Conditioned Adequacy Routing for
Physically Editable Captured Worlds**

Internal method name: **CAR — Counterfactual Adequacy Router**.

## Locked problem

Current inverse-physics / physical-world pipelines usually return a reconstructed
scene, material parameters, or a simulated future once observations have been fit.
That is insufficient for an editable captured world.

A requested edit q changes the physical regime. The question is not whether the
world has low training error; it is whether the *particular counterfactual required
by q* is supported by the evidence and by the simulator.

Different failures require different actions:

- insufficient evidence → acquire a discriminating physical observation;
- wrong boundary/support assumption → repair the structural support model;
- unresolved numerical error → refine/alter numerical discretization;
- model-family inadequacy → escalate the physical model;
- ambiguous correspondence/observation support → recapture or rebind;
- adequate model/evidence → allow the counterfactual only after independent
  re-verification.

Existing identifiability, UQ, selective prediction, active experiment design and
simulation V&V address pieces of this problem, but the Vulkax target is the
**requested-edit-conditioned composition of diagnosis, repair routing and disjoint
re-verification**.

## Proposed novel mechanism: Counterfactual Adequacy Graph

For each requested rewrite q, construct a directed adequacy graph whose nodes are
falsifiable assumptions rather than one scalar confidence score.

A node k owns:

- a prerequisite set;
- a pair or family of matched *witness worlds* differing only along adequacy axis k;
- a target-conditioned discrepancy d_k(q);
- a pass / refuse / unresolved state;
- a cause code;
- a permitted repair action;
- evidence provenance and leakage guards.

Minimum graph:

1. Observation Evidence
2. Observation ↔ Physical Support
3. Numerical Time Resolution
4. Numerical Space / Transfer Resolution
5. Model Family Adequacy
6. Parameter / Identifiability Adequacy
7. Requested Counterfactual Verification

The graph is not evaluated as an unstructured average. A failure blocks descendants
whose interpretation would otherwise be confounded.

## Target-conditioned witness discrepancy

For a requested edit q and observable Y_q, define an effect-normalized witness
disagreement

    d_k(q) = || Y_q(W_k^a) - Y_q(W_k^b) || /
             max(|| Y_q(W_ref) - Y_0 ||, epsilon)

where W_k^a and W_k^b are matched worlds differing only in the tested adequacy
assumption after prerequisite axes have passed.

Examples:

- numerical witness: adjacent converged timestep or particle/grid levels;
- transfer witness: APIC vs alternate transfer after adequate discretization;
- support witness: endpoint-only vs independently calibrated finite fixture support;
- model witness: competing constitutive families after numerical adequacy;
- evidence witness: posterior / model-family disagreement before and after a
  newly acquired intervention.

The exact estimator may change; the *matched witness and prerequisite principle*
is locked.

## Repair routing

A failed node is not merely an abstention.

### Evidence-insufficient

Choose the next physical observation/intervention using
**target-risk reduction**, not generic parameter information alone.

Candidate utility:

    U(a | q) =
        [ predicted reduction in target witness disagreement / target risk ]
        / acquisition_cost(a)

Generic Fisher/Jacobian information gain is a baseline, not the claimed novelty.

### Support/boundary mismatch

Estimate support only from calibration observations that do not use target
simulation error. Apply the structural repair and evaluate on disjoint repeats.

The current GAUGE odd/even finite-support protocol is the first real-data example.

### Numerical inadequacy

Do not fit a new physical parameter to absorb discretization error. Route to the
numerical verifier (time, particle/grid, phase, transfer), and refuse while the
target effect is not separated from numerical disagreement.

### Model-family inadequacy

Only after numerical/support witnesses are sufficiently controlled, compare model
families. If no family explains independent evidence, refuse and escalate model
class rather than re-fitting parameters indefinitely.

### Correspondence / observation-support mismatch

Request additional markers/views/rebinding when the observable support cannot
distinguish a physical-body support assumption.

## Re-verification contract

A repair is successful only when:

1. calibration and repair-selection evidence is disjoint from the final target;
2. target thresholds were frozen before target labels;
3. the repaired world passes its previously failing adequacy node;
4. earlier prerequisite nodes remain passed;
5. an independent target intervention is evaluated after repair;
6. risk/coverage and cause-attribution metrics are reported, including zero-coverage
   failures as failures rather than safety wins.

## Required baselines

The flagship evaluation must include:

- always-answer / always-simulate;
- always-refuse;
- training-fit residual;
- held-out replay residual;
- local identifiability / smallest singular value;
- generic uncertainty or ensemble disagreement;
- scalar multi-evidence certificate;
- generic Fisher/Jacobian experiment design;
- active model-discrimination baseline where applicable;
- selective-prediction / abstention baseline;
- simulator V&V-style multi-axis score;
- no-routing ablation (same diagnostics but no cause-specific action);
- no-disjoint-target ablation.

## Required benchmark structure

### Controlled synthetic truth

Needed for cause labels that cannot be known in real data:

- evidence insufficiency;
- parameter misspecification;
- wrong Poisson constraint / constitutive family;
- transfer mismatch;
- timestep error;
- spatial discretization error;
- correspondence/support mismatch.

### GAUGE real deformables

Primary measured-data role:

- show that a no-fit physical simulator can be worse than a kinematic null;
- derive a structural support hypothesis without target-error fitting;
- test repair on disjoint held-out repeats;
- keep material fitting locked until structural adequacy earns it.

### IRIS or second measured regime

Use a simpler-dynamics benchmark to test whether CAR's evidence/refusal logic
transfers beyond one deformable MPM case.

## Primary paper claims allowed if evidence succeeds

1. **Problem formulation:** physical edit reliability is an
   intervention-conditioned adequacy-routing problem rather than global scene
   confidence.
2. **Method:** Counterfactual Adequacy Graph with prerequisite-aware matched
   falsification witnesses and cause-specific repair actions.
3. **Active repair:** target-risk-reduction evidence acquisition converts some
   refusals into independently verified counterfactuals.
4. **Reason-coded refusal:** the system separates repairable evidence insufficiency
   from structural, numerical and model-family inadequacy.
5. **Measured validation:** at least one real GAUGE failure is repaired using an
   independently derived structural hypothesis and held-out evaluation.
6. **Negative-result discipline:** unresolved numerics/model families cause refusal
   rather than parameter compensation.

Claims explicitly forbidden:

- “first Gaussian + MPM”;
- “first physics from video”;
- “first material inference from video”;
- “first identifiability analysis”;
- “first active experiment design”;
- “first simulator credibility framework”;
- “first counterfactual physical world model”;
- “guaranteed correct counterfactuals”.

## Tier-1 readiness gates

Do not call the method paper-ready until all are true:

- fresh-domain cause-attribution accuracy materially exceeds scalar baselines;
- accepted-counterfactual unsafe rate meets the frozen target at useful coverage;
- at least two qualitatively different refusal causes are correctly routed;
- at least one repairable and one irreparable failure family are demonstrated;
- target-risk evidence acquisition beats generic information/max-motion/random
  choices on independent targets;
- GAUGE definitive structural-support gate passes or is replaced by another real
  independently validated repair case;
- a second measured regime demonstrates transfer;
- numerical inadequacy is explicitly bounded or refused, never hidden;
- all headline results reproduce from one integrated research head.

## Current status at lock time

Strong evidence already available:

- synthetic wrong-model counterfactual failures;
- closed-loop evidence repair separating adequate from wrong model/transfer cases;
- negative certificate validations retained;
- deep timestep convergence forensic;
- unresolved spatial/transfer forensic;
- GAUGE all-repeat no-fit failure vs affine null;
- stable measured effective support span;
- 10/10 pilot held-out dual-metric support-repair wins.

Still required:

- definitive high-resolution GAUGE support result;
- target-risk acquisition implementation and generic-OED comparison;
- unified adequacy-graph implementation rather than disconnected probes;
- fresh post-lock synthetic benchmark;
- second measured regime;
- final novelty sweep before submission.

This problem/solution pair is now locked unless a kill condition in the hypothesis
filter is triggered.

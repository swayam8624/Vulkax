# DCS Limitations, Failure Modes, and Kill Criteria

DCS should be killed, narrowed, or reported with explicit limitations when the
evidence requires it.

## Fundamental limitations

### 1. Higher-order cancellation amplifies noise
Signed differences can subtract large nearly equal quantities. High-order stencils
may have large coefficient norms and therefore amplify sensor noise, tracking error
and floating-point error.

Required mitigation:
- report coefficient norm/noise gain;
- regularize stencil selection;
- compare to raw-response SNR;
- stop increasing order when the witness becomes noise dominated.

### 2. Intervention count can grow combinatorially
A full k-way counterfactual cumulant requires up to 2^k subset responses.

Required mitigation:
- sparse/adaptive stencil synthesis;
- stop at the first separating order;
- exploit symmetry;
- never claim arbitrary high-order scalability without measurements.

### 3. Smooth local expansions fail at events
Contact onset, slip/stick transitions, fracture, topology changes, and plastic yield
can make Taylor-style local order arguments invalid.

DCS response:
- treat event crossing as a separate regime;
- use finite algebraic cumulants/path-aware witnesses instead of derivative claims;
- never interpret a discontinuous witness as a smooth response tensor.

### 4. Hysteresis/path dependence breaks subset semantics
For irreversible or history-dependent materials, F(S) is not fully defined by an
unordered intervention subset.

Required extension:
- ordered intervention words/paths;
- commutators/loop witnesses may be useful here, but they are supporting tools;
- report path dependence explicitly.

### 5. A zero witness does not prove correctness
Symmetry or an unfortunate stencil can make incorrect models share the same
dark-field response.

Required mitigation:
- active witness search over feasible intervention space;
- multiple independent observables;
- no "certificate of correctness" language.

### 6. Shared model-form error
If all candidate worlds omit the same mechanism, candidate-model dispersion can be
small while all candidates are wrong.

Required mitigation:
- compare to measured dark-field response, not only between-model disagreement;
- include deliberately expanded model families;
- retain refusal when real witness lies outside candidate support.

### 7. Numerical artifacts can mimic physical interactions
The current Vulkax spatial refinement studies already show large unresolved
discretization effects.

Required mitigation:
- witness scale-flow gate;
- no material refitting to absorb numerical error;
- report unresolved spatial/transfer witnesses as unresolved.

### 8. Physical experiments may be unavailable
A captured scene may not permit safe, reversible, repeatable interventions.

Consequence:
- DCS becomes observational/retrospective only;
- active DCS claims do not apply to such scenes.

### 9. Acquisition cost
Multiple signed interventions may be slower or more expensive than one passive
capture.

DCS must therefore demonstrate that added experiments prevent consequential false
repairs rather than merely improving a score.

### 10. Observation correspondence
A dark-field residual can arise from tracker/correspondence failure rather than
physics.

Required controls:
- synthetic correspondence perturbation family;
- multi-channel witness consistency;
- explicit cause ambiguity when not separable.

## Novelty kill criteria

Demote DCS as flagship if a direct prior method is found that already combines:
1. captured/executable 3D physical-world repair;
2. signed intervention ensembles chosen to annihilate agreed lower-order response;
3. higher-order witness used specifically to detect observationally successful but
   physically worse repairs;
4. adaptive first-separating mechanism order;
5. numerical-fidelity survival before interpreting the witness;
6. rewrite accept/refuse decision.

Individual prior art in higher-order FRFs, Volterra kernels, Möbius inversion,
metamorphic testing, finite differences, OED, active model discrimination,
cumulants, or RG is not itself a collision, but must be cited and cannot be claimed
as novel.

## Experimental kill criteria

Kill or substantially narrow DCS if:
- D1 constructed positive control fails;
- fresh D2 deceptive-repair AUPRC does not materially exceed held-out residual;
- DCS separation gain disappears after realistic measurement noise;
- numerical-witness flow changes the sign/ranking of most claimed mechanisms;
- active DCS does not outperform raw model-output separation on matched cost;
- no fresh measured deceptive repair can be found prospectively;
- DCS rejects nearly everything, yielding unusable coverage;
- success depends entirely on the already-known GAUGE fixture case.

## Claims DCS must never make

- proof that a world is physically correct;
- universal physical certificate;
- universal superiority of high-order response;
- novelty of finite differences/Taylor expansion/cumulants;
- correctness outside tested intervention regimes;
- correctness through unmodeled event changes.

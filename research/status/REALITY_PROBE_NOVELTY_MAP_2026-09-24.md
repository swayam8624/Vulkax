# Reality Probe Novelty Map — 2026-09-24

This file is a claim-positioning guard for the manuscript. It does not add a new
experimental claim and it does not replace the locked result ledgers.

## Central contribution

**Reality Probe is a rewrite-level verification contract for executable captured
worlds.** A candidate physical rewrite is proposed using fitting/ordinary
evidence; a separately reserved physical observable or intervention is then
evaluated; uncertainty is carried into a standardized evidence score; and the
rewrite transaction ends in **SUPPORT**, **VETO**, or **UNRESOLVED** before
commit/rollback.

The contribution is the operational composition and evidence discipline around
an individual physical rewrite under information limits, together with a
falsification-driven evaluation showing both successful and failed verification
channels.

## Explicitly not claimed as new

| Adjacent idea | What prior work already supplies / what this paper does **not** claim |
|---|---|
| Inverse physics / system identification | Estimating physical parameters from observations is established. IRIS and the cited inverse-physics literature are proposal/measurement context, not the Reality Probe novelty. |
| Physics-aware NeRFs / Gaussian dynamics / MPM | Executable visual representations and physics simulation are established. The paper begins after a candidate physical rewrite exists. |
| Structural identifiability | The fact that parameters can be underdetermined is established. Reality Probe does not claim a new general identifiability theory. |
| VVUQ / simulation credibility | Verification, validation, uncertainty quantification, and model discrepancy are established fields. The novelty claim is scoped to a rewrite transaction in a persistent captured world, not generic VVUQ. |
| Optimal experimental design | Choosing informative interventions is established. Reality Probe uses information-channel adequacy as an empirical constraint but does not claim OED itself. |
| Abstention / selective prediction | Refusing low-confidence decisions is established. UNRESOLVED is not claimed as a new decision-theory primitive; it is part of the rewrite contract. |
| Finite differences / moment cancellation | Signed contrasts and lower-order cancellation are established mathematics. DCS is one tested probe inside the framework, not the central novelty. |
| Finite-amplitude pendulum physics | The pendulum relation is textbook physics. Its role is a matched physical verification observable in a locked real-video experiment. |
| Small-angle pendulum baseline | Textbook approximation, retained only as a diagnostic baseline and not presented as a strong modern competitor. |
| Gravity / free-fall estimation | Not novel. The IRIS free-fall lane is a failed second-domain replication and is preserved as such. |

## What the experiments are allowed to establish

1. **Deceptive rewrite exists as an operational failure mode:** ordinary
   improvement can coexist with worse untouched physical behavior.
2. **Probe validity is not enough:** DCS can work on a constructed control and
   still provide zero useful prospective coverage under frozen realistic
   regimes.
3. **Changing the information channel matters:** known-force compliance raises
   the synthetic standardized-evidence scale by 11.46x while still remaining
   below the frozen decision boundary.
4. **A matched channel can work on measured video:** the locked 10-video IRIS
   pendulum final set gives 90/110 correct controlled decisions for the
   finite-amplitude probe versus 40/110 for the matched small-angle diagnostic
   baseline, with 10/10 placebo cases left UNRESOLVED.
5. **Success is domain-conditional:** the separately frozen IRIS free-fall
   validation fails (111.7% median acceleration relative error) and is not
   rescued after validation.

## Wording guard

Prefer:
- "separately reserved verification channel/evidence"
- "repository-locked before final-data access"
- "standardized evidence score"
- "rewrite-level support/veto/unresolved transaction"
- "information-limited in the tested regime"
- "10 physical videos with 110 nested controlled cases"

Avoid unless literally true for a specific experiment:
- "independent sensor" / "independent sensing"
- "preregistered"
- "universal verifier"
- "statistical z-score"
- "110 independent experiments" or "220 independent experiments"
- "novel gravity/pendulum estimator"
- "DCS mathematics is new"
- "free-fall confirmation"

## Reviewer-threat mapping

- **R04 / R19:** addressed by narrowing novelty from generic VVUQ/credibility to
  individual rewrite decisions in an executable captured world.
- **R06 / R07 / R32:** inverse-physics estimators and IRIS are baselines/proposal
  sources, not the novelty.
- **R14:** manuscript should use "standardized evidence score" for the frozen
  score unless a statistical z interpretation is explicitly justified.
- **R16:** abstention itself is not claimed as novel.
- **R22:** use "repository-frozen before access," not "preregistered."
- **R36:** the causal paper story is rewrite -> deceptive ordinary improvement ->
  weak channel -> information diagnosis -> matched channel -> locked positive
  result -> preserved cross-domain failure.

## Claim boundary

The strongest defensible paper claim is conditional:

> Reality Probe provides an evidence-governed transaction for deciding whether
> to commit a physical rewrite in a captured world. In the tested experiments,
> useful verification depends on whether the reserved physical channel contains
> discriminating information for that rewrite; when it does not, the system must
> remain unresolved or retire the lane rather than manufacture certification.

This claim must remain subordinate to the exact locked result ledgers and any
post-final reviewer-hardening analysis.

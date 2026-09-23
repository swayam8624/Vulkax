# Reality Probe Validation / Related-Work Map — 2026-09-23

This map is for manuscript positioning and experiment design. It is not a novelty
claim by itself.

## 1. Verification, validation, and uncertainty quantification

The paper should explicitly distinguish:

- **code / numerical verification** — whether the computational implementation
  solves the intended mathematical problem with controlled numerical error;
- **model validation** — whether the model is adequate for the physical use case
  against independent evidence;
- **uncertainty quantification** — how measurement, numerical, model-form, and
  parameter uncertainty propagate into a decision.

Useful standards and guidance:

- ASME VVUQ overview:
  https://www.asme.org/codes-standards/publications-information/verification-validation-uncertainty
- ASME VVUQ 1 terminology:
  https://www.asme.org/codes-standards/find-codes-standards/v-v-10-verification-and-validation-in-computational-solid-mechanics
- ASME V&V 10 / solid mechanics:
  https://www.asme.org/codes-standards/find-codes-standards/v-v-10-verification-and-validation-in-computational-solid-mechanics
- ASME VVUQ 10.2, credibility assessment:
  https://www.asme.org/codes-standards/find-codes-standards/vvuq-10-2-2024-credibility-assessment-of-computational-models
- NIST, *Credibility Consideration for Digital Twins in Manufacturing*:
  https://www.nist.gov/publications/credibility-consideration-digital-twins-manufacturing

Reality Probe should be positioned as a **decision-facing physical interrogation
contract inside a captured/executable-world update loop**, not as a replacement
for the larger VVUQ discipline.

## 2. Digital-twin credibility

NIST's 2026 Digital Twins Workshops Summary Report continues to identify VVUQ and
trustworthy use of digital twins as open practical challenges:

https://www.nist.gov/publications/digital-twins-workshops-summary-report

The manuscript should therefore avoid claiming that "digital-twin credibility" is
unsolved in general. The narrower question is whether a proposed *local rewrite*
can be checked with evidence that was not used to produce that rewrite, with
explicit abstention when the information budget is insufficient.

A recent evidence-fusion example for digital-twin credibility evaluation should
also be compared directly:

https://www.sciencedirect.com/science/article/abs/pii/S1569190X25000875

Comparison dimensions:
- what is treated as evidence;
- whether evidence sources are independent or correlated;
- whether the method returns an abstention/unresolved state;
- how thresholds are calibrated;
- whether ground truth interventions exist;
- whether the method evaluates a static twin or a proposed state update.

## 3. Active system identification / experiment design

Reality Probe's intervention selection overlaps conceptually with active system
identification, optimal experimental design, Fisher-information methods, and
model-discrimination experiments. The paper must therefore compare against at
least a Fisher/Jacobian selector and a same-cost raw intervention bundle, which the
repository already implements in D2-D4V.

The contribution cannot be "we use active experiments." The potentially distinct
contract is:

**proposal from one evidence channel -> independent physical interrogation ->
support / veto / unresolved -> transactional commit or rollback.**

## 4. Hypothesis testing and selective prediction

The three-way decision should be evaluated as selective prediction rather than
binary accuracy alone.

Required plots/tables:
- risk versus coverage;
- false support on truly veto cases;
- false veto on truly support cases;
- false assertion on intentionally unresolved/placebo cases;
- calibration/reliability of the evidence magnitude;
- threshold sensitivity shown as diagnostic only, never used to retune final data.

## 5. Formal verification terminology

The paper should reserve "formal verification" for mathematical/software proof
techniques. Reality Probe performs empirical/counterfactual physical verification
of a proposed captured-world update. This terminology distinction prevents a
reviewer from interpreting the method as a formal-methods contribution.

## 6. Manuscript positioning sentence

A defensible positioning sentence is:

> Reality Probe studies whether a captured-world update should be committed when
> the proposal is generated from one observation channel but evaluated by a
> separately frozen physical interrogation, with support, veto, and unresolved
> outcomes and explicit accounting for numerical and measurement uncertainty.

That sentence says what the system does without claiming that its component
finite-difference, active-design, uncertainty, or digital-twin credibility
machinery is individually novel.

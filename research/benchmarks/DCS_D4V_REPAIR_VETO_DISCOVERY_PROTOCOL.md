# DCS D4V Pair-Specific Deceptive-Repair Veto Discovery Protocol

Date: 2026-09-20
Status: **discovery only**, frozen before opening this partition.

This is a deliberate narrowing after D2 and D3 falsified DCS as a universal
candidate-world ranking method.

## Scientific question

When an ordinary held-out metric proposes a physical-model repair, can a
**pair-specific dark-field experiment** determine whether that repair actually moves
the intervention mechanism closer to reality, farther from reality, or remains
unresolved?

This is the task that motivated DCS in the first place.

## Decision structure

For a baseline model A and proposed repair B:

1. A and B are fit only on calibration observations.
2. Ordinary held-out observations propose B only if:
   L_holdout(B) < L_holdout(A).
3. The DCS probe is a separate physical acquisition not used in fitting or proposal.
4. A pair-specific order-2 annihilating stencil is synthesized using only predictions
   from A and B plus their nominal-vs-half-dt numerical responses.
5. The physical probe yields D_real.
6. Compute dark-field witness errors:
   e_A = RMS(D_A - D_real)
   e_B = RMS(D_B - D_real).
7. Define standardized repair progress:
   P = (e_A - e_B) / sigma_pair.

Interpretation:
- P >= +2: mechanism evidence supports the repair;
- P <= -2: mechanism evidence vetoes the repair;
- otherwise: unresolved / refuse to decide.

The +/-2 reference is inherited unchanged from the existing mechanism-resolution
standard. It is **not tuned on D4V**.

## Hidden evaluation label

The stronger unseen counterfactual target remains:
- shear = +0.095
- axial = +0.075

A proposed repair is labelled **deceptive** only for evaluation when:
- held-out error improves, but
- stronger-target error worsens.

A proposed repair is **beneficial** when both held-out and stronger-target error
improve.

Target labels are never visible to proposal generation, stencil synthesis, or the
veto policy.

## Fresh D4V truth partition

Disjoint from all previous DCS truth values:

- E = {13,625; 15,875; 17,425} Pa
- nu = {0.25; 0.35}

Total: **6 truth worlds**.

Truth:
- APIC
- dt=2.5e-5 s.

## Calibration observations

Same frozen single-axis set:
- (+0.025,0)
- (-0.025,0)
- (0,+0.025)
- (0,-0.025)

## Ordinary held-out proposal observations

Separate from the DCS probe:

- (+0.040,+0.040)
- (-0.040,+0.040)
- (+0.040,-0.040)

Ordinary proposal metric:
RMS over all marker coordinates across these three held-out responses.

## Candidate model families

Unchanged:
1. APIC, dt=1e-4, E/nu fit
2. PIC, dt=1e-4, E/nu fit
3. APIC constrained nu=0.10, E fit
4. coarse APIC, dt=4e-4, E/nu fit

Fit grid:
- E={12,000;15,000;18,000} Pa
- nu={0.20;0.30;0.40}

Every ordered pair A->B with strictly better held-out error for B becomes a repair
proposal.

## DCS physical probe basis

Fixed 3x3 lattice:
- shear in {-0.060,0,+0.060}
- axial in {-0.060,0,+0.060}

Pair-specific stencil:
- order 2 only;
- exact annihilation of constant and first-order response;
- direct witness-space nominal-vs-half-dt numerical uncertainty;
- no order tuning.

D3 selected order 2 on all six discovery worlds and order 3 was strictly weaker.
D4V therefore freezes k=2 rather than continuing response order escalation.

## Pair uncertainty

For the pair-specific stencil mu:

Measurement/repeat variance:
- propagated by sum_i w_i^2.

Numerical variance:
- for A: RMS^2(D_mu[A_h]-D_mu[A_h/2])
- for B: RMS^2(D_mu[B_h]-D_mu[B_h/2])

Pair scale:
sigma_pair = sqrt(
  sigma_meas,mu^2 +
  sigma_repeat,mu^2 +
  sigma_num,A,mu^2 +
  sigma_num,B,mu^2
).

This is a conservative scale for the repair-progress statistic, not a formal
Gaussian confidence interval.

## Fair baselines

For every identical repair proposal:

1. **Accept ordinary repair** — no extra verification.
2. **DCS pair-specific veto** — flagship hypothesis.
3. **Same-cost raw 9-probe bundle** — all DCS measurements, no signed annihilation.
4. **Raw pair-specific single probe** — probe maximizing pairwise standardized
   candidate separation.
5. **Fisher E/nu sensitivity probe**.
6. **Maximum-motion probe**.

Extra-probe baselines use the same support/veto/refuse concept where a standardized
progress statistic can be defined.

## Discovery metrics

Primary:
- number of ordinary repair proposals;
- deceptive proposals;
- beneficial proposals;
- DCS resolved coverage;
- deceptive-repair veto recall;
- veto precision;
- false-veto rate on beneficial repairs;
- beneficial-repair support rate;
- unresolved rate.

Secondary:
- median standardized progress for deceptive vs beneficial repairs;
- pair-family stratification;
- stencil moment residual;
- witness-space numerical RMS.

## Discovery advancement rule

D4V is promising enough to freeze a new validation partition only if:

1. at least 4 deceptive repair proposals exist naturally;
2. DCS resolves at least 30% of all proposals;
3. among resolved deceptive repairs, veto recall >=60%;
4. DCS false-veto rate on resolved beneficial repairs <=20%;
5. DCS veto precision >=70%;
6. same-cost raw bundle does not dominate DCS on both deceptive recall and false-veto
   rate;
7. moment residual <=1e-9 for every synthesized stencil.

These are discovery advancement rules, not universal guarantees.

If D4V fails, DCS should be narrowed to a diagnostic visualization/analysis tool or
killed as the Vulkax flagship. Do not rescue it by raising response order or tuning
the six D4V truths.

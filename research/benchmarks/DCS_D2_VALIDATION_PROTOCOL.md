# DCS D2 Fresh Solver-Native Validation Protocol

Frozen: 2026-09-20, before any D2-validation labels/results are generated.

This protocol is separate from the completed four-world discovery studies.

## Purpose

Test whether automatically synthesized order-2 dark-field intervention contrasts
predict stronger unseen counterfactual ranking better than ordinary/raw experiment
selectors on fresh MPM truth worlds.

This does **not** validate the final paper by itself. It is the first controlled
post-discovery test.

## Fresh truth set

Discovery truth values were:
- E = {13,750; 16,250} Pa
- nu = {0.24; 0.36}

Validation uses disjoint off-grid values:

- E = {12,875; 14,375; 15,625; 17,125} Pa
- nu = {0.22; 0.28; 0.34; 0.38}

Total: **16 truth worlds**.

Truth simulator:
- APIC
- dt = 2.5e-5 s
- same physical body/grid/horizon as D2 discovery
- no candidate fitting on target/witness data.

## Candidate fitting

Calibration interventions remain single-axis:
- (+0.025, 0)
- (-0.025, 0)
- (0, +0.025)
- (0, -0.025)

Candidate families remain frozen:
1. APIC, dt = 1e-4, E/nu fit on coarse grid
2. PIC, dt = 1e-4, E/nu fit
3. APIC with constrained nu=0.10, E fit
4. coarse APIC, dt = 4e-4, E/nu fit

Parameter search grid remains:
- E = {12,000; 15,000; 18,000} Pa
- nu = {0.20; 0.30; 0.40}

## Probe intervention set

Order-2 DCS uses the same five-point set:
- (+0.060,+0.060)
- (+0.060,-0.060)
- (-0.060,+0.060)
- (-0.060,-0.060)
- (0,0)

The target remains hidden from selector construction:
- shear = +0.095
- axial = +0.075

## Uncertainty normalization

Measurement and repeat noise use the frozen synthetic observation amplitude.

Numerical uncertainty is **not** the discovery placeholder.

For each fitted candidate model:
1. evaluate all probe points at its nominal dt;
2. evaluate the same probe points at dt/2 using the same fitted physical
   parameters and transfer family;
3. compute pointwise per-observable RMS disagreement;
4. use the maximum squared disagreement across candidates/probe points as the
   numerical-variance term supplied to every selector for that truth world.

This deliberately favors caution. It uses no truth/target labels.

If a candidate dt/2 evaluation becomes invalid, numerical uncertainty is treated as
unresolved and DCS must refuse interpretation rather than silently drop the model.

## Frozen selectors

1. DCS order-2 maximin standardized annihilating stencil
2. same-cost raw bundle over all five probe points
3. raw maximin single probe point
4. Fisher/Jacobian local E/nu sensitivity point
5. maximum-motion point
6. deterministic pseudo-random point

No selector may inspect:
- truth material parameters;
- target response;
- target error;
- final pairwise correctness labels.

## Primary result

For every truth world, compare every candidate pair.

The stronger unseen target defines which candidate is actually better.

For each selector report:
- pairwise target-ranking agreement;
- raw-metric mirage count;
- correction rate on raw-metric mirages;
- new-error rate on pairs the raw selector originally ranked correctly;
- coverage/refusal if mechanism resolution is below threshold.

## Frozen advancement gate

DCS advances beyond order-2 solver-native validation only if all are true:

1. pairwise target-ranking agreement > raw maximin;
2. pairwise target-ranking agreement > same-cost raw bundle;
3. DCS corrects at least 60% of raw-metric mirage pairs;
4. DCS introduces fewer new ranking errors than mirages it corrects;
5. the result is not carried by a single candidate failure family;
6. synthesized stencils satisfy moment residual <= 1e-9;
7. numerical uncertainty does not make >50% of truth worlds unresolved.

No threshold may be relaxed after results are visible.

If this gate fails:
- preserve the result;
- do not tune amplitudes on these 16 truth worlds;
- move to adaptive order / richer intervention basis only on a **new** discovery
  partition, then define another fresh validation partition.

## Mechanism-resolution threshold

For this D2 validation, a selected dark-field witness must achieve standardized
between-model worst-case separation >= 2.0 to count as resolved.

This is a research gate, not a universal physical confidence guarantee.

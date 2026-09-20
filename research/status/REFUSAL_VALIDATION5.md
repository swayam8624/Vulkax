# Refusal-policy Validation-5 — end-to-end computational evidence

## Why Validation-4 failed

Validation-4 showed that adaptive timestep refinement of the **final target rollout only** can declare numerical convergence while the inverse parameters remain biased by the coarse solver used during fitting. The accepted coarse-APIC cases therefore remained unsafe.

This is not repaired by tuning a new threshold. The computational evidence has to pass through the entire inverse-to-counterfactual pipeline.

## Frozen mechanism before Validation-5 labels

For each case:

1. acquire the same sensitivity-selected physical evidence;
2. fit the inverse material parameters at the variant's base solver timestep;
3. predict the requested counterfactual;
4. halve timestep;
5. **refit the material parameters using the same training + evidence data at the refined timestep**;
6. re-evaluate held-out and evidence residuals;
7. recompute the target counterfactual;
8. continue refinement for at most three levels;
9. require two consecutive end-to-end target changes <= 5% of target effect, after the solver reaches dt <= 1e-4 s;
10. require final evidence residual <= 2x measurement noise and held-out residual <= 2x measurement noise.

No Validation-4 outcome is used to choose these thresholds.

## Stronger synthetic truth

Validation-5 truth is integrated at 2.5e-5 s, four times finer than the nominal fine fitting solver (1e-4 s). This prevents the validation target from being identical to the numerical fidelity used by the initial inverse solver.

Fresh domain changes:
- E and nu truths;
- marker IDs;
- observation times;
- noise level (40 micrometres);
- training deformation;
- evidence interventions;
- target deformation;
- deterministic noise phases.

## Comparator

The same cases also run the old "fit once, refine target only" mechanism. This isolates whether re-fitting under refined numerics actually repairs counterfactual validity.

## Predeclared success

The refine-refit policy must achieve:
- accepted unsafe rate <= 10%;
- coverage >= 25%.

Unsafe means unseen-target relative error > 10%. Failure remains a negative result; no post-hoc threshold adjustment is permitted.


## Result — predeclared gate failed

GitHub Actions run `35517464505` completed successfully at
`c8bf23a9a68fbe8a8b2435741df18463c9dfec33`. The experiment itself is a
negative result.

Across the 64 fresh Validation-5 cases, accept-all unseen-target unsafe rate was
**78.125%**. Neither the old target-only refinement policy nor the end-to-end
refine/refit/revalidate policy accepted any case:

- target-only coverage: **0 / 64**;
- refine/refit coverage: **0 / 64**;
- predeclared >=25% coverage / <=10% unsafe target: **failed**.

The refine/refit mechanism did reduce median target error for some variants, but
the inverse-to-counterfactual trajectory did not meet the locked convergence
contract:

| variant | median pre error | median target-only error | median refine/refit error | median final adjacent-change fraction |
| --- | ---: | ---: | ---: | ---: |
| coarse APIC | 45.41% | 12.84% | 11.70% | 12.60% |
| fine APIC | 31.07% | 26.27% | 10.98% | 9.89% |
| constrained nu | 26.52% | 47.98% | 24.83% | 12.41% |
| PIC | 50.08% | 88.16% | 88.52% | 85.58% |

Median consecutive stable refinement count was zero for every variant. Fine APIC
also moved by a median **1500 Pa** in E and **0.05** in nu across refinement;
the constrained-nu family moved by a median **3750 Pa** in E. Coarse APIC
usually retained the same grid-fit parameters yet still failed the numerical
target-change gate, so parameter-grid jumps alone cannot explain the failure.

### Decision

Do **not** relax the 5% convergence budget and do not tune a new acceptance
threshold on Validation-5 labels.

Before any Validation-6 policy is defined, run a label-free solver convergence
forensic that separates:

1. forward time-step convergence at fixed physical parameters;
2. inverse-parameter convergence as the forward timestep is refined;
3. target-counterfactual convergence after refitting;
4. parameter-grid quantization from genuine solver non-convergence.

Only after that diagnostic identifies a mechanism may a new certificate be
pre-registered on a fresh validation domain.

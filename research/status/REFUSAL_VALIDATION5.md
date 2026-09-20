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

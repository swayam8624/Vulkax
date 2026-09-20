# Refusal-policy Validation-4 — pre-registration

Validation-3 restored useful coverage with adaptive timestep refinement, but its accepted unsafe rate exceeded the predeclared 10% ceiling. The next test must not tune on Validation-3.

## Frozen hypothesis

A refusal policy should distinguish **why** it lacks trust. Numerical mismatch can sometimes be repaired with additional computational evidence (timestep refinement), while model-family/identifiability warning signs should block acceptance rather than be "repaired" numerically.

## Policy locked before Validation-4 outcomes

Base gates remain unchanged:

1. selected physical-evidence residual <= 2x known measurement noise;
2. same-experiment held-out residual <= 2x known measurement noise;
3. refine target simulation by dt/2 up to three levels;
4. require adjacent refined target predictions to differ by <= 5% of predicted target effect.

One additional reason-coded gate is permitted:

- classify model-family inadequacy from `selected_condition`;
- choose direction and threshold using **only the original synthetic calibration set** and the already-defined controlled label `constrained_nu or pic`;
- choose the deterministic balanced-accuracy optimum with specificity then sensitivity tie-breaks;
- refuse a core-accepted rewrite when that calibration-locked rule flags model inadequacy.

Validation-1, Validation-2, Validation-3, and Validation-4 labels are forbidden from threshold selection.

## Fresh Validation-4 domain

Validation-4 changes all of the following relative to Validation-3:

- off-grid Young's-modulus truths;
- off-grid Poisson-ratio truths;
- marker IDs;
- observation timestamps;
- measurement noise (35 micrometres);
- training deformation;
- candidate evidence interventions;
- target deformation;
- deterministic noise phases.

The simulator-family variants remain controlled so failure causes are known: fine APIC, coarse APIC, PIC mismatch, and constrained-Poisson model mismatch.

## Predeclared success condition

Reason-coded policy must achieve both:

- accepted unsafe rate <= 10% for the unseen target;
- coverage >= 25%.

Unsafe means refined target relative error > 10%.

A failure remains a negative result; thresholds will not be retuned on Validation-4.

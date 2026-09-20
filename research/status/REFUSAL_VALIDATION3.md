# Refusal-policy Validation-3

This branch is a fresh synthetic validation designed after Validation-2 killed the fixed absolute numerical threshold by producing zero coverage.

## Locked mechanism before Validation-3 labels

The new mechanism does **not** loosen the numerical gate. It changes the action after a numerical refusal:

1. acquire sensitivity-selected physical evidence and refit exactly as before;
2. require evidence residual <= 2× known measurement noise;
3. require same-experiment held-out residual <= 2× known measurement noise;
4. evaluate the requested target at the fitted solver timestep;
5. halve timestep repeatedly, up to three refinement levels;
6. require adjacent target predictions to differ by <= 5% of the predicted target effect;
7. optionally require APIC/PIC target disagreement <= 1× target effect as a broad model-family warning.

The 2× noise and 5% numerical budget are the already-used physics-motivated rules. Validation-2 labels are **not** used to choose replacement thresholds.

## Fresh domain

Validation-3 changes:
- off-grid material truths,
- marker IDs,
- observation times,
- measurement noise (30 μm),
- weak training deformation,
- evidence intervention set,
- target deformation,
- deterministic noise phases.

Primary question: can reason-coded numerical refusal be repaired by acquiring **computational evidence** (higher integration fidelity) while retaining useful coverage and <=10% unseen-target error?

# Open questions after first novelty attack

1. Can a wrong deformable model achieve excellent held-out replay yet fail badly under a new intervention in the actual Vulkax MPM system?
2. Does an identifiability diagnostic predict counterfactual error, or merely detect local rank defects?
3. Can a certificate combining identifiability, local nonlinear error and numerical convergence predict unsafe counterfactuals better than any component alone?
4. Can Vulkax distinguish parameter error, model-form error and numerical error operationally rather than only naming them?
5. When two material models fit the same capture, which intervention maximally separates their predicted responses?
6. Can the system correctly refuse an edit and recommend a minimal added measurement that changes the relevant rank/uncertainty?
7. On IRIS, can refusal/verification improve the reliability of physical predictions beyond always-return-a-parameter baselines?
8. For DOT C2, is the rejected rewrite caused primarily by volumetric-model mismatch, rest-state/load ambiguity, numerical error, derivative approximation, or insufficient observation?
9. Do actual Vulkax Operator Influence matrices show low-rank or spatially local structure, rather than only constructed controls?
10. Can a counterfactual trust radius be empirically calibrated across scenes and event regimes?
11. Does trust collapse near contact/event changes in a predictable way?
12. Is the requested edit's signal larger than discretization uncertainty?


13. Can a reason-coded refusal policy distinguish *large but converging* numerical differences from genuinely non-convergent/unstable simulations?
14. Can a scale-normalized convergence ratio transfer across intervention severity better than an absolute timestep-refinement fraction?
15. Does model-family disagreement add predictive value beyond held-out residual once numerical convergence is accounted for?
16. On GAUGE, which unresolved structural assumption dominates the no-fit miss: spatial resolution, transfer dissipation, fixture thickness, cross-section proxy, gravity frame, or constitutive family?
17. Can any independently justified structural repair beat the affine null on a fresh GAUGE task without tuning material parameters?

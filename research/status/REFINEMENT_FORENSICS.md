# Label-free refinement forensics — 2026-09-20

## Purpose

This diagnostic was launched after Validation-5 failed with zero accepted cases. It
uses no safe/unsafe validation labels and does not select a new certificate threshold.
The already-frozen 5% numerical budget is used only as a diagnostic reference.

## Reproducible run

- branch: `research/refinement-forensics`
- evidence head: `489a7180a60273c47f05490fda01b435e223eb52`
- GitHub Actions run: `35518099878` — **success**
- artifact: `vulkax-refinement-forensics-489a7180a60273c47f05490fda01b435e223eb52`
- provenance: `synthetic-label-free-numerical-diagnostic`

Two fixed-physics cases were evaluated:

- on-grid truth: E = 15,000 Pa, nu = 0.30;
- off-grid truth: E = 15,900 Pa, nu = 0.32.

The timestep ladder was
`4e-4, 2e-4, 1e-4, 5e-5, 2.5e-5, 1.25e-5 s`.

## Main result

The forward APIC/MPM target trajectory is **not numerically converged on the
evaluated ladder even when the physical parameters are held at truth**.

Final adjacent-timestep target-change fractions:

| case | fixed-parameter forward | refit counterfactual |
| --- | ---: | ---: |
| on-grid | **11.926%** | **8.878%** |
| off-grid | **11.802%** | **9.813%** |

Neither fixed-parameter case ever crossed the frozen 5% numerical budget. The
fixed-parameter adjacent differences also did not show a clean decreasing trend:
roughly 15.8% -> 13.2% -> 11.6% -> 12.4% -> 11.9% for the on-grid case.

This means Validation-5's failure cannot be repaired by changing only the inverse
optimizer or material-parameter grid. A counterfactual certificate cannot claim
numerical convergence when the underlying fixed-physics forward rollout has not
earned it.

## Inverse-path observations

The inverse fit is additionally timestep-sensitive:

- on-grid fit changed parameter cell four times across the six timesteps, finally
  recovering the exact grid truth only at 1.25e-5 s;
- off-grid fit changed cell three times and ended at 16,500 Pa / 0.35;
- off-grid final parameter error was +600 Pa / +0.03.

Those jumps are real diagnostic evidence, but they are downstream of the more
fundamental forward-convergence failure.

## Decision

Do **not** define Validation-6 yet.

The next experiment must remain label-free and isolate the fixed-physics numerical
mechanism. Required next checks:

1. extend the fixed-parameter timestep ladder below 1.25e-5 s;
2. test whether the adjacent difference approaches an asymptotic regime rather
   than assuming that smaller dt is automatically better;
3. separate timestep error from grid/transfer error with a controlled spatial and
   transfer ablation;
4. only after a converged forward reference exists may inverse-grid/optimizer
   refinement be promoted to the next certificate experiment.

The finest evaluated trajectory in this diagnostic is a numerical reference, not
continuum truth.

# Vulkax live repository audit — discovery control

Audit date: 2026-09-20

## Immutable control

The scientific control is the annotated tag `v1.0.0`, which resolves to commit:

```
2e7c306d5275718438e6c658d208f60edec52e53
```

Current `main` is:

```
9860f20f01673f176f457983388a419266b63449
```

and differs from the frozen control by PR #53, a presentation-only showcase polish commit. Research comparisons must therefore name the exact control SHA and must never silently redefine “Vulkax 1.0”.

## Discovery substrate

This branch starts from PR #59 head:

```
d48dbded55136e729d6ca8b03538cc93d57e62cc
```

because it contains the post-1.0 executable WorldIR reality-loop substrate: typed observations, explicit observation spaces, uncertainty weighting, addressed parameters and bounds, finite-difference Gauss-Newton fitting, local identifiability analysis, metric-anchor promotion, video-track import, camera/bounce experiments and fit/validation separation.

## Status summary

The 1.0 capture → calibration → held-out replay → robustness → influence → adaptive proposal → independent verification → commit/rollback → certificate path is the control. DOT C2 is measured-data-supported but does **not** provide true stress-free geometry, true Young's modulus, density, thickness, loads or material ground truth.

Post-1.0 work is explicitly experimental. Local identifiability currently has synthetic regression evidence. The native viewer line has correctness/performance evidence but is not a scientific-physics claim. The 1.1 GPU scheduler line remains an execution optimization line.

## Research rule

A low loss is not evidence of physical truth. A held-out frame from the same experiment is not counterfactual validation. A derivative is not trustworthy merely because an adjoint returns a number. A Gaussian representation is not novelty by itself.

## First cheap falsification targets

1. Parameter identifiability/null spaces.
2. Held-out replay versus unseen-intervention transfer.
3. Counterfactual trust-radius estimation.
4. Parameter drift across discretizations.
5. Influence/Jacobian singular spectrum and compressibility.
6. Verification calibration: false accept versus false reject.

All first probes are synthetic scaffolds unless explicitly marked otherwise. They may validate tooling or kill hypotheses; they are not publication evidence.


## Historical release-gate evidence

The release-candidate head `1c8f0692a2146ba71e6a7340f4814b3b09f4a219`, merged by PR #51 into tagged control `2e7c306d...`, has successful 2026-08-31 GitHub Actions runs for full CI, captured-world orchestration, measured DOT C2, showcase, release hardening and release smoke. These historical runs are part of the scientific control audit; discovery work never silently redefines them.

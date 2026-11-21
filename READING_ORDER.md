# VULKAX / Reality Probe — guided source study path

The repository contains a large numerical/graphics toolkit and a **frozen scientific experiment**. Learn the system in this order: conceptual physical-world model → forward simulation → observations and inverse fitting → deliberately untouched physical checks → support/veto/refusal decisions → prospective evidence and replication. The complete alphabetical directory inventory is in [SOURCE_CATALOG.md](docs/SOURCE_CATALOG.md); the path below is the educational order.

## Core question and call graph

A captured world can fit visible observations while being physically wrong. The verification channel must be treated as **additional evidence**, not as permission to accept every edit.

```text
Captured observations and WorldIR
        |             |
        v             v
  Forward solver   Observation/residual evaluation
        \             /
         candidate world / proposed repair
                      |
              verification probes
          (DCS, force, measured motion)
                      |
             SUPPORT / VETO / UNRESOLVED
                      |
         frozen evidence and independent replay
```

Read the limitations alongside every positive outcome. On the locked pendulum protocol, the reported finite-amplitude probe got 90/110 controlled decisions correct versus 40/110 for the diagnostic baseline across 10 videos, but rows from one physical video are correlated. The free-fall replication **failed** untouched validation; it must not be presented as support.

## Stage 1 — executable and mathematical contracts

1. [`README.md`](README.md) — project question, frozen snapshot, negative results, non-claims, and reproducibility commands.
2. [`src/main.cpp`](src/main.cpp) — CLI dispatch and subsystem composition; understand how separate renderer, backend, solver and research commands are called. These CLI routes are not themselves a verifier.
3. [`include/vulkax/core/units.hpp`](include/vulkax/core/units.hpp) and [`include/vulkax/core/math.hpp`](include/vulkax/core/math.hpp) — mathematical types, units and calculation conventions.
4. [`include/vulkax/problem/problem_ir.hpp`](include/vulkax/problem/problem_ir.hpp), [`include/vulkax/problem/document.hpp`](include/vulkax/problem/document.hpp), and [`include/vulkax/problem/validation.hpp`](include/vulkax/problem/validation.hpp) — problem inputs, serialization/document representations, and rejection of invalid states.

## Stage 2 — forward physical models and world state

5. [`include/vulkax/world/world_ir.hpp`](include/vulkax/world/world_ir.hpp), then [`src/world/world_ir.cpp`](src/world/world_ir.cpp) — scene and observation-role representation. Keep training data separate from held-out/untouched verification information.
6. [`include/vulkax/solvers/fem.hpp`](include/vulkax/solvers/fem.hpp), [`include/vulkax/solvers/mpm.hpp`](include/vulkax/solvers/mpm.hpp), and [`include/vulkax/solvers/dem.hpp`](include/vulkax/solvers/dem.hpp) — alternative discretization/simulation methods. Follow each into its matching `src/solvers/` implementation. Numerical convergence and constitutive assumptions are part of the claim.
7. [`include/vulkax/world/reality_loop.hpp`](include/vulkax/world/reality_loop.hpp), then [`src/world/reality_loop.cpp`](src/world/reality_loop.cpp) — candidate-world evaluation and observational fitting. A lower residual is not sufficient evidence of physical correctness.
8. [`include/vulkax/capture/video_track.hpp`](include/vulkax/capture/video_track.hpp) and [`include/vulkax/world/metric_video_bounce.hpp`](include/vulkax/world/metric_video_bounce.hpp) — video-to-physical-observable interfaces, with acquisition/tracking error distinct from model error.

## Stage 3 — counterfactual verification and decision boundaries

9. [`include/vulkax/research/dark_field_counterfactual.hpp`](include/vulkax/research/dark_field_counterfactual.hpp), then [`src/research/dark_field_counterfactual.cpp`](src/research/dark_field_counterfactual.cpp) — define response witnesses and counterfactual contrasts. Higher-order subtraction may amplify measurement noise; no zero witness can prove a physical world correct.
10. [`include/vulkax/world/verified_rewrite.hpp`](include/vulkax/world/verified_rewrite.hpp), then [`src/world/verified_rewrite.cpp`](src/world/verified_rewrite.cpp) — proposed edits and acceptance/refusal transactions. Trace why an ordinary visual improvement must be separated from physical evidence.
11. [`include/vulkax/world/transaction.hpp`](include/vulkax/world/transaction.hpp), then [`src/world/transaction.cpp`](src/world/transaction.cpp) — the decision record, transaction state and audit output.
12. [`include/vulkax/verify/result_certificate.hpp`](include/vulkax/verify/result_certificate.hpp), then [`src/verify/result_certificate.cpp`](src/verify/result_certificate.cpp) — verification criteria, trust states, and JSON evidence. A certificate only summarizes checks that actually ran.

## Stage 4 — experimental design, falsification, and evidence

13. [`include/vulkax/experiment/design.hpp`](include/vulkax/experiment/design.hpp) and [`src/experiment/design.cpp`](src/experiment/design.cpp) — experiment configuration and intervention design.
14. [`tests/dark_field_counterfactual_tests.cpp`](tests/dark_field_counterfactual_tests.cpp), [`tests/verified_rewrite_tests.cpp`](tests/verified_rewrite_tests.cpp), and [`tests/captured_reality_loop_tests.cpp`](tests/captured_reality_loop_tests.cpp) — check method behavior, edge cases and refusal gates.
15. [`research/validation/README.md`](research/validation/README.md) and [`research/paper_data/REPRODUCIBILITY_CHECKLIST.md`](research/paper_data/REPRODUCIBILITY_CHECKLIST.md) — distinguish development, prospective validation and final held-out evidence. Independent data splits matter as much as code.
16. [`research/paper_data/MANUSCRIPT_DOSSIER_SIGGRAPH.md`](research/paper_data/MANUSCRIPT_DOSSIER_SIGGRAPH.md) and [`research/paper_data/EXPERIMENT_MATRIX.csv`](research/paper_data/EXPERIMENT_MATRIX.csv) — trace the manuscript statements back to frozen experiments, including negative results.
17. [`CMakeLists.txt`](CMakeLists.txt), [`run_everything.sh`](run_everything.sh), and [`tests/`](tests) — understand build targets, ordinary smoke tests, expensive replication workflows and platform gates before attempting reproduction.

## API vocabulary to carry through the source

| Concept / API | Meaning | Typical interaction |
|---|---|---|
| World IR | Declared model and observations | Captured inputs → solver / inverse estimator |
| Forward solver | Predicts physical observations from model parameters | Numerical method → residual calculation |
| Residual / objective | Measures fit to a chosen observation set | Evaluation loop; **not proof** of physical correctness |
| Counterfactual response / DCS witness | Difference under interventions | Multiple forward/physical responses → discriminating witness |
| SUPPORT / VETO / UNRESOLVED | Deliberate tri-state evidence decision | Verification → accept/reject/abstain transaction |
| ResultCertificate / TrustState | Structured record of checks | Criterion results → serializable audit report |
| Frozen validation | Prospective protocol that cannot be retuned after final inspection | Locked parameters + reserved physical data → final analysis |

The project explicitly **does not** claim universal physical correctness, perfect acquisition invariance, or success on its failed free-fall validation. Preserve the original frozen outcome while adding teaching comments. Software is already [Apache-2.0 licensed](LICENSE) with [third-party notices](THIRD_PARTY_NOTICES.md); datasets and external assets retain their own terms.

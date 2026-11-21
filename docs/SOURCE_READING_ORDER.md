# Teaching source map — Vulkax

> A guided source-reading itinerary. It is not a test result or claim that every file is documented yet.

## How to use this guide

Read this as an experiment-driven research system: implementation is distinct from scientific inference. The September 2026 scientific snapshot and later validation must not be conflated.

Read each phase in order. Every link goes to a source file or directory in this repository. For a directory, inspect its public interface and then its implementation files. After each phase, identify the caller, the state it owns, the inputs it validates, and its observable outputs.

## 1. Scientific claims

1. [`README.md`](README.md)
2. [`research/README.md`](research/README.md)
3. [`research/status/DCS_LIMITATIONS_AND_KILL_CRITERIA.md`](research/status/DCS_LIMITATIONS_AND_KILL_CRITERIA.md)
4. [`research/paper_data/MANUSCRIPT_DOSSIER_SIGGRAPH.md`](research/paper_data/MANUSCRIPT_DOSSIER_SIGGRAPH.md)

## 2. Build/API boundaries

1. [`CMakeLists.txt`](CMakeLists.txt)
2. [`src/main.cpp`](src/main.cpp)
3. [`include`](include)
4. [`schemas`](schemas)

## 3. Forward models

1. [`src/core`](src/core)
2. [`src/world`](src/world)
3. [`src/geometry`](src/geometry)
4. [`src/numerics`](src/numerics)
5. [`src/solvers`](src/solvers)
6. [`src/operators`](src/operators)
7. [`src/compute`](src/compute)
8. [`src/backend`](src/backend)
9. [`src/gaussian`](src/gaussian)
10. [`src/render`](src/render)

## 4. Counterfactual pipeline

1. [`src/capture`](src/capture)
2. [`src/inverse`](src/inverse)
3. [`src/optimize`](src/optimize)
4. [`src/experiment`](src/experiment)
5. [`src/verify`](src/verify)
6. [`src/research`](src/research)
7. [`research/probes`](research/probes)
8. [`research/validation`](research/validation)

## 5. Proof and communication

1. [`tests/dark_field_counterfactual_tests.cpp`](tests/dark_field_counterfactual_tests.cpp)
2. [`tests/verified_rewrite_tests.cpp`](tests/verified_rewrite_tests.cpp)
3. [`tests/captured_reality_loop_tests.cpp`](tests/captured_reality_loop_tests.cpp)
4. [`tests/metric_video_bounce_tests.cpp`](tests/metric_video_bounce_tests.cpp)
5. [`research/paper_data/REPRODUCIBILITY_CHECKLIST.md`](research/paper_data/REPRODUCIBILITY_CHECKLIST.md)
6. [`run_everything.sh`](run_everything.sh)

## End-to-end interaction map

Source observations enter capture/world models. Forward solvers predict responses under interventions; inverse/optimization code proposes candidate repairs. Verification compares physical witnesses against the untouched target and chooses SUPPORT, VETO, or UNRESOLVED; experiments and scripts freeze the measurement, statistical decision, and publication evidence.

## APIs and code-reading questions

For each function, distinguish **contract** (preconditions, postconditions, failure modes), **implementation** (state/algorithms), **collaborators** (imports, calls and consumers), **lifetime/ownership**, and **verification** (test or evidence that checks it). For C++ modules, `export module` names the public compiled interface and `import` expresses module dependencies; an implementation unit's `module ...;` contributes definitions without exporting them.

## Boundaries and honesty

Pendulum success does not imply universality: the free-fall validation failed and temporal downsampling materially weakens accuracy. Keep original frozen decisions separate from post-final analyses.

## Documentation rules

- Teaching comments describe *why*, invariants, edge cases, API ownership and cross-file effects; avoid narrating obvious assignments.
- Preserve behavior during documentation-only edits; keep correctness changes in separate reviewed commits.
- Link function names and tests where possible; mark claims as source-present, CI-observed, host-verified or research-evidenced.
- License grants apply only to material for which the repository owner holds rights; third-party source, models, media and datasets keep their own notices.

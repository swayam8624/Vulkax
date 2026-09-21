# Vulkax

<p align="center">
  <strong>Verified Rewritable Reality</strong><br/>
  Captured scenes → executable physical worlds → counterfactual edits → evidence → commit or refuse.
</p>

<p align="center">
  <a href="https://github.com/swayam8624/Vulkax/actions/workflows/paper-evidence-smoke.yml"><img src="https://github.com/swayam8624/Vulkax/actions/workflows/paper-evidence-smoke.yml/badge.svg?branch=main" alt="Paper evidence smoke"/></a>
  <a href="https://github.com/swayam8624/Vulkax/actions/workflows/paper-evidence-full.yml"><img src="https://github.com/swayam8624/Vulkax/actions/workflows/paper-evidence-full.yml/badge.svg?branch=main" alt="Full reproduction"/></a>
  <img src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus" alt="C++20"/>
  <img src="https://img.shields.io/badge/macOS-Metal-black?logo=apple" alt="Metal"/>
  <img src="https://img.shields.io/badge/Linux-Vulkan-A41E22?logo=vulkan" alt="Vulkan"/>
  <img src="https://img.shields.io/badge/research-evidence--first-informational" alt="Evidence first"/>
</p>

---

## Why Vulkax exists

A captured 3D scene can look convincing while being **physically wrong**.

A reconstructed object may reproduce the trajectory that was observed, yet fail as
soon as we ask a counterfactual question:

- What if the material becomes stiffer?
- What if the fixture moves?
- What if the load direction changes?
- What if two interventions are applied together?
- What if we edit one local region and leave the rest unchanged?

That is the research problem behind Vulkax:

> **How do we turn a captured scene into a persistent, editable, physically
> executable world—and how do we know when an apparently successful physical repair
> is actually deceptive?**

Vulkax treats **verification, falsification, uncertainty, provenance and refusal**
as first-class parts of the world model.

It is not merely a renderer with physics attached.

---

## Research thesis

The project starts from a failure mode that ordinary fitting metrics cannot rule out:

```math
L_{\text{observation}}(M_{\text{repair}})
<
L_{\text{observation}}(M_{\text{baseline}})
```

does **not** imply

```math
M_{\text{repair}}
\text{ is physically more correct.}
```

A model can become observationally better while becoming mechanistically worse.

Vulkax calls this a **deceptive physical repair**.

The current research line investigates whether we can expose such repairs by
constructing counterfactual experiments that deliberately cancel model-common
response and reveal weaker mechanism-specific interaction structure.

---

# Dark-Field Counterfactual Spectroscopy

The central research instrument in the current branch is **Dark-Field
Counterfactual Spectroscopy (DCS)**.

The analogy is dark-field imaging: suppress the dominant signal so weak structure
becomes visible.

For a physical world **M**, let

```math
F_M(u)
```

be an observable response under intervention vector **u**.

Instead of looking only at raw motion, construct a signed intervention stencil

```math
\mathcal D_{\mu}[F]
=
\sum_i w_i F(u_i)
```

with moment-annihilation constraints such as

```math
\sum_i w_i = 0,
\qquad
\sum_i w_i u_i = 0.
```

Constant and first-order/common response are cancelled, leaving higher-order
interaction structure.

A simple symmetric second-order witness is

```math
F(+a)+F(-a)-2F(0)
```

A mixed two-intervention witness is

```math
F(A,B)-F(A,0)-F(0,B)+F(0,0)
```

which isolates finite-amplitude response that cannot be explained by either
intervention independently.

For finite intervention sets, Vulkax also uses the Möbius-style interaction contrast

```math
\kappa(S)
=
\sum_{T\subseteq S}
(-1)^{|S|-|T|}F(T)
```

The mathematical primitives themselves are not claimed as new. The research
question is whether they can be turned into a **mechanism-selective falsification
system for captured executable worlds**.

---

## The deeper object: mechanism order of contact

Two candidate worlds may agree in ordinary state space and still disagree in how
they respond to perturbation.

Vulkax treats model equivalence as graded:

```math
j^k F_{M_1}(0)
\approx
j^k F_{M_2}(0)
```

means the worlds agree through mechanism order **k**, within the available
measurement and numerical resolution.

The first order at which they separate is a candidate **mechanism order of contact**.

But higher order is useful only while the signal remains observable.

Define a maximum observable mechanism order through a signal-to-uncertainty rule:

```math
k_{\max}
=
\max
\left\{
k :
\frac{\|\mathcal D^{(k)}\|}
{\sigma_{\text{measurement}}
+\sigma_{\text{numerical}}
+\sigma_{\text{repeat}}}
>
\tau
\right\}
```

If two models differ only beyond the apparatus' mechanism resolution, Vulkax should
**refuse to pretend it knows**.

---

## Active experiment selection

The active-selection problem is not simply:

> find an input that makes two models disagree.

Instead, Vulkax searches for a **physically realizable contrast whose null space
contains the behavior the surviving models already share**.

For candidate worlds $M_1,\ldots,M_K$,

```math
\Gamma_{\mu}(M_i,M_j)
=
\frac{
\|\mathcal D_{\mu}[M_i]-\mathcal D_{\mu}[M_j]\|
}{
\sqrt{
\sigma^2_{\text{measurement},\mu}
+
\sigma^2_{\text{numerical},\mu}
}
}
```

One natural experiment-design target is

```math
\mu^\*
=
\arg\max_{\mu}
\min_{i\ne j}
\Gamma_{\mu}(M_i,M_j)
```

subject to moment annihilation, experiment cost and physical safety constraints.

---

# System architecture

```mermaid
flowchart LR
    A[Captured appearance<br/>+ observations]
    B[Stable appearance ↔ physics identity]
    C[Executable physical world]
    D[Fit-only calibration]
    E[Held-out replay]
    F[Candidate physical rewrite]
    G[Counterfactual witness synthesis]
    H[Measurement + numerical uncertainty]
    I{Evidence sufficient?}
    J[Commit rewrite]
    K[Refuse / rollback]
    L[Certificate + provenance]
    M[Native Metal / Vulkan render]

    A --> B --> C --> D --> E --> F
    F --> G --> H --> I
    I -->|yes| J --> L --> M
    I -->|no| K --> L
```

The renderer and physical solver are intentionally not treated as one monolithic
state. Appearance, semantics, physical discretization, correspondence and evidence
remain explicit.

---

# Research directives

These are the rules that govern Vulkax research. They are stronger than ordinary
engineering conventions because the project is explicitly trying to avoid producing
convincing but false physical conclusions.

### 1. Falsify before promoting

Every promising mechanism must survive explicit attempts to kill it.

Positive controls show that an implementation can work. They do **not** establish
scientific validity.

### 2. Proposal and verification are separate

The mechanism that proposes a repair may not certify its own success.

### 3. Fit and evaluation data remain disjoint

No threshold may be tuned on the partition later described as frozen validation.

### 4. Negative results remain visible

Failed D2/D3/D4V gates are part of the research record and may not be rewritten into
a success story.

### 5. Numerical artifacts are physical-claim blockers

A witness that changes sign, explodes or fails refinement checks cannot be promoted
as mechanistic evidence.

### 6. Measurement provenance is explicit

Measured, derived, fitted, literature-proxy and unavailable quantities are kept
separate.

### 7. Refusal is a valid system result

When evidence is insufficient, the correct outcome is often:

```text
unresolved → refuse edit
```

rather than a low-confidence physical claim.

### 8. Research claims must survive matched baselines

DCS is compared against raw response, same-cost raw bundles, Fisher/Jacobian probes,
maximum-motion probes and random controls.

### 9. Retrospective evidence stays retrospective

The GAUGE mechanism contradiction is scientifically useful but is not re-labelled
as prospective confirmation.

### 10. Reproducibility is part of the result

A paper-facing number is not accepted into the canonical ledger unless its source,
configuration and reproduction path are preserved.

---

# Current research dashboard

> **Project completion and scientific outcome are different things.**
>
> Vulkax engineering, benchmarking, documentation, reproducibility and paper-data
> packaging are complete for the executed research program. Some experiments
> completed with a **negative** or **contradictory** scientific result. That is an
> outcome, not unfinished work.

| Stage | Execution status | Scientific outcome | Meaning |
|---|---|---|---|
| **D0** | ✅ **complete** | positive control passed | core annihilation/math contracts are correct |
| **D1** | ✅ **complete** | 64/64 constructed cases behaved as designed | implementation can expose deliberately constructed deceptive repairs |
| **D2 frozen** | ✅ **complete** | **negative validation** — 0/16 worlds resolved | fixed DCS did not achieve sufficient mechanism resolution |
| **D3** | ✅ **complete** | **negative discovery result** — 0/6 worlds resolved | lower numerical error did not create enough physical observability |
| **D4V** | ✅ **complete** | **negative repair-verification result** — 0% resolved coverage | 14/36 repairs were deceptive, but none of the tested verification channels reached the frozen credibility threshold |
| **GAUGE** | ✅ **complete** | **retrospective channel contradiction** | aggregate/marker metrics favor overlap while longitudinal mechanism evidence favors endpoint |
| **D5** | ✅ **stop-rule completed** | **not advanced by design** | preregistered progression stopped because D2/D3/D4V did not justify consuming a fresh confirmatory dataset |
| **D6** | ✅ **complete** | systems capability implemented | spatial witness localization/export is available |

### Completion state

| Area | Status |
|---|---|
| Core implementation | ✅ complete |
| Unit/regression tests | ✅ complete |
| D1–D4V experiment execution | ✅ complete |
| GAUGE retrospective execution | ✅ complete |
| D5 confirmatory runner/infrastructure | ✅ complete |
| D5 fresh positive measured experiment | ⛔ intentionally not consumed after failed advancement gates |
| D6 spatial localization | ✅ complete |
| Benchmark/result ledgers | ✅ complete |
| Paper-data figures/tables | ✅ complete |
| One-command reproduction | ✅ complete |
| Documentation / claim guard | ✅ complete |

The current **scientific conclusion** is therefore not “unfinished DCS.” It is:

> **The tested DCS formulations were fully implemented and tested, then rejected as
> a prospective flagship verifier because the available physical information was
> insufficient.**

That conclusion is itself a completed research outcome.

### What “resolved”, “negative”, and “contradictory” mean

- **Resolved** is a statistical/evidential state: the signal cleared the frozen
  credibility threshold. “0 resolved” does **not** mean the code or experiment was
  incomplete.
- **Negative validation/discovery** means the experiment finished and falsified the
  tested hypothesis.
- **Contradictory channels** means two valid observables prefer different physical
  explanations. That disagreement is the measured result.
- **Not advanced by design** means a preregistered stop rule prevented spending a
  fresh confirmatory dataset on a method that had already failed its prerequisite
  gates.

### Key frozen numbers

```text
D1 constructed positive control      64 cases
D2 frozen truth worlds               16
D2 resolved worlds                    0
D2 median standardized separation     0.041578   (reference = 2)

D3 truth worlds                       6
D3 resolved worlds                    0
D3 median k=2 numerical RMS            8.435e-7 m
D3 median k=3 numerical RMS            2.153e-7 m

D4V repair proposals                  36
D4V deceptive                         14
D4V beneficial                        22
D4V resolved coverage                  0%

GAUGE ordinary overlap wins           10 / 10
GAUGE marker dark-field endpoint wins  0 / 10
GAUGE longitudinal endpoint wins       9 / 10
```

---

## Information frontier

The D4V result is not merely a near miss.

At the inherited **|z| ≥ 2** credibility reference:

| Method | Median |z| | Maximum |z| | Median signal amplification needed to reach 2 |
|---|---:|---:|---:|
| DCS | 0.05024 | 0.17654 | **39.83×** |
| raw bundle | 0.04991 | 0.40615 | 40.10× |
| raw point | 0.14707 | 0.63142 | **13.60×** |
| Fisher | 0.06637 | 0.63142 | 30.15× |
| max motion | 0.05773 | 0.57505 | 34.64× |

This is a post-hoc diagnostic, not a new gate.

It strengthens the current engineering conclusion:

> **The tested regime is information-limited. The next breakthrough needs a more
> informative physical channel, not a slightly looser threshold.**

See
[`research/results/DCS_INFORMATION_FRONTIER_2026-09-20.md`](research/results/DCS_INFORMATION_FRONTIER_2026-09-20.md).

---

# GAUGE: the motivating measured contradiction

The public GAUGE foam-shearing benchmark produced the clearest mechanism-level
contradiction in the current research line.

A finite fixture-overlap repair improved ordinary held-out metrics in every tested
repeat:

```text
ordinary face + marker preference:
overlap 10 / 10
```

Marker dark-field response agreed with the aggregate metric:

```text
marker dark-field:
overlap 10 / 10
```

But the longitudinal mechanism channel reversed:

```text
longitudinal dark-field:
endpoint 9 / 10
```

Exploratory retrospective exact sign-test:

```math
p=0.02148
```

This result is intentionally labelled **retrospective** because the metric mirage was
known before DCS was designed.

---

# What Vulkax currently contributes

### Systems contributions

- persistent stable identity across appearance and physical representations;
- versioned captured-world evidence bundles;
- nonlinear APIC/MPM execution;
- fit-only inverse calibration;
- held-out replay and robustness evidence;
- operator/material influence estimation;
- adaptive local rewrite proposals;
- evidence-derived atomic commit / rollback semantics;
- native Metal and Vulkan Gaussian rendering;
- schema-versioned certificates and evidence registries;
- reproducible measured-data pipelines.

### Research infrastructure

- annihilating intervention contrasts;
- finite-amplitude counterfactual interaction decomposition;
- automatic stencil synthesis;
- pair-aware uncertainty handling;
- witness-space numerical uncertainty;
- adaptive-order experiments;
- deceptive-repair benchmarks;
- matched raw/Fisher/max-motion baselines;
- spatial dark-field localization;
- frozen confirmatory replay infrastructure;
- deterministic paper-data/figure generation.

### Research finding

The strongest current finding is not that DCS is already a deployable verifier.

It is that **ordinary observational improvement can be physically deceptive**, while
the currently tested counterfactual verification channels can remain too
information-poor to issue a credible support/veto decision.

That is a more useful result than hiding the failure behind a relaxed threshold.

---

# Potential impact

If the central research program succeeds, the same abstraction could matter wherever
a captured or reconstructed scene is later used as an executable world rather than
a static visualization:

- physically editable digital twins;
- inverse graphics + inverse mechanics;
- robotics simulation from real captures;
- XR environments whose objects are meant to behave, not merely render;
- material and boundary-condition model criticism;
- simulation credibility / verification workflows;
- captured-world authoring tools that can **refuse unsupported edits**.

The long-term goal is a world model that can answer not only

> “does this reconstruction look right?”

but

> **“what physical claims are actually justified by the evidence we have?”**

---

# One-command full research reproduction

The canonical stable branch is:

```text
main
```

From a fresh clone:

```bash
git clone https://github.com/swayam8624/Vulkax.git
cd Vulkax

./run_everything.sh --clean
```

The runner automatically selects Metal on macOS and Vulkan on Linux when available.

Useful variants:

```bash
./run_everything.sh --backend Metal
./run_everything.sh --backend Vulkan
./run_everything.sh --backend none
./run_everything.sh --skip-gauge
./run_everything.sh --skip-performance
./run_everything.sh --exhaustive
```

The full run performs:

1. environment and hardware provenance capture;
2. reusable Python-tool validation/self-tests;
3. complete Release build;
4. full CTest suite;
5. evidence/release/CLI validation;
6. native backend conformance;
7. deterministic captured-world execution;
8. timing evidence;
9. D1;
10. solver-native discovery;
11. automatic active selection;
12. D2;
13. D3;
14. D4V;
15. GAUGE measured-data fetch and effective-span validation;
16. 20 definitive GAUGE forward simulations;
17. information-frontier diagnostics;
18. frozen-result reproduction validation;
19. deterministic paper figures/tables;
20. SHA-256-indexed evidence packaging;
21. portable archive creation.

Detailed guide:
[`docs/PAPER_EVIDENCE_REPRODUCTION.md`](docs/PAPER_EVIDENCE_REPRODUCTION.md).

---

# Paper-data package

The repository contains the complete non-manuscript source package for the research
that has actually been executed.

Start here:

- [research paper-data map](research/paper_data/README.md)
- [final benchmark summary](research/results/DCS_FINAL_BENCHMARK_SUMMARY_2026-09-20.md)
- [machine-readable final ledger](research/results/DCS_FINAL_RESULTS_2026-09-20.json)
- [experiment matrix](research/paper_data/EXPERIMENT_MATRIX.csv)
- [ablation matrix](research/paper_data/ABLATION_MATRIX.md)
- [figure/table source map](research/paper_data/FIGURE_TABLE_SOURCE_MAP.md)
- [reproducibility checklist](research/paper_data/REPRODUCIBILITY_CHECKLIST.md)
- [claim guard](research/literature/CLAIM_GUARD.md)
- [limitations / kill criteria](research/status/DCS_LIMITATIONS_AND_KILL_CRITERIA.md)

A successful full run creates:

```text
build/paper-evidence/
├── README.md
├── manifest.json
├── artifact_index.csv
├── SHA256SUMS
├── canonical/
├── generated/
├── logs/
└── system/
```

plus:

```text
build/vulkax-paper-evidence-<commit>.tar.gz
build/vulkax-paper-evidence-<commit>.tar.gz.sha256
```

---

# Generated result widgets

The paper-data generator produces deterministic SVG figures with a source CSV for
each figure:

<details>
<summary><strong>Open generated-asset list</strong></summary>

```text
fig_ranking_agreement.svg
fig_standardized_separation.svg
fig_numerical_floor.svg
fig_d4v_proposals.svg
fig_gauge_channel_contradiction.svg
fig_information_frontier.svg

table_stage_outcomes.csv
table_claim_boundaries.csv
table_information_frontier.csv
figure_manifest.json
```

Generated under:

```text
build/paper-figures/
```

</details>

---

# Repository / branch hygiene

The repository cleanup is complete.

| Branch class | Count | State |
|---|---:|---|
| `main` | **1** | canonical stable + research implementation |
| historical release branch | **1** | `release/1.0.0` |
| historical legacy snapshot | **1** | `legacy/studio-v1-2026-08-10` |
| **Total working branches** | **3** | clean |

Open pull requests: **0**.  
Open issues: **0**.

The final topology is:

```text
main                           ← canonical implementation
release/1.0.0                  ← historical release lineage
legacy/studio-v1-2026-08-10   ← historical snapshot
```

The previous 26 fully-contained working branches were deleted after proving they
contained zero unique commits relative to `main`.

The remaining 28 divergent historical branches were **not discarded**. Each endpoint
was preserved as an annotated tag under:

```text
archive/2026-09-21/<old-branch-name>
```

and only then was the working branch ref removed.

That keeps the normal branch list small without losing experimental or renderer
history.

Current audit:
[`research/status/BRANCH_AUDIT_2026-09-21.md`](research/status/BRANCH_AUDIT_2026-09-21.md).

For future temporary branches, use the audited helper before deletion:

```bash
./research/scripts/audit_branch_cleanup.sh
```

---

## Current implementation — Vulkax 1.0

**Vulkax 1.0 is the stable verified-rewritable-reality baseline.**

The stable release freezes the implemented 0.39–0.90 engineering path without
retroactively adding research claims.

See [`docs/RELEASE_1_0.md`](docs/RELEASE_1_0.md).

### Baseline capabilities

- renderer-independent Gaussian appearance;
- ASCII/binary 3DGS PLY ingestion;
- persistent composite `GaussianId`;
- reorder-safe selection/correspondence/rollback;
- versioned captured-deformable evidence bundles;
- affine MLS appearance↔physics coupling;
- nonlinear APIC/MPM replay;
- fit-only material calibration;
- held-out replay;
- finite-difference and controlled adjoint influence paths;
- adaptive local rewrite proposals;
- atomic verified rewrite transactions;
- native Metal / Vulkan Gaussian paths;
- schema-versioned certificates;
- measured DOT C2 benchmark support;
- deterministic presentation/showcase assets.

The project remains **C++20**.

---

## One-command captured-world research + showcase — 0.80

The release-facing captured-world workflow remains available independently of the
new paper-evidence runner:

```bash
./build/vulkax captured-world-run \
  build/captured-example/capture.vkcap \
  build/captured-world-run \
  m4 0.003 1 1 1 \
  Metal 0.08 0.01 0.02 12345 \
  --showcase studio_pedestal \
  --showcase-assets build/demo-assets \
  --showcase-resolution 1280x720 \
  --turntable 12
```

Use `Vulkan` on a Vulkan-capable Linux build or `none` when no native render
dependency is desired.

A completed run is not synonymous with a verified rewrite. The certificate records
run completion separately from the rewrite decision.

See
[`docs/CAPTURED_WORLD_RUN_0_80.md`](docs/CAPTURED_WORLD_RUN_0_80.md).

---

# Measured deformable benchmark — 0.45

The stable release also includes the public CC0 DOT C2 measured deformable benchmark.

Current controlled result:

```text
stable measured correspondences       225
observations                           675
fit / held-out rows                    585 / 90
model-conditioned effective E          7500 Pa
model-conditioned nu                   0.45
fit dynamic RMS                        0.004390821778 m
held-out dynamic RMS                   0.004417317099 m
adaptive regions                       8
adaptive particles                     182 / 225
retained absolute-gradient mass        0.9480125633
selected measured rewrite              rejected
rollback                               performed
```

The rejected rewrite is intentionally preserved as evidence.

See
[`docs/MEASURED_BENCHMARK_0_45.md`](docs/MEASURED_BENCHMARK_0_45.md).

---

# Build and test

### macOS / Linux

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DVULKAX_BUILD_TESTS=ON

cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### Windows

```powershell
cmake -S . -B build -DVULKAX_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Detailed platform setup:
[`docs/INSTALL_0_90.md`](docs/INSTALL_0_90.md).

Performance methodology:
[`docs/PERFORMANCE_0_90.md`](docs/PERFORMANCE_0_90.md).

---

# Release validation

```bash
python3 scripts/validate_evidence_registry.py .
python3 scripts/audit_release_claims.py . --expected-project-version 1.0.0
python3 scripts/test_release_cli_failures.py --executable build/vulkax
python3 scripts/benchmark_captured_world_run.py \
  --executable build/vulkax \
  --iterations 3 \
  --backend none
```

---

# Research integrity / current non-claims

The current repository **does support**:

- deterministic controlled captured-world execution;
- stable capture / Gaussian identity contracts;
- transactional rewrite rollback;
- native Metal/Vulkan image regression;
- real measured-data ingestion and held-out replay;
- DCS implementation and matched-baseline experiments;
- frozen negative D2/D3/D4V results;
- retrospective GAUGE mechanism contradiction;
- one-command evidence reproduction;
- SHA-256-indexed paper-data packaging.

The completed research program **does not support the following positive claims**:

- DCS as a universal physical correctness certificate;
- DCS superiority over matched raw/Fisher baselines;
- useful prospective repair-veto coverage in the tested D4V regime;
- fresh measured prospective D5 confirmation (intentionally not consumed after the frozen stop rule);
- true material-property recovery from GAUGE or DOT where the source data do not
  provide the required ground truth;
- publication acceptance;
- publication novelty by implementation alone.

---

# Research map

| Need | Start here |
|---|---|
| What is Vulkax trying to solve? | this README |
| Current research state | [research/README.md](research/README.md) |
| Final numbers | [research/results/README.md](research/results/README.md) |
| Paper data | [research/paper_data/README.md](research/paper_data/README.md) |
| Reproduction | [docs/PAPER_EVIDENCE_REPRODUCTION.md](docs/PAPER_EVIDENCE_REPRODUCTION.md) |
| DCS program | [research/status/DCS_RESEARCH_PROGRAM.md](research/status/DCS_RESEARCH_PROGRAM.md) |
| Limitations | [research/status/DCS_LIMITATIONS_AND_KILL_CRITERIA.md](research/status/DCS_LIMITATIONS_AND_KILL_CRITERIA.md) |
| Novelty threats | [research/literature/DCS_NOVELTY_THREAT_MAP.md](research/literature/DCS_NOVELTY_THREAT_MAP.md) |
| Claim boundaries | [research/literature/CLAIM_GUARD.md](research/literature/CLAIM_GUARD.md) |
| Branch cleanup | [research/status/BRANCH_AUDIT_2026-09-21.md](research/status/BRANCH_AUDIT_2026-09-21.md) |
| Release roadmap | [docs/ROADMAP_1_0.md](docs/ROADMAP_1_0.md) |

---

## Development rules

- Proposal and verification remain separate.
- Synthetic evidence stays labelled synthetic.
- Measured, derived and proxy quantities remain distinguishable.
- Frozen validation partitions are never threshold-tuning datasets.
- Numerical convergence is part of physical credibility.
- Failed experiments remain visible.
- A system may refuse to make a physical claim.
- Reproduction artifacts are evidence, not decoration.


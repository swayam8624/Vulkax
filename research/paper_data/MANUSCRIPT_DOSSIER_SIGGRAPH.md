# Reality Probe Manuscript Dossier

Date: 2026-09-21  
Repository: VULKAX  
Paper-facing name: Reality Probe  
Scientific freeze: `paper-freeze-2026-09-21`  
Frozen commit: `a9da8c0aa8689ebeea0d84baf95a74907659a837`  
Current repository status: research execution complete; manuscript preparation next

This document is the handoff from completed research execution to paper writing. It
collects the paper claim, section plan, exact evidence, equations, literature
boundaries, figure and video paths, supplementary material, anonymity issues,
licensing boundaries, and the current SIGGRAPH submission format baseline.

It is not manuscript prose. It is the source map for writing the manuscript without
changing the frozen science.

## 1. Paper identity

### Working title

**Reality Probe: Counterfactual Physical Verification of Captured Worlds**

A more result-specific alternative is:

**Reality Probe: Deceptive Physical Repair and Information-Limited Verification in Captured Worlds**

The second title is harder to overread as a successful universal verifier. The first
is shorter and better for a system paper if the abstract states the information-limit
result immediately.

### One-sentence paper thesis

A physical repair can improve ordinary held-out observations while making an
untouched physical target worse; mechanism-selective counterfactual probes expose
stronger disagreement, but the tested verification regime remains information-limited
and must refuse certification when the evidence stays below the frozen decision
threshold.

### Paper-facing naming

- Repository/system codename: VULKAX
- Paper-facing research name: Reality Probe
- Internal method investigated: Dark-Field Counterfactual Spectroscopy (DCS)
- Follow-on physical channel: known-force compliance
- Failure mode: deceptive physical repair
- Final scientific result: information-limited physical verification

Use **Reality Probe** throughout the paper. Mention VULKAX once in the implementation
section or supplement if the repository name must be explained.

## 2. Claims that the paper can support

The manuscript can defend the following claims.

1. **Deceptive repair exists in the tested captured-world setting.** A repair can
   reduce ordinary held-out error while worsening an untouched physical target.

2. **Ordinary observational agreement is insufficient evidence of physical
   correctness.** The repair objective and the independent physical target can
   disagree.

3. **Mechanism-selective counterfactual evidence is operationally implemented.**
   DCS signed intervention contrasts, lower-moment annihilation, uncertainty
   propagation, witness-space refinement, active stencil synthesis, support/veto/
   unresolved decisions, and spatial localization are executable and source-audited.

4. **The tested DCS formulations do not validate a prospective repair verifier.**
   D2, D3, and D4V remain unresolved at the frozen decision reference.

5. **The information gap is quantified.** In D4V, the median DCS absolute z is about
   0.0502 against a decision magnitude of 2, requiring about 39.8x median signal
   amplification to reach the reference under the post-hoc diagnostic.

6. **Changing the physical information channel materially changes observability.**
   The fresh known-force compliance experiment raises the median absolute z from
   0.04877 to 0.55863, an 11.455x increase.

7. **Stronger evidence is still not sufficient evidence.** The best force magnitude
   is 1.31911, below the frozen magnitude 2 decision reference, so all 36 fresh
   proposals remain unresolved.

8. **A measured retrospective channel contradiction exists in GAUGE.** Ordinary and
   marker channels favor one model while the longitudinal mechanism channel favors
   the other in 9 of 10 held-out repeats.

9. **Refusal is a valid system outcome.** The completed research supports refusing
   a rewrite when available evidence does not justify support or veto.

## 3. Claims that the paper must not make

Do not claim:

- DCS is a universal or validated prospective verifier.
- The known-force channel provides reliable repair certification.
- DCS is superior to all matched raw/Fisher/max-motion baselines.
- GAUGE is prospective confirmation.
- The fresh force-compliance result is measured-world evidence.
- The Stanford Bunny is benchmark geometry.
- Display-magnified deformation is physical displacement magnitude.
- APIC is generally better than PIC.
- PIC caused the selected deceptive repair because of an unproven mechanism.
- Gaussian plus MPM simulation is novel.
- physical-parameter inference from video is novel.
- identifiability, Fisher/Jacobian experiment design, model discrepancy, or
  counterfactual prediction are novel in isolation.
- the implemented maximin stencil search is a globally optimal solver.
- a complete symbolic response jet or response tensor is reconstructed.
- a real force-sensor confirmatory dataset was executed.
- publication acceptance or novelty follows from implementation alone.

Primary sources:
- `research/literature/CLAIM_GUARD.md`
- `research/literature/DCS_NOVELTY_THREAT_MAP.md`
- `research/status/DCS_MATH_IMPLEMENTATION_AUDIT_2026-09-21.md`
- `research/status/DCS_LIMITATIONS_AND_KILL_CRITERIA.md`

## 4. Recommended contribution statement

Use four main contributions in the paper rather than a long implementation list.

### Contribution 1: deceptive physical repair

Formalize and experimentally instantiate the failure mode in which ordinary held-out
fit improves while an untouched physical target worsens.

Core condition:

```math
L_{obs}(R_1) < L_{obs}(R_0)
```

while:

```math
L_{target}(R_1) > L_{target}(R_0).
```

### Contribution 2: mechanism-selective counterfactual verification

Develop and implement a verification laboratory that evaluates candidate repairs
through signed intervention contrasts, exact lower-moment annihilation, propagated
measurement/numerical/repeat uncertainty, active stencil synthesis, and a
support/veto/unresolved decision rule.

This contribution is the verification framework and experimental instrument. Do not
present DCS as already successful at prospective certification.

### Contribution 3: falsification sequence and information frontier

Execute D1 through D4V with matched raw, Fisher/Jacobian, maximum-motion, and random
comparisons where applicable, preserve the negative results, and quantify how far
the tested evidence remains from the frozen decision reference.

### Contribution 4: orthogonal physical information

Execute a separately frozen known-force compliance experiment on fresh truth worlds.
Show that a genuinely different physical channel increases median standardized
signal by 11.46x while still producing zero resolved proposals. Use this to support
the information-limit interpretation rather than a successful-verifier claim.

GAUGE is best presented as measured retrospective motivation/evidence, not as a fifth
headline contribution unless space permits.

## 5. Suggested manuscript structure

This structure is suitable for a SIGGRAPH/TOG-style full manuscript and can later be
compressed for a 7-page dual-track submission if needed.

### 1. Introduction

Purpose:
- establish the captured-world setting;
- show why observationally improving physical edits can still be wrong;
- define deceptive repair with one equation and one visual;
- state why a verifier must be allowed to remain unresolved;
- summarize the four contributions above.

Use:
- `research/paper_data/PAPER_POSITIONING.md`
- `research/results/VULKAX_FINAL_RESEARCH_SUMMARY_2026-09-21.md`
- `docs/readme_assets/02_deceptive_repair.svg`
- `docs/readme_assets/03_deception_map.svg`

Recommended opening sequence:
1. captured worlds are increasingly executable, not only renderable;
2. inverse fitting gives plausible parameters/models;
3. a repair that improves held-out observations can still be mechanistically worse;
4. the missing question is not only how to repair but when evidence justifies the
   repair;
5. Reality Probe interrogates the candidate world with counterfactual evidence and
   explicitly permits unresolved decisions.

### 2. Related Work

Organize by problem, not by chronology.

#### 2.1 Physics-aware neural/Gaussian scene models

Seed papers:
- PhysGaussian
- PAC-NeRF
- PhysDreamer
- PhysFlow
- PUGS
- GASP
- EMPM
- MonoPhysics
- i-PhysGaussian
- M-PhyGs
- ProJo4D
- MPMWorlds

Main distinction:
Reality Probe is not another Gaussian-plus-physics or video-to-parameter method. The
paper studies whether an apparently improved physical rewrite is sufficiently
supported by independent evidence.

#### 2.2 Differentiable simulation and inverse physics

Seed papers:
- ChainQueen
- DiffTaichi
- Physics from Video
- IRIS
- stress-free inverse identification work

Main distinction:
gradients, parameter fitting, and identifiability diagnostics are infrastructure and
prior art, not the paper's novelty claim.

#### 2.3 Experimental design and identifiability

Seed papers:
- Asadi and Laksari optimal hyperelastic experiment design
- tissue material identifiability literature
- Fisher/Jacobian experiment design

Main distinction:
Reality Probe uses such ideas as baselines or enabling machinery. Generic
sensitivity-based experiment selection is not claimed as new.

#### 2.4 Model discrepancy, simulation credibility, and digital twins

Seed papers:
- Predictive Capability Maturity Model
- modular Bayesian model discrepancy
- Digital Twin Counterfactual Framework
- task-dependent UQ work

Main distinction:
the paper operationalizes evidence sufficiency at a requested physical rewrite and
tests the accept/refuse behavior on deceptive repairs.

#### 2.5 Measurement-grounded physical-fidelity benchmarks

Seed:
- GAUGE

Main distinction:
GAUGE is a benchmark and retrospective measured substrate. Reality Probe studies
repair verification and channel disagreement.

Literature source files:
- `research/literature/papers.json` (29 seed-reviewed entries)
- `research/literature/novelty_matrix.md`
- `research/literature/DCS_NOVELTY_THREAT_MAP.md`
- `research/literature/CLAIM_GUARD.md`

Before submission, every seed citation must be checked against the actual paper PDF
and converted to a verified BibTeX entry. The JSON file is a literature audit, not a
finished bibliography.

### 3. Problem formulation

Define:

- captured executable world (M);
- observation/fitting data;
- held-out ordinary observation loss;
- candidate repair (R);
- untouched target loss;
- deceptive repair;
- independent verification channel;
- support, veto, unresolved.

Suggested deceptive-repair definition:

```math
L_{obs}(R_1) < L_{obs}(R_0)
quad	ext{and}quad
L_{target}(R_1) > L_{target}(R_0).
```

Suggested evidence decision:

```math
z = \frac{e_{baseline}-e_{repair}}{\sigma}.
```

Decision:

```text
z >= +2   support
z <= -2   veto
otherwise unresolved
```

State early that the threshold is frozen and is not tuned after observing the
evaluation partition.

Primary sources:
- `research/status/DCS_MATH_IMPLEMENTATION_AUDIT_2026-09-21.md`
- `research/status/PROBLEM_SOLUTION_LOCK.md`
- `research/benchmarks/ORTHOGONAL_FORCE_COMPLIANCE_PROTOCOL_2026-09-21.md`

### 4. Reality Probe method

#### 4.1 Captured-world and rewrite pipeline

Describe only the system pieces needed to understand the paper:

```text
captured appearance and observations
physical replay and fit-only calibration
candidate physical rewrite
independent counterfactual evidence
support / veto / unresolved
commit or rollback
```

Do not spend main-paper space on every VULKAX 1.0 subsystem. Put stable identity,
render backends, schema details, and release engineering in supplement unless needed
for reproducibility.

Useful sources:
- `docs/RELEASE_1_0.md`
- `README.md`
- `docs/PAPER_EVIDENCE_REPRODUCTION.md`

#### 4.2 Dark-Field Counterfactual Spectroscopy

For response (F_M(u)), define:

```math
\mathcal D_{\mu}[F]
=
\sum_i w_i F(u_i).
```

For declared mechanism order (k), the implementation enforces lower-order
annihilation by checking every multivariate monomial with total degree below (k):

```math
r_\alpha = \sum_i w_i u_i^\alpha.
```

A simple explanatory order-2 example is:

```math
F(+a)+F(-a)-2F(0).
```

The explanatory second-difference figure must be marked schematic because the
implemented comparator uses an optimized order-2 stencil on a 3x3 intervention
lattice.

Möbius finite-amplitude interaction:

```math
\kappa(S)
=
\sum_{T\subseteq S}
(-1)^{|S|-|T|} F(T).
```

State explicitly that this is a finite-amplitude interaction contrast, not a claim
of a newly derived mixed derivative theorem.

#### 4.3 Uncertainty

Executable uncertainty separates:
- measurement variance;
- repeat variance;
- numerical variance.

For signed stencils:
- independent measurement/repeat variance scales with (sum_i w_i^2);
- numerical variance uses a conservative squared-L1 gain;
- D3 additionally measures the numerical floor directly in witness space by nominal
  versus refined-resolution comparison.

#### 4.4 Active stencil synthesis

The code projects candidate disagreement into the exact lower-moment nullspace and
searches deterministic candidate directions to maximize worst-case standardized
separation.

Do not call this globally optimal. The source audit explicitly classifies it as a
deterministic heuristic for the non-convex maximin problem.

#### 4.5 Orthogonal known-force compliance

Fresh protocol:
- 6 new truth worlds;
- truth APIC;
- Young's modulus in {14125, 16625, 18125} Pa;
- Poisson ratio in {0.22, 0.32};
- truth timestep 2.5e-5 s;
- fixed candidate families and fitting grid;
- bottom layer fixed;
- gravity zero;
- 40 N per top particle;
- directions +X, -X, +Y, +Z;
- six-component response per force experiment:
  top-layer dx,dy,dz and interior-layer dx,dy,dz;
- numerical uncertainty from nominal versus half-dt response bundles;
- measurement and repeat scales each frozen at 2e-5 m per response component;
- same absolute decision reference (|z| >= 2).

Force statistic:

```math
z_{force}
=
\frac{e(B,Y)-e(R,Y)}
{\sqrt{
\sigma_{meas}^2+
\sigma_{repeat}^2+
\sigma_{num,B}^2+
\sigma_{num,R}^2
}}.
```

Primary source:
`research/benchmarks/ORTHOGONAL_FORCE_COMPLIANCE_PROTOCOL_2026-09-21.md`

### 5. Experimental program

Present the sequence as deliberate falsification, not a sequence of failed attempts.

#### D1: constructed positive control

- 64 deceptive repairs
- 64 ordinary accepts
- 64/64 rejected by DCS in the constructed control
- median ordinary observation improvement: 0.6088 = 60.88%
- median witness degradation ratio: 9.375

Purpose:
implementation sanity check only.

#### D2b: active-selection discovery comparison

Ranking agreement:
- raw maximin: 95.83%
- Fisher/Jacobian: 87.50%
- same-cost raw bundle: 75.00%
- maximum motion: 66.67%
- DCS order-2 maximin: 62.50%
- random: 45.83%

Purpose:
show that DCS did not beat matched baselines in generic ranking.

#### D2 frozen validation

- 16 truth worlds
- 0 resolved
- median standardized separation 0.041578
- median numerical RMS 6.469e-6 m
- maximum moment residual 3.886e-16

#### D3 witness-space and adaptive-order study

- 6 truth worlds
- adaptive order 2 selected 6/6
- adaptive order 3 selected 0/6
- 0 resolved
- median adaptive separation 0.0588583
- median k2 numerical RMS 8.435e-7 m
- median k3 numerical RMS 2.153e-7 m
- ranking agreement:
  Fisher 61.11%;
  raw bundle 58.33%;
  max motion 58.33%;
  raw pair-aware 55.56%;
  adaptive DCS 52.78%;
  k2 DCS 52.78%;
  random 52.78%;
  k3 DCS 47.22%

Purpose:
witness-space refinement reduces numerical floor but does not recover observability.

#### D4V repair-veto discovery

- 36 ordinary-heldout-improving proposals
- 14 deceptive
- 22 beneficial
- 0% resolved coverage for DCS
- 0% resolved coverage for raw bundle
- 0% resolved coverage for raw pair-specific
- 0% resolved coverage for Fisher
- 0% resolved coverage for max-motion

Purpose:
establish deceptive repair population and information-limited verification.

#### D5 infrastructure

The generic confirmatory replay runner is implemented with frozen-stencil and no-fit
semantics, but fresh measured confirmation was intentionally not consumed after the
advancement prerequisites failed.

This is a stop rule, not unfinished implementation.

#### D6 spatial localization

Implemented:
- per-region spatial dark-field residuals;
- standardized local residuals;
- resolved/unresolved flags;
- particle/Gaussian/surface component mapping;
- PLY export.

D6 is presentation/localization infrastructure, not scientific validation.

#### GAUGE retrospective measured study

10 held-out even repeats:

- ordinary face + marker: overlap wins 10/10
- marker dark-field: endpoint wins 0/10
- longitudinal dark-field: endpoint wins 9/10

Median errors:
- marker endpoint: 0.002428023 m
- marker overlap: 0.001709196 m
- longitudinal endpoint: 0.003131336
- longitudinal overlap: 0.005584182

Post-hoc paired longitudinal exact two-sided sign-test:
- p = 0.021484375

Keep "retrospective" in every figure caption and results paragraph.

#### Fresh orthogonal force-compliance study

- 6 fresh truth worlds
- 36 ordinary-heldout-improving proposals
- 12 deceptive
- 24 beneficial
- DCS median |z| 0.0487670
- DCS max |z| 0.1727887
- force median |z| 0.5586292
- force max |z| 1.3191113
- median force/DCS ratio 11.4550585x
- DCS resolved 0/36
- force resolved 0/36
- force median additional factor to z=2: 3.58019x
- force best-case additional factor to z=2: 1.51617x
- advancement gate: failed

Exploratory post-hoc classification metrics exist but are not preregistered headline
metrics:
- DCS AUROC 0.4444
- DCS AP 0.3109
- force AUROC 0.7604
- force AP 0.5784

Do not promote these exploratory metrics to the abstract.

## 6. Results narrative

Recommended Results order:

1. **Ordinary repair can be deceptive.**
   Use the proposal landscape and one exact hero case.

2. **DCS works in the constructed control but not as a general verifier.**
   Move quickly from D1 to D2/D3 to avoid making the positive control look like the
   main result.

3. **Numerical refinement is not the missing ingredient.**
   D3 lowers the witness-space numerical floor but leaves all cases unresolved.

4. **The problem persists across matched verification baselines.**
   D4V has zero resolved coverage for DCS and all listed matched channels.

5. **The gap to the decision reference is large.**
   Present the information frontier.

6. **Orthogonal physical information changes the signal scale.**
   Show the fresh force-compliance increase.

7. **The stronger channel still does not certify.**
   Show 0.5586 median and 1.3191 maximum against magnitude 2.

8. **Measured retrospective evidence shows the same kind of channel disagreement.**
   Present GAUGE only after the synthetic prospective story is already clear.

The final sentence of Results should not say the method "fails." It should say that
the tested evidence is insufficient for a resolved decision under the frozen rule.

## 7. Discussion structure

### 7.1 What the negative gates mean

They falsify the stronger claim that the tested DCS formulations are already a
prospective repair verifier.

### 7.2 Why the force result matters

The 11.46x increase shows that observability is strongly dependent on the physical
question being asked. This supports the information-content interpretation.

### 7.3 Why unresolved is a result

A trustworthy verifier must distinguish "no evidence" from "evidence against" and
must not convert a weak score into a confident support/veto decision.

### 7.4 Limitations

Must include:

- fresh prospective force-compliance study is synthetic;
- no fresh measured force-sensor confirmation;
- current solver/constitutive regime is limited;
- frozen threshold is operational, not a universal physical constant;
- hero case is post-hoc visualization only;
- DCS active synthesis is heuristic;
- response-jet interpretation is operational rather than symbolic-complete;
- GAUGE analysis is retrospective;
- DOT model uses explicit proxies and is not true cloth-property recovery;
- spatial localization is not validation;
- Stanford Bunny visuals are presentation carriers;
- no claim of generic physical correctness certification.

### 7.5 Future work

A future study may add:
- real force sensing;
- frequency/modal excitation;
- independently frozen new truth/measurement data;
- sensor design for maximizing information;
- richer constitutive families;
- intervention-specific evidence requests.

Do not add new same-partition experiments to this paper.

## 8. Conclusion content

The conclusion should contain only three ideas:

1. observational improvement can hide physical degradation;
2. independent physical probes can add substantial discriminating information;
3. a verifier should refuse certification when that information remains
   insufficient.

Avoid repeating the entire experiment list.

## 9. Figure plan

### Figure 1: teaser / central failure mode

Preferred tracked source:
- `docs/readme_assets/02_deceptive_repair.svg`
- PNG counterpart: `docs/readme_assets/02_deceptive_repair.png`

Alternative high-detail visual:
- `docs/readme_assets/reality_probe_hero_standalone.svg`

Content:
- baseline ordinary evidence
- improved repair
- hidden target worsening
- no implication that the force threshold was crossed

Caption must say:
- exact selected case;
- post-hoc visualization only;
- selected from the fresh synthetic OFC population;
- ordinary improvement about 14.93%;
- untouched target worsening about 9.03%.

### Figure 2: Reality Probe mechanism

Tracked sources:
- `docs/readme_assets/04_same_probe.svg`
- `docs/readme_assets/05_response_overlay.svg`
- `docs/readme_assets/06_fingerprint.svg`
- `docs/readme_assets/07_residual_field.svg`

Preferred layout:
four panels showing same probe, truth/repair response, directional fingerprint,
spatial residual.

This figure should create intuition before the DCS equation block.

### Figure 3: fresh deceptive-repair landscape

Tracked source:
- `docs/readme_assets/03_deception_map.svg`

Frozen generated source:
- `build/paper-figures/fig_d4v_proposals.svg` for D4V
- fresh OFC visualization data:
  `visualization/data/ofc_proposals_visualization_2026-09-21.csv`

If using the fresh 36-row OFC map, caption counts must be 12 deceptive / 24
beneficial. If using D4V, counts must be 14 / 22. Never mix the populations.

### Figure 4: D2/D3/D4V falsification sequence

Generated assets:
- `build/paper-figures/fig_ranking_agreement.svg`
- `build/paper-figures/fig_standardized_separation.svg`
- `build/paper-figures/fig_numerical_floor.svg`

Recommended multi-panel composition:
(a) ranking agreement;
(b) standardized separation;
(c) numerical-floor reduction.

Message:
better numerical witness fidelity did not create resolved physical evidence.

### Figure 5: information frontier

Generated source:
- `build/paper-figures/fig_information_frontier.svg`

Tracked explainer still:
- `docs/readme_assets/11_information_limit.svg`

Message:
DCS and matched channels are not narrowly missing the decision reference.

### Figure 6: orthogonal force information gain

Generated:
- `build/paper-figures/fig_orthogonal_force_gain.svg`

Tracked explainer:
- `docs/readme_assets/10_signal_gain.svg`
- `docs/readme_assets/11_information_limit.svg`

Message:
11.46x median increase, still 0 resolved.

### Figure 7: GAUGE retrospective channel contradiction

Generated:
- `build/paper-figures/fig_gauge_channel_contradiction.svg`

Source table:
- `research/results/GAUGE_PAIRED_DIAGNOSTICS_2026-09-20.csv`

Caption must include "retrospective measured analysis."

### Optional figure: system architecture / captured-world pipeline

Use only if page space permits. Prefer a compact custom vector based on the README
engineering journey rather than a large software block diagram.

System architecture details can move to supplement in a 7-page submission.

## 10. Main table plan

### Table 1: stage outcome summary

Generated:
- `build/paper-figures/table_stage_outcomes.csv`

Use for D1, D2, D3, D4V, GAUGE, OFC.

### Table 2: force-compliance result

Generated:
- `build/paper-figures/table_orthogonal_force_compliance.csv`

Include:
- proposal counts;
- medians/maxima;
- coverage;
- gain ratio;
- threshold.

### Table 3: claim boundary

Generated:
- `build/paper-figures/table_claim_boundaries.csv`

Likely supplementary unless the paper needs an explicit supported/not-supported
summary.

### Supplement tables

- `research/paper_data/EXPERIMENT_MATRIX.csv`
- `research/paper_data/ABLATION_MATRIX.md`
- `research/results/DCS_FINAL_BENCHMARK_TABLE_2026-09-20.csv`
- `build/dcs-d2-validation/cases.csv`
- `build/dcs-d3-discovery/cases.csv`
- `build/dcs-d3-discovery/stencils.csv`
- `build/dcs-d3-discovery/method_summary.csv`
- `build/dcs-d4v-discovery/proposals.csv`
- `build/orthogonal-force-compliance/proposals.csv`
- `build/gauge-dcs-retrospective/per_trial.csv`

## 11. Video and media plan

Canonical explainer source:
- `visualization/explainer/render.py`
- `visualization/explainer/reality_probe.json`
- `visualization/explainer/narration.srt`
- `visualization/explainer/README.md`

Tracked final media:
- `docs/readme_assets/reality_probe_explainer.mp4`
- `docs/readme_assets/reality_probe_explainer_preview.gif`
- `docs/readme_assets/storyboard.png`

The current film is:
- 116 seconds;
- 2560x1440;
- 30 fps;
- H.264;
- silent;
- deterministic vector animation;
- no AI-generated scientific pixels.

SIGGRAPH's current published Technical Papers rules allow an optional companion video
up to five minutes. The existing 116-second film already fits comfortably.

For submission:
- keep the paper understandable without the video;
- remove author-identifying metadata;
- keep the video silent or use non-identifying narration;
- retain third-party attribution if any third-party visual carrier is included;
- prefer the solver-body vector explainer, which does not depend on the Stanford
  Bunny.

## 12. Exact visualization provenance

### Fresh hero case

File:
`visualization/data/hero_case_ofc_2026-09-21.json`

Selection:
post-hoc visualization only, most negative force-progress z among deceptive fresh
OFC proposals.

Case:
- truth_id 5
- baseline APIC
- repair PIC
- baseline held-out 1.0517712035032275e-05 m
- repair held-out 8.947388417773706e-06 m
- baseline target 1.0987448733584696e-05 m
- repair target 1.1979910421188088e-05 m
- DCS signed z -0.1126168
- force signed z -1.3181063
- label deceptive

The selected case ratio is about 11.70x. This is not the aggregate 11.46x headline.

### Stanford Bunny

Fetcher:
`visualization/assets/fetch_stanford_bunny.sh`

Use:
visualization carrier only.

It is not benchmark geometry. Publication images that use it must credit the Stanford
Computer Graphics Laboratory.

### Display magnification

Solver-driven display deformations can use 400x magnification. This is presentation
only. Numerical/statistical values remain unscaled.

### Residual field

Explanatory field:
`D(x,p) = ||u_repair(x,p) - u_truth(x,p)||`

The four directional panels share a common normalization. Do not independently
normalize each panel.

## 13. Reproducibility package

Primary entry point:

```bash
./run_everything.sh --clean
```

Main generated roots:
- `build/paper-evidence/`
- `build/paper-figures/`

Portable archive:
- `build/vulkax-paper-evidence-<commit>.tar.gz`
- matching SHA-256 file

Evidence package includes:
- canonical result copies;
- generated figures and tables;
- logs;
- compiler/system/GPU provenance;
- artifact index;
- SHA256SUMS;
- frozen-result reproduction validation.

Source maps:
- `research/paper_data/PAPER_DATA_MANIFEST.json`
- `research/paper_data/FIGURE_TABLE_SOURCE_MAP.md`
- `research/paper_data/PUBLICATION_ASSET_LOCK.md`
- `research/paper_data/REPRODUCIBILITY_CHECKLIST.md`
- `docs/PAPER_EVIDENCE_REPRODUCTION.md`

## 14. Current SIGGRAPH submission baseline

As of 2026-09-21, a SIGGRAPH 2027 Technical Papers call is not yet the published
source of record in this repository. Use the official SIGGRAPH 2026 rules as the
planning baseline and refresh this section as soon as the 2027 call appears.

Official 2026 sources:
- https://s2026.siggraph.org/program/technical-papers/
- https://s2026.siggraph.org/technical-papers-submissions-faq/
- https://s2026.siggraph.org/anonymity-policy/
- https://www.siggraph.org/preparing-your-content/author-instructions/

Current baseline:

- Technical Papers use ACM `acmtog` double-column review formatting.
- Anonymous LaTeX review command:
  `\documentclass[acmtog,anonymous,review]{acmart}`
- Add `\acmSubmissionID{paper ID}`.
- SIGGRAPH uses author-year citations.
- Use a current `acmart` release accepted by SIGGRAPH; the 2026 call explicitly
  requires version 2.16 or newer.
- Review is double-blind.
- The submission PDF, images, video, supplement, code/data, and PDF metadata must not
  identify authors or institutions.
- The paper must stand on its own; supplemental material is optional and reviewers
  are not required to inspect it.
- Companion video maximum: 5 minutes.
- MP4 is encouraged for video; JPG/PNG are encouraged for stills.
- Dual-track submission limit: 7 core pages excluding references, plus at most two
  figures-only pages after references.
- Journal-only submission has no hard maximum; recent guidance says typical journal
  papers are roughly 8-12 pages excluding references.
- Appendices do not belong in the main paper; put them in supplemental material.
- URLs to identifying public repositories are discouraged. A frozen, anonymized
  repository may be used when necessary.
- A representative image is required by the submission process and must have
  publication permission.
- Supplemental packages above 500 MB are not guaranteed to be downloaded/reviewed.
- Conflict information is mandatory in the submission system.
- The 2026 SIGGRAPH policy requires disclosure when generative AI is used to create
  manuscript content beyond grammar correction. Because this project uses LLM
  assistance during drafting and software work, maintain a factual use log and
  prepare the required disclosure rather than trying to conceal the assistance.
  Human authors remain responsible for every claim, citation, equation, and result.

### Recommended venue strategy

Primary fit: SIGGRAPH / SIGGRAPH Asia Technical Papers, especially if the manuscript
is framed around executable captured worlds, counterfactual physical verification,
deceptive repair, and visually interpretable mechanism evidence.

A full journal-style draft is the safest internal source version because the method,
negative-gate sequence, measured retrospective study, and orthogonal-information
follow-on require careful qualification. If the target-year call retains the 2026
track structure, decide later whether to submit journal-only or compress to the
7-page dual-track format.

Do not simultaneously submit substantially the same manuscript elsewhere during a
SIGGRAPH review period. The current SIGGRAPH submission policy forbids simultaneous
peer-reviewed submission of substantially similar work.

Strong adjacent graphics options after a completed review cycle include SIGGRAPH
Asia, ACM Transactions on Graphics, Eurographics / Computer Graphics Forum, ACM
Symposium on Computer Animation, and Pacific Graphics. Venue adaptation may change
emphasis and length, but must not change frozen results or evidence classes.

### Recommended writing strategy

Write a complete internal manuscript first at roughly 9-10 pages plus references,
because the research story is too nuanced to author directly as a compressed
7-page document.

Then prepare either:

1. a journal-only SIGGRAPH/TOG submission from the complete version; or
2. a 7-page dual-track cut that keeps the core result in the paper and moves
   implementation detail, extra ablations, and reproduction detail to supplement.

Do not let the supplement carry a result that is necessary to believe the central
claim.

## 15. Adjacent venue adaptation

The same manuscript can be adapted to:

- SIGGRAPH Asia Technical Papers;
- ACM Transactions on Graphics;
- Eurographics / Computer Graphics Forum;
- ACM Symposium on Computer Animation;
- Pacific Graphics or another graphics venue if the final positioning is narrower.

For SIGGRAPH/SIGGRAPH Asia, emphasize:
- executable captured worlds;
- visual physical reasoning;
- mechanism-selective counterfactual interrogation;
- deceptive repair;
- visual evidence and information limits.

For simulation/animation-focused venues, emphasize:
- verification under model mismatch;
- intervention design;
- uncertainty-normalized response;
- commit/refusal semantics.

Do not change the frozen result to fit a venue.

## 16. Anonymity audit before submission

The public repository is not an anonymous supplement. Do not link it directly in a
double-blind submission.

Identity-bearing material currently includes:

- GitHub repository URLs containing the account name;
- README badges and raw media URLs;
- the `NOTICE` copyright owner;
- the project `LICENSE` appendix copyright name;
- local path examples in `visualization/explainer/README.md`, including
  `/Users/swayamsingal/...`;
- Git history and commit metadata;
- user-agent strings in fetch scripts that contain the public GitHub repository URL.

For an anonymous supplement, create a clean export rather than copying the Git
repository wholesale.

The anonymous package should:

1. contain no `.git/` history;
2. remove names, emails, affiliations, acknowledgements, and user-specific paths;
3. replace public repository URLs with neutral text or an anonymized frozen URL;
4. strip PDF and media metadata;
5. include only code/data necessary for review;
6. preserve dataset citations and third-party attribution without identifying the
   paper authors;
7. use a frozen checksum manifest;
8. keep the scientific freeze and result hashes;
9. avoid linking to the public main repository during review;
10. run the same reproduction validation before packaging.

A dedicated anonymized export script should be created when manuscript preparation
starts.

## 17. Licensing and publication rights

Current project license:
- Apache License 2.0
- `LICENSE`
- `NOTICE`

Third-party boundaries:
- `THIRD_PARTY_NOTICES.md`

Important paper-media points:
- Stanford Bunny remains under Stanford's source terms and must be credited when used.
- Poly Haven showcase assets are CC0.
- DOT C2 is recorded as CC0 1.0.
- GAUGE displays MIT at the frozen audit; re-check asset-level terms before
  redistribution.
- future Wikimedia development footage includes CC-BY and CC-BY-SA sources and
  should not be silently mixed into ACM supplementary media.

The solver-body mathematical explainer is the cleanest companion video because it
uses project-generated vector graphics and frozen solver output without the Stanford
Bunny or Wikimedia footage.

## 18. Manuscript source hierarchy

When two repository files disagree, use this priority order:

1. `research/results/VULKAX_FINAL_RESULTS_2026-09-21.json`
2. `research/results/ORTHOGONAL_FORCE_COMPLIANCE_RESULT_2026-09-21.json`
3. `research/results/DCS_FINAL_RESULTS_2026-09-20.json`
4. frozen protocol documents
5. `research/status/DCS_MATH_IMPLEMENTATION_AUDIT_2026-09-21.md`
6. `research/paper_data/PAPER_POSITIONING.md`
7. `research/literature/CLAIM_GUARD.md`
8. visualization-derived summaries
9. README wording

Never take a rounded value from a figure when the exact JSON value exists.

## 19. Caption rules

Every scientific caption should identify:

- evidence class: constructed, discovery, frozen validation, retrospective measured,
  fresh synthetic follow-on, or post-hoc diagnostic;
- population size;
- whether the figure uses a frozen threshold;
- whether any displayed geometry is magnified;
- whether a representative case is post-hoc selected;
- whether a visual carrier is not the benchmark geometry.

Avoid captions that say "our method succeeds" when the actual decision is unresolved.

## 20. Abstract content checklist

A strong abstract should contain, in this order:

1. problem: executable captured worlds can accept observationally improving but
   physically deceptive repairs;
2. approach: independent mechanism-selective counterfactual verification;
3. negative result: DCS and matched verification channels remain unresolved under
   frozen tests;
4. diagnostic result: the information frontier shows the gap is large, not a near
   miss;
5. positive follow-on: orthogonal force compliance increases median standardized
   signal by 11.46x;
6. boundary: even the stronger channel remains below the decision threshold;
7. conclusion: physical verification is information-limited and should permit
   explicit refusal.

Do not spend abstract words on Vulkan/Metal, Gaussian rendering, or release
engineering unless the final paper becomes primarily a systems paper.

## 21. Representative image

Best current representative-image candidates:

1. `docs/readme_assets/02_deceptive_repair.png` for immediate problem statement.
2. a combined crop of `02_deceptive_repair`, `07_residual_field`, and
   `11_information_limit` for a three-stage visual story.
3. `reality_probe_hero_standalone.svg` rendered to a high-resolution JPEG if a
   more cinematic representative image is desired.

The representative image must not imply that the force channel crossed the decision
threshold.

## 22. Supplement structure

Recommended anonymous supplemental PDF:

1. full system and solver configuration;
2. DCS math-to-code audit;
3. complete D1-D4V protocol;
4. uncertainty propagation details;
5. matched-baseline definitions;
6. information-frontier derivation and tables;
7. full orthogonal-force protocol;
8. complete per-case proposal statistics;
9. GAUGE retrospective protocol and per-trial results;
10. DOT measured-source provenance;
11. numerical convergence/refinement;
12. reproduction instructions;
13. additional visualizations.

Recommended supplemental archive:

```text
supplement/
  README.txt
  figures/
  tables/
  protocols/
  source-data/
  reproduction/
  video/
  checksums/
```

Do not include the public Git history in the anonymous package.

## 23. Files to open first when writing

For Introduction:
- `research/paper_data/PAPER_POSITIONING.md`
- `research/results/VULKAX_FINAL_RESEARCH_SUMMARY_2026-09-21.md`

For Related Work:
- `research/literature/papers.json`
- `research/literature/novelty_matrix.md`
- `research/literature/DCS_NOVELTY_THREAT_MAP.md`
- `research/literature/CLAIM_GUARD.md`

For Method:
- `research/status/DCS_MATH_IMPLEMENTATION_AUDIT_2026-09-21.md`
- `research/status/DCS_RESEARCH_PROGRAM.md`
- `research/benchmarks/ORTHOGONAL_FORCE_COMPLIANCE_PROTOCOL_2026-09-21.md`

For Experiments:
- `research/paper_data/EXPERIMENT_MATRIX.csv`
- `research/paper_data/ABLATION_MATRIX.md`
- frozen D2/D3/D4V protocol/result files

For Results:
- `research/results/VULKAX_FINAL_RESULTS_2026-09-21.json`
- `research/results/DCS_FINAL_RESULTS_2026-09-20.json`
- `research/results/DCS_INFORMATION_FRONTIER_2026-09-20.json`
- `research/results/ORTHOGONAL_FORCE_COMPLIANCE_RESULT_2026-09-21.json`
- `research/results/GAUGE_PAIRED_DIAGNOSTICS_2026-09-20.csv`

For Discussion:
- `research/status/DCS_LIMITATIONS_AND_KILL_CRITERIA.md`
- `research/literature/CLAIM_GUARD.md`
- `research/status/CURRENT_RESEARCH_STATE_2026-09-21.md`

For figures:
- `research/paper_data/PUBLICATION_ASSET_LOCK.md`
- `research/paper_data/FIGURE_TABLE_SOURCE_MAP.md`
- `visualization/README.md`
- `visualization/explainer/README.md`
- `docs/readme_assets/`

For reproducibility:
- `research/paper_data/REPRODUCIBILITY_CHECKLIST.md`
- `docs/PAPER_EVIDENCE_REPRODUCTION.md`

## 24. Paper-writing stop conditions

Before changing any scientific value during writing, check the frozen JSON first.

Do not:
- rerun a failed partition and replace it with a more favorable seed;
- alter the threshold;
- retune the 40 N force;
- change fresh truth values;
- relabel retrospective evidence;
- use post-hoc AUROC/AP as preregistered primary metrics;
- merge D4V counts with fresh OFC counts;
- replace aggregate 11.46x with the selected case's approximately 11.70x;
- report display magnification as physical displacement;
- present the explanatory fingerprint as a literal dense compliance matrix.

If a new experiment becomes necessary during review, it must be separately frozen and
clearly separated from the existing paper freeze.

## 25. Immediate next step

The research package is ready for manuscript construction.

The next repository phase should create:

```text
paper/
  main.tex
  references.bib
  sections/
  figures/
  tables/
  supplement/
  Makefile
```

Start with the full journal-style internal manuscript. Keep every numerical statement
traceable to the frozen source hierarchy in Section 18 of this dossier.

# Reality Probe — Next Manuscript Revision Plan

**Frozen planning date:** 2026-09-23  
**Scope:** manuscript evidence/structure plan. The free-fall evidence lane is now
closed as a preserved negative result (2026-09-24), so the structural manuscript
revision is authorized. The positive measured claim is the repository-locked IRIS
pendulum result; the free-fall lane must remain negative and nonconfirmatory.

This plan consolidates the current reviewer red-team, validation evidence, and
external faculty feedback. It is intentionally stricter than a normal editing
checklist: every future prose change must remain consistent with the claim guard
and with the prospective/retrospective boundaries already recorded in the repo.

## 1. Central paper story

The paper must open with one plain-language problem:

> A captured-world model can fit observations better after a rewrite and still
> become physically worse. Reality Probe separates proposing a rewrite from
> independently interrogating whether the physical change is supported.

The primary contribution is the **verification transaction/contract**:

1. ordinary evidence proposes a physical rewrite;
2. a deliberately distinct interrogation produces verification evidence;
3. the system returns SUPPORT, VETO, or UNRESOLVED;
4. commit/rollback/abstention follows that evidence.

The annihilation / moment-cancellation construction is machinery used to build
one interrogation family. Finite differences, moment cancellation, inverse
physics, parameter fitting, and credibility/VVUQ concepts must not be described
as individually novel.

## 2. Abstract rewrite

External feedback repeatedly identified the current abstract as too opaque.

The next abstract must:

- define Reality Probe in the first 1–2 sentences;
- avoid unexplained labels such as D2/D3/D4V/OFC;
- avoid isolated numerical values whose denominator/meaning is unavailable to
  an abstract reader;
- state one concrete failure result and one concrete successful locked result;
- explicitly say that the result is **channel/information dependent**, not a
  universal verifier;
- reserve protocol detail for the body.

A reader should understand the problem, transaction, experiment type, and main
finding without knowing any internal stage name.

## 3. Introduction and contribution boundary

The introduction must distinguish:

- proposal quality vs physical validity;
- observation fit vs independent physical evidence;
- verification contract vs the individual probe equations.

Add an explicit novelty-boundary paragraph against:

- model validation / VVUQ and credibility assessment;
- inverse physics / system identification;
- hypothesis testing and abstention/selective prediction;
- dependency between proposal and verification channels;
- counterfactual model checking;
- dark-field / cancellation ideas and finite-difference moment cancellation.

The paper should state the narrower contribution directly rather than defending
dozens of things it is “not” claiming. Reduce repetitive “not X / not Y”
phrasing and replace it with positive definitions wherever possible.

## 4. Concrete end-to-end example

Donald House’s main readability objection must be closed with a full worked
example, early in the paper:

1. show the actual captured input;
2. show the initial physical model and parameters;
3. show the observation-driven proposed rewrite;
4. show why ordinary fit prefers that rewrite;
5. show the independent probe/interrogation;
6. show the resulting standardized evidence score;
7. show SUPPORT/VETO/UNRESOLVED;
8. show the physical ground truth used only for evaluation.

Figures must include physical units, model parameters, before/after state, and
the specific observable being interrogated. “Decorative” images without those
references do not satisfy this requirement.

## 5. Experimental ladder

Preserve the negative-result history instead of hiding it.

The paper should present the experimental ladder as:

- constructed control: implementation sanity check;
- D2/D3/D4V: ordinary-looking improvements can remain unidentified;
- force-compliance/OFC: a stronger channel increases evidence but still does
  not cross the frozen decision threshold;
- prospective GAUGE: real measured deformable data remains information-limited;
- locked IRIS pendulum: a better-matched physical observable produces useful
  SUPPORT/VETO/UNRESOLVED decisions on 10 fresh real videos;
- post-final reviewer hardening: clustered statistics, stronger baselines,
  GT-hidden proposals, corruption/dependence/measurement-uncertainty tests;
- free-fall second-domain attempt: report V5 failure and every later rescue
  outcome honestly. It becomes supporting evidence only if fresh V6 validation
  and lock-gated final testing succeed.

This sequence is scientifically stronger than pretending every stage succeeded:
it demonstrates the paper’s information-boundary thesis.

## 6. Realism and graphics relevance

Multiple reviewers converged on the need for stronger graphics-facing evidence.

Before targeting a major graphics venue, the manuscript should include:

- benchmark/public datasets with reproducible access;
- visual before/after examples with physical and rendering context;
- at least one realistic captured-world or digital-twin example, not only an
  abstract 2-D/foam schematic;
- explicit visual-quality outputs where the venue expects them;
- an end-to-end “proposal -> verification -> gate” visualization;
- failure/corner cases, not only successful cases.

For an XR venue, an XR-ready world/interface or user-facing evaluation would be
needed. For a graphics venue, benchmark data and visual quality are the higher
priority. Do not add an XR user study merely to decorate a graphics paper.

## 7. Statistics and experimental units

Never describe repeated controlled rows as independent physical experiments.

For the locked IRIS pendulum result:

- physical units = 10 videos;
- controlled case/method rows are repeated measurements nested within those
  videos;
- use clustered/video-level statistics;
- retain the exact sign-test result where appropriate;
- use confidence/coverage language that matches the small number of physical
  units.

Use **standardized evidence score**, not “z-score”, unless a standard-normal
sampling distribution is established.

## 8. Baselines

Baselines must answer distinct reviewer questions:

- matched small-angle physical baseline;
- direct finite-amplitude period residual;
- damped nonlinear ODE residual;
- official/pinned IRIS parameter-recovery references, clearly labelled as
  parameter-estimation references rather than SUPPORT/VETO/UNRESOLVED
  verification baselines;
- proposal-only/ordinary-fit decision as the failure mode Reality Probe is
  intended to guard.

Avoid straw-man wording. If a baseline performs poorly on this task, state only
that measured result.

## 9. Reproducibility package

The paper and repository must let another researcher reproduce a concrete case.

Required items:

- exact public dataset revisions and hashes;
- scene/take manifests;
- protocol/config hashes;
- lock files for final tests;
- one-command reproduction for each claimed table/figure;
- source artifact links for result rows;
- explicit development/validation/final partition history;
- all failed validation attempts retained in provenance;
- no silent retuning after held-out data are opened.

## 10. Writing/style pass

The next prose pass must be human-readable before it is compact.

Rules:

- define every term before abbreviation;
- one idea per sentence where the concept is difficult;
- use concrete nouns instead of stacks of abstract labels;
- explain why a number matters immediately after giving it;
- reduce parenthetical caveats and repeated negative constructions;
- keep internal stage labels out of the abstract;
- use the author’s own explanatory voice rather than generic “paper-sounding”
  transitions;
- preserve the required AI-use disclosure for the target venue/policy.

External feedback explicitly perceived the existing prose as heavily
AI-produced/opaque. The response is not cosmetic “humanization”; it is clearer
scientific exposition, concrete examples, and fewer undefined abstractions.

## 11. Related-work expansion

The related-work section must be rebuilt around the paper’s actual claim
boundary, not a broad graphics bibliography.

At minimum cover and compare against:

- verification, validation, uncertainty quantification, credibility;
- inverse problems/system identification from video;
- physics-informed reconstruction/digital twins;
- counterfactual/model selection and hypothesis testing;
- selective prediction/abstention where relevant;
- experimental design / identifiability / information content;
- the specific graphics/captured-world systems closest to each empirical lane.

For every comparison state: **same problem? same evidence boundary? same decision
contract? same output?** This is more useful than claiming novelty by absence of
an identical acronym.

## 12. Figures/tables for the next revision

Plan the manuscript around a small set of explanatory visuals:

1. transaction diagram: propose -> separately reserved verification probe -> support/veto/unresolved
   -> commit/rollback/abstain;
2. one concrete captured-world worked example with physical units;
3. information-boundary ladder showing why early channels remained unresolved;
4. locked IRIS real-video example with observed trajectory and matched candidate
   physics;
5. risk/coverage or score distribution at the video-cluster level;
6. reviewer-hardening robustness figure;
7. second-domain free-fall failure figure showing the untouched validation miss,
   with the unopened final split and retrospective-rescue boundary explicit.

Tables must report physical-unit counts separately from row counts.

## 13. Claims allowed now

The manuscript may currently argue:

- observational improvement does not by itself certify physical improvement;
- a separate rewrite-verification transaction can explicitly support, veto, or abstain;
- some tested channels are information-limited under a frozen threshold;
- changing the physical evidence channel can materially change observability;
- on the locked 10-video IRIS pendulum final set, the finite-amplitude probe
  produced 90/110 correct controlled decisions vs 40/110 for the matched
  small-angle baseline, with 10/10 placebo abstentions;
- the second-domain IRIS free-fall attempt failed untouched validation despite
  5/5 low-level quality passes, with 111.7% median acceleration relative error;
- verification effectiveness is channel/information dependent, and failed lanes
  can be retired rather than post-hoc rescued.
- post-final video-cluster statistics show the primary method ahead on 10/10
  physical videos (two-sided exact sign-test p=0.001953125);
- stronger post-final comparators do not recover the locked result (45.45%
  direct finite-amplitude residual; 9.09% damped nonlinear ODE residual);
- a GT-hidden post-final proposal experiment produces 10 ordinary-improving
  proposals (8 deceptive, 2 physically better) and the reserved verifier is
  correct on all 10 after proposal hash closure;
- canonical labels remain stable through the recorded ±2 sigma rope-length
  uncertainty interval;
- the full corruption sweep preserves the clean result under the tested noise,
  blur, frame duplication/drop, occlusion, and crop conditions, while exposing
  temporal downsampling as a limitation (63.64% at 30 fps; 45.45% at 15 fps).

It may not claim:

- universal physical verification;
- independent sensing when the same video supplies proposal/probe information;
- 220 independent experiments;
- cloth/material/collision generality from pendulum evidence;
- that DCS/moment cancellation itself is new mathematics;
- successful free-fall confirmation or use of the unopened drop_150 final split;
- that retrospective V6.7 rescue diagnostics are confirmatory evidence.

## 14. Revision order

**Status 2026-09-24:** the evidence campaign is closed for this manuscript pass.
The structural rewrite, claim table, related-work/novelty boundary, clustered
statistics, stronger baselines, GT-hidden proposal analysis, robustness
limitations, and disclosure pass have now been integrated. The remaining work is
submission-format polish and artifact/repository hygiene rather than method
retuning.

The completed revision order was:

1. freeze final claim table from repository evidence;
2. rewrite abstract;
3. rewrite introduction/contributions;
4. rebuild related work around the claim boundary;
5. add the concrete worked example;
6. replace/upgrade graphics and figures;
7. rewrite experiments as the evidence ladder;
8. add clustered statistics/baselines/limitations;
9. perform full claim-to-artifact audit;
10. final prose/style/disclosure pass.

Do not start by polishing sentences in the current manuscript. The structure and
evidence presentation should change first.

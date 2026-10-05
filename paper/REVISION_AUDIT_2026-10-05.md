# Reality Probe revision audit — 2026-10-05

This revision consolidates the external manuscript comments received before the
CAVW resubmission and records which changes were made. It does not alter the
frozen scientific result ledgers or reopen any spent validation set.

## Donald House

- **Abstract was opaque and numerical labels appeared before the reader knew
  what they meant.** Rewritten around the concrete acceptance problem; internal
  D-stage labels are absent from the abstract and the IRIS headline now explains
  what the finite-amplitude and small-angle tests are.
- **Show a concrete captured-data/model/edit/test flow.** The introduction now
  defines what a physical edit is, who/what may propose it, and where Reality
  Probe begins. The measured DOT path is explicitly tied to its table and
  held-out geometry figure.
- **Use clearer human-facing prose.** The introduction/contributions and
  discussion were rewritten with concrete state transitions rather than dense
  stage terminology.

## Brendan David-John

- **Explain why physical edits exist and how they are made.** The first
  paragraphs now motivate animation/authoring, digital-twin recalibration, and
  robotics-style state updates, and state that the proposal may come from an
  optimizer, scripted calibration, authoring UI, or learned estimator.
- **Smooth the inverse-physics transition.** Related work now separates parameter
  estimation, identifiability/experiment design, V&V, and the paper's narrower
  commit decision.
- **Clarify novelty against verification/validation.** A dedicated positioning
  paragraph explicitly disclaims novelty for VVUQ, finite differences, moment
  cancellation, abstention, and inverse physics. The contribution is the
  evidence-gated persistent-state transition and the information-boundary study.
- **Figure 1 looked like slides.** The 3x3 storyboard was removed from the
  manuscript. Figure 1 is now a purpose-built vector/TikZ transaction diagram
  showing proposal, freeze, reserved test, uncertainty comparison, and the
  SUPPORT/VETO/UNRESOLVED outcomes.
- **Show the actual physical state/edit rather than only abstract diagnostics.**
  The force-compliance visuals are now grouped as one worked example showing the
  simulated states under the same 40 N load, their response overlay, response
  fingerprint, and residual field. DOT C2 remains the measured-geometry example.
- **Contribution wording should state findings, not call an experiment a
  contribution.** Contribution 3 now states the measured result and the failed
  transfer result directly.
- **Small-angle / IRIS terms were dangling.** The manuscript now explains that
  the baseline is the common small-angle pendulum period approximation and the
  primary test uses a finite-amplitude relation accounting for measured swing
  amplitude.

## Jian

- **Related work and experiments were not publication-ready in the early
  version.** The current manuscript contains the expanded captured-world,
  identifiability/experiment-design, V&V, and abstention positioning plus the
  locked IRIS pendulum study, failed free-fall transfer, stronger baselines,
  clustered statistics, GT-hidden proposals, corruption tests, and
  cross-platform replay. No new result was fabricated for this prose revision.

## Anthony Steed

- **Graphics venues need benchmark data and visual examples.** The paper keeps
  the measured DOT/GAUGE/IRIS evidence and now makes the controlled physical
  edit/intervention visually explicit. It does not claim photorealistic
  reconstruction quality, which is outside the verification endpoint.

## CAVW editorial return (manuscript 2457984)

- **Figures/tables uploaded but not cited.** CI now rejects any manuscript
  figure/table label that lacks a prose reference. The current source passes
  that check.
- **Reviewer PDF could not be generated.** All manuscript figure dependencies
  are local to `paper/`. CI builds `CAVW_Main_LaTeX.zip`, unpacks that exact
  archive into a clean directory, and compiles `main.tex` there. The archive is
  committed only after that reviewer-PDF build succeeds.

## Declarations and submission hygiene

The manuscript now contains explicit Ethics, Data Availability, Funding,
Conflict of Interest, Permissions, and Generative AI Use declarations. The AI
statement records the use window, task categories, validation procedure, and
responsibility boundary. The repository package contains only the main
manuscript, bibliography, and figure dependencies; internal review notes and
research ledgers are not bundled for Wiley as the main document.

## Comments not treated as technical Reality Probe reviews

- The APS/PRE desk decision supplied no technical review.
- Kaan Akşit recommended immediate-area reviewer feedback but did not provide a
  detailed Reality Probe critique in that message.
- Ioannis Ivrissimtzis's detailed work-savings/rendering comments were about
  MAVEB; they are not attributed to Reality Probe.
- Frank Langbein stated that he would review the two manuscripts separately; no
  later Reality Probe-specific review was available at this revision point.

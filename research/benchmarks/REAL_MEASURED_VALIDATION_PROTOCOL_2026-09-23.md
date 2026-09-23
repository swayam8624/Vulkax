# Prospective Real / Measured Validation Protocol

Status: **planned; no measured result is claimed by this document.**

This protocol is intentionally separate from the retrospective GAUGE analysis.

## Objective

Test whether a frozen Reality Probe decision rule can correctly return **support**,
**veto**, or **unresolved** when the physical truth is established independently of
the proposal-generation channel.

The experiment is not intended to demonstrate that every captured-world property is
identifiable. A correct unresolved outcome is part of the hypothesis.

## Minimal physical design

A useful low-cost experiment requires:

1. a deformable or compliant object whose relevant property can be independently
   measured;
2. a capture channel used to build/propose the world update;
3. a physically distinct interrogation channel used only for verification;
4. an external ground-truth measurement that is not derived from either model's
   score.

An example is a clamped compliant specimen where visual deformation is the proposal
channel and known-force displacement/compliance is the verification channel.
Independent ground truth can come from calibrated load/displacement measurements.

## Required truth conditions

Each physical regime must provide all three classes.

### SUPPORT
The proposed rewrite moves the relevant physical prediction closer to independently
measured truth by a predeclared margin.

### VETO
The proposal improves or preserves the proposal-channel fit but worsens the
independent physical target by a predeclared margin.

### UNRESOLVED
The available interrogation is intentionally made non-identifying, for example by
insufficient excitation, observation dropout, or a channel that cannot distinguish
the competing models within the frozen uncertainty budget.

## Data separation

- Development objects/repeats: implementation only.
- Validation objects/repeats: verify the frozen pipeline and exclusion rules.
- Final-test objects/repeats: unopened until
  `freeze_publication_validation.py` creates the protocol lock.

No final-test repeat may be moved into development after its label is known.

## Predeclared exclusions

Permitted exclusions must be observable without looking at the method's correctness,
for example:
- corrupted file/checksum;
- instrument saturation;
- failed calibration standard;
- missing timestamp synchronization beyond a predeclared bound.

A hard case, an unresolved case, or a wrong Reality Probe decision is **not** an
exclusion reason.

## Required outputs per trial

Every method emits a row matching
`research/validation/record_schema_v1.json`, including:
- paired trial key;
- truth class;
- signed score;
- support/veto/unresolved decision;
- frozen threshold;
- perturbation metadata;
- source artifact and seed/repeat identifier.

Raw instrument and capture files must be checksum-indexed separately.

## Required reporting

Headline:
- three-way accuracy;
- support and veto recall;
- false assertion on unresolved trials;
- coverage and selective risk;
- bootstrap confidence intervals clustered by physical scene/object.

Stress tests:
- noise;
- missing observations;
- probe strength / dose response;
- proposal/probe channel dependence.

Negative controls:
- null load / null intervention;
- shuffled or unrelated verification evidence where physically meaningful.

## Stop / interpretation rule

If the primary frozen threshold again produces near-zero coverage, report that
result. Do not lower the threshold on final-test data merely to obtain decisions.

If a different threshold is explored afterward, label it diagnostic and require a
new untouched confirmation set before making a prospective claim.

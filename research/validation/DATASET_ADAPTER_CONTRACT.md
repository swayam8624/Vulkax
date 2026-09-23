# Multi-Dataset Adapter Contract

Every dataset-specific runner used for the publication-validation campaign must
conform to this contract. The goal is to prevent dataset-specific convenience
choices from changing the scientific question.

## World manifest

The final campaign manifest must contain one row per independently resampled
scene/object/world with at least:

```text
dataset,scene,split,adapter,source_uri,truth_source,units,exclusion_rule
```

`split` must be one of `development`, `validation`, or `final_test`.

The final-test manifest is part of the protocol lock and may not be edited after
labels/results are opened except to apply a predeclared exclusion rule.

## Truth requirements

An adapter may emit:

- **support** only when an independent physical target says the proposal improves
  the predeclared quantity;
- **veto** only when that target says the proposal worsens it;
- **unresolved** only when non-identifiability is known by construction or by a
  predeclared independent adequacy test.

A missing label is not automatically `unresolved`. If a dataset cannot establish
truth for a requested trial, mark the trial as unavailable in the adapter report
and do not create a scientific record for it.

## Dataset-native processing

Use native poses, depth, calibration, meshes, force/trajectory measurements, and
provided train/test splits where available. Do not replace trustworthy native
geometry/pose metadata with a blind reconstruction step merely to make datasets
look uniform.

Every conversion must record:
- source coordinate convention;
- destination convention;
- length/force/time units;
- scale transform;
- frame/pose transform;
- any interpolation or resampling;
- checksums of source and generated evidence.

## Proposal/probe separation

The proposal-generation inputs and verification inputs must be listed separately.
If they overlap, record the planned `channel_dependence` value or a conservative
dependence category. A supposedly independent probe must not silently reuse the
same held-out measurement used to rank the proposal.

## Required method fairness

All compared methods receive the same admissible information budget for a trial.
A method may decline/return unresolved. If a baseline cannot consume a modality,
record the reason rather than giving Reality Probe extra evidence without
disclosure.

At minimum, where mathematically applicable, compare:
- Reality Probe / DCS;
- orthogonal physical channel;
- same-cost raw bundle;
- raw pairwise intervention;
- Fisher/Jacobian selection;
- maximum-motion selection;
- simple residual/uncertainty baseline.

## Output

Each method/trial emits exactly one standardized CSV row matching
`record_schema_v1.json`.

A dataset adapter should also emit `adapter_report.json` containing:
- dataset and scene counts;
- source checksums;
- excluded scenes and rule used;
- unavailable trial families and why;
- unit/convention transforms;
- proposal-channel inputs;
- probe-channel inputs;
- git commit and adapter version.

## Failure policy

The adapter must stop rather than:
- silently dropping failed worlds;
- relabeling difficult cases;
- substituting another scene after seeing its result;
- clipping evidence to make a threshold pass;
- changing units or scale heuristically without provenance.

Failed preparation belongs in the benchmark accounting. It is not a reason to
reduce the denominator unless the frozen exclusion rule permits it.

# Vulkax 0.40 — captured deformable evidence bundle

Vulkax 0.40 defines a versioned input/evidence contract around the captured-deformable payloads already used by replay, material identification and Operator Influence.

The goal is deliberately narrower than reconstruction: **make the exact evidence entering a captured-object experiment explicit, traceable and rejectable before simulation.** This milestone does not create measured data and does not claim that a bundle is physically correct merely because it is structurally valid.

## Bundle layout

The deterministic generator now emits:

```text
capture.vkcap
object.ply
particles.csv
observations.csv
uncertainty.csv
truth.csv
```

`truth.csv` is controlled-regression ground truth only. It is not referenced by the bundle manifest and is not required for a measured capture.

The four evidence payloads referenced by `capture.vkcap` are:

- `object.ply` — captured Gaussian appearance;
- `particles.csv` — stable physical particle IDs, rest positions, masses and rest volumes;
- `observations.csv` — marker-to-particle observations, timestamps and fit/validation roles;
- `uncertainty.csv` — one position-uncertainty row for every marker/time observation.

## Manifest schema v1

The schema is a small deterministic quoted-text format consistent with Vulkax's existing document style:

```text
vulkax_capture 1
id "vulkax-controlled-captured-deformable-v1"
appearance "object.ply" "<sha256>"
particles "particles.csv" "<sha256>"
observations "observations.csv" "<sha256>"
uncertainty "uncertainty.csv" "<sha256>"
units "m" "kg" "s"
frame "controlled-world" "right-handed-y-up"
time_step 0.0001
source synthetic "deterministic Vulkax controlled regression; nonzero-time rows declare 1e-6 m component uncertainty"
```

Schema v1 requires payload values to already be expressed in SI metres, kilograms and seconds. Vulkax does **not** silently rescale non-SI payloads in this schema.

The `source` kind is one of:

```text
synthetic
measured
derived
```

This field and its description are provenance declarations, not cryptographic proof of where data originated.

## Observation uncertainty sidecar

`uncertainty.csv` has the exact header:

```text
marker_id,time,sigma_x,sigma_y,sigma_z
```

Each observation in `observations.csv` must have exactly one matching uncertainty row by `(marker_id, time)`. Sigma components must be finite and non-negative.

For the deterministic controlled bundle, `t = 0` rows declare zero component sigma and nonzero-time rows declare `1e-6 m` component sigma. Those values are a **synthetic contract/regression choice**, not an estimate of camera or tracker accuracy.

## Validation performed before simulation

The public bundle validator rejects malformed evidence before the captured solver path runs. Schema-v1 validation covers:

- supported manifest version and required records;
- non-empty bundle ID, coordinate frame, axis convention and source description;
- SI payload unit declarations (`m`, `kg`, `s`);
- finite positive solver timestep;
- portable relative payload paths and lexical parent traversal such as `../...`;
- exact SHA-256 identity of the four referenced payload files;
- valid Gaussian appearance input;
- duplicate or invalid physical particle IDs;
- positive finite particle mass and rest volume;
- duplicate marker/time observations;
- observations referencing unknown particle IDs;
- stable marker-to-particle identity over time;
- timestamps lying on the declared solver timestep lattice;
- at least four distinct `t = 0` fit particles for affine captured-pose initialization;
- at least one nonzero-time fit observation and one nonzero-time held-out validation observation;
- exact observation/uncertainty coverage;
- a `t = 0` initialization observation and at least one dynamic observation for every marker;
- stable fit/validation assignment across each marker's **nonzero-time** trajectory.

The `t = 0` split is allowed to differ from a marker's dynamic split because initialization and held-out dynamic evaluation have different roles in the current captured benchmark. This is intentional and matches the controlled dataset.

The underlying CSV loaders continue to enforce their existing syntax and identity constraints; 0.40 adds bundle-level meaning rather than replacing those payload formats.

## Public validation command

Generate and validate the controlled bundle:

```bash
./build/vulkax captured-deformable-generate-example build/captured-example

./build/vulkax captured-deformable-validate-bundle \
  build/captured-example/capture.vkcap
```

A valid command reports the schema/provenance metadata, payload counts, maximum declared position sigma and all four SHA-256 hashes.

The Linux end-to-end gate then copies the generated bundle, mutates `observations.csv` without changing the manifest, and requires validation to fail with a SHA-256 mismatch. The same validated payload is subsequently used by the existing calibration, finite-difference/adjoint influence and observation-robustness gates.

## Integrity and provenance meaning

SHA-256 provides exact input identity for experiment traceability. It means a later run can determine whether its bytes match the bytes named by the manifest.

It does **not** by itself establish:

- that a measured file came from the claimed camera, tracker or operator;
- that marker correspondences are physically correct rather than merely internally consistent;
- that uncertainty values are calibrated estimates;
- that the coordinate frame is externally registered correctly;
- that a Neo-Hookean/APIC model is an adequate model of the real object;
- that the data are free from systematic sensor error or model discrepancy.

Those questions require measured acquisition and external evidence.

## Controlled regression status

The schema/parser, SHA-256 implementation, uncertainty sidecar and malformed-bundle checks are covered by the cross-platform C++ suite. Known SHA-256 vectors are included to avoid treating a self-consistent but incorrect hash implementation as evidence.

The Linux public path additionally requires:

- the deterministic generator to emit `capture.vkcap` and `uncertainty.csv`;
- the generated 64-particle / 20-observation / 20-uncertainty-row bundle to validate;
- deliberate observation-payload mutation to be rejected by the hash check;
- the already-established calibration, material-influence, observation-robustness and Vulkan gates to continue passing on the same payload.

## What 0.40 does not establish

0.40 is an **evidence-contract milestone**, not the measured-deformable milestone. In particular it does not establish:

- a real capture acquisition pipeline;
- real camera/tracker uncertainty values;
- automatic marker tracking or correspondence recovery;
- robustness to incorrect-but-internally-consistent correspondences;
- physical model adequacy for real material behavior;
- real-world material recovery or Operator Influence validity.

The next roadmap gate, 0.45, intentionally requires one genuinely measured deformable sequence. That requirement must not be satisfied by generating another synthetic dataset.

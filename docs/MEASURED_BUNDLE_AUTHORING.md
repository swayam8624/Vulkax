# Captured bundle authoring for measured-data preparation

Vulkax 0.40 validates a captured-deformable evidence bundle. This authoring bridge removes the remaining hand-written-manifest step for payloads that already exist on disk.

It does **not** acquire, synthesize, rescale or repair measurements. It only records the supplied payload paths and metadata, computes SHA-256 identities, writes `capture.vkcap`, and immediately runs the same bundle validator used before simulation.

## Public command

```bash
./build/vulkax captured-deformable-author-bundle \
  captures/soft-body/capture.vkcap \
  captures/soft-body/object.ply \
  captures/soft-body/particles.csv \
  captures/soft-body/observations.csv \
  captures/soft-body/uncertainty.csv \
  soft-body-trial-001 \
  1e-4 \
  lab-table-frame \
  right-handed-y-up \
  measured \
  "camera reconstruction plus tracked physical markers, trial 001"
```

Arguments are:

```text
manifest output path
Gaussian appearance payload
physical-particle payload
marker-observation payload
observation-uncertainty payload
bundle ID
solver timestep
coordinate-frame name
axis convention
source kind: synthetic | measured | derived
source description
```

Schema v1 remains SI-only: positions are metres, masses are kilograms and timestamps are seconds. The command does not perform unit conversion.

## Payload ownership rule

All four payload files must already be regular files inside the directory tree containing the output manifest. Vulkax stores portable relative paths in the manifest and rejects an authoring request that would require `..` to reach a payload outside that tree.

The command never copies or rewrites payload bytes. Its success output therefore reports:

```text
payloads_modified: no
```

The caller is responsible for deciding which files constitute one capture bundle and for placing them together before authoring.

## Provenance rule

`source_kind` and `source_description` are caller declarations. Vulkax does not inspect a file and infer whether it is genuinely measured.

A successful command explicitly reports:

```text
source_authenticity: caller-declared, not inferred by Vulkax
```

Therefore a `measured` source label is **not** evidence that a real experiment occurred. The 0.45 roadmap gate still requires genuinely measured acquisition and evaluation evidence outside this authoring operation.

## Validation after authoring

Authoring succeeds only if the newly written manifest passes the existing full validator. That includes:

- SHA-256 identity of all four payloads;
- valid Gaussian appearance input;
- particle ID, mass and volume constraints;
- marker-to-particle identity consistency;
- timestep-lattice timestamps;
- initialization support;
- nonzero-time fit and held-out validation coverage;
- complete per-observation uncertainty coverage;
- marker trajectory completeness and stable dynamic split assignment.

The command can therefore create a manifest only for payloads that are already usable by the current captured-deformable pipeline.

## CI coverage

The cross-platform suite contains a dedicated authoring regression that:

- authors a nested `payloads/` directory into a manifest;
- checks the stored portable relative paths;
- checks all four SHA-256 identities;
- reloads and validates the authored bundle;
- rejects a payload outside the bundle tree;
- rejects a missing payload;
- rejects incomplete provenance metadata.

The Linux public-path gate also generates the controlled synthetic bundle, re-authors a second manifest from the exact existing payload bytes with source kind `derived`, validates it, and confirms that no payload modification is reported. The controlled data remain labelled as controlled/synthetic-derived evidence rather than being presented as measured.

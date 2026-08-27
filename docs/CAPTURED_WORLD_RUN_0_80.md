# Vulkax 0.80 — one-command captured-world research + showcase

Vulkax 0.80 is the integration milestone: it does not add another physics subsystem. It takes the existing capture, calibration, robustness, Operator Influence, adaptive proposal, verified transaction and Gaussian rendering pieces and exposes them through one reproducible command.

The public entry point is:

```text
vulkax captured-world-run <capture.vkcap> <output-dir> <marker-id> <time> <dir-x> <dir-y> <dir-z> [settings]
```

The core path is:

```text
capture bundle validation
        -> fit-only material calibration
        -> held-out replay
        -> observation robustness
        -> particle APIC material adjoint
        -> adaptive spatial proposal
        -> independent nonlinear + derivative verification
        -> verified transaction or automatic rollback
        -> Gaussian appearance state
        -> native research render
        -> optional deterministic showcase
        -> SHA-256 indexed result certificate
```

## Run success is not rewrite acceptance

Certificate schema v2 deliberately separates these two questions:

```json
{
  "schema": "vulkax_captured_world_run",
  "schema_version": 2,
  "run_status": "completed",
  "rewrite": {
    "status": "verified",
    "rollback_performed": false
  }
}
```

A scientifically complete run may instead contain:

```json
{
  "run_status": "completed",
  "rewrite": {
    "status": "rejected",
    "rollback_performed": true
  }
}
```

That distinction is required by the measured DOT C2 result: its proposed local material edit passes the configured nonlinear linearization tolerance but fails the complete independent derivative-oracle contract, so Vulkax correctly rejects the edit and restores the previous world state. 0.80 must preserve that outcome rather than convert it into a failed run or a fake successful rewrite.

The validator supports explicit expectations:

```bash
python3 scripts/validate_captured_world_run.py build/world-run \
  --expected-backend Vulkan \
  --expected-rewrite verified \
  --expected-showcase studio_pedestal
```

or, for a measured run whose edit is rejected:

```bash
python3 scripts/validate_captured_world_run.py build/dot-world-run \
  --expected-backend Vulkan \
  --expected-rewrite rejected \
  --expected-showcase cloth_showcase
```

## Controlled one-command demo

Generate the deterministic controlled bundle:

```bash
./build/vulkax captured-deformable-generate-example build/captured-example
```

Fetch the optional pinned presentation asset pack:

```bash
python3 scripts/fetch_demo_assets.py \
  assets/demo/showcase_assets.lock.json \
  build/demo-assets

python3 scripts/validate_demo_assets.py \
  assets/demo/showcase_assets.lock.json \
  build/demo-assets
```

Then execute the complete research + visual path:

```bash
./build/vulkax captured-world-run \
  build/captured-example/capture.vkcap \
  build/captured-world-run \
  m4 0.003 1 1 1 \
  Metal 0.08 0.01 0.02 12345 \
  --showcase studio_pedestal \
  --showcase-assets build/demo-assets \
  --showcase-resolution 1920x1080 \
  --turntable 12
```

Use `Vulkan` instead of `Metal` on a Vulkan system.

For a research-only run without a native renderer:

```bash
./build/vulkax captured-world-run \
  build/captured-example/capture.vkcap \
  build/captured-world-run \
  m4 0.003 1 1 1 \
  none 0.08 0.01 0.02 12345
```

Showcase mode requires native rendering and is therefore not permitted with `none`.

## Research rendering vs showcase rendering

0.80 keeps evidence and presentation conceptually separate.

### Research render

The native Vulkax Gaussian path writes:

```text
render/before.ppm
render/after.ppm
render/comparison.csv
```

These outputs are part of the research evidence bundle. They use the same Gaussian appearance representation and native backend that the renderer regression paths exercise.

### Showcase render

When `--showcase` is enabled, Vulkax additionally writes deterministic presentation artifacts under:

```text
render/showcase/
```

The first presets are:

- `studio_pedestal` — neutral floor plus a restrained Gaussian pedestal for controlled objects;
- `cloth_showcase` — neutral inspection/tabletop presentation without the pedestal.

The showcase renderer adds decorative Gaussian support geometry around the captured object and deterministic hero/detail/turntable cameras. Those props are presentation-only and are excluded from physical or reconstruction claims.

For a verified rewrite the visual names are:

```text
hero_before.ppm
hero_after.ppm
closeup_before.ppm
closeup_after.ppm
```

For a rejected rewrite Vulkax does **not** manufacture a committed after-state. It writes:

```text
hero_baseline.ppm
hero_rollback.ppm
closeup_baseline.ppm
closeup_rollback.ppm
```

The summary card states that the proposal was rejected and the rollback state is being shown.

Every showcase can also produce:

```text
turntable/frame_000.ppm ...
contact_sheet.ppm
summary_card.svg
showcase_manifest.json
```

## Pinned external presentation asset

`assets/demo/showcase_assets.lock.json` pins the current presentation environment reference. The repository stores only metadata; the downloaded file goes under `build/demo-assets/`.

The lock records:

- canonical source page;
- exact download URL;
- CC0 license declaration;
- author;
- expected byte count;
- expected SHA-256;
- deterministic local destination;
- intended presentation-only role.

`scripts/fetch_demo_assets.py` rejects an asset whose bytes or SHA-256 do not match the lock. `scripts/validate_demo_assets.py` rechecks the local pack independently.

### Important rendering boundary

The current Gaussian renderer uses stored Gaussian spherical-harmonic appearance. Vulkax 0.80 therefore does **not** claim that the pinned HDRI physically relights the captured Gaussian object. The HDRI is recorded as a reproducible environment/reference asset for the showcase layer while the native renderer preserves the stored SH appearance.

A future physically relightable appearance or optional external PBR presentation backend would be a separate graphics feature and is not allowed to silently expand the 1.0 research scope.

## Output tree

A complete controlled run has the shape:

```text
captured-world-run/
├── input/
│   ├── validated_manifest.txt
│   └── source_manifest_sha256.txt
├── calibration/
│   ├── material_grid.csv
│   ├── selected_samples.csv
│   ├── selected_summary.csv
│   └── selected_evidence.csv
├── robustness/
│   ├── robustness.csv
│   └── scenarios.csv
├── influence/
│   ├── reference.csv
│   ├── counterfactual.csv
│   ├── adjoint_influence.csv
│   ├── particle_adjoint.csv
│   ├── derivative_comparison.csv
│   ├── adaptive_proposal_summary.csv
│   ├── adaptive_reference.csv
│   ├── adaptive_counterfactual.csv
│   ├── adaptive_adjoint.csv
│   ├── adaptive_derivative_comparison.csv
│   └── selected_rewrite_region.csv
├── rewrite/
│   ├── transaction_evidence.csv
│   ├── transaction_summary.csv
│   ├── provenance.csv
│   └── physical_evidence/
├── appearance/
│   ├── before.ply
│   └── rewritten.ply
├── render/
│   ├── before.ppm
│   ├── after.ppm
│   ├── comparison.csv
│   └── showcase/
│       ├── hero_*.ppm
│       ├── closeup_*.ppm
│       ├── turntable/
│       ├── contact_sheet.ppm
│       ├── summary_card.svg
│       └── showcase_manifest.json
├── run_summary.csv
└── certificate.json
```

The exact set is SHA-256 indexed inside `certificate.json`. The certificate deliberately does not index itself.

## CI gates

`.github/workflows/captured-world-run.yml` exercises the public executable rather than only the library API:

- Linux: Vulkan research render + pinned asset fetch + studio showcase;
- macOS: Metal research render + pinned asset fetch + studio showcase;
- Windows: complete research pipeline with rendering disabled so the core path has no platform-specific graphics dependency.

The workflow verifies that the command is actually reachable from the public `vulkax` executable, validates the result certificate and uploads the evidence bundle.

The existing `.github/workflows/measured-dot-c2.yml` remains the canonical measured-source benchmark and must stay green as 0.80 evolves.

## 0.80 acceptance boundary

0.80 is ready to version only when the exact candidate satisfies all of the following:

1. public `vulkax captured-world-run` is callable;
2. controlled one-command run is reproducible;
3. run completion and rewrite acceptance remain separate certificate fields;
4. a rejected rewrite is accepted only with rollback and complete evidence;
5. research artifacts are SHA-256 indexed;
6. Vulkan and Metal research renders pass their public gates;
7. pinned presentation assets pass independent checksum validation;
8. studio/cloth showcase semantics never misrepresent a rejected edit as committed;
9. Linux, macOS and Windows CI are green;
10. the measured DOT C2 regression remains green;
11. README/roadmap wording matches the actual implementation;
12. only then is the project version advanced from `0.70.0` to `0.80.0`.

0.80 is an integration, visualization and packaging milestone. New research subsystems remain out of scope until the existing 1.0 path is shipped.

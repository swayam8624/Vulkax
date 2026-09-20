# Branch retirement manifest — 2026-09-20

Active research branch: `research/dcs-darkfield`.

The branches below were individually compared against the active DCS head and have
**ahead_by = 0**. Their commits are ancestors of the DCS head, so deleting the
branch ref does not delete unique repository history.

## Safe to delete

- `research/constitutive-adequacy`
- `research/convergence-certificate`
- `research/discovery`
- `research/fixed-forward-co-refinement`
- `research/fixed-forward-deep-refinement`
- `research/fixed-forward-space-transfer`
- `research/fixture-overlap-heldout`
- `research/gauge-structural-forensics`
- `research/grid-phase-falsification`
- `research/refinement-forensics`
- `research/refusal-validation3`
- `research/refusal-validation4`
- `research/refusal-validation5`
- `develop/1.1`
- `feat/adaptive-material-influence-regions`
- `feat/captured-adjoint-influence`
- `feat/captured-world-run`
- `feat/reality-loop-worldir`
- `feat/release-hardening-0-90`
- `feat/surface-proxy-showcase-1-0-main`
- `feat/verified-rewrite-transactions`
- `feat/verified-rewrite-transactions-check`

## Preserve

- `main` — production/default lineage.
- `release/1.0.0` — release anchor even though its commits are ancestors.
- `legacy/studio-v1-2026-08-10` — explicit historical anchor.
- `research/integration-20260920` — keep until DCS is merged/frozen.
- `research/dcs-darkfield` — active flagship work.

## Not approved for deletion yet

Any branch reported as `diverged` with `ahead_by > 0`.
Some contain obsolete experiments, but unique commits must be either intentionally
archived or verified content-equivalent before deleting the ref.

## Connector limitation

The connected GitHub actions available in this session can create/update refs but
do not expose GitHub's DELETE ref endpoint. Force-moving a branch would not be an
honest substitute for deletion, so no branch ref was destructively rewritten.

The exact safe deletion command, once run in a Git-capable environment, is:

```bash
git push origin --delete \
  research/constitutive-adequacy \
  research/convergence-certificate \
  research/discovery \
  research/fixed-forward-co-refinement \
  research/fixed-forward-deep-refinement \
  research/fixed-forward-space-transfer \
  research/fixture-overlap-heldout \
  research/gauge-structural-forensics \
  research/grid-phase-falsification \
  research/refinement-forensics \
  research/refusal-validation3 \
  research/refusal-validation4 \
  research/refusal-validation5 \
  develop/1.1 \
  feat/adaptive-material-influence-regions \
  feat/captured-adjoint-influence \
  feat/captured-world-run \
  feat/reality-loop-worldir \
  feat/release-hardening-0-90 \
  feat/surface-proxy-showcase-1-0-main \
  feat/verified-rewrite-transactions \
  feat/verified-rewrite-transactions-check
```

Before running it, verify the active branch has been pushed and CI evidence is
preserved.

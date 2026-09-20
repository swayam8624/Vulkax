# Branch Cleanup Manifest — 2026-09-20

Canonical active research branch: `research/dcs-darkfield`.

Rule: a branch is eligible for deletion only when GitHub compare reports
`ahead_by = 0` relative to `research/dcs-darkfield`. Diverged branches with any
unique commits are retained until their content is explicitly audited.

The connected GitHub action surface used in this session does not expose branch-ref
deletion. Therefore this file records the verified deletion set; stale PRs were
closed, but refs themselves remain until deleted through GitHub UI or git CLI.

## Verified fully-contained branches — eligible for deletion

These all reported `ahead_by = 0` relative to DCS:

- research/constitutive-adequacy
- research/convergence-certificate
- research/discovery
- research/fixed-forward-co-refinement
- research/fixed-forward-deep-refinement
- research/fixed-forward-space-transfer
- research/fixture-overlap-heldout
- research/gauge-structural-forensics
- research/grid-phase-falsification
- research/refinement-forensics
- research/refusal-validation3
- research/refusal-validation4
- research/refusal-validation5
- research/validation-truth-convergence
- develop/1.1
- feat/reality-loop-worldir
- feat/release-hardening-0-90
- feat/verified-rewrite-transactions
- feat/verified-rewrite-transactions-check

## Intentionally retained

- main — production/default line
- release/1.0.0 — immutable release lineage
- legacy/studio-v1-2026-08-10 — historical snapshot
- research/integration-20260920 — integration checkpoint until DCS stabilizes
- research/dcs-darkfield — active flagship research

## Diverged branches — DO NOT DELETE YET

These have unique commits and require content-level audit before removal:

- research/gauge-asset-geometry — 1 unique commit
- research/gauge-asset-mechanism — 2
- research/gauge-boundary-extrapolation — 2
- research/gauge-forward — 5
- research/gauge-marker-correspondence — 2
- research/gauge-mechanism-diagnostic — 3
- research/gauge-mode-diagnostic — 2
- research/gauge-mode-diagnostic-v2 — 19
- research/refusal-cause — 2
- research/refusal-validation5-integrated — 5
- research/refusal-validation5-integrated-v2 — 5
- staging/gauge-assets-20260920 — 1
- captured-data-preflight — 2
- feat/captured-bundle-contract — 20
- feat/captured-example-repro — 9
- feat/captured-observation-robustness — 23
- feat/captured-operator-influence — 19
- feat/interactive-viewer-mvp — 42
- feat/measured-bundle-authoring — 7
- feat/measured-deformable-benchmark — 18

## Closed superseded PRs

- #61 held-out GAUGE fixture-support falsification
- #62 fixed-forward numerical attribution
- #63 particle-grid co-refinement
- #64 grid-phase falsification
- #66 renderer integration (history already preserved in integration branch)

Active:
- #67 Dark-Field Counterfactual Spectroscopy

## Deletion command template

After manual review, the verified-contained refs can be removed with:

`git push origin --delete <branch-name>`

Do not bulk-delete the diverged set.

# Branch and PR Cleanup Manifest — 2026-09-20

Historical integration branch at the time of this audit:
`research/integration-20260920`

Current canonical stable branch after promotion: `main`

Merged DCS implementation:
`e2fa8cadbfa765367971030ca9614d9d7a063aae`

## Pull requests

Open PR count after cleanup: **0**

Closed as superseded/archival:
- #52 Vulkax 1.1 development
- #54 surface-proxy beauty renderer
- #55 WebGL viewer MVP
- #56 native macOS Metal viewer
- #57 GPU-resident Metal depth keys
- #58 native OBJ/image Gaussian authoring
- #59 executable WorldIR reality loop
- #60 native viewer research checkpoint
- #61 held-out GAUGE fixture-support falsification
- #62 fixed-forward numerical attribution
- #63 particle-grid co-refinement
- #64 grid-phase falsification
- #66 renderer integration
- #67 DCS development PR

Merged:
- #65 numerical falsification lineage
- #68 complete DCS research implementation

Historical release PRs remain closed/merged according to their original state.

## Branch deletion policy

A branch is eligible for deletion only when GitHub comparison against
`research/integration-20260920` reports `ahead_by = 0`.

The current connector does not expose branch-ref deletion. Therefore the safe-delete
set below is fully audited but refs remain until removed with GitHub UI or git CLI.

## Safe-delete set — fully contained in integration

All of the following have **0 unique commits ahead** of the integration head:

1. `develop/1.1`
2. `feat/adaptive-material-influence-regions`
3. `feat/captured-adjoint-influence`
4. `feat/captured-world-run`
5. `feat/reality-loop-worldir`
6. `feat/release-hardening-0-90`
7. `feat/surface-proxy-showcase-1-0-main`
8. `feat/verified-rewrite-transactions`
9. `feat/verified-rewrite-transactions-check`
10. `fix/video-observation-refusal-outcome`
11. `research/constitutive-adequacy`
12. `research/convergence-certificate`
13. `research/dcs-darkfield`
14. `research/discovery`
15. `research/fixed-forward-co-refinement`
16. `research/fixed-forward-deep-refinement`
17. `research/fixed-forward-space-transfer`
18. `research/fixture-overlap-heldout`
19. `research/gauge-structural-forensics`
20. `research/grid-phase-falsification`
21. `research/refinement-forensics`
22. `research/refusal-validation3`
23. `research/refusal-validation4`
24. `research/refusal-validation5`
25. `research/validation-truth-convergence`

Recommended deletion command, one branch at a time:

`git push origin --delete <branch>`

## Intentionally retained canonical/historical branches

- `main` — production/default branch
- `release/1.0.0` — immutable release lineage
- `legacy/studio-v1-2026-08-10` — historical snapshot
- `research/integration-20260920` — historical integration lineage; removable after confirming `main` is at the same/newer head

## Divergent branches with unique commits — retain pending audit

Do **not** bulk-delete these.

### Captured-world / measurement branches
- `captured-data-preflight` — 2 unique commits
- `feat/captured-bundle-contract` — 20
- `feat/captured-example-repro` — 9
- `feat/captured-observation-robustness` — 23
- `feat/captured-operator-influence` — 19
- `feat/measured-bundle-authoring` — 7
- `feat/measured-deformable-benchmark` — 18

### Viewer / renderer branches
- `feat/interactive-viewer-mvp` — 42
- `feat/native-asset-import` — 118
- `feat/native-metal-gpu-sort` — 91
- `feat/native-metal-viewer-mvp` — 70
- `feat/native-research-checkpoint` — 130
- `feat/scalable-gaussian-execution` — 29
- `feat/scale-safe-gaussian-identity` — 5
- `feat/surface-proxy-showcase-1-0` — 14

### Research branches with unique commits
- `research/dcs3` — 6
- `research/gauge-asset-geometry` — 1
- `research/gauge-asset-mechanism` — 2
- `research/gauge-boundary-extrapolation` — 2
- `research/gauge-forward` — 5
- `research/gauge-marker-correspondence` — 2
- `research/gauge-mechanism-diagnostic` — 3
- `research/gauge-mode-diagnostic` — 2
- `research/gauge-mode-diagnostic-v2` — 19
- `research/refusal-cause` — 2
- `research/refusal-validation5-integrated` — 5
- `research/refusal-validation5-integrated-v2` — 5
- `staging/gauge-assets-20260920` — 1

## Cleanup outcome

- stale open PRs: **0**
- fully contained branches audited for safe deletion: **25**
- divergent unique-work branches: retained
- canonical stable line after promotion: `main`

This manifest supersedes the earlier cleanup file written before DCS integration.

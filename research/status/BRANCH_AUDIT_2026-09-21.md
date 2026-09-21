# Branch Audit — 2026-09-21

Canonical stable branch after promotion:
`main`

Audit method:

```text
base = main
safe-delete iff branch ahead_by == 0 relative to base
```

The audit was originally computed against the integration branch immediately before promotion. Because `main` is fast-forwarded to the same head, the containment relationships remain valid; after promotion the dated integration branch itself also becomes removable.

## Executive result

| Class | Count | Action |
|---|---:|---|
| Canonical / intentionally retained | 3 | Keep |
| Fully contained / redundant after promotion | 26 | Safe to delete |
| Divergent with unique commits | 28 | Do not bulk-delete; archive/audit first |
| **Total branches** | **57** | |

Open pull requests at audit time: **0**.

### Canonical refs

Keep:

- `main` — production/default lineage
- `release/1.0.0` — release lineage
- `legacy/studio-v1-2026-08-10` — historical snapshot
- `research/integration-20260920` — historical integration lineage; redundant once `main` is promoted

For day-to-day development, `main` is the canonical active branch. The release and legacy refs are historical preservation.

## Safe-delete branches

The original 25 branches below had **zero unique commits ahead** of the pre-promotion integration head. After `main` is fast-forwarded to that head, they remain redundant, and `research/integration-20260920` becomes redundant as well.

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
26. `research/integration-20260920` — safe after promotion because `main` contains the same history

These branches add no commit that is absent from canonical `main` after promotion.

## Divergent branches

These still have commits that are not ancestors of the integration head. Their
existence does **not** mean they should stay active forever; it only means they
should not be deleted blindly.

### Captured-world / measurement lineage

| Branch | Unique commits ahead |
|---|---:|
| `captured-data-preflight` | 2 |
| `feat/captured-bundle-contract` | 20 |
| `feat/captured-example-repro` | 9 |
| `feat/captured-observation-robustness` | 23 |
| `feat/captured-operator-influence` | 19 |
| `feat/measured-bundle-authoring` | 7 |
| `feat/measured-deformable-benchmark` | 18 |

These are old implementation lineages. Their final functionality largely overlaps
the current integrated system, but the unique commits should be inspected before
being replaced by tags/deleted.

### Viewer / renderer lineage

| Branch | Unique commits ahead |
|---|---:|
| `feat/interactive-viewer-mvp` | 42 |
| `feat/native-asset-import` | 118 |
| `feat/native-metal-gpu-sort` | 91 |
| `feat/native-metal-viewer-mvp` | 70 |
| `feat/native-research-checkpoint` | 130 |
| `feat/scalable-gaussian-execution` | 29 |
| `feat/scale-safe-gaussian-identity` | 5 |
| `feat/surface-proxy-showcase-1-0` | 14 |

These are the largest archival lineages. They should not all remain ordinary active
branches. Recommended policy: inspect for unrecovered implementation/files, preserve
interesting endpoints as immutable tags or archival refs, then delete the working
branches.

### Research lineage

| Branch | Unique commits ahead |
|---|---:|
| `research/dcs3` | 6 |
| `research/gauge-asset-geometry` | 1 |
| `research/gauge-asset-mechanism` | 2 |
| `research/gauge-boundary-extrapolation` | 2 |
| `research/gauge-forward` | 5 |
| `research/gauge-marker-correspondence` | 2 |
| `research/gauge-mechanism-diagnostic` | 3 |
| `research/gauge-mode-diagnostic` | 2 |
| `research/gauge-mode-diagnostic-v2` | 19 |
| `research/refusal-cause` | 2 |
| `research/refusal-validation5-integrated` | 5 |
| `research/refusal-validation5-integrated-v2` | 5 |
| `staging/gauge-assets-20260920` | 1 |

These preserve falsification/discovery history. The scientific conclusions are
already documented in the canonical research ledger, so most can eventually become
archival tags rather than active branches after commit/file inspection.

## Recommended final branch topology

Target long-term topology:

```text
main                           canonical stable implementation
release/1.0.0                  historical release
legacy/studio-v1-2026-08-10   historical snapshot
```

Plus temporary feature/research branches only while active.

Completed experiments should be merged, tagged/archived when useful, then removed as
working branches.

## Safe cleanup helper

Use:

```bash
bash research/scripts/audit_branch_cleanup.sh
```

for a dry-run.

To delete only branches that are still proven fully contained at execution time:

```bash
bash research/scripts/audit_branch_cleanup.sh --delete
```

The script refuses to delete protected refs and refuses any branch with one or more unique commits relative to canonical `main`.

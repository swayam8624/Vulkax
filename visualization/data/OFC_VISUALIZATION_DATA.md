# Frozen OFC visualization table

`ofc_proposals_visualization_2026-09-21.csv` is a reduced visualization-only
projection of the frozen orthogonal-force/compliance proposal table.

Source provenance:

- workflow run: `35555415551`
- artifact id: `10620460094`
- artifact name: `orthogonal-force-compliance-b21513038021a22a1aa9f601fbe535fcc0a4c7fc`
- artifact SHA-256: `dcfc8de472dadd6da5fc16e2afa3194a142c10b2e73944e606c958e497b72728`
- original `proposals.csv` SHA-256: `489b4f154f33e60023b62fbd2599ed16e35ddd294b3d58950d207a7b659c0cea`
- original row count: 36

The reduced file contains only fields needed by paper visualizations. The derived
columns are deterministic:

```text
ordinary_improvement_pct = 100 * (baseline_holdout - repair_holdout) / baseline_holdout
target_change_pct        = 100 * (repair_target - baseline_target) / baseline_target
```

Positive `ordinary_improvement_pct` means the candidate looks better under the
ordinary held-out criterion. Positive `target_change_pct` means the untouched
physical target became worse. No proposal labels, z-scores, thresholds, or
scientific decisions were changed.

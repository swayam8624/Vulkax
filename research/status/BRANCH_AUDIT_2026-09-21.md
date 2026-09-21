# Branch Audit — 2026-09-21

Status: **CLEANUP COMPLETE**

Canonical stable branch:
`main`

## Final topology

| Branch | Role |
|---|---|
| `main` | canonical stable implementation and Phase I research line |
| `release/1.0.0` | historical release lineage |
| `legacy/studio-v1-2026-08-10` | historical snapshot |

Final working branch count: **3**

Open pull requests at final audit: **0**  
Open issues at final audit: **0**

## Cleanup executed

The cleanup was performed in two evidence-preserving stages.

### Fully-contained branches

Branches with:

```text
git rev-list --count origin/main..origin/<branch> == 0
```

were deleted directly because they contained no commit absent from `main`.

This removed **26 redundant working branches**, including the old
`research/integration-20260920` branch after its complete history had been promoted
to `main`.

### Divergent historical branches

The remaining **28 branches** still contained unique commits.

They were not deleted blindly.

For each branch:

1. the unique endpoint was preserved as an annotated tag;
2. the tag was pushed successfully;
3. only then was the working branch deleted.

Archive namespace:

```text
archive/2026-09-21/<old-branch-name>
```

This preserves the exact historical commit graph while keeping old feature and
experiment lineages out of the active branch list.

## Historical archive families

The archive tags preserve the former:

- captured-world / measurement lineages;
- native Metal/viewer/asset-import lineages;
- scalable Gaussian execution experiments;
- DCS3 and GAUGE diagnostic branches;
- refusal-validation integrated variants;
- staging GAUGE asset endpoint.

These are history, not active development branches.

## Policy going forward

Normal development should happen on:

```text
main
```

with temporary feature/research branches only while work is active.

Before deleting a future branch:

```bash
./research/scripts/audit_branch_cleanup.sh
```

The helper compares against `origin/main` and refuses to classify a branch as safe
when it still contains unique commits.

If a future divergent branch is worth preserving but should no longer be active,
archive its endpoint as a tag before deleting the working ref.

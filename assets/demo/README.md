# Vulkax 0.80 showcase assets

This directory contains **metadata only**. Large presentation assets are fetched into `build/demo-assets/` and are intentionally not committed to the repository.

The locked pack currently includes Poly Haven's `studio_small_03_1k` HDRI, published under CC0 1.0. It is used as a reproducible visual-showcase environment/reference. It is **not** measurement evidence, does not alter the captured physics inputs, and is not used to claim physical relighting of the stored Gaussian appearance.

Fetch and validate the pack with:

```bash
python3 scripts/fetch_demo_assets.py \
  assets/demo/showcase_assets.lock.json \
  build/demo-assets

python3 scripts/validate_demo_assets.py \
  assets/demo/showcase_assets.lock.json \
  build/demo-assets
```

The lock records the source page, direct download, license, expected byte count, SHA-256 and local destination. A mismatched download is rejected rather than silently accepted.

## Research vs showcase boundary

Vulkax keeps two concepts separate:

- **research rendering**: native Vulkax Gaussian output used as evidence;
- **showcase presentation**: deterministic camera/layout/environment presentation intended for README, portfolio and talks.

The showcase layer must never turn a rejected rewrite into a fake committed `after` result. If independent verification rejects an edit, presentation output must identify it as a proposal followed by rollback.

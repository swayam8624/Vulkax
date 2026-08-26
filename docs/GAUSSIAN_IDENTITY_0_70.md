# Vulkax 0.70 — scale-safe Gaussian identity and selection

Vulkax 0.70 removes transient Gaussian array position from the persistent world/correspondence contract. Appearance identity is now explicit, serializable, selection-safe and transaction-safe under reorder and filtering.

## Stable Gaussian identity

Each `GaussianSplat` carries a composite stable ID:

```text
GaussianId = (namespace_id:uint32, local_id:uint32)
```

Both components must be nonzero. The packed representation is 64 bits, so the explicit identity payload is 8 bytes per splat.

`GaussianIndexView` is deliberately transient: it maps stable IDs to the cloud's **current** storage indices and is rebuilt after reorder/filter operations. Duplicate or invalid IDs are rejected.

### Legacy PLY compatibility

Ordinary 3DGS PLY files that do not contain Vulkax identity properties remain loadable. They receive deterministic fallback IDs:

```text
namespace = 1
local_id  = source vertex index + 1
```

This fallback is deterministic only with respect to that source vertex order. It is **not a global-uniqueness guarantee across unrelated legacy clouds**.

Once Vulkax serializes a cloud, the PLY contains explicit:

```text
vulkax_id_namespace
vulkax_id_local
```

properties, and those IDs survive subsequent Vulkax serialization/filter/reorder operations.

## Reorder-safe correspondence and transactions

`WorldCorrespondenceGraph` now stores Gaussian IDs instead of `std::size_t` vector indices. Transaction validation, touched appearance sets, position snapshots, rollback and unaffected-region drift use the same stable IDs.

The controlled regressions prove that:

- semantic appearance correspondence survives storage reorder;
- a geometry transaction after reorder still changes the intended splats;
- a rollback after another reorder still restores the intended splats;
- unaffected-region drift compares by identity, not vector position.

Filtering is explicit rather than silent. `filterGaussianCloudByIds` preserves the requested stable IDs. If a world correspondence graph still contains bindings for splats that were removed by filtering, validation fails until `pruneMissingGaussians` explicitly removes those absent appearance bindings. Entity and physical bindings remain intact.

## Durable selection groups

`GaussianSelectionSet` stores only stable Gaussian IDs. The deterministic `vulkax_gaussian_selection 1` text format persists named groups independently of storage order.

Selection groups can be parsed, serialized, validated against a cloud and resolved to the cloud's current indices when an algorithm needs transient indexing.

## Spatial lookup

0.70 reuses the existing `GaussianHierarchy`; it does not add a second BVH. The historical index-returning AABB query remains available for immediate algorithms. `queryGaussianHierarchyAabbIds` wraps the same hierarchy and returns stable IDs in deterministic packed-ID order.

This keeps the distinction explicit:

```text
storage index = transient execution coordinate
GaussianId    = persistent identity
```

## Controlled scale evidence

The public benchmark is:

```bash
./build/vulkax_gaussian_identity_benchmark \
  build/gaussian-identity-scaling.csv \
  4096 65536 3 32

python3 scripts/validate_gaussian_identity.py \
  build/gaussian-identity-scaling.csv
```

The controlled Linux CI sweep contains 4,096, 16,384 and 65,536 synthetic splats. For every size, the benchmark constructs the stable-ID view and existing hierarchy, resolves a stable selection, validates semantic correspondence, reverses storage order, rebuilds the transient views/hierarchy and requires all four invariants to remain true:

- identity lookup after reorder;
- selection membership after reorder;
- correspondence validity after reorder;
- stable hierarchy-query membership after reorder.

The validator also requires strictly increasing splat/payload counts, nonempty bounded selection/query counts, exactly 8 bytes of explicit ID payload per splat, and finite non-negative timing fields.

Timing is recorded as evidence but **there is no speed threshold or production-performance claim**. CI proves controlled structural behavior through 65,536 synthetic splats, not production-scale throughput.

## Regression coverage

The dedicated Gaussian identity regression covers:

- deterministic fallback IDs for legacy PLY;
- explicit stable-ID PLY round trip;
- partial and duplicate explicit-ID rejection;
- durable selection serialization/parsing;
- selection resolution after reorder;
- ID-preserving filtering;
- explicit correspondence pruning after filtering;
- stable hierarchy AABB membership after reorder;
- reorder-safe transaction and rollback behavior through the world tests.

The 0.70 functional candidate passed 43 tests on Linux, macOS and Windows, with Linux additionally passing the public 4K→65K benchmark validator and all existing captured-world/Vulkan evidence gates.

## Scope and limitations

0.70 establishes persistent appearance identity inside Vulkax's single-process world model. It does **not** establish:

- globally allocated UUIDs across unrelated legacy clouds;
- a distributed identity service;
- a new entity-ID scheme (`EntityId` remains the existing world semantic ID);
- automatic semantic correspondence reconstruction after arbitrary external edits;
- production-scale selection/correspondence performance;
- a new spatial accelerator beyond the existing Gaussian hierarchy.

Measured deformable validation remains the separate, external-data-blocked 0.45 requirement.

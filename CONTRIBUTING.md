# Contributing to Vulkax

Vulkax combines a native physics editor, GPU runtime, numerical references and preserved research experiments. Changes should make those boundaries clearer rather than adding another implicit dependency between them.

## Change scope

Prefer focused commits and pull requests. A commit should communicate one reviewable idea: a numerical correction, runtime change, build-system change, test, documentation update or reproducible research result. Avoid generic messages such as `Bulk update` when the change can be named precisely.

Keep product/editor changes, GPU-runtime changes and preserved BEACON/GeoBEACON/Atlas changes separable when practical. When a cross-cutting change is unavoidable, explain the dependency in the pull-request description.

## Generated data

Do not commit complete render sequences or transient build output. Under `docs/results`, commit manifests, compact metrics, required golden assets and deliberately curated documentation stills. Publish full reproducible sequences as GitHub Actions artifacts or release assets.

Generated shader and build products should live under the build directory for new code. Legacy source-relative SPIR-V paths remain only for compatibility and should not be copied into new subsystems.

## Validation

For C++ changes, run the smallest relevant CTest labels/targets first and then the full suite before merging. Vulkan/Metal changes should include the appropriate native GPU smoke path when hardware is available. Numerical changes should add a regression against an invariant, analytical reference, convergence property or independent oracle rather than validating only that the output is finite.

Do not remove a failing assertion or relax a tolerance without documenting why the previous condition was invalid.

## Commit messages

Use an imperative subject that names the subsystem and effect, for example:

- `Fix Kerr wavelength-domain radiance integration`
- `Harden one-shot Vulkan submission errors`
- `Move generated Physics-IR shaders into the build tree`
- `Add native Metal regression coverage`

The repository history is part of the research audit trail; make it useful to someone who did not author the change.

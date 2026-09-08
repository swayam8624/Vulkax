# Native viewer next stages

This note records the staged performance path after the native Metal viewer visual-correctness checkpoint.

1. Keep the current CPU visibility/LOD selector as the deterministic reference.
2. Add a Metal compute pass that produces per-visible-splat camera depth keys and source indices.
3. Validate GPU-generated keys against the CPU reference before using them for blending order.
4. Add a GPU sorting path for retained splats, initially bounded by the viewer splat budget.
5. Keep an automatic CPU fallback when the Metal compute path is unavailable or validation fails.
6. Move visibility rejection and importance scoring onto GPU only after sorting is stable.
7. Add tile binning after correctness, not before it.

The scientific/capture state remains read-only throughout this work.

# GAUGE prospective development failure and operator redesign — 2026-09-23

Status: **development evidence only; validation/final-test trials remain unopened.**

## Frozen first prospective development result

Workflow run: `35814800013`  
Head commit: `9b7796187e05d1cdf2b3f0e56c0f7a07fb977930`  
Combined artifact: `gauge-prospective-development` (artifact id `10731495815`)

Measured worlds:
- GAUGE foam stretching and compression only;
- soft + hard foam;
- development repeats 1–4;
- 16 independently recorded worlds;
- 48 paired decisions per method: 16 SUPPORT, 16 VETO, 16 placebo UNRESOLVED.

Controlled rewrite:
- SUPPORT: E = 2x metadata baseline -> independently measured metadata E proposal;
- VETO: measured metadata E baseline -> E = 2x proposal;
- UNRESOLVED: identical baseline/proposal placebo.

No material parameter was fit to trajectory error. The primary decision threshold remained
`|score| = 2`.

### Result

| Method | Coverage | Three-way accuracy | Selective accuracy | Support recall | Veto recall | Direction sign |
|---|---:|---:|---:|---:|---:|---:|
| temporal second-difference Reality Probe | 0.375 | 0.375 | 0.1111 | 0.0625 | 0.0625 | 0.25 |
| same-cost raw 3-frame residual | 0.6667 | 1.0 | 1.0 | 1.0 | 1.0 | 1.0 |
| full-trajectory residual | 0.6667 | 1.0 | 1.0 | 1.0 | 1.0 | 1.0 |

All three methods correctly left all 16 placebo cases unresolved.

The failure is not a threshold-only failure. On multiple stretching worlds the temporal
DCS response was confidently sign-reversed. Example:

- scene `foam stretching/soft/02`;
- correct 2x -> 1x rewrite:
  - temporal DCS = **-2.7548** (wrong-direction VETO);
  - same-cost raw = **+4.3319**;
  - full residual = **+4.4035**.

The inverse rewrite produced the exact sign reversal.

## Mechanistic diagnosis

The original temporal response is

[
D_t^2 x = x(t_1) - 2x(t_m) + x(t_0).
]

It intentionally cancels response components that are approximately first-order in the
driver/progress coordinate. That was useful for the earlier retrospective GAUGE shearing
failure because the sought discrepancy lived in residual curvature/non-affine response.

For approximately monotone stretch/compression, however, a large part of the
Young's-modulus-sensitive signal is itself first-order deformation. The operator therefore
cancels part of the **target signal**, not just nuisance response. A higher annihilation
order cannot fix that conceptual mismatch.

The development result therefore falsifies the claim that one fixed temporal
lower-order-annihilating response is an appropriate Reality Probe for every physical
rewrite.

## Pre-validation redesign: selective annihilation

The revised design rule is:

> annihilate only a predeclared nuisance subspace while preserving sensitivity to the
> physical quantity being verified.

For GAUGE stretch/compression, global rigid translation/rotation is nuisance, while
strain is informative. The new probe therefore uses a deterministic minimum-spanning-tree
of the measured rest marker geometry and evaluates **relative edge-length change**:

[
epsilon_{ij}(t)
=
rac{lVert x_i(t)-x_j(t)Vert-lVert x_i(0)-x_j(0)Vert}
     {lVert x_i(0)-x_j(0)Vert}.
]

Pairwise length is exactly invariant to global translation and rotation, but it preserves
stretch/compression. Only MST edges are used to avoid inflating evidence with all
correlated marker pairs. The evidence vector is the per-edge RMS mismatch at the frozen
mid/final probe times.

This redesign is made **only on development trials 1–4**. It does not change:
- the primary `|score| = 2` decision threshold;
- support/veto/placebo truth definitions;
- solver numerics;
- measured metadata truth;
- validation trials 5–7;
- final-test trials 8–10.

The original temporal DCS remains in all future tables as a failed ablation. It is not
deleted or rewritten.

## Gate before validation

The redesigned rigid-invariant strain probe may advance to trials 5–7 only if the complete
16-world development rerun shows:

1. all placebo cases remain unresolved;
2. directional sign accuracy >= 0.80;
3. support recall >= 0.75;
4. veto recall >= 0.75;
5. no task family (stretch or compression) has directional sign accuracy below 0.70.

If the redesigned probe fails this gate, it is not promoted to validation.

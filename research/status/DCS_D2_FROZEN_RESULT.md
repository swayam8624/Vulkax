# DCS D2 Frozen Validation Result — 2026-09-20

Workflow run: `35527463327`
Job: `106122053488`

Protocol: `research/benchmarks/DCS_D2_VALIDATION_PROTOCOL.md`

## Frozen outcome

The order-2 maximin DCS configuration **failed its preregistered advancement gate**.

- fresh truth worlds: **16 / 16 executed**
- DCS-resolved worlds at frozen >=2.0 standardized separation: **0 / 16**
- coverage: **0%**
- median numerical RMS floor: **6.468865e-6 m**
- median predicted worst-case standardized DCS separation: **0.041578**
- maximum annihilation moment residual: **3.886e-16**

All deterministic moment-cancellation contracts passed. The failure is therefore not
an algebra/implementation failure. It is an **experimental observability / numerical
fidelity failure**: after lower-order response is cancelled, candidate-model
differences are much smaller than the conservative numerical uncertainty estimated
from nominal-vs-half-dt probe disagreement.

Frozen gate:

- coverage >= 50%: FAIL
- beat raw maximin: FAIL / undefined because DCS refused all worlds
- beat same-cost raw bundle: FAIL / undefined
- correct >=60% of raw mirages: FAIL / undefined
- net positive corrections: FAIL / undefined
- not carried by one family: FAIL / undefined
- annihilation moment contract <=1e-9: PASS
- overall: **FAIL**

Decision emitted by analysis:

`freeze_failure_and_do_not_tune_this_partition`

## Scientific interpretation

This kills the naive claim:

> "An order-2 dark-field contrast is automatically a usable physical discriminator."

It also validates a central DCS design requirement: a dark-field witness is not
allowed to become a physical claim merely because its raw discrepancy is small or
large. It must clear the numerical observability floor.

The current conservative D2 uncertainty estimate was frozen before labels:
for every fitted candidate, all probe responses were repeated at half timestep and
the maximum per-probe RMS disagreement was used as the numerical uncertainty floor.

The result shows that this raw-response numerical floor dominates the surviving
order-2 interaction signal.

## What is forbidden now

Do not:
- lower the 2.0 mechanism-resolution threshold on these 16 worlds;
- change probe amplitude using these labels;
- remove difficult candidate families;
- reduce the numerical variance estimate post hoc and re-score this partition;
- claim target-ranking superiority from unresolved DCS scores;
- reuse these 16 worlds as validation for the next method.

## Next falsifiable hypothesis

The next discovery phase tests a different, more physically appropriate uncertainty
object:

> Numerical uncertainty should be estimated **after applying the same annihilating
> stencil**, because truncation/discretization error can contain large common
> lower-order components that DCS itself cancels.

This yields witness-space numerical uncertainty:

[
\sigma^2_{num,\mu}
=
\max_M
\operatorname{RMS}^2(
\mathcal D_\mu[F_{M,h}]
-
\mathcal D_\mu[F_{M,h/2}]
).
]

The next selector may rank candidate stencils by worst-case model separation divided
by:
- propagated measurement variance;
- propagated repeat variance;
- **direct witness-space numerical variance**.

This change must be developed on a new discovery partition and validated on another
fresh partition.

## Additional next-step hypothesis

The five-point order-2 intervention basis may itself be too restrictive. A fresh
discovery study will test a 3x3 two-axis intervention lattice, allowing:
- richer order-2 nullspace directions;
- order-3 annihilating stencils;
- adaptive choice of the first numerically observable separating order.

If neither witness-space numerical guarding nor adaptive order produces useful
coverage on fresh discovery worlds, DCS should be substantially narrowed or killed
as the Vulkax flagship.

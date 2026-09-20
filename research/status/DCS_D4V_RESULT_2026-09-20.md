# DCS D4V Pair-Specific Repair-Veto Discovery Result

Date: 2026-09-20

Workflow run: `35529275996`

Protocol:
`research/benchmarks/DCS_D4V_REPAIR_VETO_DISCOVERY_PROTOCOL.md`

## Result

**NEGATIVE DISCOVERY — pair-specific DCS repair veto did not clear the frozen credibility threshold.**

Fresh partition:
- truth worlds: 6
- ordinary repair proposals: **36**
- deceptive proposals: **14**
- beneficial proposals: **22**

At the inherited support/veto threshold `|z| >= 2`:

| Method | Resolved coverage | Vetoes | Supports |
|---|---:|---:|---:|
| Pair-specific DCS | **0%** | 0 | 0 |
| Same-cost raw bundle | **0%** | 0 | 0 |
| Raw pair-specific probe | **0%** | 0 | 0 |
| Fisher probe | **0%** | 0 | 0 |
| Maximum-motion probe | **0%** | 0 | 0 |

The moment-annihilation contract passed, but every proposal remained unresolved.

Frozen gate:
- >=4 deceptive proposals: PASS
- >=30% DCS coverage: **FAIL**
- >=60% resolved deceptive veto recall: **FAIL / undefined**
- <=20% resolved beneficial false-veto rate: **FAIL / undefined**
- >=70% veto precision: **FAIL / undefined**
- raw bundle not strictly dominant: PASS
- moment residual <=1e-9: PASS

Decision emitted by analysis:

`d4v_not_strong_enough`

## Interpretation

This does **not** show that ordinary repair acceptance is safe. The partition contains
14 repairs that improve held-out observations while worsening the stronger unseen
target.

It shows that, under the present probe amplitudes/noise/numerics and the inherited
credibility threshold, none of the tested verification channels contains enough
resolved information to support or veto those repairs.

Do not lower the threshold on this partition. Do not reuse these six truth worlds to
tune a replacement policy.

## Consequence

DCS is not supported as:
- a universal world-ranking method;
- a fixed-order ranking method;
- an adaptive-order ranking method;
- or a pair-specific repair-veto policy under the current experimental information.

The reusable implementation remains valuable as a mechanism-isolation and
falsification laboratory. Any future scientific claim must change the physical
information channel rather than tune D2/D3/D4V.

# IRIS Pendulum Linux Replay Result — 2026-09-24

This record captures the independent Ubuntu/current-dependency replay of the frozen IRIS pendulum campaign. It is a **cross-platform reproducibility audit**, not a new scientific evaluation and not a basis for retuning the frozen method.

## Replay identity

- GitHub Actions run: `35982677687`
- Job: `107578051311`
- Frozen algorithm lock: `9e56f06145b8a56dd26c1c81e064ed9b185c77d7`
- IRIS revision: `c253822f55431ca80ef2084de4bc5e79d1a488f1`
- Final setting: `pendulum_90`

## What reproduced exactly

The Ubuntu replay completed development, validation, final-test locking, and the reserved 10-video final campaign.

The discrete final result matches the locked ledger exactly:

- 10/10 final videos pass quality control.
- Finite-amplitude period probe: 90/110 correct.
- Small-angle diagnostic baseline: 40/110 correct.
- Primary SUPPORT: 40/50 correct.
- Primary VETO: 40/50 correct.
- Primary UNRESOLVED placebo: 10/10 correct.
- Paired comparison: 50 primary-only wins, 0 baseline-only wins, 60 ties.

## Continuous scalar drift

The only failed exact-result assertion is the median period-inferred length relative error:

- locked Mac/Metal ledger: `0.16810864509359352`
- Ubuntu replay: `0.16793838106989228`
- absolute difference: `0.00017026402370123872`
- relative difference: `0.10128213430454168%`

The GitHub Actions job therefore ends in failure at the exact scalar-equality assertion even though the decision-level result reproduces.

## Disposition

R35 is closed at the **decision/result level**. Exact cross-platform floating/numerical identity is not claimed. The original locked result remains canonical, and the replay is not allowed to change the tracker, candidate schedule, threshold, final-test population, or manuscript headline decisions.

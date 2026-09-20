# Vulkax DCS Final Benchmark Summary — 2026-09-20

Status: **implementation complete; scientific flagship claim not established**

This file is the canonical compact result ledger for the DCS research program.
Detailed protocols and per-stage result files remain authoritative for each frozen
partition.

## 1. Result hierarchy

| Stage | Purpose | Result | Scientific status |
|---|---|---|---|
| D0 | mathematical correctness | PASS | implementation evidence |
| D1 | constructed deceptive-repair positive control | PASS | implementation evidence only |
| D2a | fixed-witness solver-native discovery | MIXED / NEGATIVE | discovery |
| D2b | maximin active-selection discovery | NEGATIVE | discovery |
| D2 frozen | fresh order-2 validation | NEGATIVE | frozen validation |
| D3 | witness-space + adaptive order | NEGATIVE | discovery |
| D4V | pair-specific repair veto | NEGATIVE | discovery |
| GAUGE | measured retrospective metric mirage | MIXED / MECHANISM-SPECIFIC | retrospective only |
| D5 | fresh measured confirmation | NOT EXECUTED | required for future positive claim |
| D6 | spatial Gaussian/particle localization | IMPLEMENTED | systems capability, not validation |

## 2. D0 — mathematical regression

Result: **PASS**

Validated:
- Möbius counterfactual cumulants;
- arbitrary-order annihilation contracts;
- order-2/order-3 polynomial controls;
- mechanism order/contact helpers;
- uncertainty propagation;
- multi-seed nullspace synthesis;
- witness-space numerical uncertainty;
- spatial witness localization.

The final implementation-complete workflow passed every C++ regression and reusable
Python self-test.

## 3. D1 — constructed deceptive-repair positive control

Result: **PASS**

64 constructed cases:

- ordinary metric accepted repair: **64 / 64**
- deceptive-repair ground truth: **64 / 64**
- DCS accepted deceptive repair: **0 / 64**
- median ordinary observation improvement: **60.88%**
- median dark-field witness degradation ratio: **9.375x**

Interpretation: implementation can detect the pattern it was designed to detect
when that pattern is deliberately constructed. This is not scientific validation.

## 4. D2a — solver-native fixed-witness discovery

Four fresh MPM truth worlds x four candidate families.

Pairwise stronger-target ranking agreement:

- held-out trajectory RMSE: **62.5%**
- fixed hand-designed order-2 DCS witness: **62.5%**

Ordinary-metric mirage pairs: **9**
DCS corrected those mirages: **9 / 9**

Because global agreement did not improve, the witness also introduced a comparable
number of new ordering mistakes.

Decision:
> kill the hypothesis that one hand-picked mixed second-order witness is a generally
> superior physical-world ranking statistic.

## 5. D2b — automatic maximin active-selection discovery

Target-ranking agreement:

| Selector | Agreement |
|---|---:|
| raw maximin single intervention | **95.83%** |
| Fisher/Jacobian sensitivity | **87.50%** |
| same-cost raw bundle | **75.00%** |
| maximum motion | **66.67%** |
| DCS maximin order-2 stencil | **62.50%** |
| random | **45.83%** |

Decision:
> automatic order-2 dark-field acquisition did not beat matched-cost raw/Fisher
> baselines.

## 6. D2 frozen fresh validation

Fresh truth worlds: **16**
Candidates/world: **4**

Frozen observability reference:
- minimum standardized separation: **2.0**

Result:

- resolved worlds: **0 / 16**
- usable coverage: **0%**
- median predicted standardized separation: **0.041578**
- median raw nominal-vs-half-dt numerical RMS floor: **6.469e-6 m**
- maximum stencil moment residual: **3.886e-16**

Decision:
`freeze_failure_and_do_not_tune_this_partition`

The 16 worlds remain permanently unavailable for method tuning.

## 7. D3 — direct witness-space uncertainty + adaptive order

Fresh truth worlds: **6**

Direct witness-space refinement greatly reduced the measured numerical floor:

- median order-2 numerical witness RMS: **8.435e-7 m**
- median order-3 numerical witness RMS: **2.153e-7 m**

But candidate separation remained unresolved:

- adaptive resolved worlds: **0 / 6**
- order-2 resolved worlds: **0 / 6**
- order-3 resolved worlds: **0 / 6**

Adaptive order:
- order 2: **6 / 6**
- order 3: **0 / 6**

Median predicted separation:
- adaptive/order 2: **0.0588583**
- order 3: **0.0157173**

Target-ranking agreement:

| Method | Agreement |
|---|---:|
| Fisher sensitivity | **61.11%** |
| same-cost raw bundle | **58.33%** |
| maximum motion | **58.33%** |
| raw pair-aware | **55.56%** |
| adaptive DCS | **52.78%** |
| fixed order-2 DCS | **52.78%** |
| random | **52.78%** |
| fixed order-3 DCS | **47.22%** |

Adaptive DCS corrected **37.5%** of raw-pair-aware mirages but introduced **7** new
errors on pairs the raw selector ranked correctly.

Decision:
`d3_not_yet_strong_enough_for_validation`

## 8. D4V — pair-specific deceptive-repair veto

Fresh truth worlds: **6**
Natural ordinary repair proposals: **36**

Labels revealed only after proposal construction:

- deceptive repairs: **14**
- beneficial repairs: **22**

At inherited `|z| >= 2` support/veto threshold:

| Method | Resolved coverage | Vetoes | Supports |
|---|---:|---:|---:|
| pair-specific DCS | **0%** | 0 | 0 |
| same-cost raw bundle | **0%** | 0 | 0 |
| raw pair-specific probe | **0%** | 0 | 0 |
| Fisher probe | **0%** | 0 | 0 |
| maximum-motion probe | **0%** | 0 | 0 |

Moment-annihilation contract: **PASS**

Decision:
`d4v_not_strong_enough`

Interpretation:
14 held-out-improving repairs were genuinely deceptive, but none of the tested
verification channels contained enough resolved signal under the frozen credibility
standard to support or veto them.

## 9. GAUGE measured retrospective

This case was known before DCS was developed and is therefore **retrospective only**.

Across 10 held-out even repeats:

- ordinary face+marker metrics prefer finite overlap repair: **10 / 10**
- marker dark-field residual prefers original endpoint model: **0 / 10**
- longitudinal dark-field residual prefers original endpoint model: **9 / 10**

Median marker dark-field error:
- endpoint: **2.4280 mm**
- overlap: **1.7092 mm**

Median longitudinal dark-field error:
- endpoint: **0.0031313**
- overlap: **0.0055842**

Driver-path checks:
- median selected mid-progress: **0.493356**
- maximum orthogonal driver fraction: **0.012798**

Interpretation:
the observation-space channel and the mechanism-specific longitudinal channel give
opposite conclusions. The implementation must preserve that contradiction rather
than average it into one favorable scalar.

## 10. What is supported

Supported by executed evidence:

1. ordinary held-out improvement can coexist with worse unseen physical behavior;
2. wrong model families can compensate through other parameters;
3. numerical error can be absorbed into inferred physical parameters;
4. one-shot scalar credibility rules are unsafe or collapse to zero useful coverage;
5. mechanism-specific channels can disagree strongly with aggregate observational
   metrics;
6. a lower-order-annihilating response representation is mathematically valid and
   computationally implemented;
7. DCS is useful as a mechanism-isolation/falsification laboratory.

## 11. What is not supported

The executed evidence does **not** support claims that:

- DCS is a superior universal candidate-world ranker;
- fixed order-2 DCS is sufficient;
- adaptive k=2/3 DCS is sufficient;
- pair-specific DCS provides useful prospective support/veto coverage under the
  tested information regime;
- DCS has passed fresh measured-domain confirmation;
- a physically correct rewrite certificate has been achieved.

## 12. Current publication posture

The engineering program is complete, but no positive flagship algorithm has cleared
fresh prospective validation.

A future positive paper path requires a new physical-information channel with a
fresh discovery partition followed by an untouched confirmatory regime.

Negative results must remain visible in any manuscript that discusses this sequence.

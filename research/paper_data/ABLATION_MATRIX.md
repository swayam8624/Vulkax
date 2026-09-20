# Executed Ablation / Comparison Matrix

This file is structured source material for later Results writing. It contains no
new claim beyond the frozen ledgers.

## D2b active-selection comparison

| Method | Target-ranking agreement |
|---|---:|
| raw maximin | 95.83% |
| Fisher/Jacobian | 87.50% |
| same-cost raw bundle | 75.00% |
| maximum motion | 66.67% |
| DCS order-2 maximin | 62.50% |
| random | 45.83% |

Interpretation status: **discovery only**. DCS did not beat matched baselines.

## D2 fresh frozen validation

| Quantity | Result |
|---|---:|
| truth worlds | 16 |
| resolved worlds | 0 |
| coverage | 0% |
| median standardized separation | 0.041578 |
| frozen observability reference | 2.0 |
| median nominal-vs-half-dt numerical RMS | 6.469e-6 m |
| maximum moment residual | 3.886e-16 |

Interpretation status: **frozen negative validation**.

## D3 uncertainty/order ablation

| Method | Target-ranking agreement |
|---|---:|
| Fisher/Jacobian | 61.11% |
| same-cost raw bundle | 58.33% |
| maximum motion | 58.33% |
| raw pair-aware | 55.56% |
| adaptive DCS | 52.78% |
| fixed k=2 DCS | 52.78% |
| random | 52.78% |
| fixed k=3 DCS | 47.22% |

Mechanism-resolution ablation:

| Quantity | D2/raw floor | D3 k=2 witness | D3 k=3 witness |
|---|---:|---:|---:|
| median numerical RMS (m) | 6.469e-6 | 8.435e-7 | 2.153e-7 |
| median standardized separation | 0.041578 | 0.0588583 | 0.0157173 |
| resolved worlds | 0/16 | 0/6 | 0/6 |

Key implementation observation: measuring numerical uncertainty in witness space
reduced the numerical floor, but did not make the candidate worlds resolvable.

## D4V repair-veto comparison

Population:
- 36 ordinary held-out-improving proposals;
- 14 deceptive under the stronger hidden target;
- 22 beneficial.

At the inherited |z| >= 2 support/veto reference:

| Method | Resolved coverage |
|---|---:|
| pair-specific DCS | 0% |
| same-cost raw bundle | 0% |
| raw pair-specific probe | 0% |
| Fisher probe | 0% |
| maximum-motion probe | 0% |

Interpretation: the bottleneck under this regime is not unique to DCS; none of the
tested verification channels provided enough resolved signal.

## GAUGE channel ablation

Across 10 held-out even repeats:

| Channel | Endpoint preferred |
|---|---:|
| ordinary face+marker aggregate | 0/10 (overlap 10/10) |
| marker dark-field | 0/10 |
| longitudinal dark-field | 9/10 |

Median dark-field errors:

| Channel | endpoint | overlap |
|---|---:|---:|
| marker (m) | 0.0024280 | 0.0017092 |
| longitudinal | 0.0031313 | 0.0055842 |

Evidence class: **retrospective measured**.

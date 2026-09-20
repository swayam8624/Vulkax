# DCS Information Frontier — Post-Hoc Diagnostic

Date: 2026-09-20

Evidence class: **post-hoc diagnostic**.

This analysis does not change any preregistered D2/D3/D4V decision and does not turn
the GAUGE retrospective into prospective confirmation.

## D4V distance from resolvability

D4V generated 36 ordinary held-out-improving repair proposals:
- 14 deceptive;
- 22 beneficial.

All verification methods had 0 resolved proposals at the inherited `|z| >= 2`
reference.

The post-hoc information-frontier calculation quantifies how far the signals were
from that reference.

| Method | median |z| | max |z| | median signal amplification needed to reach 2 | best-case amplification needed |
|---|---:|---:|---:|---:|
| DCS | 0.05024 | 0.17654 | **39.83x** | **11.33x** |
| raw bundle | 0.04991 | 0.40615 | 40.10x | 4.92x |
| raw point | 0.14707 | 0.63142 | **13.60x** | **3.17x** |
| Fisher | 0.06637 | 0.63142 | 30.15x | 3.17x |
| max motion | 0.05773 | 0.57505 | 34.64x | 3.48x |

For DCS, the median uncertainty standard deviation would have to fall to roughly
**2.51%** of its observed value if the signal stayed fixed, equivalent to a variance
fraction of about **0.000631**.

This supports the final engineering diagnosis that the tested regime is
**information-limited**, not merely slightly mis-thresholded.

### Below-threshold sign direction

Because no proposal is resolved, sign direction is diagnostic only.

| Method | overall sign accuracy | deceptive repairs with negative progress | beneficial repairs with positive progress |
|---|---:|---:|---:|
| DCS | 58.33% | 78.57% | 45.45% |
| raw bundle | 69.44% | 35.71% | 90.91% |
| raw point | **77.78%** | 64.29% | 86.36% |
| Fisher | 63.89% | 28.57% | 86.36% |
| max motion | 58.33% | 28.57% | 77.27% |

These values must not be reported as support/veto accuracy because the frozen
credibility threshold was not met.

## GAUGE paired retrospective diagnostics

Differences are defined as endpoint minus overlap, so negative values favor endpoint.

| Channel | endpoint better | overlap better | median difference | exact two-sided sign-test p |
|---|---:|---:|---:|---:|
| face NRMSE | 0/10 | 10/10 | +0.07838 | 0.001953 |
| marker RMSE | 0/10 | 10/10 | +0.0001308 m | 0.001953 |
| marker dark-field | 0/10 | 10/10 | +0.0007518 m | 0.001953 |
| longitudinal dark-field | **9/10** | 1/10 | **-0.002666** | **0.02148** |

The exact sign tests are exploratory retrospective diagnostics, not a preregistered
confirmatory analysis.

## Research implication

The strongest quantitative conclusion is not that DCS nearly passed. It did not.

The final D4V signals are far below the frozen credibility reference. A future
positive mechanism therefore needs a **qualitatively higher-information physical
measurement/intervention channel** rather than a small algebraic or threshold
adjustment.

# DCS D3 Witness-Space / Adaptive-Order Discovery Result

Date: 2026-09-20

Protocol:
`research/benchmarks/DCS_D3_DISCOVERY_PROTOCOL.md`

Workflow:
`Vulkax DCS D3 witness-space discovery`, run `35528791346`.

## Status

**NEGATIVE DISCOVERY — DO NOT PROMOTE TO NEW VALIDATION**

The implementation built successfully and the DCS mathematical regression passed
after fixing a nullspace-seeding bug. The scientific result remained negative.

## Fresh discovery partition

- truth worlds: **6**
- candidates/world: **4**
- intervention basis: **3x3 shear/axial lattice**
- candidate witness orders: **2 and 3**
- numerical uncertainty: direct nominal-vs-half-dt **witness-space** RMS
- order selection: maximum predicted worst-case standardized separation
- hidden stronger target was never used for selector construction

## Main result

Adaptive order selection chose:

- order 2: **6 / 6**
- order 3: **0 / 6**

Observability at the existing standardized-separation reference >=2:

- adaptive: **0 / 6**
- fixed order 2: **0 / 6**
- fixed order 3: **0 / 6**

Median predicted worst-case standardized separation:

- adaptive/order 2: **0.0588583**
- order 3: **0.0157173**

Witness-space numerical RMS was indeed much smaller than D2's raw-response
uncertainty floor:

- D2 median raw numerical RMS: **6.469e-6 m**
- D3 median order-2 witness numerical RMS: **8.435e-7 m**
- D3 median order-3 witness numerical RMS: **2.153e-7 m**

So the witness-space uncertainty hypothesis was directionally correct about numerical
cancellation, but it was **not enough** to make the candidate worlds experimentally
separable.

## Target-ranking agreement

Across candidate pairs:

| Method | Agreement |
|---|---:|
| Fisher sensitivity | **61.11%** |
| same-cost raw bundle | **58.33%** |
| maximum motion | **58.33%** |
| raw pair-aware single probe | **55.56%** |
| adaptive DCS | **52.78%** |
| fixed order 2 DCS | **52.78%** |
| random | **52.78%** |
| fixed order 3 DCS | **47.22%** |

Adaptive DCS:
- corrected **37.5%** of raw-pair-aware mirages;
- introduced **7** errors on raw-correct pairs;
- therefore did not provide positive net ranking value.

Decision emitted by analysis:

`d3_not_yet_strong_enough_for_validation`

## What D3 falsifies

Do not promote any of the following as the flagship algorithm:

- fixed order-2 dark-field world ranking;
- fixed order-3 dark-field world ranking;
- adaptive choice between order 2 and 3 by worst-case standardized separation;
- the claim that direct witness-space numerical uncertainty alone makes DCS a
  competitive universal model-ranking method.

## What remains scientifically alive

The original motivating task was **repair falsification**, not arbitrary total
ordering of every model family.

DCS may still be useful as a second-stage test:

1. an ordinary metric proposes a repair because observation error improves;
2. DCS asks whether an independent mechanism-selective contrast contradicts that
   apparent improvement;
3. DCS vetoes only when the contradiction is sufficiently resolved.

This is narrower than universal ranking and is closer to the real GAUGE failure that
motivated the project.

Any such reformulation must:
- be declared before opening a new partition;
- report false vetoes as well as deceptive-repair catches;
- preserve the frozen D2 and D3 failures;
- not reuse D2/D3 worlds to tune its policy.

## Hard stop

Do **not** keep increasing response order simply because k=2 and k=3 failed.
Higher-order signal will generally become less observable.

The next experiment must change the scientific decision problem, not merely add k=4.

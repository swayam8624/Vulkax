# GAUGE prospective development gate — redesigned probe result

Date: 2026-09-23  
Status: **development passed; not validation or confirmatory evidence**

## Frozen evidence

The first prospective development run falsified the fixed temporal second-difference
probe for Young's-modulus rewrites. That failure is preserved in
`GAUGE_DEVELOPMENT_FAILURE_AND_REDESIGN_2026-09-23.md`.

After that failure, and before opening validation trials, the probe was redesigned to
annihilate only rigid translation/rotation nuisance while preserving strain.

Redesigned workflow run: `35815315853`  
Aggregate job: `107035931301`  
Aggregate artifact: `gauge-prospective-development`  
Artifact id: `10731636214`  
Artifact SHA-256: `da064e78f69d4a9427cac6d910bd6b522dcab1d89490c154ba23e4e2ec8d7d2a`

Measured development worlds:
- GAUGE foam stretching + compression only;
- soft + hard materials;
- repeats 1–4 only;
- 16 measured worlds;
- 16 SUPPORT + 16 VETO + 16 placebo UNRESOLVED decisions per method.

No parameter was fit to marker trajectory error. The primary decision threshold remained
`|score| = 2`.

## Aggregate result

| Method | Coverage | 3-way accuracy | Support recall | Veto recall | False assertion on unresolved | Direction sign | Median |score| |
|---|---:|---:|---:|---:|---:|---:|---:|
| legacy temporal DCS | 0.3750 | 0.3750 | 0.0625 | 0.0625 | 0.0000 | 0.2500 | 1.1857 |
| **rigid-invariant strain Reality Probe** | **0.6667** | **1.0000** | **1.0000** | **1.0000** | **0.0000** | **1.0000** | **3.7965** |
| same-cost raw 3-frame residual | 0.6667 | 1.0000 | 1.0000 | 1.0000 | 0.0000 | 1.0000 | 4.3294 |
| full-trajectory residual | 0.6667 | 1.0000 | 1.0000 | 1.0000 | 0.0000 | 1.0000 | 4.4485 |

The redesigned method's bootstrap interval for strict three-way accuracy is
`[1.0, 1.0]` on this development set.

## Predeclared gate evaluation

The gate was frozen before this rerun:

1. all placebo cases unresolved — **PASS (16/16)**;
2. directional sign accuracy >= 0.80 — **PASS (1.00)**;
3. support recall >= 0.75 — **PASS (1.00)**;
4. veto recall >= 0.75 — **PASS (1.00)**;
5. no task family directional sign below 0.70 — **PASS** in the completed
   stretching/compression development shards.

Therefore the rigid-invariant strain probe is allowed to proceed to further
**development stress testing**. This does not yet authorize validation trials 5–7.

## What the result establishes

It establishes only that, for the deliberately strong `2x E <-> measured E` controlled
rewrite on the 16 development worlds, a nuisance-selective strain response is compatible
with the correct support/veto direction and abstains on exact placebos.

It does **not** establish:
- a practical detection limit;
- superiority over raw residuals on clean aligned data;
- robustness to sensor-frame nuisance;
- robustness to noise/missing observations;
- validation generalization;
- final-test performance.

Those questions remain prospective.

## Next development gates

Before validation data are opened:

1. keep the same `|score|=2` threshold and run E perturbations of
   2.5%, 5%, 10%, and 20%;
2. estimate the detection boundary without retuning the threshold;
3. inject predeclared global rigid verification-channel nuisance and compare against
   raw and nuisance-corrected baselines;
4. run the selected operator with definitive numerical resolution or a documented
   numerical-sensitivity check;
5. only then freeze the validation protocol and open trials 5–7.

Validation trials 5–7 and final-test trials 8–10 remain unopened at this stage.

# GAUGE affine/non-affine repair protocol — frozen before released-asset result

## Question

The frozen historical GAUGE prediction has two separable error scales:

1. **global affine response** — best-fit 3D deformation gradient and translation;
2. **local non-affine response** — marker/face deformation remaining after each trajectory's own best affine map is removed.

On the historical baseline, the global affine field is substantially closer to the measured motion than the local residual: median normalized deformation-gradient error is roughly 10–12%, while predicted non-affine face-area response is roughly 2.4–2.6x the measured level.

The next test asks what the released-asset geometry actually repairs.

## Candidate

`released_asset_aspect` changes only the body-support geometry justified from released GAUGE information:

- aspect 1:1:4 from the released foam asset;
- volume from measured mass/density;
- therefore 50 x 50 x 200 mm physical proxy;
- same measured material metadata;
- same APIC transfer;
- same constitutive law;
- same driver trajectory;
- same prescribed-boundary mechanism;
- no trajectory fitting.

## Frozen comparison dimensions

For each material, compare candidate versus historical baseline on:

### Global affine field

- median normalized Frobenius error in deformation gradient;
- mean normalized Frobenius error;
- absolute final determinant error, `|det(F_pred)-det(F_measured)|`.

### Local/non-affine field

- distance of predicted/measured non-affine face-RMS ratio from 1;
- distance of predicted/measured non-affine marker-RMS ratio from 1;
- mean vector RMSE of non-affine face residuals;
- mean vector RMSE of non-affine marker residuals.

### Ordinary trajectory guard

The structural-forensics dual-metric guard remains authoritative:

- mean benchmark-native face-area NRMSE must not improve by worsening mean marker-position RMSE;
- a mechanism-specific gain cannot override an ordinary dual-metric regression.

## Frozen interpretation

Classify the candidate as one of:

- `macro_and_local_repair`: global F error and both non-affine amplitude-ratio errors improve for both materials, with ordinary dual-metric improvement;
- `macro_only_repair`: global F error improves for both materials, local ratios do not consistently improve;
- `local_only_repair`: local non-affine amplitude-ratio errors improve for both materials, global F does not consistently improve;
- `partial_or_mixed`: improvements are material- or metric-specific;
- `no_mechanistic_repair`: neither scale improves consistently.

A metric is considered improved only by strict numerical decrease; no tolerance is tuned after seeing the candidate.

## Scientific guard

Even `macro_and_local_repair` would **not** establish correct fixture mechanics or material recovery. It would only support the narrower statement that a provenance-backed body-support correction repairs specific observed failure modes.

Inverse material fitting remains locked until a fresh no-fit adequacy gate beats the analytic null on held-out measured data.

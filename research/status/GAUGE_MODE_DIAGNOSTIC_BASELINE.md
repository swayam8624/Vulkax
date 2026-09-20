# GAUGE baseline deformation-mode diagnostic — 2026-09-20

## Provenance

This is a post-hoc diagnostic of an already frozen no-fit measured-data run. No material parameter, geometry parameter, boundary parameter, transfer parameter, or timestep is fitted here.

- source workflow run: `35514461226`
- source head: `4637e7e22f6f2532aea2ff8bc7e4583f23a07c9a`
- source artifact: `10606386956`
- source artifact digest: `sha256:53633f8da3b2e00582aab03c79ca51395bf381f40d08c0711e4a46fc1b0cf157`
- source model: historical metadata-only APIC + compressible Neo-Hookean GAUGE shearing forward test
- fit performed: **false**

## Diagnostic-harness correction

The first mode-decomposition implementation assumed that every tracked foam marker belonged to one 2xN surface strip. The frozen selected trajectories contain **28 tracked markers**, while the released GAUGE face topology used by the benchmark-native area observable references marker indices **0–13** only.

The diagnostic was corrected before promotion:

1. preserve the repository's existing face-index convention: faces index the sorted marker array;
2. derive the topology-supported prefix from `max(face_index)+1`;
3. require the resulting 14-marker subset to match the exact released 2x7 triangulation;
4. reject rather than invent a decomposition if that topology check fails.

This harness failure is retained because silently decomposing all 28 markers would have produced invalid science.

## Corrected baseline result

Normalized RMSE is relative to the measured peak absolute magnitude of that mode.

| mode | soft NRMSE | hard NRMSE | soft predicted/measured peak | hard predicted/measured peak |
|---|---:|---:|---:|---:|
| face-area relative RMS | 0.9418 | 1.0794 | 2.858x | 2.970x |
| face-area relative mean | 0.6417 | 0.8403 | 2.292x | 2.634x |
| longitudinal strain mean | 0.6286 | 0.6233 | 0.191x | 0.170x |
| shear-cosine-delta RMS | 0.6066 | 0.6417 | 1.998x | 2.090x |
| transverse strain RMS | 0.2653 | 0.2689 | 1.481x | 1.494x |
| longitudinal strain RMS | 0.1792 | 0.2020 | 0.688x | 0.648x |
| transverse strain mean | 0.1301 | 0.1330 | 1.255x | 1.275x |
| shear-cosine-delta mean | 0.0995 | 0.1518 | 1.127x | 1.277x |

## Wrong-sign longitudinal coupling

The clearest mechanism-level failure is not simply “insufficient shear.”

### Soft foam

- measured final longitudinal mean strain: `-0.0293077`
- predicted final longitudinal mean strain: `+0.0059894`
- temporal correlation: `-0.9836`

### Hard foam

- measured final longitudinal mean strain: `-0.0251814`
- predicted final longitudinal mean strain: `+0.0042875`
- temporal correlation: `-0.9910`

The measured surface strip contracts on average along its longitudinal direction while the current Vulkax world predicts slight extension. This sign reversal appears in both frozen materials.

At the same time, the average transverse and shear signals are temporally well tracked (roughly 0.99 correlation), while their RMS/spatial-heterogeneity components are too large. The model therefore appears to capture much of the gross shear timing while converting that motion into the wrong normal/longitudinal coupling and excessive local deformation heterogeneity.

## Interpretation guard

This result does **not** identify the cause.

Still-live explanations include:

- incomplete body geometry / marker-to-volume support;
- fixture/contact compliance or slip absent from the prescribed-boundary approximation;
- missing foam-specific constitutive response under finite shear;
- correspondence error between surface observations and the volumetric proxy;
- interactions among the above.

The repository already found that changing among the tested Neo-Hookean/StVK laws alone does not repair the real-data gate, so no single constitutive-family story is promoted.

Shear-induced normal response / Poynting behavior in elastomeric foams is established mechanics, not a Vulkax novelty claim. Existing work has explicitly shown that models fitted to shear response can still predict shear-induced normal response poorly. The research opportunity, if any, is in **automatically diagnosing which physical assumption makes a requested captured-world counterfactual unsupported, and acquiring/using the missing evidence**, not in rediscovering the Poynting effect.

## Next falsification

Compare this exact mode signature under the newly added released-GAUGE-asset geometry (1:1:4 aspect, absolute volume from measured mass/density) without changing material parameters.

A meaningful geometry repair must simultaneously:

- reduce face-area and marker-trajectory error;
- move longitudinal mean strain toward the measured negative sign for both materials;
- reduce shear/area RMS over-amplification;
- preserve or improve the already strong timing of average shear/transverse response.

If it improves only one scalar benchmark metric while worsening these mechanism-level checks, reject it.

Inverse material fitting remains locked.


## Global-affine versus local-non-affine decomposition

A second post-hoc diagnostic was run on the same immutable workflow artifact and same frozen baseline prediction. Each measured and predicted frame is independently decomposed into its best 3D affine map from the initial 28-marker cloud plus the remaining local residual.

This decomposition is kinematic only; no physical parameter is fitted.

| quantity | soft | hard |
|---|---:|---:|
| median normalized global deformation-gradient error | **0.121431** | **0.108019** |
| mean normalized global deformation-gradient error | 0.150657 | 0.236923 |
| final measured det(F) | 0.985184 | 0.996548 |
| final predicted det(F) | 1.018259 | 1.012583 |
| measured mean non-affine marker RMS | 0.7133 mm | 0.6815 mm |
| predicted mean non-affine marker RMS | 1.1153 mm | 1.0341 mm |
| predicted / measured non-affine marker amplitude | **1.5635x** | **1.5175x** |
| measured mean non-affine face-area RMS | 0.018750 | 0.015691 |
| predicted mean non-affine face-area RMS | 0.044469 | 0.040241 |
| predicted / measured non-affine face-area amplitude | **2.3716x** | **2.5646x** |
| mean non-affine face residual-vector RMSE | 0.031100 | 0.031968 |

The important separation is therefore:

- the gross affine deformation is imperfect but comparatively close;
- the local/non-affine response is much too strong, especially in face-area deformation;
- this excess local response appears in both materials.

This does **not** identify whether the local excess comes from body geometry, fixture/contact representation, Gaussian-to-volume correspondence, transfer numerics, constitutive inadequacy, or an interaction among them. It does make those hypotheses testable at the correct spatial scale.

The released-asset geometry is pre-registered in `GAUGE_AFFINE_NONAFFINE_PROTOCOL.md` to be classified as macro repair, local repair, both, mixed, or neither.

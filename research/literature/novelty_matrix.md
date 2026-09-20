# Initial novelty matrix — 2026-09-20

This is a threat map, not a novelty claim. Full-text review and citation expansion are still required.

| Candidate Vulkax claim | Collision level | Closest seed prior art | Current interpretation |
|---|---|---|---|
| Gaussian + MPM physical dynamics | **Very high** | PhysGaussian, PhysFlow, i-PhysGaussian | Kill as flagship novelty. |
| Infer physical parameters from multi-view visual observations | **Very high** | PAC-NeRF, EMPM | Kill as standalone claim. |
| Infer geometry + appearance + physics from monocular video | **Very high** | MonoPhysics | Kill as standalone claim. |
| Gaussian-based physical-property prediction | **High** | PUGS | Not sufficient. |
| Gaussian↔generic physics-engine bridging | **High** | GASP | Representation bridge alone is weak. |
| Online sensory update of deformable physical model | **High** | EMPM | Need a stronger trust/refusal mechanism. |
| Local identifiability diagnostics inside a persistent visual world | **Medium** | mechanics identifiability literature; MonoPhysics is a direct context threat | Potential only if coupled to refusal, extra-measurement recommendation, or counterfactual reliability. |
| Active physical intervention chosen to disambiguate competing captured-world models | **Medium** | optimal experiment design literature | Potential gap in graphics/captured-world setting; generic Fisher/Jacobian OED is not novel. |
| Held-out replay explicitly separated from unseen-intervention transfer | **Medium/unknown** | system ID literature; deformable inverse methods | Promising evaluation contribution; must survey direct precedents. |
| Counterfactual trust radius for physically editable captured worlds | **Medium/unknown** | trust-region / local-linearization literature | Potential if tied to spatial edits, event boundaries and calibrated failure prediction. |
| Verification certificate predicting counterfactual correctness | **Low-to-medium direct collision found so far** | model-error/UQ literature broadly | High-priority search target; do not claim novelty yet. |
| Trustworthy refusal: classify why a physical edit cannot be certified | **Low-to-medium direct collision found so far** | identifiability/UQ/model-error literature | Very promising Vulkax identity if quantitatively evaluated. |
| Numerical-error-aware rewrite certification | **Low-to-medium direct collision found so far** | numerical verification/UQ literature | Promising if effect-vs-discretization error becomes an operational certificate gate. |
| Event horizon for derivative validity across contact/topology regime changes | **Unknown** | differentiable contact/discontinuity literature | High-risk/high-upside; needs dedicated survey. |
| Many-world GPU batching for counterfactual verification | **Systems novelty unknown** | differentiable/GPU simulation literature | Keep as enabling systems track, not flagship until measured. |

## Current search conclusion

The easy story is gone: Vulkax cannot win by saying “we simulate Gaussian splats with physics” or “we infer a material parameter from video.” The research must move one level up: **when is the inferred physical world actually knowable, when does it generalize to interventions, what additional experiment resolves ambiguity, and when should the system refuse to commit a rewrite?**

## 2026-09-20 threat expansion after first solver experiment

- **Generic identifiability from video is no longer a plausible flagship**: ICML 2026 *Physics from Video* gives explicit structural-identifiability conditions.
- **Generic extrapolation evaluation is crowded**: IRIS explicitly includes extrapolation and identifiability; MPMWorlds studies physical-dynamics extrapolation.
- **Multi-material Gaussian physical inference is crowded**: CVPR 2026 M-PhyGs estimates material segmentation, Young's modulus and density from real interaction video.
- **Sensitivity-guided inverse optimization is crowded**: ProJo4D uses parameter sensitivity to structure progressive joint optimization.
- **Measurement-grounded physical-fidelity diagnosis is now crowded**: GAUGE directly evaluates real physics engines/world models and diagnoses mechanism-specific failures across textiles and volumetric deformables.
- **Counterfactual reasoning itself is old**: VRDP and counterfactual-physics benchmarks predate Vulkax.

### Current narrower target

The current candidate contribution is therefore not any single word—identifiability, uncertainty, counterfactual, experiment design, physical fidelity, or refusal. The target must be an operational mechanism whose **accept/reject decision for a requested physical rewrite is empirically calibrated against unseen interventions while accounting for model inadequacy and numerical error, and can turn a refusal into a concrete next-evidence request**.

This target remains unproven and may still collide with work not yet reviewed.


## Certificate/refusal threat update

The one-shot 108-case experiment produced a useful negative result: generic evidence aggregation is not automatically safer than a single task-relevant held-out metric. This aligns with task-dependent UQ literature.

Older simulation V&V work (PCMM and related credibility frameworks) already combines representation fidelity, physics/material fidelity, code verification, solution verification, validation, UQ and sensitivity for intended-use credibility. Therefore Vulkax cannot claim novelty for "multi-evidence simulation credibility."

A 2026 Digital Twin Counterfactual Framework also directly threatens generic claims around hierarchical validation of counterfactual digital-twin outputs.

The remaining candidate gap is narrower: an **executable physical-rewrite loop** in which the requested intervention itself determines the relevant evidence, failed certification produces a concrete additional physical observation/intervention, the world is re-identified, and an independent target counterfactual is re-tested while numerical and model-form inadequacy remain explicit refusal causes.


## Claim-guard update — experiment design, identifiability and model discrepancy

Three generic routes are now explicitly closed:

1. **Sensitivity/Jacobian experiment design is established.** Asadi & Laksari (2025) optimize hyperelastic material characterization using stress-material Jacobian determinant/conditioning and Fisher-information ideas across loading modes and constitutive models. Vulkax may use this machinery, but cannot claim novelty for “choose the best deformation from a sensitivity matrix.”

2. **Structural identifiability from video is established and active in 2026.** Physics-from-Video gives explicit conditions for unique recovery of second-order physical laws from video. Null-space/rank detection alone is therefore not a flagship contribution.

3. **Model discrepancy separated from parameter uncertainty is established.** Modular Bayesian inverse-UQ literature explicitly treats model inadequacy/numerical approximation as discrepancy that otherwise contaminates parameter calibration. “Wrong physics gets absorbed into fitted parameters” is an important Vulkax diagnostic, not novelty by itself.

### Surviving claim shape

A potentially defensible Vulkax claim must be operational and intervention-specific:

> A persistent captured-world system can refuse a requested physical rewrite when its counterfactual is not supported, provide a reason code that separates evidence insufficiency from numerical/model-family inadequacy, request an additional observation/intervention when that failure is repairable, and independently re-verify the rewritten world before commit.

Even this remains a hypothesis, not a novelty claim. It must survive fresh-domain validation and direct comparison against selective-prediction/UQ/model-validation baselines.

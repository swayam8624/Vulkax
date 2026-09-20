# GAUGE definitive finite-support result — 2026-09-20

## Frozen protocol

Source workflow: GitHub Actions run `35520181267`.

The definitive gate was declared before execution:

- measured-only effective-span calibration from odd repeats;
- held-out evaluation on even repeats;
- released 200 mm body;
- APIC;
- 7 x 7 x 49 particle resolution;
- requested dt = 1/48000 s;
- GAUGE material metadata unchanged;
- endpoint-only control vs symmetric finite support;
- repair must improve face-area NRMSE and marker RMSE in at least 8/10 repeats,
  with >=4/5 dual-metric wins per material;
- group-mean longitudinal signed error must also improve;
- no inversion/stability failure.

Calibration remained:

- shared effective deforming span: **0.1743022461 m**
- implied symmetric support at each end: **0.01284887694 m**

## Result

The finite-support candidate produced:

- **10/10 dual-metric wins**
- **all runs stable / non-inverted**

Soft foam group means:

- face-area NRMSE: **0.461275 -> 0.372667**
- marker RMSE: **3.277 mm -> 3.060 mm**
- longitudinal final absolute error: **0.043545 -> 0.051016**
- longitudinal mechanism wins: **0/5**

Hard foam group means:

- face-area NRMSE: **0.413863 -> 0.343322**
- marker RMSE: **3.144 mm -> 3.111 mm**
- longitudinal final absolute error: **0.036697 -> 0.040421**
- longitudinal mechanism wins: **0/5**

Frozen decision:

> **kill_fixture_overlap_as_primary_explanation**

## Scientific interpretation

This is not a failed experiment to hide. It is a stronger diagnostic result.

The candidate repair is a **metric mirage**:

- every held-out repeat improves both aggregate face-deformation and marker-position
  metrics;
- yet the preregistered mechanism-level longitudinal coupling becomes worse for
  both materials;
- the candidate therefore cannot be promoted as the physical explanation.

The cheaper pilot had suggested improvement in the longitudinal mode. The definitive
resolution reverses that conclusion. Therefore a candidate repair must also survive
a numerical/fidelity ladder; coarse-resolution success is not sufficient.

## Consequence

The previously locked generic adequacy-routing formulation is too broad. A stronger
problem is now justified:

> How can a captured-world system reject repairs that improve standard trajectory
> metrics while degrading the intervention-relevant physical mechanism, and request
> the measurement that separates the competing repair hypotheses?

This result motivates a **mechanism-witness repair verifier**, not inverse fitting of
E/nu and not promotion of finite fixture overlap.

Inverse material fitting remains locked.

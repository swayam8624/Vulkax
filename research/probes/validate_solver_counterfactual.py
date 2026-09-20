#!/usr/bin/env python3
import csv, json, math, pathlib, sys
root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/solver-counterfactual")
rows=list(csv.DictReader((root/"solver_counterfactual.csv").open()))
meta=json.loads((root/"summary.json").read_text())
if meta.get("provenance")!="synthetic": raise SystemExit("solver probe provenance must remain synthetic")
if not rows: raise SystemExit("no solver probe rows")
for r in rows:
    v=float(r["value"])
    if not math.isfinite(v): raise SystemExit(f"non-finite solver result: {r}")
    if r["provenance"]!="synthetic": raise SystemExit(f"bad provenance: {r}")
def metric(experiment,scenario,name):
    x=[r for r in rows if r["experiment"]==experiment and r["scenario"]==scenario and r["metric"]==name]
    if len(x)!=1: raise SystemExit(f"expected one metric {experiment}/{scenario}/{name}, got {len(x)}")
    return float(x[0]["value"])
fit=metric("model_mismatch_summary","best","fit_rms_m")
held=metric("model_mismatch_summary","best","heldout_same_intervention_rms_m")
cf=metric("model_mismatch_summary","best","counterfactual_new_deformation_rms_m")
if fit<0 or held<0 or cf<0: raise SystemExit("negative RMS")
if metric("model_mismatch_summary","truth","minimum_J")<=0: raise SystemExit("truth simulation inverted")
print("VALID Vulkax solver-integrated discovery probe")
print(f"fit_rms_m={fit:.9g}")
print(f"heldout_same_intervention_rms_m={held:.9g}")
print(f"counterfactual_new_deformation_rms_m={cf:.9g}")
print(f"counterfactual_to_heldout_ratio={cf/max(held,1e-15):.9g}")
for dt in ["dt=0.000100","dt=0.000200","dt=0.000400","dt=0.000500"]:
    print(dt, "fitted_E_pa", metric("numerics_confounding",dt,"fitted_E_pa"),
          "relative_E_error", metric("numerics_confounding",dt,"relative_E_error"))

#!/usr/bin/env python3
import csv,json,math,pathlib,sys
root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/solver-design")
rows=list(csv.DictReader((root/"solver_design.csv").open()))
meta=json.loads((root/"summary.json").read_text())
if meta.get("provenance")!="synthetic": raise SystemExit("design probe must remain synthetic")
if not rows: raise SystemExit("no design rows")
for r in rows:
    v=float(r["value"])
    if r["metric"]!="condition_number" and not math.isfinite(v):
        raise SystemExit(f"non-finite design metric: {r}")
    if r["provenance"]!="synthetic": raise SystemExit(f"bad provenance: {r}")
def one(group,metric):
    xs=[r for r in rows if r["intervention"]==group and r["metric"]==metric]
    if len(xs)!=1: raise SystemExit(f"expected one {group}/{metric}")
    return float(xs[0]["value"])
print("VALID Vulkax solver active-design probe")
print("selected_intervention",meta["selected_intervention"])
print("train_only_E",one("train_only_fit","fitted_E_pa"))
print("train_only_nu",one("train_only_fit","fitted_nu"))
print("train_only_E_error",one("train_only_fit","relative_E_error"))
print("train_only_nu_error",one("train_only_fit","absolute_nu_error"))
print("combined_E",one("train_plus_design_fit","fitted_E_pa"))
print("combined_nu",one("train_plus_design_fit","fitted_nu"))
print("combined_E_error",one("train_plus_design_fit","relative_E_error"))
print("combined_nu_error",one("train_plus_design_fit","absolute_nu_error"))

#!/usr/bin/env python3
"""Analyze grid-phase sensitivity and phase-locked co-refinement without labels."""
import csv,json,math,pathlib,statistics,sys

root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/grid-phase-falsification")
phase=list(csv.DictReader((root/"phase_sweep.csv").open()))
core=list(csv.DictReader((root/"phase_locked_corefinement.csv").open()))
meta=json.loads((root/"metadata.json").read_text())
if not phase or not core or meta.get("provenance")!="synthetic-label-free-fixed-physics-grid-phase-diagnostic":
    raise SystemExit("invalid grid-phase evidence")

def num(r,k):
    try:return float(r[k])
    except:return float("nan")

cases=sorted(set(r["case"] for r in phase))
result={
  "schema":"vulkax.grid_phase_falsification_analysis",
  "version":1,
  "provenance":meta["provenance"],
  "selection_labels_used":False,
  "reference_budget_fraction":0.05,
  "cases":{},
  "guardrails":[
    "Grid phase is numerical-coordinate alignment, not a physical fit parameter.",
    "The 5% budget is historical and diagnostic; no threshold is tuned on this experiment.",
    "No inverse-fit or counterfactual safety labels are used."
  ]
}
for case in cases:
    pg=sorted([r for r in phase if r["case"]==case],key=lambda r:num(r,"phase"))
    cg=sorted([r for r in core if r["case"]==case],key=lambda r:int(r["n_axis"]))
    phase_fracs=[num(r,"fraction_to_phase_075") for r in pg]
    phase_max=max(phase_fracs)
    trans=[num(r,"adjacent_fraction") for r in cg if math.isfinite(num(r,"adjacent_fraction"))]
    if len(trans)<2: raise SystemExit("need two phase-locked co-refinement transitions")
    tail=trans[-1]
    improves=trans[-1] < trans[-2]
    result["cases"][case]={
      "phase_sensitivity_fractions_to_075":phase_fracs,
      "maximum_phase_sensitivity_fraction":phase_max,
      "phase_materially_matters":phase_max>0.05,
      "phase_locked_corefinement_fractions":trans,
      "phase_locked_tail_decreasing":improves,
      "phase_locked_final_adjacent_fraction":tail,
      "phase_locked_below_5pct_budget":tail<=0.05,
      "diagnosis":(
        "grid_phase_explains_previous_resolution_pathology"
        if phase_max>0.05 and tail<=0.05 and improves
        else "grid_phase_matters_but_does_not_resolve_spatial_pathology"
        if phase_max>0.05
        else "grid_phase_not_primary_spatial_cause"
      )
    }

all_repaired=all(d["diagnosis"]=="grid_phase_explains_previous_resolution_pathology" for d in result["cases"].values())
result["decision"]=(
  "spatial_pathology_repaired_by_phase_control"
  if all_repaired else
  "do_not_define_validation6_spatial_mechanism_still_unresolved"
)
(root/"analysis.json").write_text(json.dumps(result,indent=2)+"\n")
print("VALID grid-phase falsification analysis")
for case,d in result["cases"].items():
    print("GRID_PHASE_ANALYSIS",case,
          "phase_max",d["maximum_phase_sensitivity_fraction"],
          "locked",d["phase_locked_corefinement_fractions"],
          "diagnosis",d["diagnosis"])
print("DECISION",result["decision"])

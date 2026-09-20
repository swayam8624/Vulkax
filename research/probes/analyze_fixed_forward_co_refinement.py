#!/usr/bin/env python3
"""Analyze label-free particle+grid co-refinement evidence."""
import csv
import json
import math
import pathlib
import sys

root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/fixed-forward-co-refinement")
rows=list(csv.DictReader((root/"levels.csv").open()))
meta=json.loads((root/"metadata.json").read_text())
if not rows or meta.get("provenance")!="synthetic-label-free-fixed-physics-particle-grid-corefinement":
    raise SystemExit("invalid co-refinement evidence")

def num(r,k):
    try:
        return float(r[k])
    except Exception:
        return float("nan")

by={}
for r in rows:
    by.setdefault(r["case"],[]).append(r)
for g in by.values():
    g.sort(key=lambda r:int(r["n_axis"]))

result={
    "schema":"vulkax.fixed_forward_co_refinement_analysis",
    "version":1,
    "provenance":meta["provenance"],
    "selection_labels_used":False,
    "reference_budget_fraction":0.05,
    "cases":{},
    "guardrails":[
        "The 5% budget was frozen before this experiment and is diagnostic only.",
        "No inverse fit, counterfactual safety label, or material selection is used.",
        "Passing a discrete tail check is not continuum-convergence proof.",
    ],
}

for case,g in by.items():
    transitions=[r for r in g if math.isfinite(num(r,"adjacent_observation_fraction"))]
    fracs=[num(r,"adjacent_observation_fraction") for r in transitions]
    if len(fracs)<2:
        raise SystemExit(f"need at least two spatial transitions for {case}")
    final=fracs[-1]
    decreasing=fracs[-1]<fracs[-2]
    below=final<=0.05
    finite_stable=all(num(r,"min_J")>0.0 for r in g)
    result["cases"][case]={
        "levels":[int(r["n_axis"]) for r in g],
        "particle_counts":[int(r["particles"]) for r in g],
        "grid_cells_m":[num(r,"grid_cell_m") for r in g],
        "adjacent_observation_fractions":fracs,
        "tail_decreasing":decreasing,
        "final_adjacent_fraction":final,
        "passes_frozen_5pct_budget":below,
        "all_levels_noninverted":finite_stable,
        "maximum_energy_drift":max(num(r,"max_relative_energy_drift") for r in g),
        "maximum_mls_rms_residual":max(num(r,"max_mls_rms_residual") for r in g),
        "diagnosis":(
            "corefinement_tail_below_budget"
            if below and decreasing and finite_stable
            else "spatial_corefinement_still_unresolved"
        ),
    }

all_pass=all(
    d["diagnosis"]=="corefinement_tail_below_budget"
    for d in result["cases"].values()
)
result["decision"]=(
    "particle_grid_consistency_repairs_fixed_particle_grid_pathology"
    if all_pass else
    "continue_spatial_corefinement_before_validation6"
)
(root/"analysis.json").write_text(json.dumps(result,indent=2)+"\n")
print("VALID fixed-forward particle-grid co-refinement")
for case,d in result["cases"].items():
    print("COREFINE_ANALYSIS",case,
          "fractions",d["adjacent_observation_fractions"],
          "final",d["final_adjacent_fraction"],
          "diagnosis",d["diagnosis"])
print("DECISION",result["decision"])

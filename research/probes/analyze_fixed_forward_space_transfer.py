#!/usr/bin/env python3
"""Combine label-free timestep, spatial, and transfer forensics.

This script does not use counterfactual safe/unsafe labels and does not tune a
certificate. The historical 5% budget is retained only as a diagnostic reference.
"""
import argparse
import csv
import json
import math
import pathlib
import statistics

def f(row,key):
    try:
        return float(row[key])
    except Exception:
        return float("nan")

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--space-transfer",required=True)
    ap.add_argument("--deep-analysis",required=True)
    ap.add_argument("--out",required=True)
    a=ap.parse_args()

    sroot=pathlib.Path(a.space_transfer)
    out=pathlib.Path(a.out)
    out.mkdir(parents=True,exist_ok=True)
    rows=list(csv.DictReader((sroot/"levels.csv").open()))
    deep=json.loads(pathlib.Path(a.deep_analysis).read_text())
    if not rows:
        raise SystemExit("empty space/transfer evidence")

    cases=sorted(set(r["case"] for r in rows))
    result={
        "schema":"vulkax.fixed_forward_numerical_attribution",
        "version":1,
        "provenance":"synthetic-label-free-fixed-physics-numerical-diagnostic",
        "selection_labels_used":False,
        "reference_budget_fraction":0.05,
        "cases":{},
        "guardrails":[
            "The 5% value is a previously frozen diagnostic budget, not a newly selected threshold.",
            "No safe/unsafe target labels, inverse fit, or material selection are used.",
            "Finest discrete timestep/grid levels are numerical comparators, not continuum truth.",
        ],
    }

    for case in cases:
        cr=[r for r in rows if r["case"]==case]
        transfers={}
        for transfer in ("APIC","PIC","FLIP"):
            tr=sorted(
                [r for r in cr if r["transfer"]==transfer],
                key=lambda r:f(r,"grid_cell_m"),
                reverse=True,
            )
            finite=[r for r in tr if math.isfinite(f(r,"adjacent_spatial_fraction"))]
            if not finite:
                raise SystemExit(f"no spatial transitions for {case}/{transfer}")
            finest=tr[-1]
            transfers[transfer]={
                "levels":len(tr),
                "finest_grid_cell_m":f(finest,"grid_cell_m"),
                "final_adjacent_spatial_fraction":f(finite[-1],"adjacent_spatial_fraction"),
                "final_fraction_to_finest_same_transfer":f(finest,"fraction_to_finest_same_transfer"),
                "finest_fraction_to_apic_same_grid":f(finest,"fraction_to_apic_same_grid"),
                "maximum_energy_drift":max(f(r,"max_relative_energy_drift") for r in tr),
                "minimum_J":min(f(r,"min_J") for r in tr),
            }

        deep_case=deep["cases"][case]
        time_fraction=float(deep_case["final_adjacent_observation_fraction"])
        spatial_fraction=float(transfers["APIC"]["final_adjacent_spatial_fraction"])
        time_below=time_fraction<=0.05
        space_below=spatial_fraction<=0.05
        transfer_fraction=max(
            float(transfers["PIC"]["finest_fraction_to_apic_same_grid"]),
            float(transfers["FLIP"]["finest_fraction_to_apic_same_grid"]),
        )
        transfer_sensitive=transfer_fraction>0.05

        if not time_below and space_below:
            attribution="timestep_unresolved_spatial_grid_below_budget"
        elif not time_below and not space_below:
            attribution="timestep_and_spatial_grid_both_unresolved"
        elif time_below and not space_below:
            attribution="spatial_grid_unresolved_after_timestep_budget_pass"
        else:
            attribution="time_and_spatial_transitions_below_budget"

        result["cases"][case]={
            "deep_timestep_final_adjacent_fraction":time_fraction,
            "apic_final_adjacent_spatial_fraction":spatial_fraction,
            "max_finest_transfer_disagreement_fraction":transfer_fraction,
            "transfer_family_sensitive":transfer_sensitive,
            "attribution":attribution,
            "transfers":transfers,
        }

    attrs=[v["attribution"] for v in result["cases"].values()]
    if len(set(attrs))==1:
        global_attr=attrs[0]
    else:
        global_attr="case_dependent_numerical_attribution"
    result["global_attribution"]=global_attr
    result["decision"]=(
        "do_not_define_validation6_yet"
        if any(
            (v["deep_timestep_final_adjacent_fraction"]>0.05 or
             v["apic_final_adjacent_spatial_fraction"]>0.05)
            for v in result["cases"].values()
        )
        else "forward_discretization_ready_for_next_preregistered_test"
    )

    (out/"analysis.json").write_text(json.dumps(result,indent=2)+"\n")
    print("VALID fixed-forward numerical attribution")
    for case,d in result["cases"].items():
        print(
            "ATTRIBUTION",case,
            "time",d["deep_timestep_final_adjacent_fraction"],
            "space",d["apic_final_adjacent_spatial_fraction"],
            "transfer",d["max_finest_transfer_disagreement_fraction"],
            d["attribution"],
        )
    print("DECISION",result["decision"])

if __name__=="__main__":
    main()

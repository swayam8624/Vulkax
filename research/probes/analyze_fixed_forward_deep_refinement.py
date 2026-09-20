#!/usr/bin/env python3
"""Analyze fixed-physics APIC deep timestep refinement without validation labels."""
import csv,json,math,pathlib,sys

root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/fixed-forward-deep-refinement")
rows=list(csv.DictReader((root/"levels.csv").open()))
meta=json.loads((root/"metadata.json").read_text())
if not rows or meta.get("provenance")!="synthetic-label-free-fixed-physics-numerical-diagnostic":
    raise SystemExit("invalid deep-refinement evidence")

def num(r,k):
    try:return float(r[k])
    except:return float("nan")

by={}
for r in rows:by.setdefault(r["case"],[]).append(r)
for g in by.values():g.sort(key=lambda r:num(r,"dt_s"),reverse=True)

def observed_order(a,b):
    if not (a>0 and b>0): return None
    return math.log(a/b,2.0)

result={
  "schema":"vulkax.fixed_forward_deep_refinement_analysis",
  "version":1,
  "provenance":meta["provenance"],
  "selection_labels_used":False,
  "locked_reference_budget_fraction":0.05,
  "cases":{},
  "interpretation_guard":(
      "The 5% value is the previously frozen diagnostic budget, not a fitted threshold. "
      "The finest discrete level is not continuum truth."
  )
}
for name,g in by.items():
    transitions=[r for r in g if math.isfinite(num(r,"adjacent_observation_fraction"))]
    diffs=[num(r,"adjacent_observation_rms_m") for r in transitions]
    fracs=[num(r,"adjacent_observation_fraction") for r in transitions]
    orders=[]
    for i in range(1,len(diffs)):
        orders.append(observed_order(diffs[i-1],diffs[i]))
    final=g[-1]
    final_frac=fracs[-1]
    last_orders=[x for x in orders[-3:] if x is not None and math.isfinite(x)]
    positive_order=bool(last_orders) and min(last_orders)>0.0
    budget_pass=final_frac<=0.05
    # "asymptotic-like" is deliberately descriptive: decreasing last two
    # adjacent differences with positive observed orders. It is not convergence proof.
    asymptotic_like=(len(diffs)>=3 and diffs[-1]<diffs[-2]<diffs[-3] and positive_order)
    result["cases"][name]={
      "levels":len(g),
      "finest_dt_s":num(final,"dt_s"),
      "final_adjacent_observation_fraction":final_frac,
      "final_observation_fraction_to_finest":num(final,"observation_fraction_to_finest"),
      "final_max_relative_energy_drift":num(final,"max_relative_energy_drift"),
      "minimum_J_over_levels":min(num(r,"min_J") for r in g),
      "adjacent_observation_fractions":fracs,
      "observed_orders":orders,
      "last_three_finite_orders":last_orders,
      "passes_frozen_5pct_budget_at_finest_transition":budget_pass,
      "shows_asymptotic_like_decrease_at_tail":asymptotic_like,
      "diagnosis":(
        "fixed_forward_below_budget_on_deep_ladder" if budget_pass
        else "fixed_forward_still_above_budget_on_deep_ladder"
      )
    }

(root/"analysis.json").write_text(json.dumps(result,indent=2)+"\n")
print("VALID fixed-forward deep refinement")
for name,d in result["cases"].items():
    print("DEEP_ANALYSIS",name,
          "finest_dt",d["finest_dt_s"],
          "final_frac",d["final_adjacent_observation_fraction"],
          "orders",d["last_three_finite_orders"],
          "asymptotic_like",d["shows_asymptotic_like_decrease_at_tail"],
          "diagnosis",d["diagnosis"])

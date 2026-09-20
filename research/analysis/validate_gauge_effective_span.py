#!/usr/bin/env python3
"""Validate the pre-registered GAUGE effective-span stability criteria."""
import json
import pathlib
import statistics
import sys

root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/gauge-effective-span")
summary=json.loads((root/"summary.json").read_text())

if summary.get("schema")!="vulkax.gauge_effective_deforming_span":
    raise SystemExit("unexpected effective-span schema")

groups=summary["groups"]
cross=summary["cross_material"]
threshold_keys=sorted(groups,key=float)

checks={}
for key in threshold_keys:
    for material in ("soft","hard"):
        g=groups[key][material]
        prefix=f"{material}_{key}"
        checks[prefix+"_r2"]=g["median_r2"]>=0.98
        checks[prefix+"_cv"]=g["effective_span_cv"]<=0.03
        checks[prefix+"_odd_even"]=g["even_trial_holdout_relative_mae"]<=0.03
    checks[f"soft_hard_{key}"]=cross[key]["soft_hard_abs_difference_m"]<=0.005

pooled=[cross[key]["pooled_midpoint_m"] for key in threshold_keys]
checks["threshold_stability"]=(max(pooled)-min(pooled))<=0.005

survives=all(checks.values())
result={
    "schema":"vulkax.gauge_effective_span_validation",
    "version":1,
    "provenance":"measured-only",
    "selection_data":"criteria frozen before all-repeat output",
    "criteria":{
        "median_r2_min":0.98,
        "effective_span_cv_max":0.03,
        "odd_even_relative_mae_max":0.03,
        "soft_hard_median_difference_max_m":0.005,
        "pooled_threshold_range_max_m":0.005,
    },
    "checks":checks,
    "survives":survives,
    "decision":(
        "proceed_to_disjoint_boundary_support_test"
        if survives else
        "kill_single_effective_span_as_reusable_boundary_summary"
    ),
    "pooled_effective_span_by_threshold_m":{
        key:cross[key]["pooled_midpoint_m"] for key in threshold_keys
    },
    "warning":(
        "Passing only establishes repeat stability of a measured kinematic summary. "
        "It does not validate fixture mechanics or unlock inverse material fitting."
    ),
}
(root/"validation.json").write_text(json.dumps(result,indent=2)+"\n")
print("VALID GAUGE effective-span preregistered validation")
print("SURVIVES",survives)
for k,v in checks.items():
    print("CHECK",k,int(v))
print("DECISION",result["decision"])

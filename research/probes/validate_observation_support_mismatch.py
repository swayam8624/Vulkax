#!/usr/bin/env python3
"""Validate the synthetic observation-support mismatch positive control."""
import csv
import json
import pathlib
import sys

root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/observation-support-mismatch")
rows={r["model"]:r for r in csv.DictReader((root/"cases.csv").open())}
meta=json.loads((root/"summary.json").read_text())

required={"full_body_correct_support","marker_envelope_truncated_body"}
if set(rows)!=required:
    raise SystemExit(f"unexpected model rows: {sorted(rows)}")

def f(model,key): return float(rows[model][key])

correct="full_body_correct_support"
bad="marker_envelope_truncated_body"

checks={
    # Deterministic positive control: the truth E is present in the candidate grid.
    "correct_recovers_truth_grid_E":abs(f(correct,"fitted_young_pa")-80000.0)<=1e-9,
    "correct_fit_numerically_exact":f(correct,"fit_relative_error")<=1e-6,
    "correct_target_numerically_exact":f(correct,"target_relative_error")<=1e-6,
    # For the mismatch to be scientifically interesting it must be able to hide
    # under the observed intervention, not merely fail everywhere.
    "truncated_training_fit_plausible":f(bad,"fit_relative_error")<=0.10,
    # The unseen intervention must expose a materially larger failure.
    "truncated_target_unsafe":f(bad,"target_relative_error")>0.10,
    "counterfactual_degrades_vs_fit":(
        f(bad,"target_relative_error") >=
        max(0.10,2.0*f(bad,"fit_relative_error"))
    ),
    "no_inversion_correct":f(correct,"min_J")>0.0,
    "no_inversion_truncated":f(bad,"min_J")>0.0,
    "exact_prescribed_boundary_correct":f(correct,"max_boundary_error_m")<=1e-12,
    "exact_prescribed_boundary_truncated":f(bad,"max_boundary_error_m")<=1e-12,
}

result={
    "schema":"vulkax.observation_support_mismatch_validation",
    "version":1,
    "provenance":"synthetic",
    "checks":checks,
    "positive_control_survives":all(checks.values()),
    "decision":(
        "retain_observation_support_mismatch_as_failure_family"
        if all(checks.values())
        else "do_not_promote_support_mismatch_positive_control"
    ),
    "rows":rows,
    "warning":(
        "This is a synthetic planted-support mismatch. Passing shows only that the "
        "failure mode can hide under one intervention and emerge under another; it "
        "does not identify the cause of the real GAUGE mismatch."
    ),
}
(root/"validation.json").write_text(json.dumps(result,indent=2)+"\n")
print("VALID observation-support mismatch positive-control analysis")
for k,v in checks.items(): print("CHECK",k,int(v))
print("SURVIVES",result["positive_control_survives"])
print("DECISION",result["decision"])

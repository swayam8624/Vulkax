#!/usr/bin/env python3
"""Analyze label-free timestep/inverse refinement evidence.

The 5% value is the already-frozen numerical budget used by the prior certificate
experiments. It is not re-fit here and no safe/unsafe target labels are consulted.
"""
import csv,json,math,pathlib,statistics,sys

root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/refinement-forensics")
rows=list(csv.DictReader((root/"refinement.csv").open()))
meta=json.loads((root/"metadata.json").read_text())
if not rows or meta.get("provenance")!="synthetic-label-free-numerical-diagnostic":
    raise SystemExit("invalid refinement forensic input")

budget=0.05
by={}
for row in rows:
    by.setdefault(row["case"],[]).append(row)
for g in by.values():
    g.sort(key=lambda r:float(r["dt_s"]),reverse=True)

def f(row,key):
    return float(row[key])

def summarize(g):
    transitions=g[1:]
    fixed=[f(r,"fixed_adjacent_fraction") for r in transitions]
    refit=[f(r,"refit_target_adjacent_fraction") for r in transitions]
    fits=[(f(r,"fit_E_pa"),f(r,"fit_nu")) for r in g]
    switches=sum(a!=b for a,b in zip(fits,fits[1:]))

    def first_pass(vals,rows):
        for value,row in zip(vals,rows):
            if value<=budget:
                return f(row,"dt_s")
        return None
    def max_consecutive(vals):
        best=cur=0
        for value in vals:
            if value<=budget:
                cur+=1;best=max(best,cur)
            else:cur=0
        return best

    final=g[-1]
    fixed_final=fixed[-1]
    refit_final=refit[-1]
    if fixed_final>budget:
        diagnosis="forward_solver_not_converged_on_evaluated_ladder"
    elif refit_final>budget:
        diagnosis="inverse_refit_instability_dominates_after_forward_convergence"
    else:
        diagnosis="both_forward_and_refit_below_single_step_budget"
    return {
      "rows":len(g),
      "fixed_first_budget_pass_dt_s":first_pass(fixed,transitions),
      "refit_first_budget_pass_dt_s":first_pass(refit,transitions),
      "fixed_max_consecutive_budget_passes":max_consecutive(fixed),
      "refit_max_consecutive_budget_passes":max_consecutive(refit),
      "final_fixed_adjacent_fraction":fixed_final,
      "final_refit_adjacent_fraction":refit_final,
      "final_refit_target_error_vs_finest_fixed":f(final,"refit_target_error_vs_finest_fixed"),
      "fit_parameter_switches":switches,
      "fit_path":[{"dt_s":f(r,"dt_s"),"E_pa":f(r,"fit_E_pa"),"nu":f(r,"fit_nu"),
                   "objective_m":f(r,"fit_objective_m")} for r in g],
      "final_parameter_error":{"E_pa":f(final,"fit_E_error_pa"),"nu":f(final,"fit_nu_error")},
      "diagnosis":diagnosis,
    }

result={
 "schema":"vulkax.refinement_forensics_analysis",
 "version":1,
 "provenance":"synthetic-label-free-numerical-diagnostic",
 "locked_numerical_budget_fraction":budget,
 "selection_labels_used":False,
 "cases":{name:summarize(g) for name,g in by.items()},
 "interpretation_guard":(
   "This diagnoses the evaluated discrete solver/inverse pipeline only. "
   "It does not certify a counterfactual, choose a safety threshold, or establish continuum convergence."
 )
}
(root/"analysis.json").write_text(json.dumps(result,indent=2)+"\n")
print("VALID label-free refinement forensic")
for name,d in result["cases"].items():
    print("REFINEMENT_ANALYSIS",name,
          "fixed_final",d["final_fixed_adjacent_fraction"],
          "refit_final",d["final_refit_adjacent_fraction"],
          "fit_switches",d["fit_parameter_switches"],
          "final_target_error",d["final_refit_target_error_vs_finest_fixed"],
          "diagnosis",d["diagnosis"])

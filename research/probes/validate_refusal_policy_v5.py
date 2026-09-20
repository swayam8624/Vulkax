#!/usr/bin/env python3
import csv,json,pathlib,statistics,sys
root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/refusal-validation5")
rows=list(csv.DictReader((root/"cases.csv").open()))
meta=json.loads((root/"summary.json").read_text())
if meta.get("provenance")!="synthetic-validation5" or len(rows)!=int(meta["case_count"]):
    raise SystemExit("bad Validation-5 metadata")
def f(r,k): return float(r[k])
def b(r,k): return int(r[k])!=0
def summarize(accept,safe,error):
    accepted=[r for r in rows if b(r,accept)]
    unsafe=[r for r in accepted if not b(r,safe)]
    return {
      "accepted":len(accepted),"coverage":len(accepted)/len(rows),
      "unsafe_rate":len(unsafe)/len(accepted) if accepted else None,
      "mean_error":statistics.fmean(f(r,error) for r in accepted) if accepted else None,
      "variants":{v:sum(r["variant"]==v for r in accepted) for v in sorted({r["variant"] for r in rows})}
    }
target_only=summarize("target_only_accept","target_only_safe_10pct","target_only_relative_error")
refit=summarize("refit_accept","refit_safe_10pct","refit_relative_error")
result={
 "schema":"vulkax.refusal_validation5_analysis","version":1,
 "provenance":"synthetic-validation5",
 "cases":len(rows),
 "truth_dt_s":meta["truth_dt_s"],
 "accept_all_unsafe_rate":sum(not b(r,"refit_safe_10pct") for r in rows)/len(rows),
 "target_only_policy":target_only,
 "refit_policy":refit,
 "predeclared_success":{"max_unsafe_rate":0.10,"min_coverage":0.25},
 "passes_predeclared_target":(
   refit["coverage"]>=0.25 and refit["unsafe_rate"] is not None and refit["unsafe_rate"]<=0.10
 ),
 "by_variant":{}
}
for v in sorted({r["variant"] for r in rows}):
    g=[r for r in rows if r["variant"]==v]
    result["by_variant"][v]={
      "cases":len(g),
      "median_pre_error":statistics.median(f(r,"pre_target_relative_error") for r in g),
      "median_target_only_error":statistics.median(f(r,"target_only_relative_error") for r in g),
      "median_refit_error":statistics.median(f(r,"refit_relative_error") for r in g),
      "target_only_accepted":sum(b(r,"target_only_accept") for r in g),
      "refit_accepted":sum(b(r,"refit_accept") for r in g),
      "median_refit_dt_s":statistics.median(f(r,"refit_final_dt_s") for r in g),
      "median_refit_fraction":statistics.median(f(r,"refit_fraction") for r in g),
      "median_consecutive_stable":statistics.median(f(r,"refit_consecutive_stable") for r in g),
      "median_abs_E_shift_pa":statistics.median(abs(f(r,"refit_E_pa")-f(r,"base_repaired_E_pa")) for r in g),
      "median_abs_nu_shift":statistics.median(abs(f(r,"refit_nu")-f(r,"base_repaired_nu")) for r in g)
    }
(root/"analysis.json").write_text(json.dumps(result,indent=2)+"\n")
print("VALID refusal policy Validation-5 refine-refit-revalidate")
print("ACCEPT_ALL unsafe",result["accept_all_unsafe_rate"])
print("TARGET_ONLY5",target_only)
print("REFIT5",refit)
for v,d in result["by_variant"].items(): print("V5_VARIANT",v,d)
print("PREDECLARED_TARGET_PASS",result["passes_predeclared_target"])

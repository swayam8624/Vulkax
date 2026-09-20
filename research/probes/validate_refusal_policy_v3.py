#!/usr/bin/env python3
import csv,json,pathlib,statistics,sys
root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/refusal-validation3")
rows=list(csv.DictReader((root/"cases.csv").open()))
meta=json.loads((root/"summary.json").read_text())
if meta.get("provenance")!="synthetic-validation3" or len(rows)!=int(meta["case_count"]):
    raise SystemExit("bad Validation-3 metadata")
def f(r,k): return float(r[k])
def b(r,k): return int(r[k])!=0
def summarize(key):
    accepted=[r for r in rows if b(r,key)]
    unsafe=[r for r in accepted if not b(r,"post_safe_10pct")]
    return {
      "accepted":len(accepted),"coverage":len(accepted)/len(rows),
      "unsafe_rate":len(unsafe)/len(accepted) if accepted else None,
      "mean_error":statistics.fmean(f(r,"post_target_relative_error_refined") for r in accepted) if accepted else None,
      "variants":{v:sum(r["variant"]==v for r in accepted) for v in sorted({r["variant"] for r in rows})}
    }
result={
 "schema":"vulkax.refusal_validation3_analysis","version":1,
 "provenance":"synthetic-validation3",
 "cases":len(rows),
 "accept_all_unsafe_rate":sum(not b(r,"post_safe_10pct") for r in rows)/len(rows),
 "core_policy":summarize("core_accept"),
 "core_plus_scheme":summarize("core_plus_scheme_accept"),
 "numerical_repair":{
   "converged_rate":sum(b(r,"numerical_converged") for r in rows)/len(rows),
   "mean_refinements":statistics.fmean(f(r,"numerical_refinements") for r in rows),
   "median_final_fraction":statistics.median(f(r,"numerical_fraction_final") for r in rows),
   "final_dt_counts":{str(x):sum(abs(f(r,"final_dt_s")-x)<1e-12 for r in rows)
                      for x in sorted({f(r,"final_dt_s") for r in rows})}
 },
 "by_variant":{}
}
for v in sorted({r["variant"] for r in rows}):
    g=[r for r in rows if r["variant"]==v]
    result["by_variant"][v]={
      "cases":len(g),
      "unsafe_rate_after_refinement":sum(not b(r,"post_safe_10pct") for r in g)/len(g),
      "median_pre_error":statistics.median(f(r,"pre_target_relative_error") for r in g),
      "median_post_error":statistics.median(f(r,"post_target_relative_error_refined") for r in g),
      "numerical_converged_rate":sum(b(r,"numerical_converged") for r in g)/len(g),
      "core_accepted":sum(b(r,"core_accept") for r in g),
      "plus_scheme_accepted":sum(b(r,"core_plus_scheme_accept") for r in g)
    }
(root/"analysis.json").write_text(json.dumps(result,indent=2)+"\n")
print("VALID refusal policy Validation-3 adaptive numerical repair")
print("ACCEPT_ALL unsafe",result["accept_all_unsafe_rate"])
for k in ("core_policy","core_plus_scheme"):
    p=result[k];print("POLICY3",k,"coverage",p["coverage"],"unsafe_rate",p["unsafe_rate"],"mean_error",p["mean_error"],"variants",p["variants"])
print("NUMERICAL_REPAIR",result["numerical_repair"])
for v,d in result["by_variant"].items():print("V3_VARIANT",v,d)

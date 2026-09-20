#!/usr/bin/env python3
import csv,json,math,pathlib,statistics,sys
root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/refusal-repair")
rows=list(csv.DictReader((root/"cases.csv").open()))
meta=json.loads((root/"summary.json").read_text())
noise=float(meta["measurement_noise_m"])
if meta.get("provenance")!="synthetic" or len(rows)!=int(meta["case_count"]): raise SystemExit("bad metadata")
def f(r,k): return float(r[k])
for r in rows:
    if r["provenance"]!="synthetic": raise SystemExit("bad provenance")
    for k in ("pre_target_relative_error","post_target_relative_error","evidence_noise_ratio",
              "numerical_fraction","scheme_fraction","relative_E_shift","absolute_nu_shift"):
        if not math.isfinite(f(r,k)): raise SystemExit(f"non-finite {k}")

def summarize(group):
    return {
      "cases":len(group),
      "pre_safe_rate":sum(f(r,"pre_target_relative_error")<=0.10 for r in group)/len(group),
      "post_safe_rate":sum(f(r,"post_target_relative_error")<=0.10 for r in group)/len(group),
      "median_pre_target_relative_error":statistics.median(f(r,"pre_target_relative_error") for r in group),
      "median_post_target_relative_error":statistics.median(f(r,"post_target_relative_error") for r in group),
      "median_evidence_noise_ratio":statistics.median(f(r,"evidence_noise_ratio") for r in group),
      "median_numerical_fraction":statistics.median(f(r,"numerical_fraction") for r in group),
      "median_scheme_fraction":statistics.median(f(r,"scheme_fraction") for r in group),
    }

by_variant={}
for v in sorted({r["variant"] for r in rows}):
    by_variant[v]=summarize([r for r in rows if r["variant"]==v])

# Fixed, physics-motivated post-repair gates: fit the requested evidence within 2x
# measurement noise, and make the target prediction numerically stable to within 5%.
policies={}
for held_gate in (False,True):
    accepted=[]
    for r in rows:
        ok=f(r,"evidence_noise_ratio")<=2.0 and f(r,"numerical_fraction")<=0.05
        if held_gate:
            ok=ok and f(r,"repaired_heldout_rms_m")<=2.0*noise
        if ok: accepted.append(r)
    name="evidence_num"+("_heldout" if held_gate else "")
    policies[name]={
      "accepted":len(accepted),
      "coverage":len(accepted)/len(rows),
      "unsafe_rate":(sum(f(r,"post_target_relative_error")>0.10 for r in accepted)/len(accepted)) if accepted else None,
      "mean_post_target_relative_error":statistics.fmean(f(r,"post_target_relative_error") for r in accepted) if accepted else None,
      "variants":{v:sum(r["variant"]==v for r in accepted) for v in sorted({r["variant"] for r in rows})}
    }

analysis={
 "schema":"vulkax.refusal_repair_analysis","version":1,"provenance":"synthetic",
 "case_count":len(rows),"by_variant":by_variant,"fixed_policies":policies,
 "warning":"Fixed gates use measurement noise and numerical-refinement criteria only; they were not tuned to target labels."
}
(root/"analysis.json").write_text(json.dumps(analysis,indent=2)+"\n")
print("VALID Vulkax refusal-repair probe")
for v,d in by_variant.items():
    print("VARIANT",v,
          "pre_safe",f'{d["pre_safe_rate"]:.3f}',
          "post_safe",f'{d["post_safe_rate"]:.3f}',
          "median_pre",f'{d["median_pre_target_relative_error"]:.4f}',
          "median_post",f'{d["median_post_target_relative_error"]:.4f}',
          "evidence_noise_ratio",f'{d["median_evidence_noise_ratio"]:.3f}',
          "numerical_fraction",f'{d["median_numerical_fraction"]:.4f}')
for name,p in policies.items():
    print("POLICY",name,"coverage",f'{p["coverage"]:.3f}',
          "unsafe_rate",("none" if p["unsafe_rate"] is None else f'{p["unsafe_rate"]:.3f}'),
          "variants",p["variants"])

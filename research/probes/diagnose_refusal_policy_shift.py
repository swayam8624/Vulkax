#!/usr/bin/env python3
import csv,json,math,pathlib,statistics,sys

args=sys.argv[1:]
paths=[pathlib.Path(x) for x in args[:3]] if len(args)>=3 else [
 pathlib.Path("build/refusal-repair/cases.csv"),
 pathlib.Path("build/refusal-validation/cases.csv"),
 pathlib.Path("build/refusal-validation2/cases.csv")]
out=pathlib.Path(args[3] if len(args)>=4 else "build/policy-shift")
out.mkdir(parents=True,exist_ok=True)
datasets={name:list(csv.DictReader(path.open())) for name,path in zip(("calibration","validation1","validation2"),paths)}
noise={"calibration":2e-5,"validation1":2e-5,"validation2":2.5e-5}
qs=[0.0,0.1,0.25,0.5,0.75,0.9,1.0]

def f(r,k): return float(r[k])
def unsafe(r): return f(r,"post_target_relative_error")>0.10
def quantile(xs,q):
    a=sorted(xs); x=(len(a)-1)*q; lo=int(math.floor(x)); hi=int(math.ceil(x))
    return a[lo] if lo==hi else a[lo]*(hi-x)+a[hi]*(x-lo)

result={"schema":"vulkax.refusal_policy_shift_diagnostic","version":1,
 "purpose":"diagnose frozen-gate coverage collapse without selecting replacement thresholds",
 "locked_thresholds":{"evidence_noise_ratio":1.677708079576348,"numerical_fraction":0.02814184971185261},
 "datasets":{}}

for name,rows in datasets.items():
    d={"cases":len(rows),"accept_all_unsafe_rate":sum(unsafe(r) for r in rows)/len(rows),"features":{}}
    for key in ("evidence_noise_ratio","numerical_fraction","scheme_fraction","selected_condition"):
        vals=[f(r,key) for r in rows]
        d["features"][key]={str(q):quantile(vals,q) for q in qs}
    held=[f(r,"repaired_heldout_rms_m")/noise[name] for r in rows]
    d["features"]["heldout_noise_ratio"]={str(q):quantile(held,q) for q in qs}
    et=result["locked_thresholds"]["evidence_noise_ratio"]; nt=result["locked_thresholds"]["numerical_fraction"]
    buckets={"accepted":[],"evidence_only":[],"numerical_only":[],"both":[]}
    for r in rows:
        e=f(r,"evidence_noise_ratio")>et; n=f(r,"numerical_fraction")>nt
        key="both" if e and n else "evidence_only" if e else "numerical_only" if n else "accepted"
        buckets[key].append(r)
    d["rejection_decomposition"]={}
    for key,g in buckets.items():
        d["rejection_decomposition"][key]={
          "count":len(g),"fraction":len(g)/len(rows),
          "unsafe_rate":sum(unsafe(r) for r in g)/len(g) if g else None,
          "median_target_error":statistics.median(f(r,"post_target_relative_error") for r in g) if g else None}
    result["datasets"][name]=d

(out/"shift_diagnostic.json").write_text(json.dumps(result,indent=2)+"\n")
print("VALID refusal-policy shift diagnostic")
for name,d in result["datasets"].items():
    print("SHIFT",name,
      "unsafe_all",d["accept_all_unsafe_rate"],
      "evidence_q10",d["features"]["evidence_noise_ratio"]["0.1"],
      "evidence_q50",d["features"]["evidence_noise_ratio"]["0.5"],
      "numerical_q50",d["features"]["numerical_fraction"]["0.5"],
      "rejections",d["rejection_decomposition"])

#!/usr/bin/env python3
import csv,json,math,pathlib,statistics,sys

if len(sys.argv)<4:
    raise SystemExit("usage: validate_refusal_policy_v3.py calibration.csv validation3.csv out_dir")
cal_path=pathlib.Path(sys.argv[1]); val_path=pathlib.Path(sys.argv[2]); out=pathlib.Path(sys.argv[3]);out.mkdir(parents=True,exist_ok=True)
cal=list(csv.DictReader(cal_path.open())); val=list(csv.DictReader(val_path.open()))
if not cal or not val: raise SystemExit("missing calibration or Validation-3 rows")

def f(r,k): return float(r[k])
def unsafe(r): return f(r,"post_target_relative_error")>0.10
features=("evidence_noise_ratio","heldout_noise_ratio","numerical_convergence_ratio","scheme_fraction")
for rows,name in ((cal,"calibration"),(val,"validation3")):
    for r in rows:
        for k in features+("post_target_relative_error","numerical_fraction","numerical_fine_fraction"):
            if not math.isfinite(f(r,k)): raise SystemExit(f"non-finite {name}/{k}")

def quantile(xs,q):
    a=sorted(xs);x=(len(a)-1)*q;lo=int(math.floor(x));hi=int(math.ceil(x))
    return a[lo] if lo==hi else a[lo]*(hi-x)+a[hi]*(x-lo)

qs=(0.25,0.40,0.50,0.60,0.70,0.80,0.90,1.00)
grid={k:[quantile([f(r,k) for r in cal],q) for q in qs] for k in features}

best=None
for e in grid["evidence_noise_ratio"]:
  for h in grid["heldout_noise_ratio"]:
    for n in grid["numerical_convergence_ratio"]:
      for s in grid["scheme_fraction"]:
        accepted=[r for r in cal if f(r,"evidence_noise_ratio")<=e and f(r,"heldout_noise_ratio")<=h
                  and f(r,"numerical_convergence_ratio")<=n and f(r,"scheme_fraction")<=s]
        if len(accepted)<8: continue
        risk=sum(unsafe(r) for r in accepted)/len(accepted)
        if risk>0.10: continue
        candidate=(len(accepted),-risk,-(e+h+n+s),{"evidence_noise_ratio":e,"heldout_noise_ratio":h,
                    "numerical_convergence_ratio":n,"scheme_fraction":s})
        if best is None or candidate[:3]>best[:3]:best=candidate
if best is None: raise SystemExit("no calibration-only policy reaches <=10% risk with >=8 accepted cases")
thresholds=best[3]

def apply(rows):
    accepted=[r for r in rows if all(f(r,k)<=v for k,v in thresholds.items())]
    return {
      "cases":len(rows),"accepted":len(accepted),"coverage":len(accepted)/len(rows),
      "accept_all_unsafe_rate":sum(unsafe(r) for r in rows)/len(rows),
      "accepted_unsafe_rate":sum(unsafe(r) for r in accepted)/len(accepted) if accepted else None,
      "accepted_mean_target_error":statistics.fmean(f(r,"post_target_relative_error") for r in accepted) if accepted else None,
      "accepted_median_target_error":statistics.median(f(r,"post_target_relative_error") for r in accepted) if accepted else None,
      "accepted_variants":{v:sum(r["variant"]==v for r in accepted) for v in sorted({r["variant"] for r in rows})}
    }

result={
 "schema":"vulkax.refusal_policy_v3",
 "version":1,
 "policy_family":"calibration-only monotone four-gate policy with two-level numerical convergence ratio",
 "selection_data":"original calibration only",
 "explicitly_unused_for_selection":["Validation-1 labels","Validation-2 labels","Validation-3 labels"],
 "thresholds":thresholds,
 "calibration":apply(cal),
 "validation3":apply(val),
 "validation3_success_rule":"accepted unsafe rate <= 0.10 with coverage >= 0.25",
 "validation3_success":False,
 "warning":"Synthetic selective-risk experiment only; not a real-world certificate."
}
v=result["validation3"]
result["validation3_success"]=v["accepted"]>0 and v["accepted_unsafe_rate"]<=0.10 and v["coverage"]>=0.25
(out/"policy_v3.json").write_text(json.dumps(result,indent=2)+"\n")
print("VALID refusal policy V3 evaluation")
print("THRESHOLDS",thresholds)
for name in ("calibration","validation3"):
    d=result[name];print(name,"coverage",d["coverage"],"unsafe_all",d["accept_all_unsafe_rate"],
                         "unsafe_accepted",d["accepted_unsafe_rate"],"variants",d["accepted_variants"])
print("VALIDATION3_SUCCESS",result["validation3_success"])

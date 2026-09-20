#!/usr/bin/env python3
import csv,json,pathlib,sys
cal=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/refusal-repair/cases.csv")
val=pathlib.Path(sys.argv[2] if len(sys.argv)>2 else "build/refusal-validation2/cases.csv")
out=pathlib.Path(sys.argv[3] if len(sys.argv)>3 else "build/refusal-validation2")
C=list(csv.DictReader(cal.open()));V=list(csv.DictReader(val.open()))
def f(r,k):return float(r[k])
def unsafe(r):return f(r,"post_target_relative_error")>.10
def evaluate(rows,et,nt):
    a=[r for r in rows if f(r,"evidence_noise_ratio")<=et and f(r,"numerical_fraction")<=nt]
    return {"accepted":len(a),"coverage":len(a)/len(rows),
            "unsafe_rate":sum(unsafe(r) for r in a)/len(a) if a else None,
            "mean_error":sum(f(r,"post_target_relative_error") for r in a)/len(a) if a else None,
            "variants":{v:sum(r["variant"]==v for r in a) for v in sorted({r["variant"] for r in rows})}}
ets=sorted({f(r,"evidence_noise_ratio") for r in C})+[float("inf")]
nts=sorted({f(r,"numerical_fraction") for r in C})+[float("inf")]
cand=[]
for et in ets:
  for nt in nts:
    p=evaluate(C,et,nt)
    if p["accepted"]>=8 and p["unsafe_rate"] is not None and p["unsafe_rate"]<=.10:
      cand.append((p["accepted"],-p["unsafe_rate"],-et,-nt,et,nt,p))
if not cand:raise SystemExit("no calibration-only policy meets target")
_,_,_,_,et,nt,cp=max(cand)
vp=evaluate(V,et,nt)
fixed=evaluate(V,2.0,.05)
accept_all={"coverage":1.0,"unsafe_rate":sum(unsafe(r) for r in V)/len(V)}
result={"schema":"vulkax.refusal_policy_validation2","version":1,
 "policy_source":"original calibration cases only; Validation-1 and Validation-2 labels excluded from threshold selection",
 "selected_thresholds":{"evidence_noise_ratio_max":et,"numerical_fraction_max":nt},
 "calibration":cp,"validation2":vp,"fixed_validation2":fixed,"accept_all_validation2":accept_all,
 "validation2_domain_changes":["off-grid truth values","different marker IDs","25um noise","different train deformation","new evidence set","new target deformation","new noise phases"],
 "warning":"Validation-2 is synthetic domain-shift evidence, not real-world certification."}
out.mkdir(parents=True,exist_ok=True);(out/"policy_validation2.json").write_text(json.dumps(result,indent=2)+"\n")
print("VALID refusal policy Validation-2")
print("LOCKED",et,nt)
print("VALIDATION2 coverage",vp["coverage"],"unsafe_rate",vp["unsafe_rate"],"mean_error",vp["mean_error"],"variants",vp["variants"])
print("FIXED_VALIDATION2 coverage",fixed["coverage"],"unsafe_rate",fixed["unsafe_rate"],"variants",fixed["variants"])
print("ACCEPT_ALL_VALIDATION2 unsafe_rate",accept_all["unsafe_rate"])

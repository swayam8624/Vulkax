#!/usr/bin/env python3
import csv,json,math,pathlib,sys
cal=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/refusal-repair/cases.csv")
val=pathlib.Path(sys.argv[2] if len(sys.argv)>2 else "build/refusal-validation/cases.csv")
outdir=pathlib.Path(sys.argv[3] if len(sys.argv)>3 else "build/refusal-validation")
C=list(csv.DictReader(cal.open()));V=list(csv.DictReader(val.open()))
def f(r,k):return float(r[k])
def unsafe(r):return f(r,"post_target_relative_error")>.10
def eval_policy(rows,et,nt,ht=None):
    a=[r for r in rows if f(r,"evidence_noise_ratio")<=et and f(r,"numerical_fraction")<=nt and (ht is None or f(r,"repaired_heldout_rms_m")/(2e-5)<=ht)]
    return {"accepted":len(a),"coverage":len(a)/len(rows),"unsafe_rate":sum(unsafe(r) for r in a)/len(a) if a else None,
            "mean_error":sum(f(r,"post_target_relative_error") for r in a)/len(a) if a else None,
            "variants":{v:sum(r["variant"]==v for r in a) for v in sorted({r["variant"] for r in rows})}}
# Calibration-only search. Validation labels are never inspected until after thresholds are locked.
ets=sorted({f(r,"evidence_noise_ratio") for r in C})+[float("inf")]
nts=sorted({f(r,"numerical_fraction") for r in C})+[float("inf")]
candidates=[]
for et in ets:
  for nt in nts:
    p=eval_policy(C,et,nt)
    if p["accepted"]>=8 and p["unsafe_rate"] is not None and p["unsafe_rate"]<=.10:
      candidates.append((p["accepted"],-p["unsafe_rate"],-et,-nt,et,nt,p))
if not candidates:raise SystemExit("no calibration policy reaches <=10% risk with >=8 accepted cases")
_,_,_,_,et,nt,calp=max(candidates)
valp=eval_policy(V,et,nt)
fixed_cal=eval_policy(C,2.0,.05);fixed_val=eval_policy(V,2.0,.05)
all_val={"coverage":1.0,"unsafe_rate":sum(unsafe(r) for r in V)/len(V)}
result={"schema":"vulkax.refusal_policy_external_validation","version":1,
 "calibration_cases":len(C),"validation_cases":len(V),
 "selected_thresholds":{"evidence_noise_ratio_max":et,"numerical_fraction_max":nt},
 "calibration_policy":calp,"validation_policy":valp,
 "fixed_policy":{"thresholds":{"evidence_noise_ratio_max":2.0,"numerical_fraction_max":.05},"calibration":fixed_cal,"validation":fixed_val},
 "validation_accept_all":all_val,
 "warning":"Thresholds selected using calibration cases only. Validation truth is used only for final evaluation."}
(outdir/"policy_validation.json").write_text(json.dumps(result,indent=2)+"\n")
print("VALID refusal policy external validation")
print("LOCKED_THRESHOLDS evidence_noise_ratio",et,"numerical_fraction",nt)
print("CALIBRATION coverage",calp["coverage"],"unsafe_rate",calp["unsafe_rate"],"variants",calp["variants"])
print("VALIDATION coverage",valp["coverage"],"unsafe_rate",valp["unsafe_rate"],"mean_error",valp["mean_error"],"variants",valp["variants"])
print("FIXED_VALIDATION coverage",fixed_val["coverage"],"unsafe_rate",fixed_val["unsafe_rate"],"variants",fixed_val["variants"])
print("ACCEPT_ALL_VALIDATION unsafe_rate",all_val["unsafe_rate"])

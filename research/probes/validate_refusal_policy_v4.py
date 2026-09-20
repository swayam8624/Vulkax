#!/usr/bin/env python3
"""Fresh Validation-4 of a calibration-locked, reason-coded refusal policy.

The model-inadequacy gate is derived ONLY from the original synthetic calibration
set (refusal-repair/cases.csv). Validation-1/2/3/4 labels never select its
feature, direction, or threshold.
"""
import csv,json,pathlib,statistics,sys

if len(sys.argv)<3:
    raise SystemExit("usage: validate_refusal_policy_v4.py calibration_cases.csv validation4_dir")
cal_path=pathlib.Path(sys.argv[1]); root=pathlib.Path(sys.argv[2])
cal=list(csv.DictReader(cal_path.open()))
rows=list(csv.DictReader((root/"cases.csv").open()))
meta=json.loads((root/"summary.json").read_text())
if meta.get("provenance")!="synthetic-validation4" or len(rows)!=int(meta["case_count"]):
    raise SystemExit("bad Validation-4 metadata")

def f(r,k): return float(r[k])
def b(r,k): return int(r[k])!=0

# This task definition was established by the earlier refusal-cause experiment:
# constrained-nu and PIC are deliberately structurally wrong relative to APIC
# truth, whereas fine/coarse APIC preserve the model family.
def model_inadequate(r):
    return r["variant"] in ("constrained_nu","pic")

def metrics_rule(rows,thr,direction):
    tp=tn=fp=fn=0
    for r in rows:
        pred=f(r,"selected_condition")>=thr if direction=="high" else f(r,"selected_condition")<=thr
        y=model_inadequate(r)
        if pred and y: tp+=1
        elif pred and not y: fp+=1
        elif not pred and y: fn+=1
        else: tn+=1
    tpr=tp/(tp+fn) if tp+fn else 0.0
    tnr=tn/(tn+fp) if tn+fp else 0.0
    return {"tp":tp,"tn":tn,"fp":fp,"fn":fn,
            "sensitivity":tpr,"specificity":tnr,
            "balanced_accuracy":0.5*(tpr+tnr)}

# Lock the threshold on calibration only, exactly using balanced accuracy then
# specificity then sensitivity as deterministic tie-breakers.
vals=sorted({f(r,"selected_condition") for r in cal})
candidates=[vals[0]-1e-12]+[(a+b)/2 for a,b in zip(vals,vals[1:])]+[vals[-1]+1e-12]
best=None
for direction in ("high","low"):
    for thr in candidates:
        m=metrics_rule(cal,thr,direction)
        key=(m["balanced_accuracy"],m["specificity"],m["sensitivity"])
        if best is None or key>best[0]:
            best=(key,thr,direction,m)
_,condition_threshold,condition_direction,cal_metrics=best

def condition_flags_model_inadequacy(r):
    x=f(r,"selected_condition")
    return x>=condition_threshold if condition_direction=="high" else x<=condition_threshold

for r in rows:
    core=b(r,"core_accept")
    reason_ok=not condition_flags_model_inadequacy(r)
    r["_reason_accept"]=core and reason_ok
    if not core:
        if f(r,"evidence_noise_ratio")>2.0: reason="evidence_residual"
        elif f(r,"heldout_noise_ratio")>2.0: reason="heldout_residual"
        elif not b(r,"numerical_converged") or f(r,"numerical_fraction_final")>0.05: reason="numerical_nonconvergence"
        else: reason="core_other"
    elif not reason_ok:
        reason="model_inadequacy_conditioning"
    else:
        reason="accepted"
    r["_reason"]=reason

def summarize(pred):
    accepted=[r for r in rows if pred(r)]
    unsafe=[r for r in accepted if not b(r,"post_safe_10pct")]
    return {
      "accepted":len(accepted),
      "coverage":len(accepted)/len(rows),
      "unsafe_rate":len(unsafe)/len(accepted) if accepted else None,
      "mean_error":statistics.fmean(f(r,"post_target_relative_error_refined") for r in accepted) if accepted else None,
      "variants":{v:sum(r["variant"]==v for r in accepted) for v in sorted({r["variant"] for r in rows})}
    }

core=summarize(lambda r:b(r,"core_accept"))
reason=summarize(lambda r:r["_reason_accept"])
result={
 "schema":"vulkax.refusal_validation4_analysis","version":1,
 "provenance":"synthetic-validation4",
 "cases":len(rows),
 "policy_lock":{
   "source":"original synthetic calibration only",
   "feature":"selected_condition",
   "task":"model_family_inadequate=(constrained_nu or pic)",
   "direction":condition_direction,
   "threshold":condition_threshold,
   "calibration_metrics":cal_metrics,
   "base_gates":"evidence<=2x noise; heldout<=2x noise; adaptive dt adjacent-difference<=5% target effect"
 },
 "accept_all_unsafe_rate":sum(not b(r,"post_safe_10pct") for r in rows)/len(rows),
 "core_policy":core,
 "reason_coded_policy":reason,
 "predeclared_success":{"max_unsafe_rate":0.10,"min_coverage":0.25},
 "passes_predeclared_target":(
    reason["coverage"]>=0.25 and
    reason["unsafe_rate"] is not None and reason["unsafe_rate"]<=0.10
 ),
 "refusal_reasons":{q:sum(r["_reason"]==q for r in rows) for q in sorted({r["_reason"] for r in rows})},
 "by_variant":{}
}
for v in sorted({r["variant"] for r in rows}):
    g=[r for r in rows if r["variant"]==v]
    accepted=[r for r in g if r["_reason_accept"]]
    result["by_variant"][v]={
      "cases":len(g),
      "median_post_error":statistics.median(f(r,"post_target_relative_error_refined") for r in g),
      "core_accepted":sum(b(r,"core_accept") for r in g),
      "reason_accepted":len(accepted),
      "reason_unsafe_rate":sum(not b(r,"post_safe_10pct") for r in accepted)/len(accepted) if accepted else None,
      "condition_median":statistics.median(f(r,"selected_condition") for r in g)
    }

(root/"analysis.json").write_text(json.dumps(result,indent=2)+"\n")
print("VALID refusal policy Validation-4 reason-coded fresh test")
print("LOCKED_CONDITION_RULE",condition_direction,condition_threshold,cal_metrics)
print("ACCEPT_ALL unsafe",result["accept_all_unsafe_rate"])
print("CORE4",core)
print("REASON_CODED4",reason)
print("REFUSAL_REASONS",result["refusal_reasons"])
for v,d in result["by_variant"].items(): print("V4_VARIANT",v,d)
print("PREDECLARED_TARGET_PASS",result["passes_predeclared_target"])

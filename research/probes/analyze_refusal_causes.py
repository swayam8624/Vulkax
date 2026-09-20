#!/usr/bin/env python3
import csv,json,math,pathlib,statistics,sys

args=sys.argv[1:]
paths=[pathlib.Path(x) for x in args[:3]] if len(args)>=3 else [
    pathlib.Path("build/refusal-repair/cases.csv"),
    pathlib.Path("build/refusal-validation/cases.csv"),
    pathlib.Path("build/refusal-validation2/cases.csv")]
out=pathlib.Path(args[3] if len(args)>=4 else "build/refusal-cause")
out.mkdir(parents=True,exist_ok=True)
sets={name:list(csv.DictReader(p.open())) for name,p in zip(("calibration","validation1","validation2"),paths)}
noise={"calibration":2e-5,"validation1":2e-5,"validation2":2.5e-5}

def f(r,k): return float(r[k])
def enrich(rows,name):
    z=[]
    for r in rows:
        q=dict(r)
        q["heldout_noise_ratio"]=f(r,"repaired_heldout_rms_m")/noise[name]
        z.append(q)
    return z
sets={k:enrich(v,k) for k,v in sets.items()}

features=("evidence_noise_ratio","numerical_fraction","scheme_fraction","heldout_noise_ratio","selected_condition")
tasks={
 "model_family_inadequate": lambda r: r["variant"] in ("constrained_nu","pic"),
 "transfer_mismatch": lambda r: r["variant"]=="pic",
 "numerical_mismatch_fine_vs_coarse": lambda r: r["variant"]=="coarse_apic",
}

def task_rows(rows,task):
    if task=="numerical_mismatch_fine_vs_coarse":
        rows=[r for r in rows if r["variant"] in ("fine_apic","coarse_apic")]
    return rows

def auc(rows,feature,label_fn):
    pos=[f(r,feature) for r in rows if label_fn(r)]
    neg=[f(r,feature) for r in rows if not label_fn(r)]
    if not pos or not neg:return None
    wins=ties=0
    for p in pos:
        for n in neg:
            if p>n:wins+=1
            elif p==n:ties+=1
    return (wins+.5*ties)/(len(pos)*len(neg))

def metrics(rows,feature,thr,direction,label_fn):
    tp=tn=fp=fn=0
    for r in rows:
        pred=f(r,feature)>=thr if direction=="high" else f(r,feature)<=thr
        y=label_fn(r)
        if pred and y:tp+=1
        elif pred and not y:fp+=1
        elif not pred and y:fn+=1
        else:tn+=1
    tpr=tp/(tp+fn) if tp+fn else None
    tnr=tn/(tn+fp) if tn+fp else None
    bal=.5*(tpr+tnr) if tpr is not None and tnr is not None else None
    prec=tp/(tp+fp) if tp+fp else None
    return {"tp":tp,"tn":tn,"fp":fp,"fn":fn,"sensitivity":tpr,"specificity":tnr,"balanced_accuracy":bal,"precision":prec}

def lock_rule(rows,feature,label_fn):
    vals=sorted({f(r,feature) for r in rows})
    if not vals:return None
    candidates=[vals[0]-1e-12]+[(a+b)/2 for a,b in zip(vals,vals[1:])]+[vals[-1]+1e-12]
    best=None
    for direction in ("high","low"):
        for t in candidates:
            m=metrics(rows,feature,t,direction,label_fn)
            key=(m["balanced_accuracy"],m["specificity"],m["sensitivity"])
            if best is None or key>best[0]:best=(key,t,direction,m)
    return {"threshold":best[1],"direction":best[2],"calibration":best[3]}

result={"schema":"vulkax.refusal_cause_diagnostic","version":1,
 "policy":"feature/rule selection uses synthetic calibration variant labels only; validation labels are evaluation-only",
 "tasks":{}}
for task,label_fn in tasks.items():
    cal=task_rows(sets["calibration"],task)
    entry={"feature_auc":{},"locked_rules":{}}
    ranked=[]
    for feat in features:
        a=auc(cal,feat,label_fn)
        oriented=max(a,1-a) if a is not None else None
        entry["feature_auc"][feat]={"raw_auc":a,"orientation_free_auc":oriented}
        if oriented is not None: ranked.append((oriented,feat))
    ranked.sort(reverse=True)
    entry["ranked_features"]=[{"feature":feat,"orientation_free_auc":score} for score,feat in ranked]
    for feat in features:
        rule=lock_rule(cal,feat,label_fn)
        if rule is None:continue
        for name in ("validation1","validation2"):
            rows=task_rows(sets[name],task)
            rule[name]=metrics(rows,feat,rule["threshold"],rule["direction"],label_fn)
        entry["locked_rules"][feat]=rule
    result["tasks"][task]=entry

(out/"cause_diagnostic.json").write_text(json.dumps(result,indent=2)+"\n")
print("VALID refusal-cause diagnostic")
for task,e in result["tasks"].items():
    top=e["ranked_features"][0]
    r=e["locked_rules"][top["feature"]]
    print("CAUSE",task,"top_feature",top["feature"],"auc",top["orientation_free_auc"],
          "rule",r["direction"],r["threshold"],
          "cal_bal",r["calibration"]["balanced_accuracy"],
          "v1_bal",r["validation1"]["balanced_accuracy"],
          "v2_bal",r["validation2"]["balanced_accuracy"])

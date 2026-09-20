#!/usr/bin/env python3
import csv,json,math,pathlib,statistics,sys
root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/solver-certificate")
rows=list(csv.DictReader((root/"cases.csv").open()))
meta=json.loads((root/"summary.json").read_text())
if meta.get("provenance")!="synthetic": raise SystemExit("bad provenance")
if len(rows)!=int(meta["case_count"]) or len(rows)<50: raise SystemExit("bad case count")
def f(r,k): return float(r[k])
for r in rows:
    if r["provenance"]!="synthetic": raise SystemExit("bad row provenance")
    for k in ("fit_rms_m","heldout_rms_m","sensitivity_smin","condition_number","intervention_distance",
              "numerical_disagreement_m","scheme_disagreement_m","candidate_effect_m","numerical_fraction",
              "scheme_fraction","counterfactual_rms_m","counterfactual_relative_error"):
        if not math.isfinite(f(r,k)): raise SystemExit(f"non-finite {k}: {r}")
labels=[int(r["unsafe_10pct"]) for r in rows]
npos=sum(labels);nneg=len(labels)-npos
if not npos or not nneg: raise SystemExit(f"need safe and unsafe cases: {nneg}/{npos}")
def auc(scores):
    pos=[s for s,y in zip(scores,labels) if y];neg=[s for s,y in zip(scores,labels) if not y]
    wins=ties=tot=0
    for p in pos:
        for n in neg:
            tot+=1
            if p>n:wins+=1
            elif p==n:ties+=1
    return (wins+0.5*ties)/tot
features={
 "fit_rms":[f(r,"fit_rms_m") for r in rows],
 "heldout_rms":[f(r,"heldout_rms_m") for r in rows],
 "condition_number":[math.log10(max(f(r,"condition_number"),1.0)) for r in rows],
 "inverse_smin":[-math.log10(max(f(r,"sensitivity_smin"),1e-30)) for r in rows],
 "intervention_distance":[f(r,"intervention_distance") for r in rows],
 "numerical_fraction":[f(r,"numerical_fraction") for r in rows],
 "scheme_fraction":[f(r,"scheme_fraction") for r in rows],
}
def ranks(v):
    order=sorted(range(len(v)),key=lambda i:(v[i],i));r=[0.0]*len(v)
    for j,i in enumerate(order):r[i]=j/max(len(v)-1,1)
    return r
rr={k:ranks(v) for k,v in features.items()}
features["untrained_rank_composite"]=[
 (rr["heldout_rms"][i]+rr["inverse_smin"][i]+rr["numerical_fraction"][i]+rr["scheme_fraction"][i]+rr["intervention_distance"][i])/5
 for i in range(len(rows))
]
def curve(scores):
    order=sorted(range(len(scores)),key=lambda i:scores[i]);out=[]
    for cov in (.25,.5,.75,1.):
        n=max(1,round(cov*len(order)));ids=order[:n]
        out.append({"coverage":cov,"accepted":len(ids),"unsafe_rate":sum(labels[i] for i in ids)/len(ids),
                    "mean_counterfactual_relative_error":statistics.fmean(f(rows[i],"counterfactual_relative_error") for i in ids)})
    return out
metrics={k:{"roc_auc":auc(v),"risk_coverage":curve(v)} for k,v in features.items()}
by_variant={}
for v in sorted({r["variant"] for r in rows}):
    g=[r for r in rows if r["variant"]==v]
    by_variant[v]={"cases":len(g),"unsafe_rate":sum(int(r["unsafe_10pct"]) for r in g)/len(g),
                   "mean_counterfactual_relative_error":statistics.fmean(f(r,"counterfactual_relative_error") for r in g),
                   "mean_heldout_rms_m":statistics.fmean(f(r,"heldout_rms_m") for r in g)}
analysis={"schema":"vulkax.counterfactual_certificate_analysis","version":1,"provenance":"synthetic",
          "case_count":len(rows),"safe_cases":nneg,"unsafe_cases":npos,"unsafe_rate":npos/len(rows),
          "feature_metrics":metrics,"by_variant":by_variant,
          "warning":"Exploratory diagnostic only; no feature or composite was trained on labels."}
(root/"analysis.json").write_text(json.dumps(analysis,indent=2)+"\n")
with (root/"feature_summary.csv").open("w",newline="") as fh:
    w=csv.writer(fh);w.writerow(["feature","roc_auc","risk_at_25pct","risk_at_50pct","risk_at_75pct","risk_at_100pct"])
    for name,m in sorted(metrics.items(),key=lambda kv:kv[1]["roc_auc"],reverse=True):
        rc=m["risk_coverage"];w.writerow([name,m["roc_auc"],rc[0]["unsafe_rate"],rc[1]["unsafe_rate"],rc[2]["unsafe_rate"],rc[3]["unsafe_rate"]])
print("VALID Vulkax solver certificate dataset")
print("cases",len(rows),"safe",nneg,"unsafe",npos,"unsafe_rate",npos/len(rows))
for name,m in sorted(metrics.items(),key=lambda kv:kv[1]["roc_auc"],reverse=True):
    print("AUC",name,f'{m["roc_auc"]:.6f}',"risk@50",f'{m["risk_coverage"][1]["unsafe_rate"]:.6f}')
for name,d in by_variant.items():
    print("VARIANT",name,"unsafe_rate",f'{d["unsafe_rate"]:.6f}',"mean_cf_rel",f'{d["mean_counterfactual_relative_error"]:.6f}')

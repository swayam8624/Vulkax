#!/usr/bin/env python3
"""Diagnose whether refusal evidence identifies the planted failure family.

This is a synthetic mechanism probe, not a real-world causal attribution claim.
The classifier is fit on the original calibration domain only and evaluated
unchanged on Validation-1/2/3.
"""
import csv,json,math,pathlib,statistics,sys

if len(sys.argv) < 6:
    raise SystemExit(
        "usage: diagnose_refusal_causes.py calibration.csv validation1.csv "
        "validation2.csv validation3.csv out_dir"
    )

paths={
    "calibration":pathlib.Path(sys.argv[1]),
    "validation1":pathlib.Path(sys.argv[2]),
    "validation2":pathlib.Path(sys.argv[3]),
    "validation3":pathlib.Path(sys.argv[4]),
}
out=pathlib.Path(sys.argv[5]); out.mkdir(parents=True,exist_ok=True)
noise={"calibration":2.0e-5,"validation1":2.0e-5,"validation2":2.5e-5,"validation3":2.2e-5}
variants=("fine_apic","coarse_apic","pic","constrained_nu")
actions={
    "fine_apic":"request_more_evidence",
    "coarse_apic":"refine_numerics",
    "pic":"reject_transfer_or_model_family",
    "constrained_nu":"expand_parameter_or_constitutive_family",
}
feature_names=("evidence_noise_ratio","heldout_noise_ratio","numerical_fraction","scheme_fraction")

def read(name,path):
    rows=list(csv.DictReader(path.open()))
    if not rows: raise SystemExit(f"empty dataset: {name}")
    for r in rows:
        if r["variant"] not in variants: raise SystemExit(f"unknown variant {r['variant']}")
        if "heldout_noise_ratio" not in r or r["heldout_noise_ratio"]=="":
            r["heldout_noise_ratio"]=str(float(r["repaired_heldout_rms_m"])/noise[name])
        for k in feature_names+("post_target_relative_error",):
            v=float(r[k])
            if not math.isfinite(v) or v<0.0:
                raise SystemExit(f"bad {name}/{k}: {r[k]}")
    return rows

datasets={name:read(name,path) for name,path in paths.items()}
cal=datasets["calibration"]

def transform(r):
    # Log compression prevents one very large residual family from dominating.
    return [math.log1p(float(r[k])) for k in feature_names]

cal_x=[transform(r) for r in cal]
means=[statistics.fmean(x[j] for x in cal_x) for j in range(len(feature_names))]
scales=[]
for j in range(len(feature_names)):
    sd=statistics.stdev(x[j] for x in cal_x)
    scales.append(max(sd,1.0e-12))

def z(r):
    x=transform(r)
    return [(x[j]-means[j])/scales[j] for j in range(len(feature_names))]

centroids={}
for v in variants:
    xs=[z(r) for r in cal if r["variant"]==v]
    if not xs: raise SystemExit(f"missing calibration variant {v}")
    centroids[v]=[statistics.fmean(x[j] for x in xs) for j in range(len(feature_names))]

def predict(r,exclude_index=None):
    q=z(r)
    # External validation always uses the frozen calibration centroids.
    return min(variants,key=lambda v:sum((q[j]-centroids[v][j])**2 for j in range(len(q))))

def summarize(name,rows):
    confusion={truth:{pred:0 for pred in variants} for truth in variants}
    unsafe_confusion={truth:{pred:0 for pred in variants} for truth in variants}
    correct=0; unsafe_correct=0; unsafe_n=0
    for r in rows:
        truth=r["variant"]; pred=predict(r)
        confusion[truth][pred]+=1
        correct+=pred==truth
        if float(r["post_target_relative_error"])>0.10:
            unsafe_n+=1
            unsafe_confusion[truth][pred]+=1
            unsafe_correct+=pred==truth
    recall={}
    for v in variants:
        n=sum(confusion[v].values())
        recall[v]=confusion[v][v]/n if n else None
    medians={}
    for v in variants:
        group=[r for r in rows if r["variant"]==v]
        medians[v]={k:statistics.median(float(r[k]) for r in group) for k in feature_names}
        medians[v]["median_post_target_relative_error"]=statistics.median(float(r["post_target_relative_error"]) for r in group)
        medians[v]["unsafe_rate"]=sum(float(r["post_target_relative_error"])>0.10 for r in group)/len(group)
    return {
        "cases":len(rows),
        "cause_accuracy":correct/len(rows),
        "unsafe_cases":unsafe_n,
        "unsafe_cause_accuracy":unsafe_correct/unsafe_n if unsafe_n else None,
        "per_variant_recall":recall,
        "confusion":confusion,
        "unsafe_confusion":unsafe_confusion,
        "feature_medians_by_variant":medians,
    }

result={
    "schema":"vulkax.refusal_cause_diagnostic",
    "version":1,
    "provenance":"synthetic planted-failure-family diagnostic",
    "classifier":"calibration-only nearest centroid in standardized log evidence space",
    "features":list(feature_names),
    "standardization":{"mean":dict(zip(feature_names,means)),"scale":dict(zip(feature_names,scales))},
    "centroids":centroids,
    "planted_variant_to_remediation":actions,
    "datasets":{name:summarize(name,rows) for name,rows in datasets.items()},
    "warning":"Variant labels encode intentionally planted synthetic mismatch families. Classification accuracy does not establish real-world causal diagnosis or novelty.",
}
(out/"refusal_cause_diagnostic.json").write_text(json.dumps(result,indent=2)+"\n")
print("VALID refusal-cause diagnostic")
for name,d in result["datasets"].items():
    print("CAUSE",name,
          "accuracy",f'{d["cause_accuracy"]:.4f}',
          "unsafe_accuracy",("none" if d["unsafe_cause_accuracy"] is None else f'{d["unsafe_cause_accuracy"]:.4f}'),
          "recall",d["per_variant_recall"])

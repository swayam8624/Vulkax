#!/usr/bin/env python3
"""Cluster-correct statistics for the locked IRIS pendulum final test.

The physical resampling unit is the video/scene, never the 220 record rows.
This is post-final descriptive inference and cannot change the frozen decisions.
"""
from __future__ import annotations
import argparse,csv,json,math,random,statistics
from collections import defaultdict
from pathlib import Path

PRIMARY="finite_amplitude_period_probe"
BASELINE="small_angle_period_baseline"

def rows(path):
    with path.open(newline="",encoding="utf-8") as f:return list(csv.DictReader(f))

def pct(xs,p):
    q=sorted(xs)
    if not q:return None
    x=(len(q)-1)*p
    a=int(math.floor(x));b=int(math.ceil(x))
    if a==b:return q[a]
    t=x-a
    return q[a]*(1-t)+q[b]*t

def exact_sign_p(pos,neg):
    n=pos+neg
    if n==0:return 1.0
    k=min(pos,neg)
    s=sum(math.comb(n,i) for i in range(k+1))
    return min(1.0,2.0*s/(2**n))

def summarize(scene_rows,method,truth=None):
    q=[r for r in scene_rows if r["method"]==method and (truth is None or r["ground_truth"]==truth)]
    if not q:return None
    correct=sum(r["decision"]==r["ground_truth"] for r in q)
    return correct/len(q)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--records",type=Path,default=Path("build/publication-validation/iris-pendulum-final-test/validation_records.csv"))
    ap.add_argument("--out",type=Path,default=Path("build/publication-validation/iris-pendulum-final-test/clustered_statistics"))
    ap.add_argument("--bootstrap-reps",type=int,default=10000)
    ap.add_argument("--seed",type=int,default=20260923)
    ap.add_argument("--self-test",action="store_true")
    a=ap.parse_args()
    if a.self_test:
        assert abs(pct([0,1],0.5)-0.5)<1e-12
        assert abs(exact_sign_p(10,0)-2/1024)<1e-12
        print("VALID clustered IRIS statistics self-test");return
    rr=rows(a.records)
    if not rr or any(r["split"]!="final_test" for r in rr):
        raise SystemExit("expected non-empty final_test records")
    by_scene=defaultdict(list)
    for r in rr:by_scene[r["scene"]].append(r)
    scenes=sorted(by_scene)
    if len(scenes)!=10:
        raise SystemExit(f"expected 10 physical video clusters, got {len(scenes)}")
    scene_table=[]
    for s in scenes:
        row={"scene":s}
        for m,label in ((PRIMARY,"primary"),(BASELINE,"baseline")):
            row[f"{label}_accuracy"]=summarize(by_scene[s],m)
            for t in ("support","veto","unresolved"):
                row[f"{label}_{t}_accuracy"]=summarize(by_scene[s],m,t)
        row["accuracy_difference"]=row["primary_accuracy"]-row["baseline_accuracy"]
        scene_table.append(row)
    a.out.mkdir(parents=True,exist_ok=True)
    with (a.out/"per_video.csv").open("w",newline="",encoding="utf-8") as f:
        w=csv.DictWriter(f,fieldnames=list(scene_table[0]));w.writeheader();w.writerows(scene_table)
    rng=random.Random(a.seed)
    metrics=["primary_accuracy","baseline_accuracy","accuracy_difference"]
    for t in ("support","veto","unresolved"):
        metrics += [f"primary_{t}_accuracy",f"baseline_{t}_accuracy"]
    boot={k:[] for k in metrics}
    for _ in range(a.bootstrap_reps):
        sample=[scene_table[rng.randrange(len(scene_table))] for __ in scenes]
        for k in metrics:
            vals=[x[k] for x in sample if x[k] is not None]
            boot[k].append(statistics.fmean(vals))
    summary={}
    for k,vals in boot.items():
        observed=statistics.fmean([x[k] for x in scene_table if x[k] is not None])
        summary[k]={"estimate":observed,"ci95":[pct(vals,0.025),pct(vals,0.975)]}
    pos=sum(x["accuracy_difference"]>0 for x in scene_table)
    neg=sum(x["accuracy_difference"]<0 for x in scene_table)
    ties=len(scene_table)-pos-neg
    summary["paired_video_sign_test"]={
        "primary_better_videos":pos,"baseline_better_videos":neg,"ties":ties,
        "two_sided_exact_p":exact_sign_p(pos,neg),
        "unit":"physical video"
    }
    summary["record_count"]=len(rr)
    summary["physical_video_count"]=len(scenes)
    summary["bootstrap_replicates"]=a.bootstrap_reps
    summary["score_language"]="The frozen record column is named score; paper prose should call it a standardized evidence score, not assume a standard-normal z statistic."
    (a.out/"summary.json").write_text(json.dumps(summary,indent=2)+"\n",encoding="utf-8")
    print("VALID cluster-correct IRIS final statistics")
    print("PHYSICAL_VIDEO_UNITS",len(scenes))
    print("PRIMARY_ACCURACY",summary["primary_accuracy"])
    print("BASELINE_ACCURACY",summary["baseline_accuracy"])
    print("DIFFERENCE",summary["accuracy_difference"])
    print("VIDEO_SIGN_TEST",summary["paired_video_sign_test"])
    print("OUT",a.out)

if __name__=="__main__":main()

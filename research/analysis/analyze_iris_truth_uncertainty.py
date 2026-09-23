#!/usr/bin/env python3
"""Sensitivity of locked IRIS truth labels to measured rope-length uncertainty.

Uses adapter manifests containing independently measured mean/std metadata. It does
not change frozen decisions or relabel the canonical result; it reports whether the
support/veto/unresolved truth assignment would remain stable across plausible truth
values.
"""
from __future__ import annotations
import argparse,csv,json,math,pathlib,re,statistics
FACTOR=re.compile(r"candidate_factor=([0-9.]+)")

def read(p):
    with p.open(newline="",encoding="utf-8") as f:return list(csv.DictReader(f))
def manifests(root):
    out={}
    for p in (root/"iris").glob("*/manifest.json"):
        m=json.loads(p.read_text());out[m["scene"]]=m
    return out
def label(L0,L1,Lt):
    a=abs(math.log(L0/Lt));b=abs(math.log(L1/Lt))
    return "support" if b+1e-12<a else ("veto" if b>a+1e-12 else "unresolved")
def param(m,key):
    e=m["ground_truth"]["parameters"][key]
    return float(e["mean"]),float(e.get("std",0.0) or 0.0)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--records",type=pathlib.Path,default=pathlib.Path("build/publication-validation/iris-pendulum-final-test/validation_records.csv"))
    ap.add_argument("--adapted-root",type=pathlib.Path,default=pathlib.Path("build/publication-validation/public-data/adapted"))
    ap.add_argument("--out",type=pathlib.Path,default=pathlib.Path("build/publication-validation/iris-pendulum-postfinal-truth-uncertainty"))
    ap.add_argument("--self-test",action="store_true")
    a=ap.parse_args()
    if a.self_test:
        assert label(1.25,1.0,1.0)=="support";print("VALID truth-uncertainty self-test");return
    rr=read(a.records);mm=manifests(a.adapted_root);rows=[]
    # one row per paired case, primary only
    seen=set()
    for r in rr:
        if r["method"]!="finite_amplitude_period_probe" or r["paired_key"] in seen:continue
        seen.add(r["paired_key"]);m=mm[r["scene"]]
        mean,std=param(m,"rope_length")
        fm=FACTOR.search(r["notes"])
        if not fm:raise ValueError("candidate factor missing")
        fac=float(fm.group(1));L0=1.25*mean;L1=fac*mean
        canonical=r["ground_truth"]
        for k in (1.0,2.0):
            if std<=0:
                labels={label(L0,L1,mean)}
            else:
                lo=max(1e-9,mean-k*std);hi=mean+k*std
                labels={label(L0,L1,lo+(hi-lo)*i/200) for i in range(201)}
            rows.append({"paired_key":r["paired_key"],"scene":r["scene"],"candidate_factor":fac,
                         "canonical_truth":canonical,"sigma_multiplier":k,"mean_m":mean,"std_m":std,
                         "labels_across_interval":";".join(sorted(labels)),
                         "canonical_label_stable":str(labels=={canonical}).lower()})
    a.out.mkdir(parents=True,exist_ok=True)
    with (a.out/"cases.csv").open("w",newline="",encoding="utf-8") as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
    summary=[]
    for k in (1.0,2.0):
        q=[r for r in rows if r["sigma_multiplier"]==k]
        summary.append({"sigma_multiplier":k,"cases":len(q),
                        "label_stable":sum(r["canonical_label_stable"]=="true" for r in q),
                        "label_stable_rate":sum(r["canonical_label_stable"]=="true" for r in q)/len(q)})
    (a.out/"summary.json").write_text(json.dumps({"schema":"vulkax.iris_truth_uncertainty","version":1,
      "results":summary,"claim_guard":"Sensitivity only; canonical final labels remain frozen."},indent=2)+"\n")
    print("VALID IRIS measured-truth uncertainty sensitivity")
    for s in summary:print(s)
    print("OUT",a.out)
if __name__=="__main__":main()

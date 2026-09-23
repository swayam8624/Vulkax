#!/usr/bin/env python3
"""Post-final proposal/probe overlap-dependence sweep on IRIS pendulum.

The proposal is GT-hidden and fixed from the first 40% of each video. The
verification window is 60% long and slides to create overlap rho in
{0,.25,.5,.75,1}. Ground truth is used only after proposal creation to score the
decision. This study diagnoses dependence; it does not alter the locked result.
"""
from __future__ import annotations
import argparse,csv,json,math,pathlib,statistics,sys
import numpy as np
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"research"/"analysis"))
from run_iris_pendulum_validation import score_candidate,decision  # noqa:E402
G=9.80665

def read(p):
    with p.open(newline="",encoding="utf-8") as f:return list(csv.DictReader(f))
def safe(s):return s.replace("/","__").replace(" ","_")
def seg(rr,lo,hi):
    n=len(rr);a=int(lo*n);b=int(hi*n);q=rr[a:max(a+4,b)]
    return np.asarray([float(x["time_s"]) for x in q]),np.asarray([float(x["motion_x_px"]) for x in q])
def period(t,x):
    if len(x)<60:raise ValueError("segment too short")
    c=np.polyfit(t,x,1);y=x-(c[0]*t+c[1]);y-=np.mean(y)
    spec=np.abs(np.fft.rfft(y*np.hanning(len(y))))**2
    freq=np.fft.rfftfreq(len(y),d=float(np.median(np.diff(t))))
    ids=np.flatnonzero((freq>=.25)&(freq<=2))
    i=int(ids[np.argmax(spec[ids])]);pf=float(freq[i]);p0=1/pf
    cyc=[]
    for up in (True,False):
        q=[]
        for j in range(1,len(y)):
            a,b=y[j-1],y[j]
            hit=(a<=0<b) if up else (a>=0>b)
            if hit:
                f=(-a/(b-a)) if abs(b-a)>1e-12 else 0
                q.append(float(t[j-1]+f*(t[j]-t[j-1])))
        cyc += [b-a for a,b in zip(q,q[1:]) if .65*p0<=b-a<=1.35*p0]
    return (statistics.median(cyc) if cyc else p0),(cyc if cyc else [p0])
def Lsmall(T):return G*(T/(2*math.pi))**2

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--campaign",type=pathlib.Path,default=pathlib.Path("build/publication-validation/iris-pendulum-final-test"))
    ap.add_argument("--out",type=pathlib.Path,default=pathlib.Path("build/publication-validation/iris-pendulum-postfinal-dependence"))
    ap.add_argument("--self-test",action="store_true")
    a=ap.parse_args()
    if a.self_test:
        assert abs(Lsmall(2*math.pi*math.sqrt(.5/G))-.5)<1e-12
        print("VALID IRIS dependence sweep self-test");return
    takes=read(a.campaign/"take_summary.csv");records=[]
    for take in takes:
        scene=take["scene"];rr=read(a.campaign/safe(scene)/"motion_signal.csv")
        t0,x0=seg(rr,0,.20);t1,x1=seg(rr,0,.40)
        pb,_=period(t0,x0);pc,_=period(t1,x1);L0=Lsmall(pb);L1=Lsmall(pc)
        Ltrue=float(take["true_length_m"]);angle=float(take["angle_deg"])
        d0=abs(math.log(L0/Ltrue));d1=abs(math.log(L1/Ltrue))
        truth="support" if d1+1e-9<d0 else ("veto" if d1>d0+1e-9 else "unresolved")
        fps=1/float(np.median(np.diff(np.asarray([float(r["time_s"]) for r in rr]))))
        for rho in (0,.25,.5,.75,1.0):
            start=.40*(1-rho);end=start+.60
            tv,xv=seg(rr,start,end);_,cyc=period(tv,xv)
            z,_,_,_=score_candidate(cyc,fps,angle,L0,L1,finite_amplitude=True)
            d=decision(z)
            records.append({"scene":scene,"rho":rho,"verification_start":start,"verification_end":end,
                            "ground_truth":truth,"score":z,"decision":d,"correct":str(d==truth).lower(),
                            "baseline_length_m":L0,"candidate_length_m":L1})
    a.out.mkdir(parents=True,exist_ok=True)
    with (a.out/"records.csv").open("w",newline="",encoding="utf-8") as f:
        w=csv.DictWriter(f,fieldnames=list(records[0]));w.writeheader();w.writerows(records)
    summary=[]
    for rho in (0,.25,.5,.75,1.0):
        q=[r for r in records if r["rho"]==rho]
        summary.append({"rho":rho,"n":len(q),"strict_accuracy":sum(r["correct"]=="true" for r in q)/len(q),
                        "coverage":sum(r["decision"]!="unresolved" for r in q)/len(q),
                        "median_abs_score":statistics.median(abs(float(r["score"])) for r in q)})
    with (a.out/"summary.csv").open("w",newline="",encoding="utf-8") as f:
        w=csv.DictWriter(f,fieldnames=list(summary[0]));w.writeheader();w.writerows(summary)
    (a.out/"summary.json").write_text(json.dumps({"schema":"vulkax.iris_channel_dependence","version":1,
      "physical_videos":len(takes),"results":summary,
      "claim_guard":"Post-final dependence diagnostic; proposal window and frozen score threshold are unchanged."},indent=2)+"\n")
    print("VALID IRIS proposal/probe dependence sweep")
    for r in summary:print(r)
    print("OUT",a.out)
if __name__=="__main__":main()

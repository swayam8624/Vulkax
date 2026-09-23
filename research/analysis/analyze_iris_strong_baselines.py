#!/usr/bin/env python3
"""Strong post-final verification baselines for the locked IRIS pendulum cases.

Adds two non-straw-man comparators without modifying the locked result:
1) direct finite-amplitude median-period residual;
2) damped nonlinear pendulum ODE residual with damping fitted only on an early
   nuisance-fit segment and evaluated on a disjoint later segment.

The existing locked finite-amplitude method is loaded only for paired reference.
"""
from __future__ import annotations
import argparse,csv,json,math,re,statistics
from collections import defaultdict
from pathlib import Path
import numpy as np

G=9.80665
TAU=2.0
FACTOR=re.compile(r"candidate_factor=([0-9.]+)")
PRIMARY="finite_amplitude_period_probe"

def read_csv(p):
    with p.open(newline="",encoding="utf-8") as f:return list(csv.DictReader(f))
def safe(s):return s.replace("/","__").replace(" ","_")
def robust(x):
    if not x:return 0.0
    m=statistics.median(x);return 1.4826*statistics.median(abs(v-m) for v in x)
def dec(z):
    return "support" if z>=TAU else ("veto" if z<=-TAU else "unresolved")
def K(k):
    a=1.0;b=math.sqrt(max(0,1-k*k))
    for _ in range(80):
        an=(a+b)/2;bn=math.sqrt(a*b);a,b=an,bn
        if abs(a-b)<=1e-15:maxv=max(1,abs(a));break
    return math.pi/(2*a)
def period(L,angle):
    return 4*math.sqrt(L/G)*K(math.sin(math.radians(abs(angle))/2))

def motion_theta(path,theta0):
    rr=read_csv(path)
    t=np.asarray([float(r["time_s"]) for r in rr])
    x=np.asarray([float(r["motion_x_px"]) for r in rr])
    coef=np.polyfit(t,x,1);x=x-(coef[0]*t+coef[1])
    x=np.convolve(x,np.ones(5)/5,mode="same")
    amp=float(np.percentile(np.abs(x),95))
    if amp<=1e-6:raise ValueError("motion amplitude too small")
    s=np.clip(x/amp,-.999,.999)*math.sin(math.radians(abs(theta0)))
    th=np.arcsin(np.clip(s,-.999,.999))
    return t,th

def fit_beta(t,th,L,stop):
    n=max(10,int(len(t)*stop));tt=t[:n];q=th[:n]
    d=np.gradient(q,tt);dd=np.gradient(d,tt)
    r0=dd+(G/L)*np.sin(q)
    den=float(np.dot(d,d))
    beta=0.0 if den<=1e-12 else -float(np.dot(d,r0))/den
    return min(5.0,max(0.0,beta))

def cycle_means(t,th,vals,start_frac):
    i0=int(len(t)*start_frac);tt=t[i0:];q=th[i0:];v=np.asarray(vals)[i0:]
    cross=[]
    for i in range(1,len(q)):
        if q[i-1]<=0<q[i]:
            den=q[i]-q[i-1]
            frac=(-q[i-1]/den) if abs(den)>1e-12 else 0
            cross.append(i-1+frac)
    out=[]
    for a,b in zip(cross,cross[1:]):
        ia=max(0,int(math.ceil(a)));ib=min(len(v),int(math.floor(b))+1)
        if ib-ia>=3:out.append(float(np.mean(v[ia:ib])))
    return out

def damped_score(path,angle,L0,L1):
    t,th=motion_theta(path,angle)
    b0=fit_beta(t,th,L0,.40);b1=fit_beta(t,th,L1,.40)
    d=np.gradient(th,t);dd=np.gradient(d,t)
    r0=dd+b0*d+(G/L0)*np.sin(th)
    r1=dd+b1*d+(G/L1)*np.sin(th)
    imp=np.abs(r0)-np.abs(r1)
    cyc=cycle_means(t,th,imp,.40)
    if len(cyc)<2:return 0.0,b0,b1,len(cyc)
    se=max(robust(cyc)/math.sqrt(len(cyc)),1e-4)
    return statistics.fmean(cyc)/se,b0,b1,len(cyc)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--campaign",type=Path,default=Path("build/publication-validation/iris-pendulum-final-test"))
    ap.add_argument("--out",type=Path,default=Path("build/publication-validation/iris-pendulum-postfinal-baselines"))
    ap.add_argument("--self-test",action="store_true")
    a=ap.parse_args()
    if a.self_test:
        assert period(.5,90)>2*math.pi*math.sqrt(.5/G)
        print("VALID strong IRIS baseline self-test");return
    records=read_csv(a.campaign/"validation_records.csv")
    takes={r["scene"]:r for r in read_csv(a.campaign/"take_summary.csv")}
    prim={(r["paired_key"]):r for r in records if r["method"]==PRIMARY}
    cases={}
    for r in records:
        if r["method"]!=PRIMARY:continue
        m=FACTOR.search(r["notes"])
        if not m:raise ValueError("candidate_factor missing")
        scene=r["scene"];truth=r["ground_truth"];fac=float(m.group(1))
        key=r["paired_key"];cases[key]=(scene,truth,fac)
    out=[]
    for key,(scene,truth,fac) in sorted(cases.items()):
        take=takes[scene];Ltrue=float(take["true_length_m"]);angle=float(take["angle_deg"])
        L0=1.25*Ltrue;L1=fac*Ltrue
        Tobs=float(take["observed_period_s"]);sig=float(take["period_sigma_s"])
        fps=60.0
        zperiod=(abs(Tobs-period(L0,angle))-abs(Tobs-period(L1,angle)))/max(sig,1/fps)
        zode,b0,b1,ncyc=damped_score(a.campaign/safe(scene)/"motion_signal.csv",angle,L0,L1)
        for method,z,notes in [
          ("direct_finite_amplitude_period_residual",zperiod,f"period_sigma={sig}"),
          ("damped_nonlinear_ode_residual",zode,f"beta_base={b0}; beta_candidate={b1}; cycles={ncyc}")
        ]:
            out.append({"paired_key":key,"scene":scene,"truth":truth,"candidate_factor":fac,
                        "method":method,"standardized_evidence_score":z,"decision":dec(z),
                        "correct":str(dec(z)==truth).lower(),"notes":notes})
    a.out.mkdir(parents=True,exist_ok=True)
    with (a.out/"records.csv").open("w",newline="",encoding="utf-8") as f:
        w=csv.DictWriter(f,fieldnames=list(out[0]));w.writeheader();w.writerows(out)
    summary={}
    for method in sorted({r["method"] for r in out}):
        q=[r for r in out if r["method"]==method]
        summary[method]={
          "n":len(q),"strict_accuracy":sum(r["correct"]=="true" for r in q)/len(q),
          "support_accuracy":sum(r["correct"]=="true" for r in q if r["truth"]=="support")/sum(r["truth"]=="support" for r in q),
          "veto_accuracy":sum(r["correct"]=="true" for r in q if r["truth"]=="veto")/sum(r["truth"]=="veto" for r in q),
          "placebo_accuracy":sum(r["correct"]=="true" for r in q if r["truth"]=="unresolved")/sum(r["truth"]=="unresolved" for r in q)
        }
    # Paired only-correct counts against locked primary.
    for method in list(summary):
        q=[r for r in out if r["method"]==method]
        wins=losses=ties=0
        for r in q:
            p=prim[r["paired_key"]];pc=p["decision"]==p["ground_truth"];bc=r["correct"]=="true"
            if pc and not bc:wins+=1
            elif bc and not pc:losses+=1
            else:ties+=1
        summary[method]["locked_primary_only_correct"]=wins
        summary[method]["baseline_only_correct"]=losses
        summary[method]["ties"]=ties
    report={"schema":"vulkax.iris_postfinal_strong_baselines","version":1,
            "physical_video_units":len(takes),"cases_per_method":len(cases),
            "locked_primary":{"strict_accuracy":90/110,"post_final_recomputed":False},
            "baselines":summary,
            "claim_guard":"Post-final comparators. They do not alter or replace the locked confirmatory result."}
    (a.out/"summary.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print("VALID IRIS post-final strong baselines")
    for k,v in summary.items():print(k,v)
    print("OUT",a.out)
if __name__=="__main__":main()

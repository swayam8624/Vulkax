#!/usr/bin/env python3
"""GT-hidden proposal->verification experiment on IRIS pendulum videos.

Proposal generation never reads true rope length:
  baseline: estimate from first 20% of motion
  candidate repair: estimate from first 40%
  ordinary acceptance: candidate must improve small-angle period residual on 40-50%
Only after proposals are hashed/written is ground truth joined for evaluation.
Reality Probe verification uses the disjoint final 50% with the frozen
finite-amplitude period model and |score|=2 rule.

This is a post-final experiment; it never replaces the locked final result.
"""
from __future__ import annotations
import argparse,csv,hashlib,json,math
from pathlib import Path
import statistics
import sys
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"research"/"analysis"))
from run_iris_pendulum_validation import predicted_period,score_candidate,decision  # noqa:E402

G=9.80665

def read_csv(p):
    with p.open(newline="",encoding="utf-8") as f:return list(csv.DictReader(f))

def safe_scene(s):return s.replace("/","__").replace(" ","_")

def estimate_period(times,values,fps):
    t=np.asarray(times,float);x=np.asarray(values,float)
    if len(x)<int(max(60,2*fps)): raise ValueError("segment too short")
    coef=np.polyfit(t,x,1);x=x-(coef[0]*t+coef[1]);x-=np.mean(x)
    spec=np.abs(np.fft.rfft(x*np.hanning(len(x))))**2
    freqs=np.fft.rfftfreq(len(x),d=1/fps)
    band=(freqs>=0.25)&(freqs<=2.0)
    ids=np.flatnonzero(band)
    peak=int(ids[np.argmax(spec[band])]);pf=float(freqs[peak])
    if pf<=0:raise ValueError("invalid period frequency")
    pfft=1/pf
    crossings=[]
    for up in (True,False):
        q=[]
        for i in range(1,len(x)):
            a,b=x[i-1],x[i]
            hit=(a<=0<b) if up else (a>=0>b)
            if hit:
                frac=(-a/(b-a)) if abs(b-a)>1e-12 else 0
                q.append(float(t[i-1]+frac*(t[i]-t[i-1])))
        crossings += [b-a for a,b in zip(q,q[1:]) if .65*pfft<=b-a<=1.35*pfft]
    if len(crossings)>=2:
        return float(statistics.median(crossings)),crossings
    return pfft,[pfft]

def segment(rows,lo,hi):
    n=len(rows);a=int(math.floor(lo*n));b=int(math.floor(hi*n))
    q=rows[a:max(a+2,b)]
    return [float(r["time_s"]) for r in q],[float(r["motion_x_px"]) for r in q]

def small_angle_length(period):return G*(period/(2*math.pi))**2

def sha256(p):
    h=hashlib.sha256()
    with p.open("rb") as f:
        for q in iter(lambda:f.read(1<<20),b""):h.update(q)
    return h.hexdigest()

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--campaign",type=Path,default=Path("build/publication-validation/iris-pendulum-final-test"))
    ap.add_argument("--out",type=Path,default=Path("build/publication-validation/iris-pendulum-postfinal-proposals"))
    ap.add_argument("--self-test",action="store_true")
    a=ap.parse_args()
    if a.self_test:
        T=2*math.pi*math.sqrt(.5/G)
        assert abs(small_angle_length(T)-.5)<1e-12
        print("VALID GT-hidden proposal gate self-test");return
    takes=read_csv(a.campaign/"take_summary.csv")
    if not takes:raise SystemExit("missing take summary")
    a.out.mkdir(parents=True,exist_ok=True)
    blind=[]
    cache={}
    for take in takes:
        scene=take["scene"];p=a.campaign/safe_scene(scene)/"motion_signal.csv"
        rr=read_csv(p);fps=1/(float(rr[1]["time_s"])-float(rr[0]["time_s"]))
        t0,x0=segment(rr,0,.20);t1,x1=segment(rr,0,.40);tm,xm=segment(rr,.40,.50);tv,xv=segment(rr,.50,1.0)
        pb,_=estimate_period(t0,x0,fps)
        pc,_=estimate_period(t1,x1,fps)
        pm,_=estimate_period(tm,xm,fps)
        pv,cycles=estimate_period(tv,xv,fps)
        L0=small_angle_length(pb);L1=small_angle_length(pc)
        eb=abs(pm-predicted_period(L0,0.0,False))
        ec=abs(pm-predicted_period(L1,0.0,False))
        accepted=ec<eb
        blind.append({
            "scene":scene,"baseline_length_m":L0,"candidate_length_m":L1,
            "proposal_period_baseline_s":pb,"proposal_period_candidate_s":pc,
            "ordinary_holdout_period_s":pm,"ordinary_baseline_error_s":eb,
            "ordinary_candidate_error_s":ec,"ordinary_candidate_improves":str(accepted).lower(),
            "verification_period_s":pv,"verification_cycle_count":len(cycles)
        })
        cache[scene]=(fps,cycles,L0,L1)
    fields=list(blind[0])
    blind_path=a.out/"blind_proposals.csv"
    with blind_path.open("w",newline="",encoding="utf-8") as f:
        w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(blind)
    proposal_hash=sha256(blind_path)
    # Truth is joined only after blind proposal artifact is closed and hashed.
    truth={r["scene"]:r for r in takes}
    evaluated=[]
    for row in blind:
        scene=row["scene"];tr=truth[scene]
        Ltrue=float(tr["true_length_m"]);angle=float(tr["angle_deg"])
        fps,cycles,L0,L1=cache[scene]
        d0=abs(math.log(L0/Ltrue));d1=abs(math.log(L1/Ltrue))
        gt="support" if d1+1e-9<d0 else ("veto" if d1>d0+1e-9 else "unresolved")
        z,signal,sigma,_=score_candidate(cycles,fps,angle,L0,L1,finite_amplitude=True)
        dec=decision(z)
        q=dict(row)
        q.update({
          "blind_proposal_sha256":proposal_hash,"true_length_m":Ltrue,"angle_deg":angle,
          "ground_truth":gt,"standardized_evidence_score":z,"decision":dec,
          "decision_correct":str(dec==gt).lower(),"physical_truth_baseline_log_error":d0,
          "physical_truth_candidate_log_error":d1,"verification_signal_s":signal,
          "verification_scale_s":sigma
        })
        evaluated.append(q)
    with (a.out/"evaluated_proposals.csv").open("w",newline="",encoding="utf-8") as f:
        w=csv.DictWriter(f,fieldnames=list(evaluated[0]));w.writeheader();w.writerows(evaluated)
    accepted=[r for r in evaluated if r["ordinary_candidate_improves"]=="true"]
    correct=sum(r["decision_correct"]=="true" for r in accepted)
    deceptive=sum(r["ground_truth"]=="veto" for r in accepted)
    supported=sum(r["ground_truth"]=="support" for r in accepted)
    summary={
      "schema":"vulkax.gt_hidden_repair_gate","version":1,
      "physical_video_units":len(evaluated),"blind_proposal_sha256":proposal_hash,
      "ordinary_improving_proposals":len(accepted),
      "ordinary_improving_physically_better":supported,
      "ordinary_improving_physically_worse_deceptive":deceptive,
      "verifier_correct_on_ordinary_improving":correct,
      "verifier_accuracy_on_ordinary_improving":correct/len(accepted) if accepted else None,
      "proposal_ground_truth_hidden_until_after_hash":True,
      "proposal_windows":{"baseline":[0,.20],"candidate":[0,.40],"ordinary_holdout":[.40,.50],"verification":[.50,1.0]},
      "claim_guard":"Post-final GT-hidden proposal experiment; does not replace locked final result."
    }
    (a.out/"summary.json").write_text(json.dumps(summary,indent=2)+"\n",encoding="utf-8")
    print("VALID GT-hidden IRIS proposal->verification experiment")
    for k,v in summary.items():
        if not isinstance(v,(dict,list)):print(k.upper(),v)
    print("OUT",a.out)
if __name__=="__main__":main()

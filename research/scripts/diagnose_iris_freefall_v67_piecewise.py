#!/usr/bin/env python3
"""Retrospective V6.7 global piecewise free-fall event model.

Uses the seeded bidirectional visual trajectory, then selects an event by evidence
for the state sequence

    stationary hold -> sustained accelerating quadratic motion -> terminal change

No target gravity, expected flight time, or drop height is used in candidate
generation or ranking. Known height/g are used only after selection for
retrospective evaluation.

drop_50/drop_100 are spent retrospective evidence. Never run this on drop_150
during method development.
"""
from __future__ import annotations
import argparse, importlib.util, json, math
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
SEEDED=ROOT/"research/scripts/diagnose_iris_freefall_v67_seeded_track.py"
G=9.80665


def load(path,name):
    spec=importlib.util.spec_from_file_location(name,path)
    m=importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(m)
    return m


def sigma_mad(x,floor=.35):
    x=np.asarray(x,float)
    x=x[np.isfinite(x)]
    if len(x)<3:return float(floor)
    med=float(np.median(x))
    return max(float(floor),1.4826*float(np.median(np.abs(x-med))))


def terminal_evidence(y,good,e,t0,c,y0,fps,noise):
    post=np.arange(e+1,min(len(y),e+7))
    post=post[good[post]]
    if len(post)==0:return 1.0,0.0,1.0
    dt=post/fps-t0
    pred=y0+0.5*c*dt*dt
    innov=np.abs(y[post]-pred)
    innov_sig=float(np.median(innov)/max(noise,.35))
    seq=np.r_[e,post]
    seq=seq[good[seq]]
    dy=np.diff(y[seq]) if len(seq)>=2 else np.array([])
    reverse=float(np.mean(dy<=0.0)) if len(dy) else 0.0
    score=max(min(innov_sig/4.0,1.0),reverse)
    return score,innov_sig,reverse


def enumerate_events(centers,scores,fps,sign):
    y=sign*np.asarray(centers[:,1],float)
    x=np.asarray(centers[:,0],float)
    score=np.asarray(scores,float)
    good=np.isfinite(y)&np.isfinite(x)&np.isfinite(score)&(score>=.22)
    n=len(y)
    cands=[]
    pre_n=9
    min_fall=12
    max_fall=min(70,n)
    for r in range(pre_n,n-min_fall-4):
        pre=np.arange(r-pre_n,r)
        pg=pre[good[pre]]
        if len(pg)<6:continue
        y0=float(np.median(y[pg]))
        x0=float(np.median(x[pg]))
        pre_noise=sigma_mad(y[pg])
        pre_v=np.diff(y[pg])
        pre_speed=float(np.median(np.abs(pre_v))) if len(pre_v) else 0.0

        for e in range(r+min_fall,min(n-3,r+max_fall)):
            ids=np.arange(r,e+1)
            q=ids[good[ids]]
            if len(q)<max(10,int(.68*len(ids))):continue
            yy=y[q];xx=x[q]
            span=max(float(np.ptp(yy)),1.0)
            # broad geometry sanity only; ranking, not a tuned g gate
            hold_frac=float(np.std(y[pg]))/span
            pre_speed_frac=pre_speed/span
            xdrift=float(np.ptp(xx))/span
            monotone=float(np.mean(np.diff(yy)>=-1.0)) if len(yy)>1 else 0.0
            if hold_frac>.22 or pre_speed_frac>.12 or xdrift>.85 or monotone<.65:
                continue

            # Fit release time locally around the state transition and a
            # release-from-rest quadratic anchored to the preceding hold.
            for off in np.linspace(-2.0,2.0,17):
                t0=(r+off)/fps
                dt=q/fps-t0
                if np.min(dt)<-.055:continue
                z=.5*dt*dt
                den=float(np.dot(z,z))
                if den<1e-12:continue
                c=float(np.dot(z,yy-y0)/den)
                if not math.isfinite(c) or c<=0:continue
                pred=y0+c*z
                res=yy-pred
                rssq=float(np.dot(res,res))
                shape=float(np.sqrt(rssq/len(q))/span)

                # Competing constant-velocity model over identical samples.
                A=np.column_stack([np.ones(len(q)),q/fps])
                beta=np.linalg.lstsq(A,yy,rcond=None)[0]
                lin= A@beta
                rl=yy-lin
                rssl=float(np.dot(rl,rl))
                eps=max(1e-8,len(q)*.05*.05)
                info=float(len(q)*math.log((rssl+eps)/(rssq+eps)))

                # Curvature significance for the anchored model.
                dof=max(1,len(q)-1)
                sig2=max(rssq/dof,.05*.05)
                se_c=math.sqrt(sig2/den)
                c_snr=c/max(se_c,1e-9)

                term,innov,reverse=terminal_evidence(
                    y,good,e,t0,c,y0,fps,max(pre_noise,.5)
                )

                # Require actual evidence for acceleration and an event boundary;
                # these are dimensionless statistical conditions, not target-g.
                if info<=0.0 or c_snr<2.0 or term<.30:
                    continue

                release_jump=abs(float(yy[0]-y0))/span
                coverage=float(len(q)/len(ids))
                cands.append({
                    "r":int(r),"e":int(e),"t0_frame":float(t0*fps),
                    "T":float(e/fps-t0),"n":int(len(q)),
                    "c_px_s2":c,"shape":shape,"span":span,
                    "hold_frac":hold_frac,"pre_speed_frac":pre_speed_frac,
                    "release_jump":release_jump,"xdrift":xdrift,
                    "monotone":monotone,"coverage":coverage,
                    "terminal":term,"innovation":innov,"reverse":reverse,
                    "info_gain":info,"c_snr":c_snr,
                    "pre_noise":pre_noise,
                })
    return cands


def policy(cands,name):
    if not cands:return None
    keys={
      # Integrated evidence should beat tiny low-residual subsegments.
      "evidence":lambda q:(-q["info_gain"],-q["c_snr"],-q["n"],q["shape"]),
      "evidence_terminal":lambda q:(-(q["info_gain"]*q["terminal"]),-q["n"],q["shape"]),
      "snr_support":lambda q:(-(q["c_snr"]*math.sqrt(q["n"])),-q["info_gain"],q["shape"]),
      "support_evidence":lambda q:(-q["n"],-q["info_gain"],q["shape"]),
      "balanced":lambda q:(
          -(q["info_gain"]*math.sqrt(q["n"])*q["terminal"]*
            max(q["monotone"],.01)),
          q["hold_frac"],q["shape"]
      ),
    }
    return min(cands,key=keys[name])


def prepare_track(seed,video,cfg,width):
    mod=seed.load(seed.ANALYZER,"pw_analyzer")
    diag=seed.load(seed.TRACK_DIAG,"pw_diag")
    cc=dict(cfg);cc["analysis_width"]=int(width)
    fps,tracks=diag.build_tracks(mod,video,cc)
    tr,p,kind=seed.choose_seed(mod,tracks,fps,cc)
    fps2,frames=seed.load_gray_frames(mod,video,width,float(cc.get("max_seconds",5.0)))
    radius=float(p.get("radius",max(p.get("w",8),p.get("h",8))/2))
    centers,scores,_=seed.bidirectional_track(
        mod,frames,int(p["frame"]),float(p["x"]),float(p["y"]),radius
    )
    return fps2,centers,scores,kind,int(tr["id"])


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--data-root",type=Path,required=True)
    ap.add_argument("--split",choices=("development","validation"),default="validation")
    ap.add_argument("--take",action="append")
    ap.add_argument("--width",type=int,default=640)
    ap.add_argument(
        "--config",type=Path,
        default=ROOT/"research/validation/iris_freefall_rescue_v6.json"
    )
    args=ap.parse_args()
    seed=load(SEEDED,"seeded")
    cfgj=json.loads(args.config.read_text())
    cfg=dict(cfgj["tracker"]);ds=cfgj["dataset"][args.split]
    setting=ds["setting"];height=float(cfgj["physics"]["drop_heights_m"][setting])
    takes=args.take or list(ds["takes"])
    policies=("evidence","evidence_terminal","snr_support","support_evidence","balanced")
    print("RETROSPECTIVE_ONLY V6.7 GLOBAL PIECEWISE MODEL")
    print("selection uses only trajectory/model evidence; target physics is evaluation-only")
    for take in takes:
        video=args.data_root/"iris"/"Dropping_ball"/setting/f"{take}.mp4"
        try:
            fps,centers,scores,kind,tid=prepare_track(seed,video,cfg,args.width)
            cands=enumerate_events(
                centers,scores,fps,float(cfg.get("expected_image_gravity_sign",1.0))
            )
            truthT=math.sqrt(2*height/G)
            def err(q):
                g=2*height/(q["T"]*q["T"])
                return abs(g-G)/G
            if not cands:
                print(f"RESULT {take} candidates=0 tracked={int(np.sum(np.isfinite(centers[:,1])))}")
                continue
            oracle=min(cands,key=err)
            print(
                f"RESULT {take} tracked={int(np.sum(np.isfinite(centers[:,1])))} "
                f"cand={len(cands)} ORACLE T={oracle['T']:.4f} ratio={oracle['T']/truthT:.3f} "
                f"err={err(oracle):.3f} event={oracle['r']}->{oracle['e']} "
                f"info={oracle['info_gain']:.1f} snr={oracle['c_snr']:.1f} "
                f"shape={oracle['shape']:.3f} span={oracle['span']:.1f}"
            )
            for name in policies:
                q=policy(cands,name)
                print(
                    f"POLICY {take} {name} T={q['T']:.4f} ratio={q['T']/truthT:.3f} "
                    f"err={err(q):.3f} event={q['r']}->{q['e']} "
                    f"info={q['info_gain']:.1f} snr={q['c_snr']:.1f} n={q['n']} "
                    f"shape={q['shape']:.3f} span={q['span']:.1f} term={q['terminal']:.2f}"
                )
        except Exception as exc:
            print(f"RESULT {take} ERROR {type(exc).__name__}: {exc}")
    return 0

if __name__=="__main__":
    raise SystemExit(main())

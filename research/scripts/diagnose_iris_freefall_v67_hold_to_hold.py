#!/usr/bin/env python3
"""Retrospective V6.7 full-track hold-to-hold free-fall diagnostic.

The same ball is first followed bidirectionally through stationary and moving
frames. Candidate physical drops are then defined by two observable states:

    upper stationary hold -> downward accelerating excursion -> lower terminal hold

Selection uses image-space hold quality, direction, displacement, and trajectory
fit only. It never uses target g or expected fall duration. Known drop height is
bound to the selected upper/lower physical anchors only after selection.

Spent drop_50/drop_100 evidence only. Never use drop_150 during development.
"""
from __future__ import annotations
import argparse, importlib.util, json, math
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
SEEDED=ROOT/"research/scripts/diagnose_iris_freefall_v67_seeded_track.py"
G=9.80665


def load(path,name):
    s=importlib.util.spec_from_file_location(name,path)
    m=importlib.util.module_from_spec(s)
    assert s.loader is not None
    s.loader.exec_module(m)
    return m


def med3(x):
    x=np.asarray(x,float);o=x.copy()
    for i in range(len(x)):
        q=x[max(0,i-1):min(len(x),i+2)]
        q=q[np.isfinite(q)]
        o[i]=float(np.median(q)) if len(q) else np.nan
    return o


def stationary_plateaus(centers,scores,sign):
    y=sign*np.asarray(centers[:,1],float)
    x=np.asarray(centers[:,0],float)
    sc=np.asarray(scores,float)
    y=med3(y);x=med3(x)
    good=np.isfinite(y)&np.isfinite(x)&np.isfinite(sc)&(sc>=.22)
    # Per-frame image speed. Threshold adapts from the low-motion part of this
    # exact track and is bounded in pixel units to avoid classifying motion as hold.
    speed=np.full(len(y),np.nan)
    for i in range(1,len(y)):
        if good[i] and good[i-1]:
            speed[i]=math.hypot(float(x[i]-x[i-1]),float(y[i]-y[i-1]))
    vv=speed[np.isfinite(speed)]
    low=float(np.percentile(vv,25)) if len(vv) else .75
    thr=float(np.clip(2.25*low,.65,2.25))
    stat=good & ((np.nan_to_num(speed,nan=0.0)<=thr))
    # Require local persistence, suppress isolated template-lock samples.
    persistent=np.zeros(len(stat),bool)
    for i in range(len(stat)):
        a=max(0,i-2);b=min(len(stat),i+3)
        persistent[i]=np.sum(stat[a:b])>=4
    out=[];i=0
    while i<len(stat):
        if not persistent[i]:
            i+=1;continue
        a=i
        while i+1<len(stat) and persistent[i+1]:i+=1
        b=i+1
        ids=np.arange(a,b)
        ids=ids[good[ids]]
        if len(ids)>=5 and ids[-1]-ids[0]+1>=5:
            yy=y[ids];xx=x[ids]
            out.append({
                "a":int(ids[0]),"b":int(ids[-1]+1),
                "frames":int(ids[-1]-ids[0]+1),
                "y":float(np.median(yy)),"x":float(np.median(xx)),
                "y_jitter":float(np.std(yy)),"x_jitter":float(np.std(xx)),
                "score":float(np.median(sc[ids])),
            })
        i+=1
    return out,y,x,good,thr


def pair_candidates(plateaus,y,x,good,fps):
    out=[]
    for top in plateaus:
        for bottom in plateaus:
            if bottom["a"]<=top["b"]:continue
            span=float(bottom["y"]-top["y"])
            if span<12.0:continue
            # Broad duration bound merely prevents pairing unrelated cycles.
            raw_frames=int(bottom["a"]-top["b"]+1)
            if raw_frames<10 or raw_frames>100:continue
            ids=np.arange(top["b"]-1,bottom["a"]+1)
            ids=ids[(ids>=0)&(ids<len(y))&good[ids]]
            if len(ids)<10:continue
            yy=y[ids];xx=x[ids]
            monotone=float(np.mean(np.diff(yy)>=-1.25)) if len(yy)>1 else 0.
            if monotone<.62:continue
            xdrift=float(np.ptp(xx))/max(span,1.)
            if xdrift>.75:continue

            # Release time floats around the end of the upper hold. Fit
            # curvature in pixels/s^2 while anchoring position to the hold.
            best=None
            for off in np.linspace(-3.,3.,25):
                t0=(top["b"]-1+off)/fps
                dt=ids/fps-t0
                if np.min(dt)<-.075:continue
                z=.5*dt*dt
                den=float(np.dot(z,z))
                if den<1e-12:continue
                c=float(np.dot(z,yy-top["y"])/den)
                if not math.isfinite(c) or c<=0:continue
                pred=top["y"]+c*z
                rms=float(np.sqrt(np.mean((yy-pred)**2)))
                shape=rms/max(span,1.)
                end_pred=float(top["y"]+.5*c*((bottom["a"]/fps-t0)**2))
                endpoint=abs(end_pred-bottom["y"])/max(span,1.)
                release=abs(float(yy[0]-top["y"]))/max(span,1.)
                score=(shape+endpoint,shape,endpoint,release)
                if best is None or score<best[0]:
                    best=(score,{
                        "t0":t0,"c":c,"shape":shape,"endpoint":endpoint,
                        "release":release,"rms":rms,
                    })
            if best is None:continue
            fit=best[1]
            T=bottom["a"]/fps-fit["t0"]
            if T<=0:continue
            out.append({
                "top":top,"bottom":bottom,"span":span,
                "raw_frames":raw_frames,"n":len(ids),
                "monotone":monotone,"xdrift":xdrift,
                "T":float(T),**fit,
            })
    return out


def choose(cands,name):
    keys={
        "largest_span":lambda q:(-q["span"],q["shape"]+q["endpoint"],
                                  -min(q["top"]["frames"],q["bottom"]["frames"])),
        "fit_then_span":lambda q:(q["shape"]+q["endpoint"],-q["span"],
                                  -min(q["top"]["frames"],q["bottom"]["frames"])),
        "holds_span_fit":lambda q:(-min(q["top"]["frames"],q["bottom"]["frames"]),
                                   -q["span"],q["shape"]+q["endpoint"]),
        "span_per_error":lambda q:(-(q["span"]/max(q["shape"]+q["endpoint"],.01)),
                                    -min(q["top"]["frames"],q["bottom"]["frames"])),
        "balanced":lambda q:(
            -(q["span"]*math.sqrt(min(q["top"]["frames"],q["bottom"]["frames"]))/
              max(1.+20.*(q["shape"]+q["endpoint"]),1e-6)),
            q["shape"]+q["endpoint"]
        ),
    }
    return min(cands,key=keys[name]) if cands else None


def track(seed,video,cfg,width):
    mod=seed.load(seed.ANALYZER,"hh_a");diag=seed.load(seed.TRACK_DIAG,"hh_d")
    cc=dict(cfg);cc["analysis_width"]=width
    fps,tracks=diag.build_tracks(mod,video,cc)
    tr,p,kind=seed.choose_seed(mod,tracks,fps,cc)
    fps,frames=seed.load_gray_frames(mod,video,width,float(cc.get("max_seconds",5.0)))
    radius=float(p.get("radius",max(p.get("w",8),p.get("h",8))/2))
    centers,scores,_=seed.bidirectional_track(
        mod,frames,int(p["frame"]),float(p["x"]),float(p["y"]),radius)
    return fps,centers,scores,kind


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--data-root",type=Path,required=True)
    ap.add_argument("--split",choices=("development","validation"),default="validation")
    ap.add_argument("--take",action="append")
    ap.add_argument("--width",type=int,default=640)
    ap.add_argument("--config",type=Path,
        default=ROOT/"research/validation/iris_freefall_rescue_v6.json")
    args=ap.parse_args()
    seed=load(SEEDED,"seed")
    cfgj=json.loads(args.config.read_text());cfg=dict(cfgj["tracker"])
    ds=cfgj["dataset"][args.split];setting=ds["setting"]
    h=float(cfgj["physics"]["drop_heights_m"][setting]);takes=args.take or ds["takes"]
    policies=("largest_span","fit_then_span","holds_span_fit","span_per_error","balanced")
    print("RETROSPECTIVE_ONLY V6.7 FULL-TRACK HOLD-TO-HOLD MODEL")
    for take in takes:
        video=args.data_root/"iris"/"Dropping_ball"/setting/f"{take}.mp4"
        try:
            fps,centers,scores,kind=track(seed,video,cfg,args.width)
            plats,y,x,good,thr=stationary_plateaus(
                centers,scores,float(cfg.get("expected_image_gravity_sign",1.0)))
            cands=pair_candidates(plats,y,x,good,fps)
            truthT=math.sqrt(2*h/G)
            def errT(q):
                g=2*h/(q["T"]*q["T"])
                return abs(g-G)/G
            def errC(q):
                g=q["c"]*h/q["span"]
                return abs(g-G)/G
            if not cands:
                print(f"RESULT {take} plateaus={len(plats)} candidates=0 threshold={thr:.3f}")
                continue
            oracle=min(cands,key=lambda q:min(errT(q),errC(q)))
            print(
                f"RESULT {take} plateaus={len(plats)} cand={len(cands)} thr={thr:.2f} "
                f"ORACLE top={oracle['top']['a']}-{oracle['top']['b']-1} "
                f"bottom={oracle['bottom']['a']}-{oracle['bottom']['b']-1} "
                f"span={oracle['span']:.1f} T={oracle['T']:.4f} "
                f"T_ratio={oracle['T']/truthT:.3f} Terr={errT(oracle):.3f} "
                f"Cerr={errC(oracle):.3f} shape={oracle['shape']:.3f} end={oracle['endpoint']:.3f}"
            )
            for name in policies:
                q=choose(cands,name)
                print(
                    f"POLICY {take} {name} "
                    f"top={q['top']['a']}-{q['top']['b']-1} "
                    f"bottom={q['bottom']['a']}-{q['bottom']['b']-1} "
                    f"span={q['span']:.1f} T={q['T']:.4f} ratio={q['T']/truthT:.3f} "
                    f"Terr={errT(q):.3f} Cerr={errC(q):.3f} "
                    f"shape={q['shape']:.3f} end={q['endpoint']:.3f} "
                    f"holds={q['top']['frames']}/{q['bottom']['frames']}"
                )
        except Exception as exc:
            print(f"RESULT {take} ERROR {type(exc).__name__}: {exc}")
    return 0

if __name__=="__main__":
    raise SystemExit(main())

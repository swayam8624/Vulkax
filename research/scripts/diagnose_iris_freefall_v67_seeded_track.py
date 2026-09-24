#!/usr/bin/env python3
"""V6.7 retrospective seeded visual-track diagnostic.

Architecture under test:
  motion/identity seed -> bidirectional appearance tracking -> target-free
  hold -> release-from-rest quadratic -> terminal impact change point.

The event generator never uses target gravity, expected fall time, or drop height.
Known height and g are used only after selection for retrospective scoring.

Only spent drop_50/drop_100 evidence is permitted. drop_150 must remain unopened.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import math
from pathlib import Path

import numpy as np

ROOT=Path(__file__).resolve().parents[2]
ANALYZER=ROOT/"research/analysis/run_iris_freefall_validation.py"
TRACK_DIAG=ROOT/"research/scripts/diagnose_iris_freefall_v66.py"
G=9.80665


def load(path,name):
    spec=importlib.util.spec_from_file_location(name,path)
    mod=importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


def load_gray_frames(mod,video,width,max_seconds):
    cv2=mod.import_cv()
    cap=cv2.VideoCapture(str(video))
    fps=float(cap.get(cv2.CAP_PROP_FPS))
    n=int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    n=min(n,max(120,int(fps*max_seconds)))
    frames=[]
    for _ in range(n):
        ok,fr=cap.read()
        if not ok:break
        frames.append(mod.resize_gray(fr,width,cv2))
    cap.release()
    if len(frames)<20:raise RuntimeError("too few frames")
    return fps,frames


def choose_seed(mod,tracks,fps,cfg):
    try:
        chosen,_=mod.choose_ballistic_track_v66(
            tracks,fps,
            minimum_interval_frames=int(cfg.get("minimum_active_frames",12)),
            identity_cfg=cfg,
        )
        tid=int(chosen["track_id"])
        target_frame=int(chosen.get("start_frame",-1))
        # choose a well-observed point around middle of selected source track
        tr=next(t for t in tracks if int(t["id"])==tid)
        pts=tr["pts"]
        mid=int(np.median([p["frame"] for p in pts]))
        p=min(pts,key=lambda q:abs(int(q["frame"])-mid))
        return tr,p,"v66"
    except Exception:
        pass

    # Fallback for clips where V6.6 has no event: choose a ball-like identity
    # chunk using morphology + downward image-space span only.
    chunks=mod._identity_track_chunks(tracks,fps,cfg)
    if not chunks:raise RuntimeError("no identity chunks for seed")
    sign=float(cfg.get("expected_image_gravity_sign",1.0))
    def rank(c):
        z=sign*np.asarray(c["y"],float)
        span=max(0.0,float(np.max(z)-np.min(z)))
        return (-float(c["identity_score"]),-span,-float(c["detected_fraction_chunk"]))
    c=min(chunks,key=rank)
    tr=next(t for t in tracks if int(t["id"])==int(c["track_id"]))
    pts=tr["pts"]
    p=max(
        pts,
        key=lambda q:(
            float(q.get("circularity",0.0))
            +float(q.get("circle_fill",0.0))
            +float(q.get("solidity",0.0))
            +float(q.get("axis_ratio",0.0))
        )
    )
    return tr,p,"identity_fallback"


def patch(img,x,y,half):
    h,w=img.shape
    cx=int(round(x));cy=int(round(y))
    x0=max(0,cx-half);x1=min(w,cx+half+1)
    y0=max(0,cy-half);y1=min(h,cy+half+1)
    q=img[y0:y1,x0:x1]
    return q,x0,y0


def local_match(cv2,img,templ,predx,predy,search):
    th,tw=templ.shape
    hh=th//2;hw=tw//2
    h,w=img.shape
    cx=int(round(predx));cy=int(round(predy))
    x0=max(0,cx-search-hw);x1=min(w,cx+search+hw+1)
    y0=max(0,cy-search-hh);y1=min(h,cy+search+hh+1)
    roi=img[y0:y1,x0:x1]
    if roi.shape[0]<th or roi.shape[1]<tw:return None
    res=cv2.matchTemplate(roi,templ,cv2.TM_CCOEFF_NORMED)
    _mn,mx,_ml,mxl=cv2.minMaxLoc(res)
    x=float(x0+mxl[0]+tw/2)
    y=float(y0+mxl[1]+th/2)
    return x,y,float(mx)


def bidirectional_track(mod,frames,seed_frame,x,y,radius):
    cv2=mod.import_cv()
    n=len(frames)
    half=int(np.clip(round(max(radius,4.0)*1.8),8,26))
    templ,_,_=patch(frames[seed_frame],x,y,half)
    if min(templ.shape)<9:raise RuntimeError("seed template too small")
    centers=np.full((n,2),np.nan,float)
    scores=np.full(n,np.nan,float)
    centers[seed_frame]=[x,y];scores[seed_frame]=1.0

    for direction in (-1,1):
        last=np.array([x,y],float)
        vel=np.zeros(2,float)
        misses=0
        i=seed_frame+direction
        while 0<=i<n:
            pred=last+direction*vel
            speed=float(np.linalg.norm(vel))
            search=int(np.clip(max(22,4*half+2.5*speed),22,120))
            m=local_match(cv2,frames[i],templ,pred[0],pred[1],search)
            if m is None:
                misses+=1;i+=direction;continue
            xx,yy,sc=m
            if sc<0.30:
                # One wider recovery attempt, still centered on kinematic prediction.
                m2=local_match(cv2,frames[i],templ,pred[0],pred[1],min(180,2*search))
                if m2 is not None and m2[2]>sc:
                    xx,yy,sc=m2
            if sc<0.22:
                misses+=1
                if misses>4:
                    vel*=0.5
                i+=direction
                continue
            cur=np.array([xx,yy],float)
            newv=(cur-last)*direction
            vel=0.65*vel+0.35*newv
            last=cur;misses=0
            centers[i]=cur;scores[i]=sc
            i+=direction
    return centers,scores,half


def robust_sigma(x):
    x=np.asarray(x,float)
    x=x[np.isfinite(x)]
    if len(x)<3:return 1.0
    m=float(np.median(x))
    return max(0.5,1.4826*float(np.median(np.abs(x-m))))


def event_candidates(centers,scores,fps,sign=1.0):
    y=sign*centers[:,1]
    x=centers[:,0]
    good=np.isfinite(y)&np.isfinite(x)&np.isfinite(scores)&(scores>=0.25)
    n=len(y)
    out=[]
    hold=7
    min_fall=12
    max_fall=min(55,n)
    for s in range(hold,n-min_fall-3):
        pre=np.arange(s-hold,s)
        if np.sum(good[pre])<5:continue
        ypre=y[pre][good[pre]]
        xpre=x[pre][good[pre]]
        y0=float(np.median(ypre))
        hold_noise=robust_sigma(ypre)
        hold_jitter=float(np.std(ypre))
        for e in range(s+min_fall,min(n-3,s+max_fall)):
            ids=np.arange(s,e+1)
            ok=good[ids]
            if np.sum(ok)<max(10,int(0.70*len(ids))):continue
            fr=ids[ok].astype(float)
            yy=y[ids][ok]
            xx=x[ids][ok]
            # fractional release time grid around the hold->motion boundary
            best=None
            for off in np.linspace(-1.5,1.5,13):
                t0=(s+off)/fps
                dt=fr/fps-t0
                if np.min(dt)<-0.04:continue
                q=0.5*dt*dt
                den=float(np.dot(q,q))
                if den<=1e-12:continue
                c=float(np.dot(q,yy-y0)/den)
                if not math.isfinite(c) or c<=0:continue
                pred=y0+c*q
                span=max(float(np.ptp(yy)),1.0)
                shape=float(np.sqrt(np.mean((yy-pred)**2))/span)
                release_jump=abs(float(yy[0]-y0))/span
                xdrift=float(np.ptp(xx))/span
                # Pre-hold must be stationary relative to ensuing displacement.
                hold_frac=hold_jitter/span
                if shape>0.10 or release_jump>0.18 or xdrift>0.50 or hold_frac>0.08:
                    continue

                # Terminality: continued ballistic prediction should cease to match
                # after e, or visible motion should stop/reverse. No target magnitude.
                post=np.arange(e+1,min(n,e+6))
                post=post[good[post]]
                terminal=0.0
                if len(post)==0:
                    terminal=1.0
                else:
                    pdt=post/fps-t0
                    ppred=y0+0.5*c*pdt*pdt
                    innov=np.abs(y[post]-ppred)
                    noise=max(hold_noise,0.75)
                    innov_sig=float(np.median(innov)/noise)
                    pv=np.diff(y[np.r_[e,post]]) if good[e] else np.diff(y[post])
                    reverse=float(np.mean(pv<=0)) if len(pv) else 0.0
                    terminal=max(min(innov_sig/4.0,1.0),reverse)
                if terminal<0.45:continue

                cand={
                    "s":int(s),"e":int(e),"t0_frame":float(t0*fps),
                    "T":float(e/fps-t0),"c_px_s2":c,
                    "shape":shape,"release_jump":release_jump,
                    "xdrift":xdrift,"hold_frac":hold_frac,
                    "terminal":terminal,
                    "coverage":float(np.mean(ok)),
                    "span_px":span,
                    "hold_noise_px":hold_noise,
                }
                rank=(
                    shape,
                    hold_frac,
                    release_jump,
                    xdrift,
                    -terminal,
                    -span,
                    s,
                )
                if best is None or rank<best[0]:
                    best=(rank,cand)
            if best is not None:
                out.append(best[1])
    return out


def select_event(cands):
    if not cands:return None
    # Prefer a clean hold->ballistic->terminal event. Earliest release is only a
    # final tie-breaker; no target duration/acceleration is present.
    return min(cands,key=lambda q:(
        q["shape"],q["hold_frac"],q["release_jump"],q["xdrift"],
        -q["terminal"],-q["span_px"],q["s"]
    ))


def run_take(mod,diag,video,cfg,width,height):
    cc=dict(cfg);cc["analysis_width"]=int(width)
    fps,tracks=diag.build_tracks(mod,video,cc)
    tr,p,seed_kind=choose_seed(mod,tracks,fps,cc)
    frames_fps,frames=load_gray_frames(
        mod,video,int(width),float(cc.get("max_seconds",5.0))
    )
    if abs(frames_fps-fps)>1e-3:fps=frames_fps
    sf=int(p["frame"])
    radius=float(p.get("radius",max(p.get("w",8),p.get("h",8))/2))
    centers,scores,half=bidirectional_track(
        mod,frames,sf,float(p["x"]),float(p["y"]),radius
    )
    cands=event_candidates(
        centers,scores,fps,float(cc.get("expected_image_gravity_sign",1.0))
    )
    chosen=select_event(cands)
    if chosen is None:
        return {
            "seed_kind":seed_kind,"seed_track":int(tr["id"]),
            "seed_frame":sf,"tracked":int(np.sum(np.isfinite(centers[:,1]))),
            "mean_score":float(np.nanmean(scores)),"candidate_count":0,
        }
    T=float(chosen["T"])
    g=2.0*height/(T*T)
    return {
        "seed_kind":seed_kind,"seed_track":int(tr["id"]),
        "seed_frame":sf,"tracked":int(np.sum(np.isfinite(centers[:,1]))),
        "mean_score":float(np.nanmean(scores)),"candidate_count":len(cands),
        **chosen,
        "g_eval":g,"gerr":abs(g-G)/G,
        "T_truth":math.sqrt(2.0*height/G),
    }


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument(
        "--config",type=Path,
        default=ROOT/"research/validation/iris_freefall_rescue_v6.json"
    )
    ap.add_argument("--data-root",type=Path,required=True)
    ap.add_argument("--width",type=int,default=640)
    args=ap.parse_args()

    mod=load(ANALYZER,"v67_seeded_analyzer")
    diag=load(TRACK_DIAG,"v67_seeded_diag")
    config=json.loads(args.config.read_text())
    cfg=dict(config["tracker"])
    results=[]
    print("RETROSPECTIVE_ONLY V6.7 SEEDED BIDIRECTIONAL TRACKER")
    print("Generator/ranker use no target g, expected fall time, or drop height.")
    for split in ("development","validation"):
        spec=config["dataset"][split]
        setting=spec["setting"]
        height=float(config["physics"]["drop_heights_m"][setting])
        for take in spec["takes"]:
            video=args.data_root/"iris"/"Dropping_ball"/setting/f"{take}.mp4"
            try:
                r=run_take(mod,diag,video,cfg,args.width,height)
                r.update({"split":split,"setting":setting,"take":take})
                results.append(r)
                if "gerr" in r:
                    print(
                        f"{setting}/{take} seed={r['seed_kind']} "
                        f"tracked={r['tracked']} cand={r['candidate_count']} "
                        f"event={r['s']}->{r['e']} t0={r['t0_frame']:.2f} "
                        f"T={r['T']:.4f}s T/Ttruth={r['T']/r['T_truth']:.3f} "
                        f"shape={r['shape']:.4f} terminal={r['terminal']:.3f} "
                        f"gerr={r['gerr']:.3f}"
                    )
                else:
                    print(
                        f"{setting}/{take} NO_EVENT seed={r['seed_kind']} "
                        f"tracked={r['tracked']} score={r['mean_score']:.3f}"
                    )
            except Exception as exc:
                print(f"{setting}/{take} ERROR {type(exc).__name__}: {exc}")

    for split in ("development","validation"):
        rr=[r for r in results if r["split"]==split and "gerr" in r]
        if rr:
            errs=[r["gerr"] for r in rr]
            print(
                "SUMMARY",split,
                "n",len(rr),
                "median_gerr",float(np.median(errs)),
                "max_gerr",float(np.max(errs)),
                "median_T_ratio",float(np.median([
                    r["T"]/r["T_truth"] for r in rr
                ]))
            )
        else:
            print("SUMMARY",split,"n",0)
    return 0


if __name__=="__main__":
    raise SystemExit(main())

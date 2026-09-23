#!/usr/bin/env python3
"""Blind IRIS free-fall validation on development/validation/final partitions.

Development revisions 1-5 (2026-09-23):
The first development run exposed a segmentation/calibration failure: the old
tracker chose the longest occupancy run between global position quantiles, which
can select slow reset/handling motion rather than the actual ballistic descent.
No validation or final free-fall result was analyzed before this redesign.

Revision 2 improved event selection but still failed development (2/4 quality
pass; 36.06% median acceleration error). Revision 3 then over-constrained the
event with plateau/shape gates and produced 0/4 quality-pass videos. Revision 4
built temporal tracks, but all four selected intervals failed the existing
active-frame gate and the short-fragment/full-height mismatch inflated gravity.
Revision 5 therefore forbids arbitrary subsegments, allows only small edge trims,
requires the existing minimum interval duration before physical calibration, and
ranks complete coherent flights ahead of tiny low-residual fragments. The target
acceleration remains absent from selection. The independently measured drop height
is bound only to the accepted full-flight interval.
"""
from __future__ import annotations
import argparse,csv,json,math,pathlib,statistics,subprocess,tempfile,traceback
import numpy as np

G=9.80665
TAU=2.0
DOSES=(.025,.05,.10,.20)


class FreefallValidationError(RuntimeError):
    """Base class for controlled free-fall analysis failures."""

class TrackSelectionError(FreefallValidationError):
    """No physically admissible track/event survived the frozen selector."""

class ContractError(FreefallValidationError):
    """Internal producer/consumer or configuration contract violation."""

CANDIDATE_NUMERIC_FIELDS=(
    "span_px","full_fall_time_s","timing_fit_rms_frames",
    "trajectory_shape_rms_fraction","release_speed_ratio",
    "x_drift_fraction","gap_penalty","detected_window_fraction",
    "identity_score","median_circularity","median_solidity",
    "median_circle_fill","median_axis_ratio","radius_cv","area_cv",
    "aspect_log_median","sign","t0_s",
)
CANDIDATE_REQUIRED_FIELDS=(
    "track_id","window_indices","abs_times","progress","interval_frames",
    "detected_frames",*CANDIDATE_NUMERIC_FIELDS,
)

def _finite_number(value,name):
    try:
        q=float(value)
    except (TypeError,ValueError) as e:
        raise ContractError(f"candidate field {name!r} is not numeric: {value!r}") from e
    if not math.isfinite(q):
        raise ContractError(f"candidate field {name!r} is not finite: {value!r}")
    return q

def validate_candidate(candidate):
    if not isinstance(candidate,dict):
        raise ContractError(
            f"candidate producer returned {type(candidate).__name__}, expected dict"
        )
    missing=[k for k in CANDIDATE_REQUIRED_FIELDS if k not in candidate]
    if missing:
        raise ContractError("candidate missing required fields: "+",".join(missing))
    for name in CANDIDATE_NUMERIC_FIELDS:
        _finite_number(candidate[name],name)
    for name in ("median_circularity","median_solidity","median_circle_fill","median_axis_ratio"):
        q=float(candidate[name])
        if not (0.0<=q<=1.0):
            raise ContractError(f"bounded shape field {name} outside [0,1]: {q}")
    if float(candidate["span_px"])<=0 or float(candidate["full_fall_time_s"])<=0:
        raise ContractError("candidate span/duration must be positive")
    if int(candidate["interval_frames"])<=0 or int(candidate["detected_frames"])<=0:
        raise ContractError("candidate frame counts must be positive")
    return candidate

def validate_selector_config(identity_cfg):
    cfg=identity_cfg or {}
    expected_sign=float(cfg.get("expected_image_gravity_sign",1.0))
    if expected_sign not in (-1.0,1.0):
        raise ContractError(
            f"expected_image_gravity_sign must be -1 or +1, got {expected_sign!r}"
        )
    min_global_span=float(cfg.get("minimum_global_progress_span",0.15))
    if not math.isfinite(min_global_span) or not (0.0 < min_global_span <= 1.0):
        raise ContractError(
            f"minimum_global_progress_span must be in (0,1], got {min_global_span!r}"
        )
    for key in (
        "minimum_median_circularity","minimum_median_solidity",
        "minimum_median_circle_fill","minimum_median_axis_ratio",
        "minimum_detected_fraction",
    ):
        if key in cfg:
            q=float(cfg[key])
            if not math.isfinite(q) or not (0.0<=q<=1.0):
                raise ContractError(f"{key} must be finite and in [0,1], got {q!r}")
    for key in (
        "maximum_radius_cv","maximum_area_cv","maximum_aspect_log_mad",
        "maximum_global_timing_fit_rms_frames",
    ):
        if key in cfg:
            q=float(cfg[key])
            if not math.isfinite(q) or q<0:
                raise ContractError(f"{key} must be finite and >=0, got {q!r}")
    return {
        "expected_sign":expected_sign,
        "minimum_global_progress_span":min_global_span,
        "maximum_global_timing_fit_rms_frames":float(
            cfg.get("maximum_global_timing_fit_rms_frames",2.5)
        ),
    }

def read_csv(p):
    with p.open(newline="",encoding="utf-8") as f:return list(csv.DictReader(f))
def robust(vals):
    if not vals:return 0.0
    m=statistics.median(vals);return 1.4826*statistics.median(abs(v-m) for v in vals)
def decision(s):
    return "support" if s>=TAU else ("veto" if s<=-TAU else "unresolved")
def safe(s):return s.replace("/","__").replace(" ","_")
def cases():
    q=[("truth_support",1.0,"support","truth_control",False),
       ("truth_veto",1.60,"veto","truth_control",False),
       ("placebo",1.25,"unresolved","placebo",True)]
    for d in DOSES:
        q += [(f"support_dose_{d:.3f}",1.25-d,"support","dose_response",False),
              (f"veto_dose_{d:.3f}",1.25+d,"veto","dose_response",False)]
    return q

def import_cv():
    try:import cv2
    except ImportError as e:raise SystemExit("opencv-python-headless required") from e
    return cv2

def resize_gray(frame,width,cv2):
    h,w=frame.shape[:2];scale=width/w
    q=cv2.resize(frame,(width,max(1,int(round(h*scale)))),interpolation=cv2.INTER_AREA) if w!=width else frame
    return cv2.cvtColor(q,cv2.COLOR_BGR2GRAY)

def smooth1(x,n=5):
    x=np.asarray(x,float)
    if n<=1 or len(x)<n:return x.copy()
    k=np.ones(n,float)/n
    q=np.convolve(x,k,mode="same")
    e=n//2
    q[:e]=x[:e];q[-e:]=x[-e:]
    return q

def extract_components(diff_crop,threshold,cv2,x0,y0):
    mm=(diff_crop>=threshold).astype(np.uint8)
    mm=cv2.morphologyEx(
        mm,cv2.MORPH_OPEN,
        cv2.getStructuringElement(cv2.MORPH_ELLIPSE,(3,3))
    )
    nlab,labels,stats,cent=cv2.connectedComponentsWithStats(mm,connectivity=8)
    roi_area=max(1,diff_crop.shape[0]*diff_crop.shape[1])
    out=[]
    for lab in range(1,nlab):
        area=int(stats[lab,cv2.CC_STAT_AREA])
        bw=int(stats[lab,cv2.CC_STAT_WIDTH]);bh=int(stats[lab,cv2.CC_STAT_HEIGHT])
        if area<5 or area>.04*roi_area or bw<=0 or bh<=0:continue
        aspect=bw/bh
        if not (.25<=aspect<=4.0):continue
        fill=area/max(1,bw*bh)
        if fill<.12:continue
        cx,cy=cent[lab]
        component_mask=(labels==lab).astype(np.uint8)*255
        contours,_=cv2.findContours(
            component_mask,cv2.RETR_EXTERNAL,cv2.CHAIN_APPROX_NONE
        )
        contour=max(contours,key=cv2.contourArea) if contours else None
        contour_area=float(cv2.contourArea(contour)) if contour is not None else 0.0
        perimeter=float(cv2.arcLength(contour,True)) if contour is not None else 0.0
        circularity=(4.0*math.pi*contour_area/(perimeter*perimeter)) if perimeter>1e-9 else 0.0
        circularity=float(np.clip(circularity,0.0,1.0))
        hull=cv2.convexHull(contour) if contour is not None and len(contour)>=3 else None
        hull_area=float(cv2.contourArea(hull)) if hull is not None else 0.0
        solidity=float(np.clip(contour_area/max(hull_area,1e-9),0.0,1.0))
        if contour is not None and len(contour)>=3:
            (_, _),radius=cv2.minEnclosingCircle(contour)
            circle_area=math.pi*float(radius)*float(radius)
            circle_fill=float(np.clip(contour_area/max(circle_area,1e-9),0.0,1.0))
            rect=cv2.minAreaRect(contour)
            rw,rh=map(float,rect[1])
            axis_ratio=(min(rw,rh)/max(rw,rh)) if max(rw,rh)>1e-9 else 0.0
        else:
            radius=0.0;circle_fill=0.0;axis_ratio=0.0
        mean_diff=float(np.mean(diff_crop[labels==lab]))
        compact=fill/math.sqrt(max(area,1))
        out.append({
            "x":float(cx+x0),"y":float(cy+y0),"area":area,
            "contour_area":contour_area,
            "w":bw,"h":bh,"fill":fill,"mean_diff":mean_diff,
            "circularity":circularity,
            "solidity":solidity,
            "circle_fill":circle_fill,
            "axis_ratio":float(np.clip(axis_ratio,0.0,1.0)),
            "radius":float(radius),
            "aspect_log_abs":float(abs(math.log(max(aspect,1e-9)))),
            "appearance_score":mean_diff*(compact+.02)
        })
    return out

def build_temporal_tracks(frame_candidates,fps):
    """Associate compact moving components through time with short-gap recovery.

    A 0.5 m free-fall lasts only ~0.3 s, so losing two or three detections can
    fragment the physical event. V5 allows short gaps while keeping a
    constant-velocity image prediction and component-scale gate. No physical truth
    or target acceleration enters association.
    """
    if not isinstance(frame_candidates,list):
        raise ContractError(
            f"frame_candidates must be list, got {type(frame_candidates).__name__}"
        )
    if not math.isfinite(float(fps)) or float(fps)<=0:
        raise ContractError(f"fps must be finite and positive, got {fps!r}")
    active=[];finished=[];next_id=0
    max_gap=5
    for frame_idx,cands in enumerate(frame_candidates):
        unmatched=set(range(len(cands)))
        proposals=[]
        for ti,tr in enumerate(active):
            last=tr["pts"][-1]
            if len(tr["pts"])>=2:
                p0=tr["pts"][-2];p1=last
                df=max(1,p1["frame"]-p0["frame"])
                vx=(p1["x"]-p0["x"])/df
                vy=(p1["y"]-p0["y"])/df
            else:vx=vy=0.0
            gap=max(1,frame_idx-last["frame"])
            predx=last["x"]+vx*gap
            predy=last["y"]+vy*gap
            last_area=last["area"]
            for ci,cc in enumerate(cands):
                dx=cc["x"]-predx;dy=cc["y"]-predy
                dist=math.hypot(dx,dy)
                area_ratio=max(cc["area"],last_area)/max(1,min(cc["area"],last_area))
                gate=max(22.0,7.0*gap+2.8*abs(vy)*gap)
                if dist>gate or area_ratio>4.5:continue
                horiz=abs(cc["x"]-last["x"])
                cost=(dist/gap)+0.25*(horiz/gap)+2.5*abs(math.log(area_ratio))-0.002*cc["appearance_score"]
                proposals.append((cost,ti,ci))
        assigned_tracks=set();assigned_cands=set()
        for cost,ti,ci in sorted(proposals):
            if ti in assigned_tracks or ci in assigned_cands:continue
            tr=active[ti];cc=dict(cands[ci]);cc["frame"]=frame_idx
            tr["pts"].append(cc);tr["missed"]=0
            assigned_tracks.add(ti);assigned_cands.add(ci);unmatched.discard(ci)
        survivors=[]
        for ti,tr in enumerate(active):
            if ti not in assigned_tracks:tr["missed"]+=1
            if tr["missed"]>max_gap:
                if len(tr["pts"])>=6:finished.append(tr)
            else:survivors.append(tr)
        active=survivors
        for ci in sorted(unmatched):
            cc=dict(cands[ci]);cc["frame"]=frame_idx
            active.append({"id":next_id,"pts":[cc],"missed":0});next_id+=1
    for tr in active:
        if len(tr["pts"])>=6:finished.append(tr)
    return finished

def crossing_after(times,progress,level,start_index=1):
    for i in range(max(1,start_index),len(progress)):
        if progress[i-1] < level <= progress[i]:
            den=progress[i]-progress[i-1]
            frac=(level-progress[i-1])/den if abs(den)>1e-12 else 0.0
            tt=float(times[i-1]+frac*(times[i]-times[i-1]))
            return tt,i
    return None,None

def fit_full_flight_progress(track,fps,minimum_interval_frames=12,identity_cfg=None):
    """Recover full fall time from 10%-90% progress crossings on one track.

    For release-from-rest motion, normalized displacement p follows p=(t/T)^2,
    hence t(p)=t0+T*sqrt(p). Fitting crossing times to sqrt(p) recovers the full
    fall duration T without using the target value of g and without assigning the
    full measured drop height to a short interior fragment.
    """
    pts=track["pts"]
    identity_cfg=identity_cfg or {}
    min_circularity=float(identity_cfg.get("minimum_median_circularity",0.0))
    min_solidity=float(identity_cfg.get("minimum_median_solidity",0.0))
    min_circle_fill=float(identity_cfg.get("minimum_median_circle_fill",0.0))
    min_axis_ratio=float(identity_cfg.get("minimum_median_axis_ratio",0.0))
    max_radius_cv=float(identity_cfg.get("maximum_radius_cv",float("inf")))
    max_area_cv=float(identity_cfg.get("maximum_area_cv",float("inf")))
    max_aspect=float(identity_cfg.get("maximum_aspect_log_mad",float("inf")))
    min_detected=float(identity_cfg.get("minimum_detected_fraction",0.45))
    frames=np.asarray([p["frame"] for p in pts],int)
    xx=np.asarray([p["x"] for p in pts],float)
    yy=np.asarray([p["y"] for p in pts],float)
    if len(frames)<8:return []

    # Treat only gaps larger than the explicit association allowance as hard breaks.
    chunks=[];aa=0
    for i in range(1,len(frames)):
        if frames[i]-frames[i-1]>5:
            chunks.append((aa,i));aa=i
    chunks.append((aa,len(frames)))

    levels=np.asarray([.10,.20,.30,.40,.50,.60,.70,.80,.90],float)
    sqrt_levels=np.sqrt(levels)
    candidates=[]

    for aa,bb in chunks:
        if bb-aa<8:continue
        fr=frames[aa:bb];x=xx[aa:bb];y=yy[aa:bb]
        chunk_pts=pts[aa:bb]
        circularities=np.asarray([p.get("circularity",0.0) for p in chunk_pts],float)
        solidities=np.asarray([p.get("solidity",0.0) for p in chunk_pts],float)
        circle_fills=np.asarray([p.get("circle_fill",0.0) for p in chunk_pts],float)
        axis_ratios=np.asarray([p.get("axis_ratio",0.0) for p in chunk_pts],float)
        radii=np.asarray([p.get("radius",0.0) for p in chunk_pts],float)
        areas=np.asarray([p["area"] for p in chunk_pts],float)
        aspects=np.asarray([p.get("aspect_log_abs",0.0) for p in chunk_pts],float)
        median_circularity=float(np.median(circularities)) if len(circularities) else 0.0
        median_solidity=float(np.median(solidities)) if len(solidities) else 0.0
        median_circle_fill=float(np.median(circle_fills)) if len(circle_fills) else 0.0
        median_axis_ratio=float(np.median(axis_ratios)) if len(axis_ratios) else 0.0
        radius_cv=float(np.std(radii)/max(np.mean(radii),1e-9)) if len(radii) else float("inf")
        area_cv=float(np.std(areas)/max(np.mean(areas),1e-9)) if len(areas) else float("inf")
        aspect_log_median=float(np.median(aspects)) if len(aspects) else float("inf")
        if (median_circularity<min_circularity
            or median_solidity<min_solidity
            or median_circle_fill<min_circle_fill
            or median_axis_ratio<min_axis_ratio
            or radius_cv>max_radius_cv
            or area_cv>max_area_cv
            or aspect_log_median>max_aspect):
            continue
        identity_score=(
            0.28*median_circularity
            +0.22*median_circle_fill
            +0.18*median_solidity
            +0.17*median_axis_ratio
            +0.15*max(0.0,1.0-min(radius_cv,1.0))
        )
        dense_frames=np.arange(int(fr[0]),int(fr[-1])+1)
        if len(dense_frames)<minimum_interval_frames:continue
        detected_fraction=len(fr)/max(1,len(dense_frames))
        if detected_fraction<min_detected:continue
        xd=np.interp(dense_frames,fr,x)
        yd=np.interp(dense_frames,fr,y)
        abs_times=dense_frames/fps

        for sign in (1.0,-1.0):
            q=smooth1(sign*yd,5)
            xs=smooth1(xd,5)
            low=float(np.percentile(q,5))
            high=float(np.percentile(q,95))
            span=high-low
            if span<18.0:continue
            progress=(q-low)/span

            # Every upward 10% crossing is a candidate traversal. Subsequent levels
            # must be crossed in order on the same temporal track.
            starts=[]
            idx=1
            while idx<len(progress):
                tt,j=crossing_after(abs_times,progress,.10,start_index=idx)
                if tt is None:break
                starts.append((tt,j))
                idx=j+1

            for t10,j10 in starts:
                cross_times=[t10];indices=[j10];cursor=j10
                ok=True
                for lev in levels[1:]:
                    tt,j=crossing_after(abs_times,progress,float(lev),start_index=cursor)
                    if tt is None:
                        ok=False;break
                    cross_times.append(tt);indices.append(j);cursor=j
                if not ok:continue
                cross_times=np.asarray(cross_times,float)

                A=np.column_stack([np.ones(len(levels)),sqrt_levels])
                coef=np.linalg.lstsq(A,cross_times,rcond=None)[0]
                t0=float(coef[0]);T=float(coef[1])
                if T<=0:continue
                full_interval_frames=T*fps
                if full_interval_frames<minimum_interval_frames:continue
                if T>1.25:continue
                pred=A@coef
                timing_rms_s=float(np.sqrt(np.mean((cross_times-pred)**2)))
                timing_rms_frames=timing_rms_s*fps

                # Analyze observed trajectory over the inferred release->impact
                # window. Clamp to available track frames only; no extrapolated
                # image positions are fabricated.
                impact=t0+T
                mask=(abs_times>=t0)&(abs_times<=impact)
                ids=np.flatnonzero(mask)
                if len(ids)<minimum_interval_frames:continue
                pseg=progress[ids]
                xseg=xs[ids]
                d=np.diff(pseg)
                monotone=float(np.mean(d>=-.015)) if len(d) else 0.0
                if monotone<.78:continue
                x_drift=float(np.percentile(xseg,95)-np.percentile(xseg,5))/max(span,1e-9)
                if x_drift>.45:continue

                # Position-shape diagnostic on observed normalized displacement.
                trel=abs_times[ids]-t0
                X=np.column_stack([np.ones(len(trel)),trel,trel*trel])
                qcoef=np.linalg.lstsq(X,pseg,rcond=None)[0]
                qpred=X@qcoef
                shape_rms=float(np.sqrt(np.mean((pseg-qpred)**2)))
                qa,qb,qc=map(float,qcoef)
                if qc<=0:continue
                release_ratio=abs(qb)/max(abs(2.0*qc*T),1e-9)
                if release_ratio>.65:continue

                detected_in_window=sum(
                    1 for ff in fr if t0*fps-1e-9 <= ff <= impact*fps+1e-9
                )
                detected_window_fraction=detected_in_window/max(1,len(ids))
                if detected_window_fraction<.45:continue
                gaps=np.diff(fr)
                gap_penalty=float(np.mean(np.maximum(gaps-1,0))) if len(gaps) else 0.0

                # Rank by free-fall timing self-consistency first, then completeness.
                # Target g is absent from this ranking.
                # Once a track is ball-like and satisfies the release-from-rest
                # model, prefer the *fastest* full-travel event. IRIS defines this
                # class as a ball released under gravity; slower same-ball motion is
                # typically reset/handling. The numerical value of g is never used.
                rank=(-identity_score,
                      timing_rms_frames,shape_rms,
                      full_interval_frames,
                      -detected_window_fraction,
                      x_drift,release_ratio,-span)
                cand={
                    "rank":rank,"track_id":track["id"],"sign":sign,
                    "span_px":span,"low_px":low,"high_px":high,
                    "progress":progress,"dense_frames":dense_frames,
                    "abs_times":abs_times,"window_indices":ids,
                    "t0_s":t0,"full_fall_time_s":T,
                    "timing_fit_rms_s":timing_rms_s,
                    "timing_fit_rms_frames":timing_rms_frames,
                    "trajectory_shape_rms_fraction":shape_rms,
                    "release_speed_ratio":release_ratio,
                    "x_drift_fraction":x_drift,
                    "monotone_fraction":monotone,
                    "gap_penalty":gap_penalty,
                    "detected_fraction":detected_fraction,
                    "detected_window_fraction":detected_window_fraction,
                    "identity_score":identity_score,
                    "median_circularity":median_circularity,
                    "median_solidity":median_solidity,
                    "median_circle_fill":median_circle_fill,
                    "median_axis_ratio":median_axis_ratio,
                    "radius_cv":radius_cv,
                    "area_cv":area_cv,
                    "aspect_log_median":aspect_log_median,
                    "interval_frames":len(ids),
                    "detected_frames":detected_in_window,
                    "crossing_times_s":cross_times.tolist(),
                }
                candidates.append(cand)
    return candidates

def _candidate_raw_y(candidate):
    progress=np.asarray(candidate["progress"],float)
    sign=float(candidate["sign"])
    low=float(candidate["low_px"])
    span=float(candidate["span_px"])
    raw=sign*(low+progress*span)
    if raw.ndim!=1 or len(raw)!=len(candidate["abs_times"]):
        raise ContractError("candidate raw-y reconstruction length mismatch")
    if not np.all(np.isfinite(raw)):
        raise ContractError("candidate raw-y reconstruction is not finite")
    return raw

def _global_spatial_envelope(candidates):
    """Estimate full top/bottom image travel from all ball-like tracks.

    Timing from reset/handling is never used here; those motions contribute only
    spatial support for the same ball's full image-space range.
    """
    by_track={}
    for q in candidates:
        tid=int(q["track_id"])
        if tid not in by_track:
            by_track[tid]=_candidate_raw_y(q)
    if not by_track:
        raise TrackSelectionError("no ball-like tracks available for spatial envelope")
    all_y=np.concatenate(list(by_track.values()))
    if len(all_y)<8:
        raise TrackSelectionError("insufficient ball samples for spatial envelope")
    top=float(np.percentile(all_y,2.0))
    bottom=float(np.percentile(all_y,98.0))
    span=bottom-top
    if not math.isfinite(span) or span<18.0:
        raise TrackSelectionError(
            f"invalid global ball spatial envelope: top={top}, bottom={bottom}"
        )
    return top,bottom,span,len(by_track)

def _recalibrate_gravity_candidate(candidate,top,bottom,envelope_px,fps,selector_cfg):
    """Fit full fall time from a partial downward fragment in global coordinates."""
    expected_sign=float(selector_cfg["expected_sign"])
    if float(candidate["sign"])!=expected_sign:
        return None

    raw_y=_candidate_raw_y(candidate)
    if expected_sign>0:
        global_progress=(raw_y-top)/envelope_px
    else:
        global_progress=(bottom-raw_y)/envelope_px

    abs_times=np.asarray(candidate["abs_times"],float)
    ids=np.asarray(candidate["window_indices"],int)
    if len(ids)<6:
        return None
    p=global_progress[ids]
    t=abs_times[ids]
    keep=np.isfinite(p)&np.isfinite(t)&(p>=-0.05)&(p<=1.05)
    p=p[keep];t=t[keep];ids=ids[keep]
    if len(p)<6:
        return None

    order=np.argsort(t)
    p=p[order];t=t[order];ids=ids[order]
    p=np.clip(p,0.0,1.0)
    pspan=float(np.max(p)-np.min(p))
    if pspan<float(selector_cfg["minimum_global_progress_span"]):
        return None

    monotone=float(np.mean(np.diff(p)>=-.02)) if len(p)>1 else 0.0
    if monotone<.78:
        return None

    sqrtp=np.sqrt(np.clip(p,0.0,1.0))
    if float(np.ptp(sqrtp))<0.08:
        return None
    A=np.column_stack([np.ones(len(p)),sqrtp])
    coef=np.linalg.lstsq(A,t,rcond=None)[0]
    t0=float(coef[0]);T=float(coef[1])
    if not math.isfinite(T) or T<=0 or T>1.25:
        return None

    pred=A@coef
    timing_rms_s=float(np.sqrt(np.mean((t-pred)**2)))
    timing_rms_frames=timing_rms_s*fps
    if timing_rms_frames>float(selector_cfg["maximum_global_timing_fit_rms_frames"]):
        return None

    pred_p=np.square(np.clip((t-t0)/T,0.0,1.0))
    shape_rms=float(np.sqrt(np.mean((p-pred_p)**2)))
    if not math.isfinite(shape_rms) or shape_rms>.12:
        return None

    q=dict(candidate)
    q["local_span_px"]=float(candidate["span_px"])
    q["span_px"]=float(envelope_px)
    q["spatial_envelope_top_px"]=float(top)
    q["spatial_envelope_bottom_px"]=float(bottom)
    q["global_progress_span"]=pspan
    q["global_progress_start"]=float(np.min(p))
    q["global_progress_end"]=float(np.max(p))
    q["progress"]=global_progress
    q["window_indices"]=ids
    q["t0_s"]=t0
    q["full_fall_time_s"]=T
    q["timing_fit_rms_s"]=timing_rms_s
    q["timing_fit_rms_frames"]=timing_rms_frames
    q["trajectory_shape_rms_fraction"]=shape_rms
    q["monotone_fraction"]=monotone
    q["interval_frames"]=len(ids)
    q["detected_frames"]=len(ids)
    q["detected_window_fraction"]=float(len(ids)/max(1,int(ids[-1]-ids[0]+1)))
    return validate_candidate(q)

def choose_ballistic_track(tracks,fps,minimum_interval_frames=12,identity_cfg=None):
    identity_cfg=identity_cfg or {}
    selector_cfg=validate_selector_config(identity_cfg)
    if not isinstance(tracks,list):
        raise ContractError(f"tracks must be a list, got {type(tracks).__name__}")

    raw_candidates=[]
    for tr in tracks:
        produced=fit_full_flight_progress(
            tr,fps,minimum_interval_frames=minimum_interval_frames,
            identity_cfg=identity_cfg
        )
        if produced is None:
            raise ContractError(
                "fit_full_flight_progress returned None; producer contract requires list"
            )
        if not isinstance(produced,list):
            raise ContractError(
                "fit_full_flight_progress returned "
                f"{type(produced).__name__}; producer contract requires list"
            )
        for candidate in produced:
            raw_candidates.append(validate_candidate(candidate))
    if not raw_candidates:
        raise TrackSelectionError(
            "no ball-like temporal motion candidates survived frozen filters"
        )

    top,bottom,envelope_px,envelope_tracks=_global_spatial_envelope(raw_candidates)
    calibrated=[]
    for q in raw_candidates:
        z=_recalibrate_gravity_candidate(
            q,top,bottom,envelope_px,fps,selector_cfg
        )
        if z is not None:
            calibrated.append(z)
    if not calibrated:
        raise TrackSelectionError(
            "no downward ball fragment survived global-envelope free-fall calibration"
        )

    # The spatial envelope may come from the later slow reset, but timing selection
    # uses only the expected gravity direction. Prefer the strongest global timing
    # fit and the largest observed fraction of the full drop; absolute event time is
    # a late tie-breaker. Target g is never used.
    def event_rank(q):
        return (
            float(q["timing_fit_rms_frames"]),
            float(q["trajectory_shape_rms_fraction"]),
            -float(q["global_progress_span"]),
            float(q["t0_s"]),
            float(q["release_speed_ratio"]),
            float(q["x_drift_fraction"]),
            -float(q["identity_score"]),
        )

    calibrated=sorted(calibrated,key=event_rank)
    chosen=calibrated[0]

    audit=[]
    for i,q in enumerate(calibrated[:20],start=1):
        audit.append({
            "event_rank":i,
            "selected":q is chosen,
            "track_id":q["track_id"],
            "sign":float(q["sign"]),
            "t0_s":float(q["t0_s"]),
            "local_span_px":float(q["local_span_px"]),
            "spatial_envelope_px":float(q["span_px"]),
            "relative_span":float(q["local_span_px"])/max(float(q["span_px"]),1e-9),
            "global_progress_span":float(q["global_progress_span"]),
            "global_progress_start":float(q["global_progress_start"]),
            "global_progress_end":float(q["global_progress_end"]),
            "full_fall_time_s":float(q["full_fall_time_s"]),
            "interval_frames":int(q["interval_frames"]),
            "detected_frames":int(q["detected_frames"]),
            "detected_fraction":float(q["detected_window_fraction"]),
            "timing_fit_rms_frames":float(q["timing_fit_rms_frames"]),
            "trajectory_shape_rms_fraction":float(q["trajectory_shape_rms_fraction"]),
            "release_speed_ratio":float(q["release_speed_ratio"]),
            "x_drift_fraction":float(q["x_drift_fraction"]),
            "gap_penalty":float(q["gap_penalty"]),
            "identity_score":float(q["identity_score"]),
            "median_circularity":float(q["median_circularity"]),
            "median_solidity":float(q["median_solidity"]),
            "median_circle_fill":float(q["median_circle_fill"]),
            "median_axis_ratio":float(q["median_axis_ratio"]),
            "radius_cv":float(q["radius_cv"]),
            "area_cv":float(q["area_cv"]),
            "aspect_log_median":float(q["aspect_log_median"]),
            "envelope_track_count":int(envelope_tracks),
        })
    return chosen,audit

def extract(video,drop_height,width=640,max_seconds=5.0,minimum_interval_frames=12,tracker_config=None):
    cv2=import_cv();cap=cv2.VideoCapture(str(video))
    if not cap.isOpened():raise RuntimeError(f"cannot open {video}")
    fps=float(cap.get(cv2.CAP_PROP_FPS));frames=int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    n=min(frames,max(120,int(fps*max_seconds)));cap.release()
    if n<int(fps*.4):raise RuntimeError("video too short")
    cap=cv2.VideoCapture(str(video));ids=np.linspace(0,n-1,min(31,n),dtype=int);samples=[]
    for i in ids:
        cap.set(cv2.CAP_PROP_POS_FRAMES,int(i));ok,fr=cap.read()
        if ok:samples.append(resize_gray(fr,width,cv2))
    cap.release()
    if len(samples)<8:raise RuntimeError("too few background samples")
    stack=np.stack(samples);bg=np.median(stack,axis=0).astype(np.uint8)
    energy=np.mean(np.abs(stack.astype(np.float32)-bg.astype(np.float32)),axis=0)
    cut=max(4.0,float(np.percentile(energy,99.0)));mask=(energy>=cut).astype(np.uint8)*255
    mask=cv2.dilate(mask,cv2.getStructuringElement(cv2.MORPH_ELLIPSE,(9,9)),iterations=2)
    contours,_=cv2.findContours(mask,cv2.RETR_EXTERNAL,cv2.CHAIN_APPROX_SIMPLE)
    if contours:
        pts=np.vstack([cc.reshape(-1,2) for cc in contours]);x,y0,w,h=cv2.boundingRect(pts)
        mx=max(25,int(.25*w));my=max(25,int(.15*h));x0=max(0,x-mx);yy0=max(0,y0-my)
        x1=min(bg.shape[1],x+w+mx);y1=min(bg.shape[0],y0+h+my)
        if (x1-x0)*(y1-yy0)<.03*bg.size:x0=yy0=0;y1,x1=bg.shape
    else:x0=yy0=0;y1,x1=bg.shape

    frame_candidates=[]
    cap=cv2.VideoCapture(str(video));i=0
    while i<n:
        ok,fr=cap.read()
        if not ok:break
        g=resize_gray(fr,width,cv2)
        diff=cv2.absdiff(g,bg);diff=cv2.GaussianBlur(diff,(5,5),0)
        crop=diff[yy0:y1,x0:x1]
        q=max(7.0,float(np.percentile(crop,99.5))*.55)
        frame_candidates.append(extract_components(crop,q,cv2,x0,yy0))
        i+=1
    cap.release()

    tracks=build_temporal_tracks(frame_candidates,fps)
    if not tracks:raise RuntimeError("no compact temporal motion tracks")
    selected=choose_ballistic_track(
        tracks,fps,minimum_interval_frames=minimum_interval_frames,
        identity_cfg=tracker_config
    )
    if (not isinstance(selected,tuple)) or len(selected)!=2:
        raise ContractError("choose_ballistic_track must return (candidate, audit)")
    chosen,candidate_audit=selected
    validate_candidate(chosen)
    if not isinstance(candidate_audit,list):
        raise ContractError("candidate audit must be a list")

    ids=np.asarray(chosen["window_indices"],int)
    abs_times=np.asarray(chosen["abs_times"],float)
    progress=np.asarray(chosen["progress"],float)
    t0=float(chosen["t0_s"]);T=float(chosen["full_fall_time_s"])
    times=abs_times[ids]-t0
    position_m=np.clip(progress[ids],0.0,1.0)*drop_height
    if len(times)<minimum_interval_frames:
        raise RuntimeError("selected full-flight window violates minimum duration")

    one_px=drop_height/max(float(chosen["span_px"]),1e-9)
    ghat=2.0*drop_height/(T*T)
    X=np.column_stack([np.ones(len(times)),times,.5*times*times])
    trajectory_coef=np.linalg.lstsq(X,position_m,rcond=None)[0]
    trajectory_g=float(trajectory_coef[2])

    return {
        "fps":fps,
        "valid_fraction":float(sum(bool(x) for x in frame_candidates)/max(1,len(frame_candidates))),
        "span_px":float(chosen["span_px"]),
        "one_pixel_m":one_px,
        "active_frames":len(times),
        "times":times,
        "position_m":position_m,
        "direct_acceleration_m_s2":ghat,
        "trajectory_acceleration_m_s2":trajectory_g,
        "full_fall_time_s":T,
        "timing_fit_rms_frames":float(chosen["timing_fit_rms_frames"]),
        "trajectory_shape_rms_fraction":float(chosen["trajectory_shape_rms_fraction"]),
        "plateau_relative_mad":0.0,
        "monotone_fraction":float(chosen["monotone_fraction"]),
        "forward_fraction":float(chosen["monotone_fraction"]),
        "selected_direction_sign":float(chosen["sign"]),
        "duration_10_90_s":float(T*(math.sqrt(.90)-math.sqrt(.10))),
        "release_time_s_absolute":t0,
        "track_id":chosen["track_id"],
        "release_speed_ratio":float(chosen["release_speed_ratio"]),
        "x_drift_fraction":float(chosen["x_drift_fraction"]),
        "gap_penalty":float(chosen["gap_penalty"]),
        "candidate_track_count":len(tracks),
        "interval_frames":int(chosen["interval_frames"]),
        "detected_frames":int(chosen["detected_frames"]),
        "detected_fraction":float(chosen["detected_window_fraction"]),
        "identity_score":float(chosen["identity_score"]),
        "median_circularity":float(chosen["median_circularity"]),
        "median_solidity":float(chosen["median_solidity"]),
        "median_circle_fill":float(chosen["median_circle_fill"]),
        "median_axis_ratio":float(chosen["median_axis_ratio"]),
        "radius_cv":float(chosen["radius_cv"]),
        "area_cv":float(chosen["area_cv"]),
        "aspect_log_median":float(chosen["aspect_log_median"]),
        "edge_trim_left":0,
        "edge_trim_right":0,
        "candidate_audit":candidate_audit,
        "roi":[int(x0),int(yy0),int(x1-x0),int(y1-yy0)]
    }

def residuals(t,y,acc,fit_n):
    X=np.column_stack([np.ones(fit_n),t[:fit_n]])
    nuisance=np.linalg.lstsq(X,y[:fit_n]-.5*acc*t[:fit_n]**2,rcond=None)[0]
    pred=nuisance[0]+nuisance[1]*t+.5*acc*t*t
    return np.abs(y-pred),nuisance

def score(tr,L0,L1):
    t=np.asarray(tr["times"]);y=np.asarray(tr["position_m"]);n=len(t);fit=max(4,int(.40*n))
    r0,_=residuals(t,y,L0,fit);r1,_=residuals(t,y,L1,fit)
    imp=(r0-r1)[fit:]
    B=min(6,len(imp));chunks=np.array_split(imp,B);block=[float(np.mean(x)) for x in chunks if len(x)]
    se=max(robust(block)/math.sqrt(len(block)),float(tr["one_pixel_m"]))
    primary=statistics.fmean(block)/se if block else 0.0
    endpoint=(float(r0[-1])-float(r1[-1]))/max(float(tr["one_pixel_m"]),1e-9)
    return primary,endpoint

def self_test_video():
    cv2=import_cv()
    with tempfile.TemporaryDirectory() as td:
        p=pathlib.Path(td)/"drop.avi"
        fps=60.0;w,h=640,360;drop_m=1.0;span_px=220.0
        T=math.sqrt(2*drop_m/G)
        writer=cv2.VideoWriter(str(p),cv2.VideoWriter_fourcc(*"MJPG"),fps,(w,h))
        if not writer.isOpened():raise RuntimeError("synthetic writer unavailable")
        total=int(fps*2.5)
        for i in range(total):
            t=i/fps
            q=np.zeros((h,w,3),dtype=np.uint8)
            if t<.35:
                frac=0.0
            elif t<.35+T:
                u=(t-.35)/T;frac=min(1.0,u*u)
            elif t<1.45:
                frac=1.0
            else:
                frac=max(0.0,1.0-(t-1.45)/.85)
            yy=60+span_px*frac
            # Make the gravity event intentionally incomplete in image space while
            # leaving the later slow reset fully visible. V6.3 must use the reset
            # only for the spatial envelope and recover full T from the partial drop.
            visible=True
            if .35<=t<.35+T:
                visible=(.12<=frac<=.88)
            if visible:
                cv2.circle(q,(320,int(round(yy))),10,(255,255,255),-1)

            # Deliberate non-ball distractor: excellent quadratic motion but wrong
            # duration/acceleration. V6 must reject it by object identity.
            if .25<=t<=.95:
                u=(t-.25)/.70
                dfrac=min(1.0,max(0.0,u*u))
                dy=45+230*dfrac
                cv2.rectangle(q,(105,int(round(dy))-5),(145,int(round(dy))+5),(255,255,255),-1)
            writer.write(q)
        writer.release()

        base=extract(p,drop_m,width=640,max_seconds=2.5,minimum_interval_frames=12)
        rel=abs(base["direct_acceleration_m_s2"]-G)/G
        assert rel<=0.20,(base["direct_acceleration_m_s2"],rel,base)
        assert base["active_frames"]>=12

        v6cfg={
            "minimum_median_circularity":0.35,
            "minimum_median_solidity":0.65,
            "minimum_median_circle_fill":0.45,
            "minimum_median_axis_ratio":0.55,
            "maximum_radius_cv":0.45,
            "maximum_area_cv":0.65,
            "maximum_aspect_log_mad":0.45,
            "minimum_detected_fraction":0.50,
            "expected_image_gravity_sign":1.0,
            "minimum_global_progress_span":0.15,
            "maximum_global_timing_fit_rms_frames":2.5,
        }
        ident=extract(
            p,drop_m,width=640,max_seconds=2.5,minimum_interval_frames=12,
            tracker_config=v6cfg
        )
        ident_rel=abs(ident["direct_acceleration_m_s2"]-G)/G
        assert ident_rel<=0.20,(ident["direct_acceleration_m_s2"],ident_rel,ident)
        assert 0.0<=ident["median_circularity"]<=1.0
        assert 0.0<=ident["median_solidity"]<=1.0
        assert 0.0<=ident["median_circle_fill"]<=1.0
        assert 0.0<=ident["median_axis_ratio"]<=1.0
        assert ident["median_circularity"]>=v6cfg["minimum_median_circularity"]
        assert ident["median_solidity"]>=v6cfg["minimum_median_solidity"]
        assert ident["median_circle_fill"]>=v6cfg["minimum_median_circle_fill"]
        assert ident["median_axis_ratio"]>=v6cfg["minimum_median_axis_ratio"]
        assert ident["radius_cv"]<=v6cfg["maximum_radius_cv"]
        assert ident["aspect_log_median"]<=v6cfg["maximum_aspect_log_mad"]
        audit=ident.get("candidate_audit",[])
        assert audit, "candidate audit missing"
        selected=[q for q in audit if q["selected"]]
        assert len(selected)==1, selected
        assert selected[0]["sign"]==v6cfg["expected_image_gravity_sign"]
        assert selected[0]["global_progress_span"]>=v6cfg["minimum_global_progress_span"]
        # The gravity event is deliberately fragmented, so selection must succeed
        # without requiring it to span most of the image-space envelope.
        assert selected[0]["relative_span"]<0.95, selected[0]
        print("VALID IRIS free-fall synthetic-video tracker",
              ident["direct_acceleration_m_s2"],ident_rel,
              "identity",ident["identity_score"],
              "circularity",ident["median_circularity"],
              "solidity",ident["median_solidity"],
              "circle_fill",ident["median_circle_fill"],
              "axis_ratio",ident["median_axis_ratio"],
              "radius_cv",ident["radius_cv"])

FIELDS=["record_version","trial_id","paired_key","dataset","scene","split","evidence_class","confirmatory",
"trial_family","ground_truth","method","score","decision","decision_threshold","confidence","physical_delta",
"target_error_delta","measurement_noise_sigma","pose_noise_sigma","missing_fraction","channel_dependence",
"negative_control","seed","source_artifact","notes"]

def manifests(root,split,config):
    spec=config["dataset"][split]
    desired=(spec["setting"],set(spec["takes"]))
    out=[]
    for p in (root/"iris").glob("*/manifest.json"):
        m=json.loads(p.read_text());parts=m["scene"].split("/")
        if len(parts)==3 and parts[0]=="dropping_ball" and parts[1]==desired[0] and parts[2] in desired[1]:
            out.append((p.parent,m))
    return sorted(out,key=lambda x:x[1]["scene"])

def drop_height_from_manifest(m,expected):
    try:h=float(m["ground_truth"]["parameters"]["drop_height"]["mean"])
    except Exception as e:raise RuntimeError(f"IRIS drop_height missing from manifest: {e}")
    if abs(h-expected)>1e-6:
        raise RuntimeError(f"drop-height drift: manifest={h} config={expected}")
    return h

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--adapted-root",type=pathlib.Path,default=pathlib.Path("build/publication-validation/public-data/adapted"))
    ap.add_argument("--config",type=pathlib.Path,default=pathlib.Path("research/validation/iris_freefall_blind_v1.json"))
    ap.add_argument("--split",choices=("development","validation","final_test"),default="development")
    ap.add_argument("--out",type=pathlib.Path,required=False)
    ap.add_argument("--require-development-summary",type=pathlib.Path)
    ap.add_argument("--require-validation-summary",type=pathlib.Path)
    ap.add_argument("--lock",type=pathlib.Path)
    ap.add_argument("--self-test",action="store_true")
    ap.add_argument("--self-test-video",action="store_true")
    a=ap.parse_args();cfg=json.loads(a.config.read_text())
    if a.self_test:
        assert len(cases())==11
        assert decision(2)=="support" and decision(-2)=="veto"

        # Producer contract: short/ineligible tracks return [], never None.
        short={"id":0,"pts":[
            {"frame":i,"x":0.0,"y":float(i),"area":10,
             "circularity":1.0,"solidity":1.0,"circle_fill":1.0,
             "axis_ratio":1.0,"radius":2.0,"aspect_log_abs":0.0}
            for i in range(7)
        ]}
        produced=fit_full_flight_progress(short,60.0,12,{})
        assert produced==[], produced

        # Consumer contract: empty candidate population is a controlled selection
        # failure, not a Python TypeError.
        try:
            choose_ballistic_track([],60.0,12,{})
        except TrackSelectionError:
            pass
        else:
            raise AssertionError("empty track population did not raise TrackSelectionError")

        # Invalid selector config is rejected explicitly.
        try:
            validate_selector_config({"minimum_global_progress_span":0.0})
        except ContractError:
            pass
        else:
            raise AssertionError("invalid global progress span was accepted")

        print("VALID IRIS free-fall analyzer self-test");return
    if a.self_test_video:
        self_test_video();return
    if a.split=="validation":
        d=json.loads(a.require_development_summary.read_text()) if a.require_development_summary else {}
        if not d.get("gate_pass"):raise SystemExit("development gate failed/missing")
        expected_revision=cfg["tracker"]["revision"]
        if d.get("tracker_revision")!=expected_revision:raise SystemExit("development tracker revision mismatch")
    if a.split=="final_test":
        v=json.loads(a.require_validation_summary.read_text()) if a.require_validation_summary else {}
        if not v.get("gate_pass"):raise SystemExit("validation gate failed/missing")
        expected_revision=cfg["tracker"]["revision"]
        if v.get("tracker_revision")!=expected_revision:raise SystemExit("validation tracker revision mismatch")
        if not a.lock:raise SystemExit("final_test requires lock")
        lock_checker=("research/analysis/freeze_iris_freefall_v6_final.py"
                      if int(cfg.get("version",5))>=6
                      else "research/analysis/freeze_iris_freefall_final_test.py")
        subprocess.run(["python3",lock_checker,"--check",str(a.lock)],check=True)
    out=a.out or pathlib.Path(f"build/publication-validation/iris-freefall-{a.split}")
    mm=manifests(a.adapted_root,a.split,cfg)
    expected=len(cfg["dataset"][a.split]["takes"])
    if len(mm)!=expected:raise SystemExit(f"expected {expected} frozen scenes, got {len(mm)}")
    rows=[];takes=[];fails=[];failure_details=[];candidate_audit_rows=[]
    setting=cfg["dataset"][a.split]["setting"]
    expected_height=float(cfg["physics"]["drop_heights_m"][setting])
    tg=cfg["tracker"]
    for pkg,m in mm:
        try:
            height=drop_height_from_manifest(m,expected_height)
            tr=extract(pathlib.Path(m["video"]["path"]),height,
                       width=tg["analysis_width"],max_seconds=tg["max_seconds"],
                       minimum_interval_frames=tg["minimum_active_frames"],
                       tracker_config=tg)
            for q in tr.get("candidate_audit",[]):
                qq=dict(q)
                qq["scene"]=m["scene"]
                qq["split"]=a.split
                qq["direct_acceleration_m_s2"]=2.0*height/(float(q["full_fall_time_s"])**2)
                qq["acceleration_relative_error"]=abs(qq["direct_acceleration_m_s2"]-G)/G
                candidate_audit_rows.append(qq)
            checks={
                "active_frames":tr["active_frames"]>=tg["minimum_active_frames"],
                "monotone":tr["monotone_fraction"]>=tg.get("minimum_monotone_fraction",.78),
                "timing_fit":tr["timing_fit_rms_frames"]<=tg.get("maximum_timing_fit_rms_frames",2.5),
                "trajectory_shape":tr["trajectory_shape_rms_fraction"]<=tg.get("maximum_trajectory_shape_rms_fraction",.10),
                "release_speed":tr["release_speed_ratio"]<=tg.get("maximum_release_speed_ratio",.65),
                "x_drift":tr["x_drift_fraction"]<=tg.get("maximum_x_drift_fraction",.45),
                "gap_penalty":tr["gap_penalty"]<=tg.get("maximum_gap_penalty",.50),
                "circularity":tr["median_circularity"]>=tg.get("minimum_median_circularity",0.0),
                "solidity":tr["median_solidity"]>=tg.get("minimum_median_solidity",0.0),
                "circle_fill":tr["median_circle_fill"]>=tg.get("minimum_median_circle_fill",0.0),
                "axis_ratio":tr["median_axis_ratio"]>=tg.get("minimum_median_axis_ratio",0.0),
                "radius_cv":tr["radius_cv"]<=tg.get("maximum_radius_cv",float("inf")),
                "area_cv":tr["area_cv"]<=tg.get("maximum_area_cv",float("inf")),
                "aspect_identity":tr["aspect_log_median"]<=tg.get("maximum_aspect_log_mad",float("inf")),
                "detected_fraction":tr["detected_fraction"]>=tg.get("minimum_detected_fraction",0.45),
            }
            qok=all(checks.values())
            reject=";".join(k for k,v in checks.items() if not v)
            takes.append({"scene":m["scene"],"quality_ok":qok,"quality_reject_reason":reject,
                          "direct_acceleration_m_s2":tr["direct_acceleration_m_s2"],
                          "trajectory_acceleration_m_s2":tr["trajectory_acceleration_m_s2"],
                          "acceleration_relative_error":abs(tr["direct_acceleration_m_s2"]-G)/G,
                          "active_frames":tr["active_frames"],"valid_fraction":tr["valid_fraction"],
                          "span_px":tr["span_px"],"one_pixel_m":tr["one_pixel_m"],
                          "full_fall_time_s":tr["full_fall_time_s"],
                          "timing_fit_rms_frames":tr["timing_fit_rms_frames"],
                          "trajectory_shape_rms_fraction":tr["trajectory_shape_rms_fraction"],
                          "release_speed_ratio":tr["release_speed_ratio"],
                          "x_drift_fraction":tr["x_drift_fraction"],
                          "gap_penalty":tr["gap_penalty"],
                          "candidate_track_count":tr["candidate_track_count"],
                          "interval_frames":tr["interval_frames"],
                          "detected_frames":tr["detected_frames"],
                          "detected_fraction":tr["detected_fraction"],
                          "selected_direction_sign":tr["selected_direction_sign"],
                          "release_time_s_absolute":tr["release_time_s_absolute"],
                          "global_progress_span":float(
                              max(np.asarray(tr["position_m"],float))/height
                              - min(np.asarray(tr["position_m"],float))/height
                          ),
                          "identity_score":tr["identity_score"],
                          "median_circularity":tr["median_circularity"],
                          "median_solidity":tr["median_solidity"],
                          "median_circle_fill":tr["median_circle_fill"],
                          "median_axis_ratio":tr["median_axis_ratio"],
                          "radius_cv":tr["radius_cv"],
                          "area_cv":tr["area_cv"],
                          "aspect_log_median":tr["aspect_log_median"],
                          "edge_trim_left":tr["edge_trim_left"],
                          "edge_trim_right":tr["edge_trim_right"],
                          "monotone_fraction":tr["monotone_fraction"]})
            if not qok:continue
            for name,fac,truth,family,neg in cases():
                g0=1.25*G;g1=fac*G;s1,s2=score(tr,g0,g1)
                for method,s in (("freefall_trajectory_probe",s1),("endpoint_kinematic_baseline",s2)):
                    d=decision(s);key=f"freefall:{m['scene']}:{name}"
                    rows.append({"record_version":1,"trial_id":f"{key}:{method}","paired_key":key,
                     "dataset":"iris_real_freefall_"+("rescue_v6" if cfg.get("version")==6 else "blind"),"scene":m["scene"],"split":a.split,
                     "evidence_class":"prospective_real_video_freefall_probe",
                     "confirmatory":str(a.split=="final_test").lower(),"trial_family":family,"ground_truth":truth,
                     "method":method,"score":s,"decision":d,"decision_threshold":TAU,"confidence":"",
                     "physical_delta":math.log(g1/g0),"target_error_delta":abs(math.log(g1/G))-abs(math.log(g0/G)),
                     "measurement_noise_sigma":tr["one_pixel_m"],"pose_noise_sigma":"","missing_fraction":1-tr["valid_fraction"],
                     "channel_dependence":0.0,"negative_control":str(neg).lower(),"seed":0,"source_artifact":str(pkg),
                     "notes":f"factor={fac}; drop_height_m={height}; tracker={tg['revision']}; standardized evidence score; take01 forbidden"})
        except Exception as e:
            kind=("no_valid_candidate" if isinstance(e,TrackSelectionError)
                  else "implementation_or_io_error")
            short={"scene":m["scene"],"kind":kind,
                   "error":f"{type(e).__name__}: {e}"}
            fails.append(short)
            failure_details.append({
                **short,
                "traceback":traceback.format_exc(),
            })
    out.mkdir(parents=True,exist_ok=True)
    if failure_details:
        (out/"failure_details.json").write_text(
            json.dumps(failure_details,indent=2)+"\n"
        )
    if rows:
        with (out/"validation_records.csv").open("w",newline="",encoding="utf-8") as f:
            w=csv.DictWriter(f,fieldnames=FIELDS);w.writeheader();w.writerows(rows)
    if takes:
        with (out/"take_summary.csv").open("w",newline="",encoding="utf-8") as f:
            w=csv.DictWriter(f,fieldnames=list(takes[0]));w.writeheader();w.writerows(takes)
    if candidate_audit_rows:
        fields=["scene","split","event_rank","selected","track_id","sign","t0_s",
                "local_span_px","spatial_envelope_px","relative_span",
                "global_progress_span","global_progress_start","global_progress_end",
                "full_fall_time_s","interval_frames","detected_frames","detected_fraction",
                "timing_fit_rms_frames","trajectory_shape_rms_fraction","release_speed_ratio",
                "x_drift_fraction","gap_penalty","identity_score","median_circularity",
                "median_solidity","median_circle_fill","median_axis_ratio","radius_cv","area_cv",
                "aspect_log_median","envelope_track_count","direct_acceleration_m_s2",
                "acceleration_relative_error"]
        with (out/"candidate_audit.csv").open("w",newline="",encoding="utf-8") as f:
            w=csv.DictWriter(f,fieldnames=fields);w.writeheader()
            for row in candidate_audit_rows:
                w.writerow({k:row.get(k,"") for k in fields})
    quality=sum(bool(x["quality_ok"]) for x in takes)
    errs=[x["acceleration_relative_error"] for x in takes if x["quality_ok"]]
    median_err=statistics.median(errs) if errs else None
    primary=[r for r in rows if r["method"]=="freefall_trajectory_probe"]
    truth_ctrl=[r for r in primary if r["trial_family"]=="truth_control"]
    placebo=[r for r in primary if r["ground_truth"]=="unresolved"]
    directional=[r for r in primary if r["ground_truth"]!="unresolved"]
    tc=sum(r["decision"]==r["ground_truth"] for r in truth_ctrl)/len(truth_ctrl) if truth_ctrl else 0
    pfar=sum(r["decision"]!="unresolved" for r in placebo)/len(placebo) if placebo else 1
    sign=sum((float(r["score"])>0)==(r["ground_truth"]=="support") for r in directional)/len(directional) if directional else 0
    if a.split=="development":
        dg=cfg.get("development_gate",{"minimum_quality_videos":3,"maximum_median_acceleration_relative_error":.20})
        gate=(quality>=dg["minimum_quality_videos"] and median_err is not None
              and median_err<=dg["maximum_median_acceleration_relative_error"])
    elif a.split=="validation":
        vg=cfg["validation_gate"]
        gate=(quality>=vg["minimum_quality_videos"]
              and tc>=vg["minimum_truth_control_accuracy"]
              and pfar<=vg["placebo_false_assertion_rate"]
              and sign>=vg["minimum_direction_sign_rate"]
              and (median_err is not None)
              and median_err<=vg.get("maximum_median_acceleration_relative_error",float("inf")))
    else:gate=True
    implementation_errors=sum(
        1 for x in fails if x.get("kind")=="implementation_or_io_error"
    )
    if implementation_errors:
        gate=False
    summary={"schema":"vulkax.iris_freefall_result","version":int(cfg.get("version",5)),"tracker_revision":tg["revision"],
      "split":a.split,"expected_videos":expected,"quality_pass_videos":quality,
      "failures":fails,"implementation_errors":implementation_errors,
      "median_acceleration_relative_error":median_err,"records":len(rows),
      "candidate_audit_records":len(candidate_audit_rows),"truth_control_accuracy":tc,
      "placebo_false_assertion_rate":pfar,"direction_sign_rate":sign,"gate_pass":gate,
      "take01_forbidden":True,
      "claim_guard":("Different equation-family replication. "+(
          "V6 uses fresh takes after the failed V5 validation; old validation is permanently nonconfirmatory. "
          if int(cfg.get("version",5))>=6 else ""
      )+"Gravity estimation itself is not novel.")}
    (out/"summary.json").write_text(json.dumps(summary,indent=2)+"\n")
    print("VALID IRIS free-fall",a.split)
    for t in takes:
        print("TAKE",t["scene"],
              "QUALITY",t["quality_ok"],
              "REJECT",t["quality_reject_reason"] or "none",
              "G_REL_ERR",t["acceleration_relative_error"],
              "TIMING_RMS_FRAMES",t["timing_fit_rms_frames"],
              "SHAPE_RMS",t["trajectory_shape_rms_fraction"],
              "RELEASE_RATIO",t["release_speed_ratio"],
              "X_DRIFT",t["x_drift_fraction"],
              "GAP",t["gap_penalty"],
              "TRACKS",t["candidate_track_count"],
              "INTERVAL_FRAMES",t["interval_frames"],
              "DETECTED_FRAMES",t["detected_frames"],
              "DETECTED_FRACTION",t["detected_fraction"],
              "SIGN",t["selected_direction_sign"],
              "GLOBAL_PROGRESS_SPAN",t["global_progress_span"],
              "IDENTITY",t["identity_score"],
              "CIRCULARITY",t["median_circularity"],
              "SOLIDITY",t["median_solidity"],
              "CIRCLE_FILL",t["median_circle_fill"],
              "AXIS_RATIO",t["median_axis_ratio"],
              "RADIUS_CV",t["radius_cv"],
              "AREA_CV",t["area_cv"],
              "ASPECT_LOG",t["aspect_log_median"])
    for e in fails:
        print("TAKE_FAIL",e["scene"],"KIND",e["kind"],e["error"])
    if failure_details:
        print("FAILURE_DETAILS",out/"failure_details.json")
    for k,v in summary.items():
        if not isinstance(v,(list,dict)):print(k.upper(),v)
    print("OUT",out)
    if not gate and a.split in ("development","validation"):
        if implementation_errors:
            raise SystemExit(
                f"{a.split} implementation error: {implementation_errors} take(s) crashed; "
                f"see {out/'failure_details.json'}"
            )
        raise SystemExit(f"{a.split} gate failed")
if __name__=="__main__":main()

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

def _reject(diagnostics, reason):
    """Record a controlled candidate rejection and return None.

    This helper is deliberately module-level so no rejection path depends on a
    closure existing in another function/scope.
    """
    if diagnostics is not None:
        diagnostics[reason]=int(diagnostics.get(reason,0))+1
    return None

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
        "plateau_max_speed_px_per_frame":float(
            cfg.get("plateau_max_speed_px_per_frame",0.75)
        ),
        "plateau_min_frames":int(cfg.get("plateau_min_frames",4)),
        "plateau_max_gap_frames":int(cfg.get("plateau_max_gap_frames",24)),
        "plateau_max_x_delta_px":float(cfg.get("plateau_max_x_delta_px",45.0)),
        "plateau_overlap_frames":int(cfg.get("plateau_overlap_frames",6)),
        "event_edge_fraction":float(cfg.get("event_edge_fraction",0.03)),
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

def _recalibrate_gravity_candidate(candidate,top,bottom,envelope_px,fps,selector_cfg,
                                  diagnostics=None):
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
        return _reject(diagnostics,"too_few_points")

    order=np.argsort(t)
    p=p[order];t=t[order];ids=ids[order]
    p=np.clip(p,0.0,1.0)
    pspan=float(np.max(p)-np.min(p))
    if pspan<float(selector_cfg["minimum_global_progress_span"]):
        return _reject(diagnostics,"progress_span")

    monotone=float(np.mean(np.diff(p)>=-.02)) if len(p)>1 else 0.0
    if monotone<.78:
        return _reject(diagnostics,"monotone")

    sqrtp=np.sqrt(np.clip(p,0.0,1.0))
    if float(np.ptp(sqrtp))<0.08:
        return None
    A=np.column_stack([np.ones(len(p)),sqrtp])
    coef=np.linalg.lstsq(A,t,rcond=None)[0]
    t0=float(coef[0]);T=float(coef[1])
    if not math.isfinite(T) or T<=0 or T>1.25:
        return _reject(diagnostics,"invalid_T")

    pred=A@coef
    timing_rms_s=float(np.sqrt(np.mean((t-pred)**2)))
    timing_rms_frames=timing_rms_s*fps
    if timing_rms_frames>float(selector_cfg["maximum_global_timing_fit_rms_frames"]):
        return _reject(diagnostics,"timing_rms")

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

def choose_ballistic_track_legacy(tracks,fps,minimum_interval_frames=12,identity_cfg=None):
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
    legacy_rejections={}
    for q in raw_candidates:
        z=_recalibrate_gravity_candidate(
            q,top,bottom,envelope_px,fps,selector_cfg,
            diagnostics=legacy_rejections
        )
        if z is not None:
            calibrated.append(z)
    if not calibrated:
        raise TrackSelectionError(
            "no downward ball fragment survived global-envelope free-fall calibration; "
            f"rejects={json.dumps(legacy_rejections,sort_keys=True)}"
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

def _identity_track_chunks(tracks,fps,identity_cfg):
    cfg=identity_cfg or {}
    min_circularity=float(cfg.get("minimum_median_circularity",0.0))
    min_solidity=float(cfg.get("minimum_median_solidity",0.0))
    min_circle_fill=float(cfg.get("minimum_median_circle_fill",0.0))
    min_axis_ratio=float(cfg.get("minimum_median_axis_ratio",0.0))
    max_radius_cv=float(cfg.get("maximum_radius_cv",float("inf")))
    max_area_cv=float(cfg.get("maximum_area_cv",float("inf")))
    max_aspect=float(cfg.get("maximum_aspect_log_mad",float("inf")))
    min_detected=float(cfg.get("minimum_detected_fraction",0.45))
    out=[]
    chunk_id=0
    for tr in tracks:
        pts=tr["pts"]
        if len(pts)<6:
            continue
        frames=np.asarray([p["frame"] for p in pts],int)
        cuts=[0]
        for i in range(1,len(frames)):
            if frames[i]-frames[i-1]>5:
                cuts.append(i)
        cuts.append(len(frames))
        for aa,bb in zip(cuts,cuts[1:]):
            if bb-aa<6:
                continue
            qpts=pts[aa:bb]
            fr=np.asarray([p["frame"] for p in qpts],int)
            x=np.asarray([p["x"] for p in qpts],float)
            y=np.asarray([p["y"] for p in qpts],float)
            circularities=np.asarray([p.get("circularity",0.0) for p in qpts],float)
            solidities=np.asarray([p.get("solidity",0.0) for p in qpts],float)
            circle_fills=np.asarray([p.get("circle_fill",0.0) for p in qpts],float)
            axis_ratios=np.asarray([p.get("axis_ratio",0.0) for p in qpts],float)
            radii=np.asarray([p.get("radius",0.0) for p in qpts],float)
            areas=np.asarray([p["area"] for p in qpts],float)
            aspects=np.asarray([p.get("aspect_log_abs",0.0) for p in qpts],float)
            mc=float(np.median(circularities));ms=float(np.median(solidities))
            mf=float(np.median(circle_fills));ma=float(np.median(axis_ratios))
            rcv=float(np.std(radii)/max(np.mean(radii),1e-9))
            acv=float(np.std(areas)/max(np.mean(areas),1e-9))
            alm=float(np.median(aspects))
            if (mc<min_circularity or ms<min_solidity or mf<min_circle_fill
                or ma<min_axis_ratio or rcv>max_radius_cv or acv>max_area_cv
                or alm>max_aspect):
                continue
            dense=np.arange(int(fr[0]),int(fr[-1])+1)
            if len(dense)<6:
                continue
            detected_fraction=len(fr)/max(1,len(dense))
            if detected_fraction<min_detected:
                continue
            xd=np.interp(dense,fr,x)
            yd=np.interp(dense,fr,y)
            ad=np.interp(dense,fr,areas)
            cad=np.interp(
                dense,fr,
                np.asarray([max(float(p.get("contour_area",p["area"])),1.0) for p in qpts],float)
            )
            rd=np.interp(dense,fr,np.maximum(radii,1e-6))
            gaps=np.diff(fr)
            gap_penalty=float(np.mean(np.maximum(gaps-1,0))) if len(gaps) else 0.0
            identity=(
                .28*mc+.22*mf+.18*ms+.17*ma
                +.15*max(0.0,1.0-min(rcv,1.0))
            )
            out.append({
                "track_id":int(tr["id"]),"chunk_id":chunk_id,
                "frames":dense,"abs_times":dense/fps,"x":xd,"y":yd,
                "area":ad,"contour_area":cad,"radius":rd,
                "detected_frames_original":fr,
                "detected_fraction_chunk":float(detected_fraction),
                "gap_penalty":gap_penalty,"identity_score":identity,
                "median_circularity":mc,"median_solidity":ms,
                "median_circle_fill":mf,"median_axis_ratio":ma,
                "radius_cv":rcv,"area_cv":acv,"aspect_log_median":alm,
            })
            chunk_id+=1
    return out

def _global_envelope_from_chunks(chunks):
    if not chunks:
        raise TrackSelectionError("no ball-like identity tracks for global envelope")
    # Use the single largest smoothed same-ball excursion as the spatial ruler.
    # This is usually the full manual reset. Its timing is ignored completely.
    # Choosing one coherent track avoids systematic percentile shrinkage when a
    # shorter gravity fragment is also present.
    best=None
    for q in chunks:
        y=smooth1(np.asarray(q["y"],float),5)
        if len(y)<6:
            continue
        lo=float(np.min(y));hi=float(np.max(y));span=hi-lo
        cand=(span,lo,hi,int(q["track_id"]),int(q["chunk_id"]))
        if best is None or cand[0]>best[0]:
            best=cand
    if best is None:
        raise TrackSelectionError("insufficient ball samples for spatial envelope")
    span,top,bottom,source_track,source_chunk=best
    if not math.isfinite(span) or span<18.0:
        raise TrackSelectionError(
            f"invalid global spatial envelope top={top} bottom={bottom}"
        )
    return top,bottom,span,source_track,source_chunk

def _monotone_runs(progress,minimum_points=6):
    """Return sustained positive-progress motion phases.

    A single temporal track can contain release, impact hold, and a later smooth
    manual reset. Split by sustained derivative sign rather than requiring one
    large negative jump. Short inactive gaps are bridged, but sustained reset
    motion is never merged into the gravity phase.
    """
    p=np.asarray(progress,float)
    if len(p)<minimum_points:
        return []
    ps=smooth1(p,5)
    dp=np.diff(ps)
    # Scale-aware activity threshold; large enough to ignore centroid jitter while
    # retaining partial free-fall fragments.
    eps=max(0.0015,0.03/max(len(p),1))
    active=dp>eps
    # Bridge at most two derivative samples of dropout inside an active phase.
    bridged=active.copy()
    for i in range(1,len(active)-1):
        if not active[i] and active[max(0,i-2):i].any() and active[i+1:min(len(active),i+3)].any():
            bridged[i]=True
    runs=[]
    i=0
    while i<len(bridged):
        if not bridged[i]:
            i+=1;continue
        start=i
        j=i
        gap=0
        while j+1<len(bridged):
            j+=1
            if bridged[j]:
                gap=0
            else:
                gap+=1
                if gap>2:
                    j-=gap
                    break
        a=max(0,start-1)
        b=min(len(p),j+2)
        if b-a>=minimum_points and float(p[b-1]-p[a])>0.0:
            runs.append((a,b))
        i=max(i+1,j+1)
    return runs

def _fit_global_fragment(chunk,aa,bb,top,bottom,envelope_px,fps,selector_cfg,
                         minimum_interval_frames,diagnostics=None):
    expected_sign=float(selector_cfg["expected_sign"])
    y=np.asarray(chunk["y"][aa:bb],float)
    x=np.asarray(chunk["x"][aa:bb],float)
    t=np.asarray(chunk["abs_times"][aa:bb],float)
    if expected_sign>0:
        p=(y-top)/envelope_px
    else:
        p=(bottom-y)/envelope_px
    keep=np.isfinite(p)&np.isfinite(t)&(p>=-0.05)&(p<=1.05)
    p=p[keep];t=t[keep];x=x[keep]
    if len(p)<6:
        return _reject(diagnostics,"too_few_points")
    p=np.clip(p,0.0,1.0)
    pspan=float(np.ptp(p))
    if pspan<float(selector_cfg["minimum_global_progress_span"]):
        return _reject(diagnostics,"progress_span")
    monotone=float(np.mean(np.diff(p)>=-.02)) if len(p)>1 else 0.0
    if monotone<.78:
        return _reject(diagnostics,"monotone")
    sqrtp=np.sqrt(p)
    if float(np.ptp(sqrtp))<.08:
        return _reject(diagnostics,"sqrt_progress_span")
    A=np.column_stack([np.ones(len(p)),sqrtp])
    coef=np.linalg.lstsq(A,t,rcond=None)[0]
    t0=float(coef[0]);T=float(coef[1])
    if not math.isfinite(T) or T<=0 or T>1.25:
        return _reject(diagnostics,"invalid_T")
    if T*fps<minimum_interval_frames:
        return _reject(diagnostics,"full_duration")
    pred=A@coef
    timing_rms_s=float(np.sqrt(np.mean((t-pred)**2)))
    timing_rms_frames=timing_rms_s*fps
    if timing_rms_frames>float(selector_cfg["maximum_global_timing_fit_rms_frames"]):
        return _reject(diagnostics,"timing_rms")
    pred_p=np.square(np.clip((t-t0)/T,0.0,1.0))
    shape=float(np.sqrt(np.mean((p-pred_p)**2)))
    if shape>.12:
        return _reject(diagnostics,"shape_rms")

    trel=t-t0
    X=np.column_stack([np.ones(len(trel)),trel,.5*trel*trel])
    qcoef=np.linalg.lstsq(X,p,rcond=None)[0]
    qa,qb,qc=map(float,qcoef)
    if qc<=0:
        return _reject(diagnostics,"nonpositive_acceleration")
    release_ratio=abs(qb)/max(abs(qc*T),1e-9)
    if release_ratio>.65:
        return _reject(diagnostics,"release_speed")
    x_drift=float(np.ptp(x))/max(envelope_px,1e-9)
    if x_drift>.45:
        return _reject(diagnostics,"x_drift")

    # Number of actually observed detections inside this dense fragment.
    frame_lo=int(chunk["frames"][aa]);frame_hi=int(chunk["frames"][bb-1])
    detected=sum(
        1 for ff in chunk["detected_frames_original"]
        if frame_lo<=int(ff)<=frame_hi
    )
    detected_fraction=detected/max(1,bb-aa)
    if detected_fraction<.45:
        return _reject(diagnostics,"detected_fraction")

    progress_full=np.full(len(chunk["frames"]),np.nan,float)
    if expected_sign>0:
        progress_full=(np.asarray(chunk["y"],float)-top)/envelope_px
    else:
        progress_full=(bottom-np.asarray(chunk["y"],float))/envelope_px
    ids=np.arange(aa,bb,dtype=int)

    return {
        "track_id":chunk["track_id"],"chunk_id":chunk["chunk_id"],
        "sign":expected_sign,
        "span_px":float(envelope_px),
        "local_span_px":float(np.ptp(y)),
        "low_px":float(top if expected_sign>0 else -bottom),
        "high_px":float(bottom if expected_sign>0 else -top),
        "progress":progress_full,
        "dense_frames":np.asarray(chunk["frames"],int),
        "abs_times":np.asarray(chunk["abs_times"],float),
        "window_indices":ids,
        "t0_s":t0,"full_fall_time_s":T,
        "timing_fit_rms_s":timing_rms_s,
        "timing_fit_rms_frames":timing_rms_frames,
        "trajectory_shape_rms_fraction":shape,
        "release_speed_ratio":release_ratio,
        "x_drift_fraction":x_drift,
        "monotone_fraction":monotone,
        "gap_penalty":float(chunk["gap_penalty"]),
        "detected_fraction":float(chunk["detected_fraction_chunk"]),
        "detected_window_fraction":float(detected_fraction),
        "identity_score":float(chunk["identity_score"]),
        "median_circularity":float(chunk["median_circularity"]),
        "median_solidity":float(chunk["median_solidity"]),
        "median_circle_fill":float(chunk["median_circle_fill"]),
        "median_axis_ratio":float(chunk["median_axis_ratio"]),
        "radius_cv":float(chunk["radius_cv"]),
        "area_cv":float(chunk["area_cv"]),
        "aspect_log_median":float(chunk["aspect_log_median"]),
        "interval_frames":int(bb-aa),
        "detected_frames":int(detected),
        "global_progress_span":pspan,
        "global_progress_start":float(np.min(p)),
        "global_progress_end":float(np.max(p)),
        "spatial_envelope_top_px":float(top),
        "spatial_envelope_bottom_px":float(bottom),
    }

def choose_ballistic_track_v63(tracks,fps,minimum_interval_frames=12,identity_cfg=None):
    cfg=identity_cfg or {}
    selector_cfg=validate_selector_config(cfg)
    chunks=_identity_track_chunks(tracks,fps,cfg)
    top,bottom,envelope_px,envelope_source_track,envelope_source_chunk=_global_envelope_from_chunks(chunks)
    candidates=[]
    rejection_counts={}
    chunk_diagnostics=[]
    for chunk in chunks:
        if selector_cfg["expected_sign"]>0:
            gp=(np.asarray(chunk["y"],float)-top)/envelope_px
        else:
            gp=(bottom-np.asarray(chunk["y"],float))/envelope_px
        runs=_monotone_runs(gp)
        chunk_diagnostics.append({
            "track_id":chunk["track_id"],
            "chunk_id":chunk["chunk_id"],
            "frames":len(chunk["frames"]),
            "frame_start":int(chunk["frames"][0]),
            "frame_end":int(chunk["frames"][-1]),
            "y_start":float(chunk["y"][0]),
            "y_end":float(chunk["y"][-1]),
            "progress_start":float(gp[0]),
            "progress_end":float(gp[-1]),
            "progress_delta":float(gp[-1]-gp[0]),
            "monotone_fraction":float(np.mean(np.diff(gp)>=-.02)) if len(gp)>1 else 0.0,
            "runs":[[int(a),int(b)] for a,b in runs],
        })
        for aa,bb in runs:
            q=_fit_global_fragment(
                chunk,aa,bb,top,bottom,envelope_px,fps,selector_cfg,
                minimum_interval_frames,diagnostics=rejection_counts
            )
            if q is not None:
                candidates.append(validate_candidate(q))
    if not candidates:
        raise TrackSelectionError(
            "no gravity-direction ball fragment survived V6.3 global calibration; "
            f"chunks={len(chunks)} envelope_px={envelope_px:.3f} "
            f"rejects={json.dumps(rejection_counts,sort_keys=True)} "
            f"chunk_diagnostics={json.dumps(chunk_diagnostics,sort_keys=True)}"
        )

    def rank(q):
        return (
            float(q["timing_fit_rms_frames"]),
            float(q["trajectory_shape_rms_fraction"]),
            -float(q["global_progress_span"]),
            float(q["t0_s"]),
            -float(q["identity_score"]),
        )
    candidates=sorted(candidates,key=rank)
    chosen=candidates[0]
    audit=[]
    for i,q in enumerate(candidates[:20],start=1):
        audit.append({
            "event_rank":i,"selected":q is chosen,
            "track_id":q["track_id"],"sign":float(q["sign"]),
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
            "envelope_track_count":len(chunks),
            "envelope_source_track":int(envelope_source_track),
            "envelope_source_chunk":int(envelope_source_chunk),
        })
    return chosen,audit


def _plateau_segments(chunks,selector_cfg):
    out=[]
    vmax=float(selector_cfg["plateau_max_speed_px_per_frame"])
    nmin=int(selector_cfg["plateau_min_frames"])
    for q in chunks:
        z=smooth1(np.asarray(q["y"],float),5)
        x=smooth1(np.asarray(q["x"],float),5)
        frames=np.asarray(q["frames"],int)
        if len(z)<nmin:
            continue
        speed=np.abs(np.gradient(z))
        stationary=speed<=vmax
        i=0
        while i<len(stationary):
            if not stationary[i]:
                i+=1;continue
            a=i
            while i+1<len(stationary) and stationary[i+1]:
                i+=1
            b=i+1
            if b-a>=nmin:
                out.append({
                    "track_id":q["track_id"],"chunk_id":q["chunk_id"],
                    "a":a,"b":b,
                    "frame_start":int(frames[a]),"frame_end":int(frames[b-1]),
                    "y":float(np.median(z[a:b])),
                    "x":float(np.median(x[a:b])),
                    "identity_score":float(q["identity_score"]),
                })
            i+=1
    return out

def _interp_crossing(frames,z,target,increasing=True):
    frames=np.asarray(frames,float);z=np.asarray(z,float)
    if len(frames)<2:
        return None
    for i in range(len(z)-1):
        a=z[i]-target;b=z[i+1]-target
        ok=(a<=0<=b) if increasing else (a>=0>=b)
        if not ok:
            continue
        dz=z[i+1]-z[i]
        if abs(dz)<1e-9:
            return float(frames[i])
        u=(target-z[i])/dz
        return float(frames[i]+u*(frames[i+1]-frames[i]))
    return None

def choose_ballistic_track_v64(tracks,fps,minimum_interval_frames=12,identity_cfg=None):
    cfg=identity_cfg or {}
    selector_cfg=validate_selector_config(cfg)
    chunks=_identity_track_chunks(tracks,fps,cfg)
    if not chunks:
        raise TrackSelectionError("no ball-like identity tracks for V6.4")
    plateaus=_plateau_segments(chunks,selector_cfg)
    if not plateaus:
        raise TrackSelectionError("no stationary ball plateaus for V6.4")

    expected=float(selector_cfg["expected_sign"])
    max_gap=int(selector_cfg["plateau_max_gap_frames"])
    max_x=float(selector_cfg["plateau_max_x_delta_px"])
    overlap=int(selector_cfg["plateau_overlap_frames"])
    edge=float(selector_cfg["event_edge_fraction"])
    candidates=[]
    rejection_counts={}

    for chunk in chunks:
        frames=np.asarray(chunk["frames"],int)
        y=np.asarray(chunk["y"],float)
        x=np.asarray(chunk["x"],float)
        z=expected*y
        zsmooth=smooth1(z,5)
        scale=max(float(np.ptp(zsmooth)),1.0)
        runs=_monotone_runs((zsmooth-float(np.min(zsmooth)))/scale)
        for aa,bb in runs:
            if bb-aa<6:
                _reject(rejection_counts,"too_few_motion_points");continue
            f0=int(frames[aa]);f1=int(frames[bb-1])
            xm=float(np.median(x[aa:bb]))
            z0=float(zsmooth[aa]);z1=float(zsmooth[bb-1])
            if z1<=z0:
                _reject(rejection_counts,"wrong_direction");continue

            prior=[
                p for p in plateaus
                if p["frame_start"]<=f0
                and p["frame_end"]<=f0+overlap
                and f0-p["frame_end"]<=max_gap
                and abs(p["x"]-xm)<=max_x
                and expected*p["y"]<=z0+12.0
            ]
            post=[
                p for p in plateaus
                if p["frame_end"]>=f1
                and p["frame_start"]>=f1-overlap
                and p["frame_start"]-f1<=max_gap
                and abs(p["x"]-xm)<=max_x
                and expected*p["y"]>=z1-12.0
            ]
            if not prior:
                _reject(rejection_counts,"missing_release_plateau");continue
            if not post:
                _reject(rejection_counts,"missing_impact_plateau");continue
            top=max(prior,key=lambda p:p["frame_end"])
            bottom=min(post,key=lambda p:p["frame_start"])
            topz=expected*float(top["y"]);botz=expected*float(bottom["y"])
            span=botz-topz
            if not math.isfinite(span) or span<18.0:
                _reject(rejection_counts,"plateau_span");continue

            target_start=topz+edge*span
            target_end=botz-edge*span
            rel_frames=frames[max(0,aa-3):min(len(frames),bb+4)]
            rel_z=zsmooth[max(0,aa-3):min(len(frames),bb+4)]
            start_cross=_interp_crossing(rel_frames,rel_z,target_start,True)
            end_cross=_interp_crossing(rel_frames,rel_z,target_end,True)
            if start_cross is not None and end_cross is not None and end_cross>start_cross:
                # Free fall position fraction is quadratic in elapsed time:
                # t(f)=t0+T*sqrt(f). Recover release/impact times from two
                # position-fraction crossings without using target gravity.
                s1=math.sqrt(edge)
                s2=math.sqrt(1.0-edge)
                denom=s2-s1
                if denom<=1e-9:
                    _reject(rejection_counts,"edge_geometry");continue
                full_frames=(end_cross-start_cross)/denom
                release_frame=start_cross-full_frames*s1
                impact_frame=release_frame+full_frames
            else:
                release_frame=float(top["frame_end"])
                impact_frame=float(bottom["frame_start"])
                full_frames=impact_frame-release_frame
            if not math.isfinite(full_frames) or full_frames<minimum_interval_frames:
                _reject(rejection_counts,"full_duration");continue
            T=full_frames/fps

            lo=max(0,aa-2);hi=min(len(frames),bb+3)
            event_frames=frames[lo:hi].astype(float)
            event_t=(event_frames-release_frame)/fps
            event_z=zsmooth[lo:hi]
            p=np.clip((event_z-topz)/span,0.0,1.0)
            valid=(event_t>=-2.0/fps)&(event_t<=T+2.0/fps)
            event_t=event_t[valid];p=p[valid]
            event_ids=np.arange(lo,hi,dtype=int)[valid]
            if len(event_t)<6:
                _reject(rejection_counts,"too_few_event_points");continue
            model=np.square(np.clip(event_t/T,0.0,1.0))
            shape=float(np.sqrt(np.mean((p-model)**2)))
            if not math.isfinite(shape) or shape>.12:
                _reject(rejection_counts,"shape_rms");continue
            sqrtp=np.sqrt(np.clip(p,0.0,1.0))
            pred_time=T*sqrtp
            timing=float(np.sqrt(np.mean((event_t-pred_time)**2))*fps)
            if timing>float(selector_cfg["maximum_global_timing_fit_rms_frames"]):
                _reject(rejection_counts,"timing_rms");continue

            det_frames=np.asarray(chunk["detected_frames_original"],int)
            detected=sum(1 for ff in det_frames if frames[lo]<=ff<=frames[hi-1])
            detected_fraction=detected/max(1,hi-lo)
            if detected_fraction<float(cfg.get("minimum_detected_fraction",.45)):
                _reject(rejection_counts,"detected_fraction");continue

            xdrift=float(np.ptp(x[lo:hi]))/span
            if xdrift>float(cfg.get("maximum_x_drift_fraction",.45)):
                _reject(rejection_counts,"x_drift");continue

            progress_full=(expected*y-topz)/span
            q={
                "track_id":chunk["track_id"],"chunk_id":chunk["chunk_id"],
                "sign":expected,"span_px":span,"local_span_px":float(np.ptp(event_z)),
                "low_px":float(topz),"high_px":float(botz),
                "progress":progress_full,
                "dense_frames":frames,
                "abs_times":frames/fps,
                "window_indices":event_ids,
                "t0_s":release_frame/fps,
                "full_fall_time_s":T,
                "timing_fit_rms_s":timing/fps,
                "timing_fit_rms_frames":timing,
                "trajectory_shape_rms_fraction":shape,
                "release_speed_ratio":0.0,
                "x_drift_fraction":xdrift,
                "monotone_fraction":float(np.mean(np.diff(p)>=-.02)) if len(p)>1 else 1.0,
                "gap_penalty":float(chunk["gap_penalty"]),
                "detected_fraction":float(chunk["detected_fraction_chunk"]),
                "detected_window_fraction":float(detected_fraction),
                "identity_score":float(chunk["identity_score"]),
                "median_circularity":float(chunk["median_circularity"]),
                "median_solidity":float(chunk["median_solidity"]),
                "median_circle_fill":float(chunk["median_circle_fill"]),
                "median_axis_ratio":float(chunk["median_axis_ratio"]),
                "radius_cv":float(chunk["radius_cv"]),
                "area_cv":float(chunk["area_cv"]),
                "aspect_log_median":float(chunk["aspect_log_median"]),
                "interval_frames":int(round(full_frames)),
                "observed_fragment_frames":int(len(event_ids)),
                "inferred_full_fall_frames":float(full_frames),
                "detected_frames":int(detected),
                "global_progress_span":float(np.ptp(p)),
                "global_progress_start":float(np.min(p)),
                "global_progress_end":float(np.max(p)),
                "release_plateau_track":int(top["track_id"]),
                "impact_plateau_track":int(bottom["track_id"]),
            }
            candidates.append(validate_candidate(q))

    if not candidates:
        raise TrackSelectionError(
            "no release->impact V6.4 event survived; "
            f"chunks={len(chunks)} plateaus={len(plateaus)} "
            f"rejects={json.dumps(rejection_counts,sort_keys=True)}"
        )

    def rank(q):
        return (
            float(q["timing_fit_rms_frames"]),
            float(q["trajectory_shape_rms_fraction"]),
            -float(q["detected_window_fraction"]),
            float(q["t0_s"]),
            -float(q["identity_score"]),
        )
    candidates=sorted(candidates,key=rank)
    chosen=candidates[0]
    audit=[]
    for i,q in enumerate(candidates[:20],1):
        audit.append({
            "event_rank":i,"selected":q is chosen,"track_id":q["track_id"],
            "sign":float(q["sign"]),"t0_s":float(q["t0_s"]),
            "local_span_px":float(q["local_span_px"]),
            "spatial_envelope_px":float(q["span_px"]),
            "relative_span":float(q["local_span_px"])/max(float(q["span_px"]),1e-9),
            "global_progress_span":float(q["global_progress_span"]),
            "global_progress_start":float(q["global_progress_start"]),
            "global_progress_end":float(q["global_progress_end"]),
            "full_fall_time_s":float(q["full_fall_time_s"]),
            "interval_frames":int(q["interval_frames"]),
            "observed_fragment_frames":int(q["observed_fragment_frames"]),
            "inferred_full_fall_frames":float(q["inferred_full_fall_frames"]),
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
            "radius_cv":float(q["radius_cv"]),"area_cv":float(q["area_cv"]),
            "aspect_log_median":float(q["aspect_log_median"]),
            "release_plateau_track":int(q["release_plateau_track"]),
            "impact_plateau_track":int(q["impact_plateau_track"]),
        })
    return chosen,audit


def _increasing_quadratic_root(a,b,c,target):
    """Return the time on the increasing branch where a+b*t+.5*c*t^2=target."""
    disc=b*b+2.0*c*(target-a)
    if not math.isfinite(disc) or disc<0.0 or not math.isfinite(c) or c<=0.0:
        return None
    return float((-b+math.sqrt(max(0.0,disc)))/c)

def _fit_constant_acceleration_fragment_v65(
        chunk,aa,bb,top,bottom,envelope_px,fps,selector_cfg,
        identity_cfg,minimum_interval_frames,diagnostics=None):
    """Fit a gravity-direction fragment without assuming observation begins at release.

    The observed ball fragment is expressed in a global top->bottom coordinate p.
    We fit

        p(t) = a + b t + 0.5 c t^2

    with nuisance offset a and nuisance initial velocity b.  The candidate
    acceleration c is selected only by constancy/shape/identity diagnostics; the
    target value of gravity is never used.  Roots at p=0 and p=1 infer the full
    release->impact interval even when the camera tracker sees only an interior
    portion of the fall.
    """
    expected=float(selector_cfg["expected_sign"])
    frames=np.asarray(chunk["frames"][aa:bb],int)
    y=np.asarray(chunk["y"][aa:bb],float)
    x=np.asarray(chunk["x"][aa:bb],float)
    t_abs=np.asarray(chunk["abs_times"][aa:bb],float)
    if len(frames)<6:
        return _reject(diagnostics,"too_few_points")

    if expected>0:
        p=(y-top)/envelope_px
    else:
        p=(bottom-y)/envelope_px
    keep=np.isfinite(p)&np.isfinite(t_abs)&(p>=-0.08)&(p<=1.08)
    p=p[keep];t_abs=t_abs[keep];x=x[keep];frames=frames[keep]
    if len(p)<6:
        return _reject(diagnostics,"too_few_in_envelope")

    order=np.argsort(t_abs)
    p=np.clip(p[order],0.0,1.0)
    t_abs=t_abs[order];x=x[order];frames=frames[order]
    raw_pspan=float(np.ptp(p))
    if raw_pspan<float(selector_cfg["minimum_global_progress_span"]):
        return _reject(diagnostics,"progress_span")

    # Stationary top/bottom holds establish the spatial envelope but are not
    # acceleration samples.  Including repeated p≈0 / p≈1 frames biases the
    # quadratic curvature toward zero.  Fit only the moving interior; partial
    # mid-flight fragments remain valid because their p values are already
    # interior.
    fit_edge=float(identity_cfg.get("kinematic_fit_edge_fraction",0.02))
    moving=(p>fit_edge)&(p<1.0-fit_edge)
    if int(np.sum(moving))<6:
        return _reject(diagnostics,"too_few_moving_points")
    p=p[moving];t_abs=t_abs[moving];x=x[moving];frames=frames[moving]
    pspan=float(np.ptp(p))
    if pspan<float(selector_cfg["minimum_global_progress_span"]):
        return _reject(diagnostics,"moving_progress_span")

    monotone=float(np.mean(np.diff(p)>=-.02)) if len(p)>1 else 0.0
    if monotone<float(identity_cfg.get("minimum_monotone_fraction",.78)):
        return _reject(diagnostics,"monotone")

    # Shift time origin for numerical conditioning.  Nuisance velocity is
    # intentionally free: a mid-flight fragment must not be forced to start at
    # zero velocity.
    t0obs=float(t_abs[0])
    tr=t_abs-t0obs
    X=np.column_stack([np.ones(len(tr)),tr,.5*tr*tr])
    coef=np.linalg.lstsq(X,p,rcond=None)[0]
    a0,b0,c0=map(float,coef)
    if not all(math.isfinite(q) for q in (a0,b0,c0)) or c0<=0.0:
        return _reject(diagnostics,"nonpositive_acceleration")

    pred=X@coef
    shape=float(np.sqrt(np.mean((p-pred)**2)))
    max_shape=float(identity_cfg.get("maximum_trajectory_shape_rms_fraction",.10))
    if not math.isfinite(shape) or shape>max_shape:
        return _reject(diagnostics,"shape_rms")

    # Reject a fit whose own predicted motion reverses over the observed window.
    deriv=b0+c0*tr
    if float(np.mean(deriv>=-1e-6))<.90:
        return _reject(diagnostics,"predicted_direction")

    # Full release/impact roots are optional diagnostics in V6.5.  A partial
    # flight fragment can identify constant acceleration without containing the
    # release or impact itself.  Eligibility therefore depends on observed motion
    # support, not successful extrapolation to p=0 and p=1.
    release_tau=_increasing_quadratic_root(a0,b0,c0,0.0)
    impact_tau=_increasing_quadratic_root(a0,b0,c0,1.0)
    roots_complete=(
        release_tau is not None and impact_tau is not None
        and impact_tau>release_tau
    )
    observed_span_s=float(t_abs[-1]-t_abs[0])
    observed_span_frames=int(frames[-1]-frames[0]+1)
    if observed_span_frames<minimum_interval_frames:
        return _reject(diagnostics,"observed_duration")

    if roots_complete:
        inferred_T=float(impact_tau-release_tau)
        if not math.isfinite(inferred_T) or inferred_T<=0.0 or inferred_T>1.25:
            roots_complete=False
    if roots_complete:
        T=inferred_T
        pre=max(0.0,-float(release_tau))
        post=max(0.0,float(impact_tau)-float(tr[-1]))
        extrap_frames=(pre+post)*fps
        release_speed=b0+c0*float(release_tau)
        release_ratio=abs(release_speed)/max(abs(c0*T),1e-9)
        event_t0_abs=float(t0obs+float(release_tau))
    else:
        # Backward-compatible duration fields describe the observed support when
        # the full event is not inferable.  They are not used to estimate g.
        T=max(observed_span_s,1.0/fps)
        extrap_frames=0.0
        release_ratio=abs(b0)/max(abs(c0*T),1e-9)
        event_t0_abs=t0obs

    # Convert position residual into a temporal residual by analytically
    # inverting the fitted constant-acceleration model.  This keeps the existing
    # timing gate meaningful without imposing the zero-release-velocity law.
    pred_times=[]
    for pp in p:
        tau=_increasing_quadratic_root(a0,b0,c0,float(pp))
        if tau is None:
            return _reject(diagnostics,"inverse_time")
        pred_times.append(t0obs+tau)
    pred_times=np.asarray(pred_times,float)
    timing=float(np.sqrt(np.mean((t_abs-pred_times)**2))*fps)
    if timing>float(selector_cfg["maximum_global_timing_fit_rms_frames"]):
        return _reject(diagnostics,"timing_rms")

    # Acceleration constancy diagnostic.  This is a ranking signal, not a target-g
    # test.  Manual handling/reset tends to have inconsistent second-order motion.
    half=max(4,len(tr)//2)
    acc_parts=[]
    for lo,hi in ((0,half),(max(0,len(tr)-half),len(tr))):
        if hi-lo<4:
            continue
        tt=tr[lo:hi];pp=p[lo:hi]
        XX=np.column_stack([np.ones(len(tt)),tt,.5*tt*tt])
        cc=float(np.linalg.lstsq(XX,pp,rcond=None)[0][2])
        if math.isfinite(cc):
            acc_parts.append(cc)
    if len(acc_parts)>=2:
        acc_stability=float(abs(acc_parts[0]-acc_parts[-1])/max(abs(c0),1e-9))
    else:
        acc_stability=0.0

    xdrift=float(np.ptp(x))/max(envelope_px,1e-9)
    if xdrift>float(identity_cfg.get("maximum_x_drift_fraction",.45)):
        return _reject(diagnostics,"x_drift")

    frame_lo=int(frames[0]);frame_hi=int(frames[-1])
    det_frames=np.asarray(chunk["detected_frames_original"],int)
    detected=sum(1 for ff in det_frames if frame_lo<=int(ff)<=frame_hi)
    detected_fraction=detected/max(1,frame_hi-frame_lo+1)
    if detected_fraction<float(identity_cfg.get("minimum_detected_fraction",.45)):
        return _reject(diagnostics,"detected_fraction")

    progress_full=(
        (expected*np.asarray(chunk["y"],float)-expected*top)/envelope_px
        if expected>0 else
        (bottom-np.asarray(chunk["y"],float))/envelope_px
    )
    # For expected=+1 the expression above is simply (y-top)/span.  Keep the
    # explicit branch for readability and future image-axis inversions.
    if expected>0:
        progress_full=(np.asarray(chunk["y"],float)-top)/envelope_px

    all_frames=np.asarray(chunk["frames"],int)
    ids=np.asarray(
        [int(np.searchsorted(all_frames,ff)) for ff in frames],
        dtype=int
    )
    if np.any(ids<0) or np.any(ids>=len(all_frames)):
        return _reject(diagnostics,"index_mapping")

    return validate_candidate({
        "track_id":chunk["track_id"],"chunk_id":chunk["chunk_id"],
        "sign":expected,
        "span_px":float(envelope_px),
        "local_span_px":float(np.ptp(y)),
        "low_px":float(top if expected>0 else -bottom),
        "high_px":float(bottom if expected>0 else -top),
        "progress":progress_full,
        "dense_frames":all_frames,
        "abs_times":np.asarray(chunk["abs_times"],float),
        "window_indices":ids,
        "t0_s":float(event_t0_abs),
        "full_fall_time_s":T,
        "timing_fit_rms_s":timing/fps,
        "timing_fit_rms_frames":timing,
        "trajectory_shape_rms_fraction":shape,
        "release_speed_ratio":float(release_ratio),
        "x_drift_fraction":xdrift,
        "monotone_fraction":monotone,
        "gap_penalty":float(chunk["gap_penalty"]),
        "detected_fraction":float(chunk["detected_fraction_chunk"]),
        "detected_window_fraction":float(detected_fraction),
        "identity_score":float(chunk["identity_score"]),
        "median_circularity":float(chunk["median_circularity"]),
        "median_solidity":float(chunk["median_solidity"]),
        "median_circle_fill":float(chunk["median_circle_fill"]),
        "median_axis_ratio":float(chunk["median_axis_ratio"]),
        "radius_cv":float(chunk["radius_cv"]),
        "area_cv":float(chunk["area_cv"]),
        "aspect_log_median":float(chunk["aspect_log_median"]),
        "interval_frames":int(observed_span_frames),
        "observed_fragment_frames":int(observed_span_frames),
        "inferred_full_fall_frames":float(
            (impact_tau-release_tau)*fps if roots_complete
            else observed_span_frames
        ),
        "detected_frames":int(detected),
        "global_progress_span":pspan,
        "global_progress_start":float(np.min(p)),
        "global_progress_end":float(np.max(p)),
        "normalized_acceleration_s2":float(c0),
        "normalized_fit_a":float(a0),
        "normalized_fit_b":float(b0),
        "duration_10_90_s":float(
            (_increasing_quadratic_root(a0,b0,c0,.90)
             - _increasing_quadratic_root(a0,b0,c0,.10))
            if (_increasing_quadratic_root(a0,b0,c0,.10) is not None
                and _increasing_quadratic_root(a0,b0,c0,.90) is not None)
            else observed_span_s
        ),
        "roots_complete":1.0 if roots_complete else 0.0,
        "acceleration_stability":float(acc_stability),
        "event_extrapolation_frames":float(extrap_frames),
        "release_plateau_track":-1,
        "impact_plateau_track":-1,
    })

def choose_ballistic_track_v66(tracks,fps,minimum_interval_frames=12,identity_cfg=None):
    """V6.6: recover overhead free fall from perspective scale, not image Y.

    IRIS Dropping_ball is filmed from above.  World vertical motion therefore
    appears primarily as a change in apparent ball scale.  Under a pinhole camera,
    inverse projected radius is proportional to camera depth.  Because equivalent
    radius is proportional to sqrt(projected area), q=1/sqrt(A) is an affine depth
    proxy.  Normalising q between the held/release state and the first impact
    removes focal length and ball-radius nuisance parameters.

    Candidate selection uses only ball identity, monotone depth increase, a
    pre-release scale plateau, first-impact reversal, and release-from-rest
    quadratic timing.  The target value of g is never used.
    """
    cfg=identity_cfg or {}
    selector_cfg=validate_selector_config(cfg)
    chunks=_identity_track_chunks(tracks,fps,cfg)
    if not chunks:
        raise TrackSelectionError("no ball-like identity tracks for V6.6 depth selector")

    min_frames=max(8,int(minimum_interval_frames))
    max_frames=max(min_frames+2,int(round(float(cfg.get("maximum_depth_event_seconds",0.75))*fps)))
    min_rel_span=float(cfg.get("minimum_depth_proxy_relative_span",0.055))
    min_mono=float(cfg.get("minimum_depth_monotone_fraction",0.72))
    plateau_frames=max(3,int(cfg.get("depth_release_plateau_frames",5)))
    reversal_frames=max(2,int(cfg.get("depth_impact_reversal_frames",4)))
    max_shape=float(cfg.get("maximum_trajectory_shape_rms_fraction",.10))
    max_timing=float(selector_cfg["maximum_global_timing_fit_rms_frames"])
    candidates=[]
    rejects={}

    for chunk in chunks:
        frames=np.asarray(chunk["frames"],int)
        times=np.asarray(chunk["abs_times"],float)
        x=np.asarray(chunk["x"],float)
        if len(frames)<min_frames:
            _reject(rejects,"short_chunk");continue

        # Try two geometry-equivalent area observables.  They differ only in how
        # the foreground boundary is discretised; ranking is based on target-free
        # timing/shape residuals.
        proxy_sources=(
            ("component_area",np.asarray(chunk["area"],float)),
            ("contour_area",np.asarray(chunk["contour_area"],float)),
        )
        for proxy_name,raw_area in proxy_sources:
            area=np.maximum(raw_area,1.0)
            # Smooth area before the nonlinear inverse-square-root transform.
            area_s=smooth1(area,3)
            depth=smooth1(1.0/np.sqrt(np.maximum(area_s,1.0)),3)
            n=len(depth)

            for i in range(0,n-min_frames+1):
                pre_lo=max(0,i-plateau_frames)
                pre=depth[pre_lo:i+1]
                q0=float(np.median(pre))
                if not math.isfinite(q0) or q0<=0:
                    continue

                for j in range(i+min_frames-1,min(n,i+max_frames)):
                    q1=float(depth[j])
                    span=q1-q0
                    if not math.isfinite(span) or span<=min_rel_span*abs(q0):
                        continue

                    # The end of the first fall is identified by a scale reversal:
                    # the ball reaches maximum camera depth at impact, then moves
                    # back toward the camera during its first bounce.  If the
                    # temporal track ends at impact, allow the endpoint instead.
                    post=depth[j+1:min(n,j+1+reversal_frames)]
                    if len(post)>=2:
                        reversal=float(q1-np.median(post))/max(span,1e-12)
                        if reversal<0.035:
                            continue
                    elif j<n-2:
                        continue
                    else:
                        reversal=0.0

                    plateau_mad=(1.4826*float(np.median(np.abs(pre-np.median(pre))))
                                 if len(pre)>=3 else 0.0)
                    plateau_rel=plateau_mad/max(span,1e-12)
                    if len(pre)>=3 and plateau_rel>0.18:
                        continue

                    seg=depth[i:j+1]
                    p=(seg-q0)/span
                    finite=np.isfinite(p)
                    if int(np.sum(finite))<min_frames:
                        continue
                    p=p[finite]
                    tt=times[i:j+1][finite]
                    xx=x[i:j+1][finite]
                    ids=np.arange(i,j+1,dtype=int)[finite]
                    if len(p)<min_frames:
                        continue

                    # Keep modest endpoint noise, but do not manufacture progress
                    # by clipping grossly wrong observations.
                    if float(np.mean((p>=-.12)&(p<=1.12)))<.90:
                        continue
                    p=np.clip(p,0.0,1.0)
                    mono=float(np.mean(np.diff(p)>=-.025)) if len(p)>1 else 1.0
                    if mono<min_mono:
                        continue
                    if float(np.ptp(p))<0.70:
                        continue

                    sqrtp=np.sqrt(p)
                    A=np.column_stack([np.ones(len(tt)),sqrtp])
                    coef=np.linalg.lstsq(A,tt,rcond=None)[0]
                    t0=float(coef[0]);T=float(coef[1])
                    if not math.isfinite(T) or T<=0.0 or T>1.25:
                        continue
                    full_frames=T*fps
                    if full_frames+1e-9<minimum_interval_frames:
                        continue

                    pred_t=A@coef
                    timing=float(np.sqrt(np.mean((tt-pred_t)**2))*fps)
                    if timing>max_timing:
                        continue
                    model=np.square(np.clip((tt-t0)/T,0.0,1.0))
                    shape=float(np.sqrt(np.mean((p-model)**2)))
                    if not math.isfinite(shape) or shape>max_shape:
                        continue

                    impact_time=t0+T
                    impact_miss=abs(impact_time-float(times[j]))*fps
                    if impact_miss>float(cfg.get("maximum_depth_impact_miss_frames",4.5)):
                        continue

                    # Lateral drift is normal under an overhead perspective when
                    # the release point is not exactly on the optical axis.  Scale
                    # it by the ball diameter rather than by the (irrelevant) Y
                    # excursion.
                    eq_radius=np.sqrt(np.maximum(area_s[i:j+1],1.0)/math.pi)
                    ball_diam=max(2.0*float(np.median(eq_radius)),1e-9)
                    xdrift=float(np.ptp(xx))/ball_diam
                    max_depth_x=float(cfg.get("maximum_depth_lateral_diameters",1.25))
                    if xdrift>max_depth_x:
                        continue

                    det_frames=np.asarray(chunk["detected_frames_original"],int)
                    flo=int(frames[i]);fhi=int(frames[j])
                    detected=sum(1 for ff in det_frames if flo<=int(ff)<=fhi)
                    detected_fraction=detected/max(1,fhi-flo+1)
                    if detected_fraction<float(cfg.get("minimum_detected_fraction",.45)):
                        continue

                    # Map the complete chunk into the same perspective-depth
                    # progress coordinate.  This is the coordinate consumed by
                    # the downstream physics residual tests.
                    progress_full=(depth-q0)/span
                    effective_radius_span=float(abs(
                        math.sqrt(max(float(area_s[i]),1.0)/math.pi)
                        -math.sqrt(max(float(area_s[j]),1.0)/math.pi)
                    ))
                    effective_radius_span=max(effective_radius_span,1e-6)

                    cand={
                        "track_id":chunk["track_id"],"chunk_id":chunk["chunk_id"],
                        "sign":1.0,
                        "span_px":effective_radius_span,
                        "local_span_px":effective_radius_span,
                        "low_px":q0,"high_px":q1,
                        "progress":progress_full,
                        "dense_frames":frames,
                        "abs_times":times,
                        "window_indices":ids,
                        "t0_s":t0,
                        "full_fall_time_s":T,
                        "timing_fit_rms_s":timing/fps,
                        "timing_fit_rms_frames":timing,
                        "trajectory_shape_rms_fraction":shape,
                        "release_speed_ratio":0.0,
                        "x_drift_fraction":xdrift,
                        "monotone_fraction":mono,
                        "gap_penalty":float(chunk["gap_penalty"]),
                        "detected_fraction":float(chunk["detected_fraction_chunk"]),
                        "detected_window_fraction":float(detected_fraction),
                        "identity_score":float(chunk["identity_score"]),
                        "median_circularity":float(chunk["median_circularity"]),
                        "median_solidity":float(chunk["median_solidity"]),
                        "median_circle_fill":float(chunk["median_circle_fill"]),
                        "median_axis_ratio":float(chunk["median_axis_ratio"]),
                        "radius_cv":float(chunk["radius_cv"]),
                        "area_cv":float(chunk["area_cv"]),
                        "aspect_log_median":float(chunk["aspect_log_median"]),
                        "interval_frames":int(round(full_frames)),
                        "observed_fragment_frames":int(fhi-flo+1),
                        "inferred_full_fall_frames":float(full_frames),
                        "detected_frames":int(detected),
                        "global_progress_span":float(np.ptp(p)),
                        "global_progress_start":float(np.min(p)),
                        "global_progress_end":float(np.max(p)),
                        # Normalized displacement p=(1/2)*(2/T^2)*tau^2.
                        "normalized_acceleration_s2":float(2.0/(T*T)),
                        "normalized_fit_a":0.0,
                        "normalized_fit_b":0.0,
                        "duration_10_90_s":float(T*(math.sqrt(.90)-math.sqrt(.10))),
                        "roots_complete":1.0,
                        "acceleration_stability":float(plateau_rel),
                        "event_extrapolation_frames":float(impact_miss),
                        "release_plateau_track":int(chunk["track_id"]),
                        "impact_plateau_track":int(chunk["track_id"]),
                        "depth_proxy":proxy_name,
                        "depth_proxy_relative_span":float(span/max(abs(q0),1e-12)),
                        "depth_impact_reversal":float(reversal),
                    }
                    candidates.append(validate_candidate(cand))

    if not candidates:
        raise TrackSelectionError(
            "no perspective-depth free-fall event survived V6.6; "
            f"chunks={len(chunks)} rejects={json.dumps(rejects,sort_keys=True)}"
        )

    # No target acceleration is present in ranking.  Prefer the event whose
    # perspective progress most closely follows release-from-rest kinematics,
    # then stronger first-impact reversal and stronger ball identity.
    def rank(q):
        return (
            float(q["timing_fit_rms_frames"]),
            float(q["trajectory_shape_rms_fraction"]),
            float(q.get("event_extrapolation_frames",0.0)),
            -float(q.get("depth_impact_reversal",0.0)),
            -float(q["detected_window_fraction"]),
            -float(q["identity_score"]),
            float(q["t0_s"]),
        )
    candidates=sorted(candidates,key=rank)
    chosen=candidates[0]
    audit=[]
    for k,q in enumerate(candidates[:20],1):
        audit.append({
            "event_rank":k,"selected":q is chosen,
            "track_id":q["track_id"],"sign":float(q["sign"]),
            "t0_s":float(q["t0_s"]),
            "local_span_px":float(q["local_span_px"]),
            "spatial_envelope_px":float(q["span_px"]),
            "relative_span":1.0,
            "global_progress_span":float(q["global_progress_span"]),
            "raw_global_progress_span":float(q["global_progress_span"]),
            "global_progress_start":float(q["global_progress_start"]),
            "global_progress_end":float(q["global_progress_end"]),
            "full_fall_time_s":float(q["full_fall_time_s"]),
            "interval_frames":int(q["interval_frames"]),
            "observed_fragment_frames":int(q["observed_fragment_frames"]),
            "inferred_full_fall_frames":float(q["inferred_full_fall_frames"]),
            "detected_frames":int(q["detected_frames"]),
            "detected_fraction":float(q["detected_window_fraction"]),
            "timing_fit_rms_frames":float(q["timing_fit_rms_frames"]),
            "trajectory_shape_rms_fraction":float(q["trajectory_shape_rms_fraction"]),
            "release_speed_ratio":0.0,
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
            "envelope_source_track":int(q["track_id"]),
            "envelope_source_chunk":int(q["chunk_id"]),
            "normalized_acceleration_s2":float(q["normalized_acceleration_s2"]),
            "normalized_fit_a":0.0,"normalized_fit_b":0.0,
            "duration_10_90_s":float(q["duration_10_90_s"]),
            "roots_complete":1.0,
            "acceleration_stability":float(q["acceleration_stability"]),
            "event_extrapolation_frames":float(q["event_extrapolation_frames"]),
            "release_plateau_track":int(q["release_plateau_track"]),
            "impact_plateau_track":int(q["impact_plateau_track"]),
            "depth_proxy":q.get("depth_proxy",""),
            "depth_proxy_relative_span":float(q.get("depth_proxy_relative_span",0.0)),
            "depth_impact_reversal":float(q.get("depth_impact_reversal",0.0)),
        })
    return chosen,audit


def choose_ballistic_track_v65(tracks,fps,minimum_interval_frames=12,identity_cfg=None):
    """V6.5: select a constant-acceleration ball fragment in global coordinates.

    Unlike V6.4 this does not require the tracker to observe the exact release and
    impact plateaus, and unlike V6.2/V6.3 it does not interpret a short interior
    fragment as the complete drop.  The global spatial envelope supplies scale;
    nuisance offset/velocity absorb late acquisition.  No target gravity value is
    used in candidate generation, filtering, or ranking.
    """
    cfg=identity_cfg or {}
    selector_cfg=validate_selector_config(cfg)
    chunks=_identity_track_chunks(tracks,fps,cfg)
    top,bottom,envelope_px,source_track,source_chunk=_global_envelope_from_chunks(chunks)
    candidates=[]
    rejection_counts={}
    for chunk in chunks:
        y=np.asarray(chunk["y"],float)
        if selector_cfg["expected_sign"]>0:
            gp=(y-top)/envelope_px
        else:
            gp=(bottom-y)/envelope_px
        for aa,bb in _monotone_runs(gp):
            q=_fit_constant_acceleration_fragment_v65(
                chunk,aa,bb,top,bottom,envelope_px,fps,selector_cfg,cfg,
                minimum_interval_frames,diagnostics=rejection_counts
            )
            if q is not None:
                candidates.append(q)

    if not candidates:
        raise TrackSelectionError(
            "no constant-acceleration gravity-direction fragment survived V6.5; "
            f"chunks={len(chunks)} envelope_px={envelope_px:.3f} "
            f"rejects={json.dumps(rejection_counts,sort_keys=True)}"
        )

    # Identity is already an eligibility gate. Prefer fragments that cover more of
    # the globally calibrated drop, then the most internally self-consistent
    # constant-acceleration timing.  Target acceleration magnitude is absent.
    def rank(q):
        return (
            -float(q["global_progress_span"]),
            float(q.get("acceleration_stability",0.0)),
            float(q["timing_fit_rms_frames"]),
            float(q["trajectory_shape_rms_fraction"]),
            float(q.get("event_extrapolation_frames",0.0)),
            float(q["release_speed_ratio"]),
            -float(q["identity_score"]),
            float(q["t0_s"]),
        )
    candidates=sorted(candidates,key=rank)
    chosen=candidates[0]
    audit=[]
    for i,q in enumerate(candidates[:20],1):
        audit.append({
            "event_rank":i,"selected":q is chosen,
            "track_id":q["track_id"],"sign":float(q["sign"]),
            "t0_s":float(q["t0_s"]),
            "local_span_px":float(q["local_span_px"]),
            "spatial_envelope_px":float(q["span_px"]),
            "relative_span":float(q["local_span_px"])/max(float(q["span_px"]),1e-9),
            "global_progress_span":float(q["global_progress_span"]),
            "global_progress_start":float(q["global_progress_start"]),
            "global_progress_end":float(q["global_progress_end"]),
            "full_fall_time_s":float(q["full_fall_time_s"]),
            "interval_frames":int(q["interval_frames"]),
            "observed_fragment_frames":int(q["observed_fragment_frames"]),
            "inferred_full_fall_frames":float(q["inferred_full_fall_frames"]),
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
            "envelope_source_track":int(source_track),
            "envelope_source_chunk":int(source_chunk),
            "normalized_acceleration_s2":float(q["normalized_acceleration_s2"]),
            "normalized_fit_a":float(q["normalized_fit_a"]),
            "normalized_fit_b":float(q["normalized_fit_b"]),
            "duration_10_90_s":float(q["duration_10_90_s"]),
            "roots_complete":float(q["roots_complete"]),
            "acceleration_stability":float(q["acceleration_stability"]),
            "event_extrapolation_frames":float(q["event_extrapolation_frames"]),
            "release_plateau_track":-1,
            "impact_plateau_track":-1,
        })
    return chosen,audit

def choose_ballistic_track(tracks,fps,minimum_interval_frames=12,identity_cfg=None):
    cfg=identity_cfg or {}
    if cfg.get("revision")=="ball_identity_v6_6_depth":
        return choose_ballistic_track_v66(
            tracks,fps,minimum_interval_frames=minimum_interval_frames,
            identity_cfg=cfg
        )
    if cfg.get("revision")=="ball_identity_v6_5":
        return choose_ballistic_track_v65(
            tracks,fps,minimum_interval_frames=minimum_interval_frames,
            identity_cfg=cfg
        )
    if cfg.get("revision")=="ball_identity_v6_4":
        return choose_ballistic_track_v64(
            tracks,fps,minimum_interval_frames=minimum_interval_frames,
            identity_cfg=cfg
        )
    if cfg.get("revision")=="ball_identity_v6_3":
        return choose_ballistic_track_v63(
            tracks,fps,minimum_interval_frames=minimum_interval_frames,
            identity_cfg=cfg
        )
    return choose_ballistic_track_legacy(
        tracks,fps,minimum_interval_frames=minimum_interval_frames,
        identity_cfg=cfg
    )

def _v66_orange_ball_observation(frame,width,cv2,cfg):
    """Return one robust IRIS orange-ball scale observation from a BGR frame.

    The development videos use the same high-saturation orange/black soccer ball.
    We segment the orange shell rather than foreground-motion area so hands and
    later floor motion do not redefine the apparent ball scale.  This is an
    experiment-specific object tracker, not a gravity calibration.
    """
    h0,w0=frame.shape[:2]
    scale=float(width)/max(float(w0),1.0)
    q=cv2.resize(
        frame,(int(width),max(1,int(round(h0*scale)))),
        interpolation=cv2.INTER_AREA
    ) if w0!=width else frame
    hsv=cv2.cvtColor(q,cv2.COLOR_BGR2HSV)
    hmin=int(cfg.get("v66_ball_hue_min",3))
    hmax=int(cfg.get("v66_ball_hue_max",30))
    smin=int(cfg.get("v66_ball_saturation_min",140))
    vmin=int(cfg.get("v66_ball_value_min",50))
    if hmin<=hmax:
        mask=((hsv[:,:,0]>=hmin)&(hsv[:,:,0]<=hmax)
              &(hsv[:,:,1]>=smin)&(hsv[:,:,2]>=vmin)).astype(np.uint8)*255
    else:
        mask=(((hsv[:,:,0]>=hmin)|(hsv[:,:,0]<=hmax))
              &(hsv[:,:,1]>=smin)&(hsv[:,:,2]>=vmin)).astype(np.uint8)*255
    k=cv2.getStructuringElement(cv2.MORPH_ELLIPSE,(3,3))
    mask=cv2.morphologyEx(mask,cv2.MORPH_OPEN,k)
    mask=cv2.morphologyEx(mask,cv2.MORPH_CLOSE,k)

    nlab,labels,stats,cent=cv2.connectedComponentsWithStats(mask,connectivity=8)
    candidates=[]
    frame_area=max(1,q.shape[0]*q.shape[1])
    min_area=max(40,int(cfg.get("v66_ball_min_component_area",80)))
    max_area=float(cfg.get("v66_ball_max_frame_fraction",0.20))*frame_area
    for lab in range(1,nlab):
        pix=int(stats[lab,cv2.CC_STAT_AREA])
        bw=int(stats[lab,cv2.CC_STAT_WIDTH]);bh=int(stats[lab,cv2.CC_STAT_HEIGHT])
        if pix<min_area or pix>max_area or bw<=0 or bh<=0:
            continue
        component=(labels==lab).astype(np.uint8)*255
        contours,_=cv2.findContours(component,cv2.RETR_EXTERNAL,cv2.CHAIN_APPROX_NONE)
        if not contours:
            continue
        contour=max(contours,key=cv2.contourArea)
        ca=float(cv2.contourArea(contour))
        per=float(cv2.arcLength(contour,True))
        if ca<=1.0 or per<=1e-9:
            continue
        circ=float(np.clip(4.0*math.pi*ca/(per*per),0.0,1.0))
        hull=cv2.convexHull(contour)
        ha=float(cv2.contourArea(hull))
        solidity=float(np.clip(ca/max(ha,1e-9),0.0,1.0))
        (cx_circle,cy_circle),rad=cv2.minEnclosingCircle(hull)
        circle_fill=float(np.clip(ca/max(math.pi*rad*rad,1e-9),0.0,1.0))
        rect=cv2.minAreaRect(hull);rw,rh=map(float,rect[1])
        axis=(min(rw,rh)/max(rw,rh)) if max(rw,rh)>1e-9 else 0.0
        cx,cy=map(float,cent[lab])
        # Favor the large compact saturated shell.  No temporal or gravity target
        # appears here; later continuity is checked at the event level.
        score=float(pix)*(0.45+0.20*circ+0.20*solidity+0.15*axis)
        candidates.append({
            "score":score,"x":cx,"y":cy,"area":float(pix),
            "contour_area":ca,"radius":float(rad),
            "circularity":circ,"solidity":solidity,
            "circle_fill":circle_fill,"axis_ratio":float(np.clip(axis,0.0,1.0)),
        })
    if not candidates:
        return None
    return max(candidates,key=lambda z:z["score"])


def extract_perspective_depth_v66(
        video,drop_height,width=640,max_seconds=5.0,
        minimum_interval_frames=12,tracker_config=None):
    """IRIS V6.6 overhead free-fall estimator.

    The ball is held near the overhead camera, released, falls along camera depth,
    hits the horizontal surface, and bounces.  Apparent projected area A therefore
    supplies a perspective depth proxy q=1/sqrt(A).  We use the initial held
    plateau as q=0 progress, the *first* post-release depth maximum as impact, and
    fit t=t0+T*sqrt(p).  The measured drop height converts T to acceleration only
    after event selection.  The reference gravity value is never consulted.
    """
    cfg=tracker_config or {}
    cv2=import_cv()
    cap=cv2.VideoCapture(str(video))
    if not cap.isOpened():
        raise RuntimeError(f"cannot open {video}")
    fps=float(cap.get(cv2.CAP_PROP_FPS))
    frames=int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    n=min(frames,max(120,int(round(fps*max_seconds))))
    obs=[]
    shape_hw=None
    for fi in range(n):
        ok,fr=cap.read()
        if not ok:
            break
        if shape_hw is None:
            h0,w0=fr.shape[:2]
            shape_hw=(max(1,int(round(h0*float(width)/max(w0,1)))),int(width))
        z=_v66_orange_ball_observation(fr,width,cv2,cfg)
        if z is not None:
            z=dict(z);z["frame"]=fi;obs.append(z)
    cap.release()
    if len(obs)<minimum_interval_frames+6:
        raise TrackSelectionError(
            f"V6.6 orange-ball detector has too few observations: {len(obs)}"
        )

    fr=np.asarray([z["frame"] for z in obs],int)
    area=np.asarray([z["area"] for z in obs],float)
    xx=np.asarray([z["x"] for z in obs],float)
    yy=np.asarray([z["y"] for z in obs],float)
    radii=np.asarray([z["radius"] for z in obs],float)
    # Work on a dense frame grid so a brief segmentation dropout does not move
    # the inferred release/impact times.
    dense=np.arange(int(fr[0]),int(fr[-1])+1,dtype=int)
    max_gap=int(cfg.get("v66_max_interpolation_gap_frames",3))
    gaps=np.diff(fr)
    if len(gaps) and int(np.max(gaps))>max_gap+1:
        # Keep the first continuous observation epoch; the real release occurs in
        # the opening epoch and later reacquisitions are bounce/reset evidence.
        cut=np.where(gaps>max_gap+1)[0]
        if len(cut):
            end=int(cut[0]+1)
            obs=obs[:end]
            fr=np.asarray([z["frame"] for z in obs],int)
            area=np.asarray([z["area"] for z in obs],float)
            xx=np.asarray([z["x"] for z in obs],float)
            yy=np.asarray([z["y"] for z in obs],float)
            radii=np.asarray([z["radius"] for z in obs],float)
            dense=np.arange(int(fr[0]),int(fr[-1])+1,dtype=int)
    if len(dense)<minimum_interval_frames+6:
        raise TrackSelectionError("V6.6 first observation epoch is too short")

    ad=np.interp(dense,fr,area)
    xd=np.interp(dense,fr,xx)
    rd=np.interp(dense,fr,radii)
    smooth_n=int(cfg.get("v66_depth_smoothing_frames",3))
    if smooth_n%2==0:
        smooth_n+=1
    area_s=np.maximum(smooth1(ad,max(1,smooth_n)),1.0)
    depth=smooth1(1.0/np.sqrt(area_s),max(1,smooth_n))
    times=dense/fps

    seed_seconds=float(cfg.get("v66_release_seed_seconds",0.25))
    seed_hi=int(np.searchsorted(dense,int(round(seed_seconds*fps)),side="right"))
    seed_hi=max(4,min(seed_hi,len(dense)))
    seed=depth[:seed_hi]
    q0=float(np.median(seed))
    seed_mad=1.4826*float(np.median(np.abs(seed-q0)))
    release_threshold=max(
        float(cfg.get("v66_release_relative_threshold",0.010))*abs(q0),
        float(cfg.get("v66_release_mad_multiplier",3.0))*seed_mad,
    )
    sustain=max(2,int(cfg.get("v66_release_sustain_frames",2)))
    release_idx=None
    for i in range(seed_hi,len(depth)-sustain+1):
        if np.all(depth[i:i+sustain] > q0+release_threshold):
            release_idx=i
            break
    if release_idx is None:
        raise TrackSelectionError("V6.6 did not find first sustained depth release")

    min_event=max(8,int(cfg.get("v66_min_observed_event_frames",8)))
    max_event=max(min_event+2,int(round(float(cfg.get("maximum_depth_event_seconds",0.75))*fps)))
    reversal_n=max(2,int(cfg.get("depth_impact_reversal_frames",4)))
    reversal_fraction=float(cfg.get("v66_minimum_impact_reversal_fraction",0.03))
    impact_idx=None
    impact_reversal=0.0
    stop=min(len(depth)-reversal_n-1,release_idx+max_event)
    for j in range(release_idx+min_event-1,stop+1):
        local=depth[max(release_idx,j-2):min(len(depth),j+3)]
        span=float(depth[j]-q0)
        if span<=0:
            continue
        post=depth[j+1:j+1+reversal_n]
        reversal=float((depth[j]-np.median(post))/max(span,1e-12))
        if depth[j]>=float(np.max(local))-1e-12 and reversal>=reversal_fraction:
            impact_idx=j
            impact_reversal=reversal
            break
    if impact_idx is None:
        raise TrackSelectionError("V6.6 did not find first post-release impact reversal")

    span=float(depth[impact_idx]-q0)
    rel_span=span/max(abs(q0),1e-12)
    if rel_span<float(cfg.get("minimum_depth_proxy_relative_span",0.055)):
        raise TrackSelectionError(
            f"V6.6 perspective depth span too small: relative={rel_span:.6f}"
        )

    # Include a few pre-threshold points so t0 is inferred from the physical
    # plateau transition rather than from an arbitrary threshold crossing.
    fit_lo=max(0,release_idx-int(cfg.get("v66_release_preframes",4)))
    ids=np.arange(fit_lo,impact_idx+1,dtype=int)
    p=(depth[ids]-q0)/span
    admissible=np.isfinite(p)&(p>=-0.08)&(p<=1.04)
    ids=ids[admissible];p=p[admissible]
    if len(ids)<minimum_interval_frames:
        raise TrackSelectionError(
            f"V6.6 release->impact support too short: {len(ids)} frames"
        )
    p=np.clip(p,0.0,1.0)
    moving=p>=float(cfg.get("v66_min_fit_progress",0.005))
    if int(np.sum(moving))<8:
        raise TrackSelectionError("V6.6 has too few moving depth samples")
    fit_ids=ids[moving];fit_p=p[moving];fit_t=times[fit_ids]

    A=np.column_stack([np.ones(len(fit_t)),np.sqrt(fit_p)])
    coef=np.linalg.lstsq(A,fit_t,rcond=None)[0]
    t0=float(coef[0]);T=float(coef[1])
    if not math.isfinite(T) or T<=0.0 or T>1.25:
        raise TrackSelectionError(f"V6.6 invalid inferred fall duration: {T}")
    inferred_frames=T*fps
    if inferred_frames+1e-9<minimum_interval_frames:
        raise TrackSelectionError(
            f"V6.6 inferred fall is shorter than minimum: {inferred_frames:.3f}"
        )

    pred_t=A@coef
    timing=float(np.sqrt(np.mean((fit_t-pred_t)**2))*fps)
    if timing>float(cfg.get("maximum_global_timing_fit_rms_frames",2.5)):
        raise TrackSelectionError(f"V6.6 timing residual too large: {timing:.3f}")
    model=np.square(np.clip((fit_t-t0)/T,0.0,1.0))
    shape=float(np.sqrt(np.mean((fit_p-model)**2)))
    if shape>float(cfg.get("maximum_trajectory_shape_rms_fraction",0.10)):
        raise TrackSelectionError(f"V6.6 trajectory shape residual too large: {shape:.4f}")

    impact_time=t0+T
    impact_miss=abs(impact_time-float(times[impact_idx]))*fps
    if impact_miss>float(cfg.get("maximum_depth_impact_miss_frames",4.5)):
        raise TrackSelectionError(f"V6.6 fitted impact misses reversal by {impact_miss:.3f} frames")

    event_frames=dense[ids]
    det_set=set(map(int,fr.tolist()))
    detected=sum(int(ff) in det_set for ff in event_frames)
    detected_fraction=detected/max(1,len(event_frames))
    if detected_fraction<float(cfg.get("minimum_detected_fraction",0.50)):
        raise TrackSelectionError(
            f"V6.6 event detection fraction too low: {detected_fraction:.3f}"
        )
    event_x=xd[ids]
    event_r=rd[ids]
    ball_diam=max(2.0*float(np.median(event_r)),1e-9)
    xdrift=float(np.ptp(event_x))/ball_diam
    if xdrift>float(cfg.get("maximum_depth_lateral_diameters",1.25)):
        raise TrackSelectionError(f"V6.6 lateral drift too large: {xdrift:.3f} diameters")

    # Identity/shape diagnostics use observations nearest the selected event.
    chosen_obs=[z for z in obs if int(event_frames[0])<=int(z["frame"])<=int(event_frames[-1])]
    circ=np.asarray([z["circularity"] for z in chosen_obs],float)
    sol=np.asarray([z["solidity"] for z in chosen_obs],float)
    cf=np.asarray([z["circle_fill"] for z in chosen_obs],float)
    ax=np.asarray([z["axis_ratio"] for z in chosen_obs],float)
    rr=np.asarray([z["radius"] for z in chosen_obs],float)
    aa=np.asarray([z["area"] for z in chosen_obs],float)
    mc=float(np.median(circ));ms=float(np.median(sol));mf=float(np.median(cf));ma=float(np.median(ax))
    rcv=float(np.std(rr)/max(np.mean(rr),1e-9))
    acv=float(np.std(aa)/max(np.mean(aa),1e-9))
    identity=(.28*mc+.22*mf+.18*ms+.17*ma+.15*max(0.0,1.0-min(rcv,1.0)))
    monotone=float(np.mean(np.diff(p)>=-.025)) if len(p)>1 else 1.0
    gap_penalty=float(np.mean(np.maximum(np.diff(fr)-1,0))) if len(fr)>1 else 0.0

    progress_full=(depth-q0)/span
    position_m=np.clip(progress_full[ids],0.0,1.0)*drop_height
    rel_times=times[ids]-t0
    release_rest_ghat=2.0*drop_height/(T*T)
    X=np.column_stack([np.ones(len(rel_times)),rel_times,.5*rel_times*rel_times])
    trajectory_coef=np.linalg.lstsq(X,position_m,rcond=None)[0]
    trajectory_g=float(trajectory_coef[2])
    eqr_top=math.sqrt(max(float(np.median(area_s[:seed_hi])),1.0)/math.pi)
    eqr_bottom=math.sqrt(max(float(area_s[impact_idx]),1.0)/math.pi)
    radius_span=max(abs(eqr_top-eqr_bottom),1e-6)

    audit=[{
        "event_rank":1,"selected":True,"track_id":0,"sign":1.0,
        "t0_s":t0,"local_span_px":radius_span,"spatial_envelope_px":radius_span,
        "relative_span":1.0,"global_progress_span":float(np.ptp(p)),
        "raw_global_progress_span":float(np.ptp(p)),
        "global_progress_start":float(np.min(p)),"global_progress_end":float(np.max(p)),
        "full_fall_time_s":T,"interval_frames":int(round(inferred_frames)),
        "observed_fragment_frames":int(len(ids)),
        "inferred_full_fall_frames":float(inferred_frames),
        "detected_frames":int(detected),"detected_fraction":float(detected_fraction),
        "timing_fit_rms_frames":timing,"trajectory_shape_rms_fraction":shape,
        "release_speed_ratio":0.0,"x_drift_fraction":xdrift,
        "gap_penalty":gap_penalty,"identity_score":identity,
        "median_circularity":mc,"median_solidity":ms,"median_circle_fill":mf,
        "median_axis_ratio":ma,"radius_cv":rcv,"area_cv":acv,
        "aspect_log_median":0.0,"envelope_source_track":0,"envelope_source_chunk":0,
        "normalized_acceleration_s2":float(2.0/(T*T)),
        "normalized_fit_a":0.0,"normalized_fit_b":0.0,
        "duration_10_90_s":float(T*(math.sqrt(.90)-math.sqrt(.10))),
        "roots_complete":1.0,"acceleration_stability":float(seed_mad/max(span,1e-12)),
        "event_extrapolation_frames":float(impact_miss),
        "release_plateau_track":0,"impact_plateau_track":0,
        "depth_proxy":"orange_component_area",
        "depth_proxy_relative_span":float(rel_span),
        "depth_impact_reversal":float(impact_reversal),
    }]

    return {
        "fps":fps,
        "valid_fraction":float(len(obs)/max(1,n)),
        "span_px":float(radius_span),"one_pixel_m":drop_height/radius_span,
        "active_frames":int(len(ids)),
        "times":rel_times,"position_m":position_m,
        "direct_acceleration_m_s2":release_rest_ghat,
        "trajectory_acceleration_m_s2":trajectory_g,
        "release_rest_acceleration_m_s2":release_rest_ghat,
        "full_fall_time_s":T,"timing_fit_rms_frames":timing,
        "trajectory_shape_rms_fraction":shape,
        "plateau_relative_mad":float(seed_mad/max(span,1e-12)),
        "monotone_fraction":monotone,"forward_fraction":monotone,
        "selected_direction_sign":1.0,
        "duration_10_90_s":float(T*(math.sqrt(.90)-math.sqrt(.10))),
        "release_time_s_absolute":t0,"track_id":0,"release_speed_ratio":0.0,
        "x_drift_fraction":xdrift,"gap_penalty":gap_penalty,
        "candidate_track_count":1,"interval_frames":int(round(inferred_frames)),
        "observed_fragment_frames":int(len(ids)),
        "inferred_full_fall_frames":float(inferred_frames),
        "detected_frames":int(detected),"detected_fraction":float(detected_fraction),
        "identity_score":identity,"median_circularity":mc,"median_solidity":ms,
        "median_circle_fill":mf,"median_axis_ratio":ma,
        "radius_cv":rcv,"area_cv":acv,"aspect_log_median":0.0,
        "edge_trim_left":0,"edge_trim_right":0,
        "acceleration_stability":float(seed_mad/max(span,1e-12)),
        "roots_complete":True,"candidate_audit":audit,
        "roi":[0,0,int(width),int(shape_hw[0] if shape_hw else width)],
    }


def extract(video,drop_height,width=640,max_seconds=5.0,minimum_interval_frames=12,tracker_config=None):
    if tracker_config and tracker_config.get("revision")=="ball_identity_v6_6_depth":
        return extract_perspective_depth_v66(
            video,drop_height,width=width,max_seconds=max_seconds,
            minimum_interval_frames=minimum_interval_frames,
            tracker_config=tracker_config
        )
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
    observed_fragment_frames=int(chosen.get("observed_fragment_frames",len(times)))
    inferred_full_fall_frames=float(
        chosen.get("inferred_full_fall_frames",T*fps)
    )
    if tracker_config and tracker_config.get("revision") in ("ball_identity_v6_5","ball_identity_v6_6_depth"):
        if observed_fragment_frames<minimum_interval_frames:
            raise ContractError(
                "V6.5 selector returned insufficient observed motion support"
            )
    elif inferred_full_fall_frames+1e-9<minimum_interval_frames:
        raise ContractError(
            "selector returned event whose inferred full fall violates minimum duration"
        )

    one_px=drop_height/max(float(chosen["span_px"]),1e-9)
    release_rest_ghat=2.0*drop_height/(T*T)
    X=np.column_stack([np.ones(len(times)),times,.5*times*times])
    trajectory_coef=np.linalg.lstsq(X,position_m,rcond=None)[0]
    trajectory_g=float(trajectory_coef[2])
    # V6.5 intentionally allows acquisition to begin mid-flight.  Its primary
    # acceleration estimate is the nuisance-velocity quadratic coefficient in
    # global spatial coordinates, not 2h/T^2 (which assumes release from rest is
    # directly observed).  Historical revisions retain the old diagnostic.
    ghat=(float(chosen["normalized_acceleration_s2"])*drop_height
          if "normalized_acceleration_s2" in chosen else release_rest_ghat)

    return {
        "fps":fps,
        "valid_fraction":float(sum(bool(x) for x in frame_candidates)/max(1,len(frame_candidates))),
        "span_px":float(chosen["span_px"]),
        "one_pixel_m":one_px,
        "active_frames":observed_fragment_frames,
        "times":times,
        "position_m":position_m,
        "direct_acceleration_m_s2":ghat,
        "trajectory_acceleration_m_s2":trajectory_g,
        "release_rest_acceleration_m_s2":release_rest_ghat,
        "full_fall_time_s":T,
        "timing_fit_rms_frames":float(chosen["timing_fit_rms_frames"]),
        "trajectory_shape_rms_fraction":float(chosen["trajectory_shape_rms_fraction"]),
        "plateau_relative_mad":0.0,
        "monotone_fraction":float(chosen["monotone_fraction"]),
        "forward_fraction":float(chosen["monotone_fraction"]),
        "selected_direction_sign":float(chosen["sign"]),
        "duration_10_90_s":float(
            chosen.get("duration_10_90_s",T*(math.sqrt(.90)-math.sqrt(.10)))
        ),
        "release_time_s_absolute":t0,
        "track_id":chosen["track_id"],
        "release_speed_ratio":float(chosen["release_speed_ratio"]),
        "x_drift_fraction":float(chosen["x_drift_fraction"]),
        "gap_penalty":float(chosen["gap_penalty"]),
        "candidate_track_count":len(tracks),
        "interval_frames":int(chosen["interval_frames"]),
        "observed_fragment_frames":int(chosen.get("observed_fragment_frames",observed_fragment_frames)),
        "inferred_full_fall_frames":float(chosen.get("inferred_full_fall_frames",inferred_full_fall_frames)),
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
        "acceleration_stability":float(chosen.get("acceleration_stability",0.0)),
        "roots_complete":bool(chosen.get("roots_complete",0.0)),
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

        # This regression intentionally includes a cleaner rectangular parabola
        # and a fragmented true ball drop. The ungated detector is expected to be
        # ambiguous; only the active V6.5 identity/kinematics path is under test here.
        v6cfg={
            "revision":"ball_identity_v6_5",
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
            "plateau_max_speed_px_per_frame":0.75,
            "plateau_min_frames":4,
            "plateau_max_gap_frames":24,
            "plateau_max_x_delta_px":45.0,
            "plateau_overlap_frames":6,
            "event_edge_fraction":0.03,
            "maximum_event_extrapolation_frames":30.0,
            "kinematic_fit_edge_fraction":0.02,
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
        assert selected[0]["global_progress_span"]>=v6cfg["minimum_global_progress_span"]
        # Direct V6.5 regression: top hold -> partially observed downward fall ->
        # bottom hold -> later upward reset.  The fitted constant acceleration may
        # begin mid-flight and must remain independent of reset timing.
        top=60.0;bottom=280.0;span=bottom-top;t0=.30
        true_T=math.sqrt(2.0*drop_m/G)
        pts=[]
        frame0=8
        frame_end=85
        for ff in range(frame0,frame_end+1):
            tt=ff/fps
            if tt<t0:
                pp=0.0
            elif tt<=t0+true_T:
                pp=((tt-t0)/true_T)**2
            elif tt<1.15:
                pp=1.0
            else:
                pp=max(0.0,1.0-(tt-1.15)/.45)
            yy=top+span*pp
            # Drop a few interior detections to exercise interpolation/partial
            # observation without losing the release or impact plateaus.
            if t0+.08<tt<t0+true_T-.06 and ff%5==0:
                continue
            pts.append({
                "frame":ff,"x":320.0,"y":yy,"area":300,
                "circularity":.90,"solidity":.98,"circle_fill":.88,
                "axis_ratio":.96,"radius":10.0,"aspect_log_abs":.02,
                "appearance_score":10.0,
            })
        chosen_evt,audit_evt=choose_ballistic_track_v65(
            [{"id":1,"pts":pts,"missed":0}],
            fps,minimum_interval_frames=12,identity_cfg=v6cfg
        )
        evt_g=float(chosen_evt["normalized_acceleration_s2"])*drop_m
        evt_rel=abs(evt_g-G)/G
        assert evt_rel<=.12,(evt_g,evt_rel,chosen_evt,audit_evt)
        assert chosen_evt["inferred_full_fall_frames"]>=12
        assert chosen_evt["observed_fragment_frames"]>=6
        assert chosen_evt["sign"]==1.0

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

        # Rejection helper is total and records reasons without relying on closure
        # scope. This is a direct regression for the V6.3 NameError incident.
        diag={}
        assert _reject(diag,"unit_test") is None
        assert diag=={"unit_test":1}

        # V6.3 fragment rejection must be controlled (None + reason), never an
        # exception. Use a nearly stationary fragment to force progress_span.
        selector_cfg={
            "expected_sign":1.0,
            "minimum_global_progress_span":0.15,
            "maximum_global_timing_fit_rms_frames":2.5,
        }
        chunk={
            "track_id":1,"chunk_id":0,
            "frames":np.arange(12,dtype=int),
            "abs_times":np.arange(12,dtype=float)/60.0,
            "x":np.zeros(12,dtype=float),
            "y":np.linspace(100.0,105.0,12),
            "detected_frames_original":np.arange(12,dtype=int),
            "detected_fraction_chunk":1.0,
            "gap_penalty":0.0,"identity_score":0.9,
            "median_circularity":0.9,"median_solidity":0.95,
            "median_circle_fill":0.9,"median_axis_ratio":0.95,
            "radius_cv":0.05,"area_cv":0.05,"aspect_log_median":0.02,
        }
        diag={}
        bad=_fit_global_fragment(
            chunk,0,12,0.0,220.0,220.0,60.0,selector_cfg,12,
            diagnostics=diag
        )
        assert bad is None
        assert diag.get("progress_span",0)==1,diag

        # Static scope guard: reject(...) must never be called as a free/local
        # name. Use AST rather than source-string matching so the test does not
        # trigger on its own assertion text or comments.
        import ast,inspect
        tree=ast.parse(pathlib.Path(__file__).read_text())
        bad=[
            (getattr(node,"lineno",None),ast.unparse(node))
            for node in ast.walk(tree)
            if isinstance(node,ast.Call)
            and isinstance(node.func,ast.Name)
            and node.func.id=="reject"
        ]
        assert not bad,bad
        sig=inspect.signature(_recalibrate_gravity_candidate)
        assert "diagnostics" in sig.parameters,sig

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
                qq["release_rest_acceleration_m_s2"]=2.0*height/(float(q["full_fall_time_s"])**2)
                qq["direct_acceleration_m_s2"]=(
                    float(q["normalized_acceleration_s2"])*height
                    if q.get("normalized_acceleration_s2","") not in ("",None)
                    else qq["release_rest_acceleration_m_s2"]
                )
                qq["acceleration_relative_error"]=abs(qq["direct_acceleration_m_s2"]-G)/G
                candidate_audit_rows.append(qq)
            checks={
                ("observed_motion_frames" if tg.get("revision")=="ball_identity_v6_5"
                 else "full_duration_frames"):
                    (tr["observed_fragment_frames"]>=tg["minimum_active_frames"]
                     if tg.get("revision")=="ball_identity_v6_5"
                     else tr["inferred_full_fall_frames"]>=tg["minimum_active_frames"]),
                "monotone":tr["monotone_fraction"]>=tg.get("minimum_monotone_fraction",.78),
                "timing_fit":tr["timing_fit_rms_frames"]<=tg.get("maximum_timing_fit_rms_frames",2.5),
                "trajectory_shape":tr["trajectory_shape_rms_fraction"]<=tg.get("maximum_trajectory_shape_rms_fraction",.10),
                "release_speed":(
                    True if tg.get("revision")=="ball_identity_v6_5"
                    else tr["release_speed_ratio"]<=tg.get("maximum_release_speed_ratio",.65)
                ),
                "x_drift":tr["x_drift_fraction"]<=(
                    tg.get("maximum_depth_lateral_diameters",1.25)
                    if tg.get("revision")=="ball_identity_v6_6_depth"
                    else tg.get("maximum_x_drift_fraction",.45)
                ),
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
                          "release_rest_acceleration_m_s2":tr["release_rest_acceleration_m_s2"],
                          "acceleration_relative_error":abs(tr["direct_acceleration_m_s2"]-G)/G,
                          "active_frames":tr["active_frames"],
                          "observed_fragment_frames":tr["observed_fragment_frames"],
                          "inferred_full_fall_frames":tr["inferred_full_fall_frames"],
                          "valid_fraction":tr["valid_fraction"],
                          "span_px":tr["span_px"],"one_pixel_m":tr["one_pixel_m"],
                          "full_fall_time_s":tr["full_fall_time_s"],
                          "timing_fit_rms_frames":tr["timing_fit_rms_frames"],
                          "trajectory_shape_rms_fraction":tr["trajectory_shape_rms_fraction"],
                          "release_speed_ratio":tr["release_speed_ratio"],
                          "acceleration_stability":tr.get("acceleration_stability",0.0),
                          "roots_complete":tr.get("roots_complete",False),
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
                "global_progress_span","raw_global_progress_span","global_progress_start","global_progress_end",
                "full_fall_time_s","interval_frames","observed_fragment_frames",
                "inferred_full_fall_frames","detected_frames","detected_fraction",
                "timing_fit_rms_frames","trajectory_shape_rms_fraction","release_speed_ratio",
                "x_drift_fraction","gap_penalty","identity_score","median_circularity",
                "median_solidity","median_circle_fill","median_axis_ratio","radius_cv","area_cv",
                "aspect_log_median","envelope_track_count","envelope_source_track",
                "envelope_source_chunk","normalized_acceleration_s2",
                "normalized_fit_a","normalized_fit_b","duration_10_90_s",
                "roots_complete","acceleration_stability","event_extrapolation_frames",
                "release_plateau_track","impact_plateau_track",
                "depth_proxy","depth_proxy_relative_span","depth_impact_reversal",
                "direct_acceleration_m_s2","release_rest_acceleration_m_s2",
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
              "OBSERVED_FRAGMENT_FRAMES",t["observed_fragment_frames"],
              "INFERRED_FULL_FALL_FRAMES",t["inferred_full_fall_frames"],
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
              "ASPECT_LOG",t["aspect_log_median"],
              "TRACKER",tg["revision"])
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

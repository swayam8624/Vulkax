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
            gaps=np.diff(fr)
            gap_penalty=float(np.mean(np.maximum(gaps-1,0))) if len(gaps) else 0.0
            identity=(
                .28*mc+.22*mf+.18*ms+.17*ma
                +.15*max(0.0,1.0-min(rcv,1.0))
            )
            out.append({
                "track_id":int(tr["id"]),"chunk_id":chunk_id,
                "frames":dense,"abs_times":dense/fps,"x":xd,"y":yd,
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


def _fit_event_time_candidate_v66(
        chunk,aa,bb,fps,selector_cfg,identity_cfg,minimum_interval_frames,
        diagnostics=None):
    """Recover a release->impact duration without a pixel-to-metre ruler.

    V6.6 fits constant *pixel* acceleration only to identify the ballistic phase.
    The release time is the fitted velocity-zero point.  The impact time is the
    terminal change point where the positive-speed ballistic run stops, reverses,
    or the track terminates.  The physical acceleration estimate is then obtained
    downstream from the independently measured drop height h and elapsed time T:

        g_hat = 2 h / T^2

    Consequently neither the largest same-ball excursion nor manual reset motion
    defines metric scale.  Target gravity is absent from candidate generation,
    filtering, and ranking.
    """
    expected=float(selector_cfg["expected_sign"])
    all_frames=np.asarray(chunk["frames"],int)
    raw_y=np.asarray(chunk["y"],float)
    raw_x=np.asarray(chunk["x"],float)
    if len(all_frames)<6 or bb-aa<6:
        return _reject(diagnostics,"too_few_points")

    z_all=smooth1(expected*raw_y,5)
    x_all=smooth1(raw_x,5)
    frames=all_frames[aa:bb]
    z=z_all[aa:bb]
    local_span=float(np.ptp(z))
    min_span=float(identity_cfg.get("minimum_raw_event_span_px",18.0))
    if not math.isfinite(local_span) or local_span<min_span:
        return _reject(diagnostics,"raw_event_span")

    # Detect the sustained positive-speed core first. Boundary samples are the
    # least reliable part of a tracked event because the 5-frame smoother leaks a
    # little motion into stationary holds. Fit the quadratic only on the moving
    # core, then use observable stationary plateaus as boundary measurements.
    v=np.gradient(z_all,all_frames.astype(float))
    active_threshold=max(
        0.20,
        float(identity_cfg.get("plateau_max_speed_px_per_frame",.75))
    )
    run_indices=np.arange(aa,bb,dtype=int)
    # A free-fall speed rises until impact.  Do not let the smoothed impact
    # transition/plateau tail enter the quadratic fit: stop at the peak positive
    # image speed and use later samples only for boundary detection.
    peak_local=int(np.argmax(v[run_indices]))
    peak_idx=int(run_indices[peak_local])
    active=run_indices[(v[run_indices]>active_threshold)&(run_indices<=peak_idx)]
    if len(active)<6:
        return _reject(diagnostics,"too_few_accelerating_samples")

    core_a=int(active[0]);core_b=peak_idx+1
    core_frames=all_frames[core_a:core_b]
    core_z=(expected*raw_y)[core_a:core_b]
    core_t=core_frames/fps
    t0core=float(core_t[0])
    tr=core_t-t0core
    X=np.column_stack([np.ones(len(tr)),tr,.5*tr*tr])
    coef=np.linalg.lstsq(X,core_z,rcond=None)[0]
    a0,b0,c0=map(float,coef)
    if not all(math.isfinite(q) for q in (a0,b0,c0)) or c0<=0.0:
        return _reject(diagnostics,"nonpositive_pixel_acceleration")

    pred=X@coef
    core_span=max(float(np.ptp(core_z)),1e-9)
    shape=float(np.sqrt(np.mean((core_z-pred)**2))/core_span)
    max_shape=float(identity_cfg.get("maximum_trajectory_shape_rms_fraction",.10))
    if not math.isfinite(shape) or shape>max_shape:
        return _reject(diagnostics,"shape_rms")

    deriv=b0+c0*tr
    if float(np.mean(deriv>=-1e-6))<.90:
        return _reject(diagnostics,"predicted_direction")

    # Release is the zero-velocity vertex of the moving-core fit. Because the fit
    # excludes the smoothed stationary edge, the vertex is not dragged backward
    # by the top hold.
    release_tau=-b0/c0
    release_abs=float(t0core+release_tau)
    release_offset_frames=(release_abs-float(core_t[0]))*fps
    max_pre_frames=float(identity_cfg.get("maximum_release_extrapolation_frames",12.0))
    max_future_frames=float(identity_cfg.get("maximum_release_future_frames",2.0))
    if release_offset_frames < -max_pre_frames:
        return _reject(diagnostics,"release_too_far_before_observation")
    if release_offset_frames > max_future_frames:
        return _reject(diagnostics,"release_after_motion_onset")

    # Prefer an actually observed post-flight stationary plateau. Its position is
    # a boundary observation only; it is NOT treated as a metre ruler. Solve the
    # fitted pixel parabola for that plateau position to estimate impact time.
    # If no plateau is visible, fall back to the terminal motion change.
    plateau_threshold=float(identity_cfg.get("plateau_max_speed_px_per_frame",.75))
    plateau_min=int(identity_cfg.get("plateau_min_frames",4))
    search0=max(core_b,bb-2)
    plateau_idx=None
    for j in range(search0,max(search0,len(all_frames)-plateau_min+1)):
        j2=min(len(all_frames),j+plateau_min)
        if j2-j<plateau_min:
            break
        if np.all(np.abs(v[j:j2])<=plateau_threshold):
            plateau_idx=(j,j2)
            break

    if plateau_idx is not None:
        j0,j1=plateau_idx
        impact_level=float(np.median((expected*raw_y)[j0:j1]))
        impact_tau=_increasing_quadratic_root(a0,b0,c0,impact_level)
        if impact_tau is None:
            return _reject(diagnostics,"impact_level_root")
        impact_abs=float(t0core+impact_tau)
        impact_idx=int(np.clip(np.searchsorted(all_frames,impact_abs*fps),0,len(all_frames)-1))
        pre_lo=max(core_a,core_b-3)
        pre_speed=float(np.median(v[pre_lo:core_b]))
        post_speed=float(np.median(v[j0:j1]))
        impact_kind="plateau"
    else:
        last_active=int(active[-1])
        impact_idx=min(last_active+1,len(all_frames)-1)
        if impact_idx<=core_a+4:
            return _reject(diagnostics,"impact_too_early")
        pre_lo=max(core_a,last_active-2)
        pre_speed=float(np.median(v[pre_lo:last_active+1]))
        post_lo=impact_idx+1
        post_hi=min(len(v),post_lo+4)
        if post_lo>=len(v):
            post_speed=0.0
            impact_kind="track_end"
        else:
            post_speed=float(np.median(v[post_lo:post_hi])) if post_hi>post_lo else 0.0
            impact_kind="reversal" if post_speed<0.0 else "slowdown"
        impact_abs=float(all_frames[impact_idx]/fps)

    if not math.isfinite(pre_speed) or pre_speed<=active_threshold:
        return _reject(diagnostics,"weak_preimpact_speed")
    impact_speed_ratio=post_speed/max(pre_speed,1e-9)
    max_impact_ratio=float(identity_cfg.get("maximum_impact_speed_ratio",.55))
    if impact_speed_ratio>max_impact_ratio:
        return _reject(diagnostics,"no_terminal_impact_change")

    T=impact_abs-release_abs
    if not math.isfinite(T) or T<=0.0 or T>1.25:
        return _reject(diagnostics,"invalid_event_duration")
    if T*fps<minimum_interval_frames:
        return _reject(diagnostics,"full_duration")

    # Define a local release->impact image coordinate only for residual/scoring
    # diagnostics.  The primary physical g estimate does not use this pixel scale.
    release_rel=release_abs-t0core
    z_release=float(a0+b0*release_rel+.5*c0*release_rel*release_rel)
    z_impact=float(z_all[impact_idx])
    event_span=z_impact-z_release
    if not math.isfinite(event_span) or event_span<min_span:
        return _reject(diagnostics,"release_impact_span")

    progress_full=(expected*raw_y-z_release)/event_span
    obs_start=max(aa,int(np.searchsorted(all_frames,math.ceil(release_abs*fps))))
    obs_end=impact_idx+1
    if obs_end-obs_start<6:
        return _reject(diagnostics,"too_few_event_observations")
    ids=np.arange(obs_start,obs_end,dtype=int)
    p=np.clip(progress_full[ids],0.0,1.0)
    monotone=float(np.mean(np.diff(p)>=-.02)) if len(p)>1 else 1.0
    if monotone<float(identity_cfg.get("minimum_monotone_fraction",.78)):
        return _reject(diagnostics,"monotone")

    xdrift=float(np.ptp(x_all[ids]))/max(event_span,1e-9)
    if xdrift>float(identity_cfg.get("maximum_x_drift_fraction",.45)):
        return _reject(diagnostics,"x_drift")

    frame_lo=int(all_frames[ids[0]]);frame_hi=int(all_frames[ids[-1]])
    det_frames=np.asarray(chunk["detected_frames_original"],int)
    detected=sum(1 for ff in det_frames if frame_lo<=int(ff)<=frame_hi)
    detected_fraction=detected/max(1,frame_hi-frame_lo+1)
    if detected_fraction<float(identity_cfg.get("minimum_detected_fraction",.45)):
        return _reject(diagnostics,"detected_fraction")

    # Split-half acceleration consistency is scale-free and therefore useful for
    # ranking without consulting the target magnitude of gravity.
    fit_ids=np.arange(aa,min(impact_idx+1,bb),dtype=int)
    acc_parts=[]
    if len(fit_ids)>=8:
        mid=len(fit_ids)//2
        for sub in (fit_ids[:max(4,mid+1)],fit_ids[max(0,mid-1):]):
            if len(sub)<4:
                continue
            tt=all_frames[sub]/fps-float(all_frames[sub[0]]/fps)
            zz=z_all[sub]
            XX=np.column_stack([np.ones(len(tt)),tt,.5*tt*tt])
            cc=float(np.linalg.lstsq(XX,zz,rcond=None)[0][2])
            if math.isfinite(cc):
                acc_parts.append(cc)
    acc_stability=(
        float(abs(acc_parts[0]-acc_parts[-1])/max(abs(c0),1e-9))
        if len(acc_parts)>=2 else 0.0
    )

    # Convert normalized position residual to an approximate temporal residual for
    # compatibility with the existing timing-quality field.
    timing=float(shape*T*fps)
    release_extrapolation_frames=max(0.0,-release_offset_frames)
    impact_rel=max(impact_abs-t0core,0.0)
    start_speed_ratio=float(
        abs(b0)/max(abs(b0+c0*impact_rel),1e-9)
    )

    return validate_candidate({
        "track_id":chunk["track_id"],"chunk_id":chunk["chunk_id"],
        "sign":expected,
        "span_px":float(event_span),
        "local_span_px":float(local_span),
        "low_px":float(z_release),
        "high_px":float(z_impact),
        "progress":progress_full,
        "dense_frames":all_frames,
        "abs_times":np.asarray(chunk["abs_times"],float),
        "window_indices":ids,
        "t0_s":release_abs,
        "full_fall_time_s":float(T),
        "timing_fit_rms_s":timing/fps,
        "timing_fit_rms_frames":timing,
        "trajectory_shape_rms_fraction":shape,
        "release_speed_ratio":start_speed_ratio,
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
        "interval_frames":int(round(T*fps)),
        "observed_fragment_frames":int(frame_hi-frame_lo+1),
        "inferred_full_fall_frames":float(T*fps),
        "detected_frames":int(detected),
        "global_progress_span":float(np.ptp(p)),
        "global_progress_start":float(np.min(p)),
        "global_progress_end":float(np.max(p)),
        "duration_10_90_s":float(T*(math.sqrt(.90)-math.sqrt(.10))),
        "roots_complete":1.0,
        "acceleration_stability":acc_stability,
        "event_extrapolation_frames":release_extrapolation_frames,
        "release_extrapolation_frames":release_extrapolation_frames,
        "impact_speed_ratio":float(impact_speed_ratio),
        "impact_boundary_kind":impact_kind,
        "release_plateau_track":-1,
        "impact_plateau_track":-1,
    })


def choose_ballistic_track_v66(tracks,fps,minimum_interval_frames=12,identity_cfg=None):
    """V6.6 event-time selector: metric-free release/impact timing."""
    cfg=identity_cfg or {}
    selector_cfg=validate_selector_config(cfg)
    chunks=_identity_track_chunks(tracks,fps,cfg)
    if not chunks:
        raise TrackSelectionError("no ball-like identity tracks for V6.6")

    candidates=[]
    rejection_counts={}
    for chunk in chunks:
        z=float(selector_cfg["expected_sign"])*np.asarray(chunk["y"],float)
        scale=max(float(np.ptp(z)),1.0)
        local_progress=(z-float(np.min(z)))/scale
        for aa,bb in _monotone_runs(local_progress):
            q=_fit_event_time_candidate_v66(
                chunk,aa,bb,fps,selector_cfg,cfg,minimum_interval_frames,
                diagnostics=rejection_counts
            )
            if q is not None:
                candidates.append(q)

    if not candidates:
        raise TrackSelectionError(
            "no release->impact constant-acceleration event survived V6.6; "
            f"chunks={len(chunks)} rejects={json.dumps(rejection_counts,sort_keys=True)}"
        )

    def rank(q):
        return (
            float(q.get("impact_speed_ratio",1.0)),
            float(q.get("release_extrapolation_frames",0.0)),
            float(q.get("acceleration_stability",0.0)),
            float(q["timing_fit_rms_frames"]),
            float(q["trajectory_shape_rms_fraction"]),
            -float(q["span_px"]),
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
            "acceleration_stability":float(q["acceleration_stability"]),
            "event_extrapolation_frames":float(q["event_extrapolation_frames"]),
            "release_extrapolation_frames":float(q["release_extrapolation_frames"]),
            "impact_speed_ratio":float(q["impact_speed_ratio"]),
            "impact_boundary_kind":q["impact_boundary_kind"],
            "release_plateau_track":-1,
            "impact_plateau_track":-1,
        })
    return chosen,audit

def _refine_candidate_with_global_plateaus_v67(
        candidate,source_chunk,plateaus,fps,selector_cfg,identity_cfg,
        minimum_interval_frames,diagnostics=None):
    """Bind a partial ballistic fragment to release/impact holds across track IDs.

    V6.6 correctly removed manual-reset metric scaling but could terminate a
    physical fall at the end of one fragmented temporal track.  V6.7 treats
    stationary, ball-identity-qualified plateaus as *boundary evidence* even when
    they were assigned a different track ID.  No target acceleration magnitude is
    used: a proposed pair is judged only by temporal order, spatial bracketing,
    object geometry, and the protocol's release-from-rest trajectory shape.
    """
    q=dict(candidate)
    expected=float(selector_cfg["expected_sign"])
    all_frames=np.asarray(source_chunk["frames"],int)
    yy=np.asarray(source_chunk["y"],float)
    xx=np.asarray(source_chunk["x"],float)
    ids=np.asarray(q["window_indices"],int)
    if len(ids)<6:
        return q

    obs_frames=all_frames[ids]
    f0=int(obs_frames[0]);f1=int(obs_frames[-1])
    z=expected*yy
    z0=float(np.median(z[ids[:min(4,len(ids))]]))
    z1=float(np.median(z[ids[-min(4,len(ids)):]]))
    x0=float(np.median(xx[ids[:min(4,len(ids))]]))
    x1=float(np.median(xx[ids[-min(4,len(ids)):]]))

    overlap=int(identity_cfg.get("plateau_overlap_frames",6))
    max_gap=int(identity_cfg.get("cross_track_plateau_max_gap_frames",30))
    max_x=float(identity_cfg.get("cross_track_plateau_max_x_delta_px",100.0))
    y_tol=float(identity_cfg.get("cross_track_plateau_y_tolerance_px",35.0))
    min_span=float(identity_cfg.get("minimum_raw_event_span_px",18.0))

    prior=[]
    post=[]
    for p in plateaus:
        pz=expected*float(p["y"])
        if (int(p["frame_end"])<=f0+overlap
            and f0-int(p["frame_end"])<=max_gap
            and abs(float(p["x"])-x0)<=max_x
            and pz<=z0+y_tol):
            prior.append(p)
        if (int(p["frame_start"])>=f1-overlap
            and int(p["frame_start"])-f1<=max_gap
            and abs(float(p["x"])-x1)<=max_x
            and pz>=z1-y_tol):
            post.append(p)

    if not prior or not post:
        q["boundary_mode"]="local_fallback"
        q["release_boundary_track"]=-1
        q["impact_boundary_track"]=-1
        q["boundary_pair_shape_rms"]=float(q["trajectory_shape_rms_fraction"])
        return q

    best=None
    for top in prior:
        topz=expected*float(top["y"])
        for bottom in post:
            botz=expected*float(bottom["y"])
            span=botz-topz
            if not math.isfinite(span) or span<min_span:
                continue

            # Mid-frame boundaries avoid systematically assigning a whole extra
            # frame to both the top hold and bottom hold.
            release_frame=float(top["frame_end"])+0.5
            impact_frame=float(bottom["frame_start"])-0.5
            full_frames=impact_frame-release_frame
            if full_frames+1e-9<minimum_interval_frames or full_frames>75.0:
                continue
            if int(bottom["frame_start"])<=int(top["frame_end"]):
                continue

            release_abs=release_frame/fps
            impact_abs=impact_frame/fps
            T=impact_abs-release_abs
            if T<=0.0:
                continue

            # Evaluate only actually observed points from the source fragment.
            detset=set(int(x) for x in np.asarray(
                source_chunk["detected_frames_original"],int
            ))
            use=np.asarray([
                i for i in ids
                if int(all_frames[i]) in detset
                and release_frame-1e-9<=float(all_frames[i])<=impact_frame+1e-9
            ],dtype=int)
            if len(use)<6:
                continue
            pp=(z[use]-topz)/span
            keep=(pp>=-0.08)&(pp<=1.08)
            use=use[keep];pp=pp[keep]
            if len(use)<6:
                continue
            pp=np.clip(pp,0.0,1.0)
            tt=all_frames[use]/fps
            model=np.square(np.clip((tt-release_abs)/T,0.0,1.0))
            shape=float(np.sqrt(np.mean((pp-model)**2)))
            if not math.isfinite(shape) or shape>float(
                identity_cfg.get("maximum_cross_track_shape_rms_fraction",.12)
            ):
                continue

            pred_t=release_abs+T*np.sqrt(np.clip(pp,0.0,1.0))
            timing=float(np.sqrt(np.mean((tt-pred_t)**2))*fps)
            if timing>float(selector_cfg["maximum_global_timing_fit_rms_frames"]):
                continue
            monotone=float(np.mean(np.diff(pp)>=-.02)) if len(pp)>1 else 1.0
            if monotone<float(identity_cfg.get("minimum_monotone_fraction",.78)):
                continue

            xuse=xx[use]
            xdrift=float(np.ptp(xuse))/max(span,1e-9)
            if xdrift>float(identity_cfg.get("maximum_x_drift_fraction",.45)):
                continue

            release_gap=max(0.0,float(f0-int(top["frame_end"])))
            impact_gap=max(0.0,float(int(bottom["frame_start"])-f1))
            # Prefer the pair with the best law-consistency, then the broadest
            # spatial bracket and closest temporal support.  Duration itself is
            # deliberately absent from ranking.
            key=(
                timing,shape,
                release_gap+impact_gap,
                -span,
                -float(top.get("identity_score",0.0))
                -float(bottom.get("identity_score",0.0)),
            )
            if best is None or key<best[0]:
                best=(key,top,bottom,use,pp,span,release_abs,T,timing,shape,
                      monotone,xdrift,release_gap,impact_gap)

    if best is None:
        _reject(diagnostics,"no_global_plateau_pair")
        q["boundary_mode"]="local_fallback"
        q["release_boundary_track"]=-1
        q["impact_boundary_track"]=-1
        q["boundary_pair_shape_rms"]=float(q["trajectory_shape_rms_fraction"])
        return q

    (_key,top,bottom,use,pp,span,release_abs,T,timing,shape,
     monotone,xdrift,release_gap,impact_gap)=best
    topz=expected*float(top["y"])
    botz=expected*float(bottom["y"])
    progress_full=(z-topz)/span
    detset=set(int(x) for x in np.asarray(source_chunk["detected_frames_original"],int))
    frame_lo=int(all_frames[use[0]]);frame_hi=int(all_frames[use[-1]])
    detected=sum(1 for ff in detset if frame_lo<=ff<=frame_hi)
    detected_fraction=detected/max(1,frame_hi-frame_lo+1)

    q.update({
        "span_px":float(span),
        "local_span_px":float(np.ptp(z[use])),
        "low_px":float(topz),
        "high_px":float(botz),
        "progress":progress_full,
        "window_indices":use,
        "t0_s":float(release_abs),
        "full_fall_time_s":float(T),
        "timing_fit_rms_s":float(timing/fps),
        "timing_fit_rms_frames":float(timing),
        "trajectory_shape_rms_fraction":float(shape),
        "release_speed_ratio":0.0,
        "x_drift_fraction":float(xdrift),
        "monotone_fraction":float(monotone),
        "detected_window_fraction":float(detected_fraction),
        "interval_frames":int(round(T*fps)),
        "observed_fragment_frames":int(frame_hi-frame_lo+1),
        "inferred_full_fall_frames":float(T*fps),
        "detected_frames":int(detected),
        "global_progress_span":float(np.ptp(pp)),
        "global_progress_start":float(np.min(pp)),
        "global_progress_end":float(np.max(pp)),
        "duration_10_90_s":float(T*(math.sqrt(.90)-math.sqrt(.10))),
        "roots_complete":1.0,
        "event_extrapolation_frames":float(release_gap+impact_gap),
        "release_extrapolation_frames":float(release_gap),
        "impact_speed_ratio":0.0,
        "impact_boundary_kind":"cross_track_plateau",
        "boundary_mode":"cross_track_plateaus",
        "release_boundary_track":int(top["track_id"]),
        "impact_boundary_track":int(bottom["track_id"]),
        "release_plateau_track":int(top["track_id"]),
        "impact_plateau_track":int(bottom["track_id"]),
        "boundary_pair_shape_rms":float(shape),
        "boundary_release_gap_frames":float(release_gap),
        "boundary_impact_gap_frames":float(impact_gap),
    })
    return validate_candidate(q)


def choose_ballistic_track_v67(tracks,fps,minimum_interval_frames=12,identity_cfg=None):
    """V6.7: V6.6 metric-free timing plus cross-track physical boundaries."""
    cfg=identity_cfg or {}
    selector_cfg=validate_selector_config(cfg)
    chunks=_identity_track_chunks(tracks,fps,cfg)
    if not chunks:
        raise TrackSelectionError("no ball-like identity tracks for V6.7")
    plateaus=_plateau_segments(chunks,selector_cfg)
    by_chunk={int(c["chunk_id"]):c for c in chunks}

    base=[]
    rejection_counts={}
    for chunk in chunks:
        z=float(selector_cfg["expected_sign"])*np.asarray(chunk["y"],float)
        scale=max(float(np.ptp(z)),1.0)
        local_progress=(z-float(np.min(z)))/scale
        for aa,bb in _monotone_runs(local_progress):
            q=_fit_event_time_candidate_v66(
                chunk,aa,bb,fps,selector_cfg,cfg,minimum_interval_frames,
                diagnostics=rejection_counts
            )
            if q is not None:
                base.append(q)
    if not base:
        raise TrackSelectionError(
            "no metric-free ballistic fragments survived V6.7 base selector; "
            f"chunks={len(chunks)} plateaus={len(plateaus)} "
            f"rejects={json.dumps(rejection_counts,sort_keys=True)}"
        )

    candidates=[]
    for q in base:
        source=by_chunk[int(q["chunk_id"])]
        candidates.append(_refine_candidate_with_global_plateaus_v67(
            q,source,plateaus,fps,selector_cfg,cfg,minimum_interval_frames,
            diagnostics=rejection_counts
        ))

    def rank(q):
        return (
            0 if q.get("boundary_mode")=="cross_track_plateaus" else 1,
            float(q["timing_fit_rms_frames"]),
            float(q["trajectory_shape_rms_fraction"]),
            float(q.get("acceleration_stability",0.0)),
            float(q.get("event_extrapolation_frames",0.0)),
            float(q.get("impact_speed_ratio",1.0)),
            -float(q["identity_score"]),
            float(q["t0_s"]),
        )
    candidates=sorted(candidates,key=rank)
    chosen=candidates[0]

    audit=[]
    for i,q in enumerate(candidates[:30],1):
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
            "acceleration_stability":float(q.get("acceleration_stability",0.0)),
            "event_extrapolation_frames":float(q.get("event_extrapolation_frames",0.0)),
            "release_extrapolation_frames":float(q.get("release_extrapolation_frames",0.0)),
            "impact_speed_ratio":float(q.get("impact_speed_ratio",0.0)),
            "impact_boundary_kind":q.get("impact_boundary_kind",""),
            "boundary_mode":q.get("boundary_mode","local_fallback"),
            "release_boundary_track":int(q.get("release_boundary_track",-1)),
            "impact_boundary_track":int(q.get("impact_boundary_track",-1)),
            "boundary_pair_shape_rms":float(q.get(
                "boundary_pair_shape_rms",q["trajectory_shape_rms_fraction"]
            )),
            "boundary_release_gap_frames":float(q.get("boundary_release_gap_frames",0.0)),
            "boundary_impact_gap_frames":float(q.get("boundary_impact_gap_frames",0.0)),
            "release_plateau_track":int(q.get("release_plateau_track",-1)),
            "impact_plateau_track":int(q.get("impact_plateau_track",-1)),
        })
    return chosen,audit

def choose_ballistic_track(tracks,fps,minimum_interval_frames=12,identity_cfg=None):
    cfg=identity_cfg or {}
    if cfg.get("revision")=="ball_identity_v6_7":
        return choose_ballistic_track_v67(
            tracks,fps,minimum_interval_frames=minimum_interval_frames,
            identity_cfg=cfg
        )
    if cfg.get("revision")=="ball_identity_v6_6":
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
    observed_fragment_frames=int(chosen.get("observed_fragment_frames",len(times)))
    inferred_full_fall_frames=float(
        chosen.get("inferred_full_fall_frames",T*fps)
    )
    if tracker_config and tracker_config.get("revision")=="ball_identity_v6_5":
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
    if tracker_config and tracker_config.get("revision") in ("ball_identity_v6_6","ball_identity_v6_7"):
        # V6.6 is deliberately metric-free in image space: use independently
        # measured drop height and release->impact event time only.
        ghat=release_rest_ghat
    else:
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
            "revision":"ball_identity_v6_7",
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
        chosen_evt,audit_evt=choose_ballistic_track_v66(
            [{"id":1,"pts":pts,"missed":0}],
            fps,minimum_interval_frames=12,identity_cfg=v6cfg
        )
        evt_g=2.0*drop_m/(float(chosen_evt["full_fall_time_s"])**2)
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
                    qq["release_rest_acceleration_m_s2"]
                    if tg.get("revision") in ("ball_identity_v6_6","ball_identity_v6_7")
                    else (
                        float(q["normalized_acceleration_s2"])*height
                        if q.get("normalized_acceleration_s2","") not in ("",None)
                        else qq["release_rest_acceleration_m_s2"]
                    )
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
                    True if tg.get("revision") in ("ball_identity_v6_5","ball_identity_v6_6")
                    else tr["release_speed_ratio"]<=tg.get("maximum_release_speed_ratio",.65)
                ),
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
                "release_extrapolation_frames","impact_speed_ratio","impact_boundary_kind",
                "release_plateau_track","impact_plateau_track",
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

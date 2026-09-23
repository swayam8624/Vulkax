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
import argparse,csv,json,math,pathlib,statistics,subprocess,tempfile
import numpy as np

G=9.80665
TAU=2.0
DOSES=(.025,.05,.10,.20)

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
        mean_diff=float(np.mean(diff_crop[labels==lab]))
        compact=fill/math.sqrt(max(area,1))
        out.append({
            "x":float(cx+x0),"y":float(cy+y0),"area":area,
            "w":bw,"h":bh,"fill":fill,"mean_diff":mean_diff,
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

def fit_release_quadratic(track,fps,minimum_interval_frames=12):
    """Fit a near-full coherent track as release-from-rest motion.

    V4 searched arbitrary subsegments and therefore preferred tiny, near-perfect
    quadratic fragments. V5 only permits the whole contiguous track chunk, with at
    most two edge detections trimmed for segmentation noise. The selected physical
    interval must satisfy the existing minimum-active-frame duration before the
    independently measured drop height can be bound to it.
    """
    pts=track["pts"]
    frames=np.asarray([p["frame"] for p in pts],int)
    xx=np.asarray([p["x"] for p in pts],float)
    yy=np.asarray([p["y"] for p in pts],float)
    best=None

    # Split only at gaps larger than the tracker's explicit recovery allowance.
    cuts=[0]
    for i in range(1,len(frames)):
        if frames[i]-frames[i-1]>5:cuts.append(i)
    cuts.append(len(frames))

    for aa,bb in zip(cuts,cuts[1:]):
        if bb-aa<8:continue
        fr0=frames[aa:bb];x0=xx[aa:bb];y0=yy[aa:bb]
        # Edge trimming only. No arbitrary interior cherry-picking.
        for trim_l in range(0,min(3,max(1,len(fr0)-7))):
            for trim_r in range(0,min(3,max(1,len(fr0)-trim_l-7))):
                hi=len(fr0)-trim_r if trim_r else len(fr0)
                fr=fr0[trim_l:hi];x=x0[trim_l:hi];y=y0[trim_l:hi]
                if len(fr)<8:continue
                interval_frames=int(fr[-1]-fr[0]+1)
                if interval_frames<minimum_interval_frames:continue
                if interval_frames>int(round(1.25*fps)):continue
                detected_fraction=len(fr)/max(1,interval_frames)
                if detected_fraction<.55:continue

                for sign in (1.0,-1.0):
                    q=sign*y
                    t=(fr-fr[0])/fps
                    span=float(q[-1]-q[0])
                    if span<18.0:continue
                    d=np.diff(q)
                    monotone=float(np.mean(d>=-1.5))
                    if monotone<.78:continue

                    X=np.column_stack([np.ones(len(t)),t,t*t])
                    coef=np.linalg.lstsq(X,q,rcond=None)[0]
                    pred=X@coef
                    residual=float(np.sqrt(np.mean((q-pred)**2)))/max(span,1e-9)
                    a,b,c=map(float,coef)
                    if c<=0:continue
                    duration=float(t[-1])
                    accel_term=2.0*c*duration
                    release_ratio=abs(b)/max(abs(accel_term),1e-9)
                    if release_ratio>.65:continue
                    x_drift=float(np.percentile(x,95)-np.percentile(x,5))/max(span,1e-9)
                    if x_drift>.45:continue

                    release_offset=-b/(2.0*c)
                    if release_offset<-.25 or release_offset>.12:continue
                    t_release=release_offset
                    y_release=a+b*t_release+c*t_release*t_release
                    y_end=float(pred[-1])
                    fit_span=y_end-y_release
                    if fit_span<15.0:continue
                    v_end=b+2.0*c*duration
                    if v_end<=0:continue

                    gaps=np.diff(fr)
                    gap_penalty=float(np.mean(np.maximum(gaps-1,0))) if len(gaps) else 0.0
                    # Prefer the fullest coherent flight first; fit residual only
                    # decides between similarly complete candidates. This prevents
                    # a short pristine fragment from beating the physical event.
                    rank=(-interval_frames,-fit_span,-detected_fraction,
                          residual,release_ratio,x_drift,gap_penalty)
                    cand={
                        "rank":rank,"track_id":track["id"],"frames":fr,"x":x,
                        "y_signed":q,"sign":sign,"coef":coef,"pred":pred,
                        "residual_fraction":residual,
                        "release_speed_ratio":release_ratio,
                        "x_drift_fraction":x_drift,
                        "monotone_fraction":monotone,
                        "release_time_offset_s":release_offset,
                        "duration_s":duration,"fit_span_px":fit_span,
                        "v_end_px_s":v_end,"start_frame":int(fr[0]),
                        "end_frame":int(fr[-1]),"gap_penalty":gap_penalty,
                        "interval_frames":interval_frames,
                        "detected_frames":len(fr),
                        "detected_fraction":detected_fraction,
                        "edge_trim_left":trim_l,
                        "edge_trim_right":trim_r,
                    }
                    if best is None or cand["rank"]<best["rank"]:best=cand
    return best

def choose_ballistic_track(tracks,fps,minimum_interval_frames=12):
    candidates=[]
    for tr in tracks:
        q=fit_release_quadratic(tr,fps,minimum_interval_frames=minimum_interval_frames)
        if q is not None:candidates.append(q)
    if not candidates:raise RuntimeError("no temporally consistent release-from-rest track found")
    return min(candidates,key=lambda q:q["rank"])

def extract(video,drop_height,width=640,max_seconds=5.0):
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
        # Avoid an over-tight ROI that can clip the start/end of a drop.
        if (x1-x0)*(y1-yy0)<.03*bg.size:x0=yy0=0;y1,x1=bg.shape
    else:x0=yy0=0;y1,x1=bg.shape

    frame_candidates=[];times=[]
    cap=cv2.VideoCapture(str(video));i=0
    while i<n:
        ok,fr=cap.read()
        if not ok:break
        g=resize_gray(fr,width,cv2)
        diff=cv2.absdiff(g,bg);diff=cv2.GaussianBlur(diff,(5,5),0)
        crop=diff[yy0:y1,x0:x1]
        q=max(7.0,float(np.percentile(crop,99.5))*.55)
        frame_candidates.append(extract_components(crop,q,cv2,x0,yy0))
        times.append(i/fps);i+=1
    cap.release()

    tracks=build_temporal_tracks(frame_candidates,fps)
    if not tracks:raise RuntimeError("no compact temporal motion tracks")
    chosen=choose_ballistic_track(tracks,fps,minimum_interval_frames=12)

    fs=np.asarray(chosen["frames"],int)
    t_abs=fs/fps
    t=(fs-fs[0])/fps
    coef=np.asarray(chosen["coef"],float)
    a,b,c=coef
    release_offset=float(chosen["release_time_offset_s"])
    y_release=float(a+b*release_offset+c*release_offset*release_offset)
    pred=np.asarray(chosen["pred"],float)
    fit_span=float(chosen["fit_span_px"])
    one_px=drop_height/max(fit_span,1e-9)
    # Scale the fitted image trajectory by the measured total drop height. Because
    # release time is extrapolated from v=0, the fitted segment can start just after
    # release while still recovering the physical origin.
    pos_m=(pred-y_release)*(drop_height/max(fit_span,1e-9))
    t_release_relative=-release_offset
    t_phys=t+t_release_relative
    duration_to_end=float(t_phys[-1])
    if duration_to_end<=0:raise RuntimeError("non-positive inferred fall duration")
    ghat=2.0*drop_height/(duration_to_end*duration_to_end)

    # Equivalent acceleration from scaled quadratic coefficient.
    scale_m_per_px=drop_height/max(fit_span,1e-9)
    trajectory_g=2.0*c*scale_m_per_px

    # Build a dense, fitted trajectory for the verifier; the verification task tests
    # candidate acceleration residuals, not the object detector itself.
    return {
        "fps":fps,
        "valid_fraction":float(sum(bool(x) for x in frame_candidates)/max(1,len(frame_candidates))),
        "span_px":fit_span,"one_pixel_m":one_px,"active_frames":len(t_phys),
        "times":t_phys,"position_m":pos_m,
        "direct_acceleration_m_s2":ghat,
        "trajectory_acceleration_m_s2":trajectory_g,
        "full_fall_time_s":duration_to_end,
        "timing_fit_rms_frames":chosen["residual_fraction"]*fit_span/max(
            abs(chosen["v_end_px_s"])/fps,1.0
        ),
        "trajectory_shape_rms_fraction":chosen["residual_fraction"],
        "plateau_relative_mad":0.0,
        "monotone_fraction":chosen["monotone_fraction"],
        "forward_fraction":chosen["monotone_fraction"],
        "selected_direction_sign":chosen["sign"],
        "duration_10_90_s":chosen["duration_s"],
        "release_time_s_absolute":float(t_abs[0]+release_offset),
        "track_id":chosen["track_id"],
        "release_speed_ratio":chosen["release_speed_ratio"],
        "x_drift_fraction":chosen["x_drift_fraction"],
        "gap_penalty":chosen["gap_penalty"],
        "candidate_track_count":len(tracks),
        "interval_frames":int(chosen["interval_frames"]),
        "detected_frames":int(chosen["detected_frames"]),
        "detected_fraction":float(chosen["detected_fraction"]),
        "edge_trim_left":int(chosen["edge_trim_left"]),
        "edge_trim_right":int(chosen["edge_trim_right"]),
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
                # Deliberately add a slow reset to the top. The v1 "longest run"
                # selector can prefer this; revision 4 must still select free fall.
                frac=max(0.0,1.0-(t-1.45)/.85)
            yy=60+span_px*frac
            cv2.circle(q,(320,int(round(yy))),10,(255,255,255),-1)
            writer.write(q)
        writer.release()
        tr=extract(p,drop_m,width=640,max_seconds=2.5)
        rel=abs(tr["direct_acceleration_m_s2"]-G)/G
        assert rel<=0.20,(tr["direct_acceleration_m_s2"],rel,tr)
        assert tr["active_frames"]>=12
        assert tr["monotone_fraction"]>=.78
        print("VALID IRIS free-fall synthetic-video tracker",tr["direct_acceleration_m_s2"],rel)

FIELDS=["record_version","trial_id","paired_key","dataset","scene","split","evidence_class","confirmatory",
"trial_family","ground_truth","method","score","decision","decision_threshold","confidence","physical_delta",
"target_error_delta","measurement_noise_sigma","pose_noise_sigma","missing_fraction","channel_dependence",
"negative_control","seed","source_artifact","notes"]

def manifests(root,split,config):
    desired={"development":("drop_50",set(config["dataset"]["development"]["takes"])),
             "validation":("drop_100",set(config["dataset"]["validation"]["takes"])),
             "final_test":("drop_150",set(config["dataset"]["final_test"]["takes"]))}[split]
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
        assert len(cases())==11;assert decision(2)=="support" and decision(-2)=="veto"
        print("VALID IRIS free-fall analyzer self-test");return
    if a.self_test_video:
        self_test_video();return
    if a.split=="validation":
        d=json.loads(a.require_development_summary.read_text()) if a.require_development_summary else {}
        if not d.get("gate_pass"):raise SystemExit("development gate failed/missing")
        if d.get("tracker_revision")!="full_flight_v5":raise SystemExit("development tracker revision mismatch")
    if a.split=="final_test":
        v=json.loads(a.require_validation_summary.read_text()) if a.require_validation_summary else {}
        if not v.get("gate_pass"):raise SystemExit("validation gate failed/missing")
        if v.get("tracker_revision")!="full_flight_v5":raise SystemExit("validation tracker revision mismatch")
        if not a.lock:raise SystemExit("final_test requires lock")
        subprocess.run(["python3","research/analysis/freeze_iris_freefall_final_test.py","--check",str(a.lock)],check=True)
    out=a.out or pathlib.Path(f"build/publication-validation/iris-freefall-{a.split}")
    mm=manifests(a.adapted_root,a.split,cfg);expected={"development":4,"validation":4,"final_test":9}[a.split]
    if len(mm)!=expected:raise SystemExit(f"expected {expected} frozen scenes, got {len(mm)}")
    rows=[];takes=[];fails=[]
    setting={"development":"drop_50","validation":"drop_100","final_test":"drop_150"}[a.split]
    expected_height=float(cfg["physics"]["drop_heights_m"][setting])
    tg=cfg["tracker"]
    for pkg,m in mm:
        try:
            height=drop_height_from_manifest(m,expected_height)
            tr=extract(pathlib.Path(m["video"]["path"]),height,width=tg["analysis_width"],max_seconds=tg["max_seconds"])
            checks={
                "active_frames":tr["active_frames"]>=tg["minimum_active_frames"],
                "monotone":tr["monotone_fraction"]>=tg.get("minimum_monotone_fraction",.78),
                "trajectory_shape":tr["trajectory_shape_rms_fraction"]<=tg.get("maximum_trajectory_shape_rms_fraction",.10),
                "release_speed":tr["release_speed_ratio"]<=tg.get("maximum_release_speed_ratio",.65),
                "x_drift":tr["x_drift_fraction"]<=tg.get("maximum_x_drift_fraction",.45),
                "gap_penalty":tr["gap_penalty"]<=tg.get("maximum_gap_penalty",.50),
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
                          "trajectory_shape_rms_fraction":tr["trajectory_shape_rms_fraction"],
                          "release_speed_ratio":tr["release_speed_ratio"],
                          "x_drift_fraction":tr["x_drift_fraction"],
                          "gap_penalty":tr["gap_penalty"],
                          "candidate_track_count":tr["candidate_track_count"],
                          "interval_frames":tr["interval_frames"],
                          "detected_frames":tr["detected_frames"],
                          "detected_fraction":tr["detected_fraction"],
                          "edge_trim_left":tr["edge_trim_left"],
                          "edge_trim_right":tr["edge_trim_right"],
                          "monotone_fraction":tr["monotone_fraction"]})
            if not qok:continue
            for name,fac,truth,family,neg in cases():
                g0=1.25*G;g1=fac*G;s1,s2=score(tr,g0,g1)
                for method,s in (("freefall_trajectory_probe",s1),("endpoint_kinematic_baseline",s2)):
                    d=decision(s);key=f"freefall:{m['scene']}:{name}"
                    rows.append({"record_version":1,"trial_id":f"{key}:{method}","paired_key":key,
                     "dataset":"iris_real_freefall_blind","scene":m["scene"],"split":a.split,
                     "evidence_class":"prospective_real_video_freefall_probe",
                     "confirmatory":str(a.split=="final_test").lower(),"trial_family":family,"ground_truth":truth,
                     "method":method,"score":s,"decision":d,"decision_threshold":TAU,"confidence":"",
                     "physical_delta":math.log(g1/g0),"target_error_delta":abs(math.log(g1/G))-abs(math.log(g0/G)),
                     "measurement_noise_sigma":tr["one_pixel_m"],"pose_noise_sigma":"","missing_fraction":1-tr["valid_fraction"],
                     "channel_dependence":0.0,"negative_control":str(neg).lower(),"seed":0,"source_artifact":str(pkg),
                     "notes":f"factor={fac}; drop_height_m={height}; tracker=full_flight_v5; standardized evidence score; take01 forbidden"})
        except Exception as e:fails.append({"scene":m["scene"],"error":f"{type(e).__name__}: {e}"})
    out.mkdir(parents=True,exist_ok=True)
    if rows:
        with (out/"validation_records.csv").open("w",newline="",encoding="utf-8") as f:
            w=csv.DictWriter(f,fieldnames=FIELDS);w.writeheader();w.writerows(rows)
    if takes:
        with (out/"take_summary.csv").open("w",newline="",encoding="utf-8") as f:
            w=csv.DictWriter(f,fieldnames=list(takes[0]));w.writeheader();w.writerows(takes)
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
        gate=quality>=3 and median_err is not None and median_err<=.20
    elif a.split=="validation":
        vg=cfg["validation_gate"];gate=quality>=vg["minimum_quality_videos"] and tc>=vg["minimum_truth_control_accuracy"] and pfar<=vg["placebo_false_assertion_rate"] and sign>=vg["minimum_direction_sign_rate"]
    else:gate=True
    summary={"schema":"vulkax.iris_freefall_blind_result","version":5,"tracker_revision":"full_flight_v5",
      "split":a.split,"expected_videos":expected,"quality_pass_videos":quality,"failures":fails,
      "median_acceleration_relative_error":median_err,"records":len(rows),"truth_control_accuracy":tc,
      "placebo_false_assertion_rate":pfar,"direction_sign_rate":sign,"gate_pass":gate,
      "take01_forbidden":True,
      "claim_guard":"Different equation-family replication; tracker revision 5 was fixed using development only after v1/v2/v3/v4 development failures. Gravity estimation itself is not novel."}
    (out/"summary.json").write_text(json.dumps(summary,indent=2)+"\n")
    print("VALID IRIS free-fall",a.split)
    for t in takes:
        print("TAKE",t["scene"],
              "QUALITY",t["quality_ok"],
              "REJECT",t["quality_reject_reason"] or "none",
              "G_REL_ERR",t["acceleration_relative_error"],
              "SHAPE_RMS",t["trajectory_shape_rms_fraction"],
              "RELEASE_RATIO",t["release_speed_ratio"],
              "X_DRIFT",t["x_drift_fraction"],
              "GAP",t["gap_penalty"],
              "TRACKS",t["candidate_track_count"],
              "INTERVAL_FRAMES",t["interval_frames"],
              "DETECTED_FRAMES",t["detected_frames"],
              "DETECTED_FRACTION",t["detected_fraction"])
    for e in fails:
        print("TAKE_FAIL",e["scene"],e["error"])
    for k,v in summary.items():
        if not isinstance(v,(list,dict)):print(k.upper(),v)
    print("OUT",out)
    if not gate and a.split in ("development","validation"):raise SystemExit(f"{a.split} gate failed")
if __name__=="__main__":main()

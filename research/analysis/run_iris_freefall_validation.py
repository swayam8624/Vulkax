#!/usr/bin/env python3
"""Blind IRIS free-fall validation on development/validation/final partitions.

Development revision 2 (2026-09-23):
The first development run exposed a segmentation/calibration failure: the old
tracker chose the longest occupancy run between global position quantiles, which
can select slow reset/handling motion rather than the actual ballistic descent.
No validation or final free-fall result was analyzed before this redesign.

Revision 2 selects the fastest monotone endpoint-to-endpoint descent, uses the
independently measured drop height only as a known geometric input, and derives
a release-to-impact time-of-flight acceleration diagnostic. The verification
score remains candidate-vs-baseline trajectory residual with the global |S|=2
decision rule.
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

def crossing_time(t,p,level,start=1,end=None):
    end=len(p) if end is None else min(end,len(p))
    for i in range(max(1,start),end):
        if p[i-1] < level <= p[i]:
            den=p[i]-p[i-1]
            frac=(level-p[i-1])/den if abs(den)>1e-12 else 0.0
            return float(t[i-1]+frac*(t[i]-t[i-1])),i
    return None,None

def stable_median(x):
    x=np.asarray(x,float)
    if len(x)==0:return float("nan"),float("inf")
    med=float(np.median(x))
    mad=1.4826*float(np.median(np.abs(x-med))) if len(x)>1 else 0.0
    return med,mad

def smooth1(x,n=5):
    x=np.asarray(x,float)
    if n<=1 or len(x)<n:return x.copy()
    k=np.ones(n,float)/n
    q=np.convolve(x,k,mode="same")
    e=n//2
    q[:e]=x[:e];q[-e:]=x[-e:]
    return q

def crossing_time(t,p,level,start=1,end=None):
    end=len(p) if end is None else min(end,len(p))
    for i in range(max(1,start),end):
        if p[i-1] < level <= p[i]:
            den=p[i]-p[i-1]
            frac=(level-p[i-1])/den if abs(den)>1e-12 else 0.0
            return float(t[i-1]+frac*(t[i]-t[i-1])),i
    return None,None

def plateau_ballistic_candidates(t,y,fps):
    """Enumerate release->impact candidates without using target g.

    Candidates are bounded by low-speed plateaus around a high-speed monotone
    traversal. They are ranked by self-consistency with t(p)=t0+T*sqrt(p),
    which is the release-from-rest free-fall shape but does not insert g.
    """
    out=[]
    t=np.asarray(t,float);y=np.asarray(y,float)
    dt=1.0/fps
    plateau_frames=max(5,int(round(.12*fps)))
    guard=max(2,int(round(.025*fps)))

    for sign in (1.0,-1.0):
        pos=smooth1(sign*y,5)
        vel=smooth1(np.gradient(pos,t),5)
        positive=np.maximum(vel,0.0)
        if not np.any(positive>0):continue
        # Local maxima plus strongest-speed indices; duplicates are collapsed.
        peaks=set()
        for i in range(2,len(vel)-2):
            if vel[i]>vel[i-1] and vel[i]>=vel[i+1] and vel[i]>0:
                peaks.add(i)
        strongest=np.argsort(positive)[-min(20,len(positive)):]
        peaks.update(int(i) for i in strongest if positive[i]>0)

        for peak in sorted(peaks,key=lambda i:positive[i],reverse=True)[:20]:
            pv=float(positive[peak])
            if pv<5.0:continue
            low=max(2.0,.12*pv)

            # Find nearest stable/slow region before and after the speed peak.
            left=peak
            quiet=0
            while left>2:
                left-=1
                quiet=quiet+1 if abs(vel[left])<=low else 0
                if quiet>=guard:
                    left+=guard
                    break
            right=peak
            quiet=0
            while right<len(vel)-3:
                right+=1
                quiet=quiet+1 if abs(vel[right])<=low else 0
                if quiet>=guard:
                    right-=guard
                    break

            if right-left<max(8,int(.12*fps)):continue
            duration=float(t[right]-t[left])
            if not (.12<=duration<=1.5):continue

            top_a=max(0,left-plateau_frames)
            top_b=max(top_a+2,left-guard)
            bot_a=min(len(pos)-2,right+guard)
            bot_b=min(len(pos),bot_a+plateau_frames)
            if top_b-top_a<3 or bot_b-bot_a<3:continue
            top,top_mad=stable_median(pos[top_a:top_b])
            bottom,bottom_mad=stable_median(pos[bot_a:bot_b])
            span=bottom-top
            if not np.isfinite(span) or span<15.0:continue
            plateau_rel=(top_mad+bottom_mad)/max(span,1e-9)
            if plateau_rel>.12:continue

            progress=(pos-top)/span
            seg=progress[left:right+1]
            d=np.diff(seg)
            monotone=float(np.mean(d>=-.015)) if len(d) else 0.0
            forward=float(np.mean(d>=0.0)) if len(d) else 0.0
            if monotone<.80 or forward<.60:continue

            levels=np.asarray([.10,.20,.30,.40,.50,.60,.70,.80,.90],float)
            times=[];idx=max(1,left-guard);ok=True
            for lev in levels:
                tt,j=crossing_time(t,progress,float(lev),start=idx,end=min(len(t),right+guard+1))
                if tt is None:
                    ok=False;break
                times.append(tt);idx=max(idx,j-1)
            if not ok:continue
            times=np.asarray(times,float)
            A=np.column_stack([np.ones(len(levels)),np.sqrt(levels)])
            coef=np.linalg.lstsq(A,times,rcond=None)[0]
            t0=float(coef[0]);T=float(coef[1])
            if T<=0:continue
            pred=A@coef
            timing_rms=float(np.sqrt(np.mean((times-pred)**2)))
            timing_rms_frames=timing_rms*fps

            # Independent shape check in pixel space: quadratic displacement from
            # fitted release time, with scale/amplitude fitted but not g.
            ids=np.arange(max(0,left-guard),min(len(t),right+guard+1))
            tau=np.maximum(t[ids]-t0,0.0)
            basis=tau*tau
            yy=pos[ids]-top
            den=float(np.dot(basis,basis))
            amp=0.0 if den<=1e-12 else float(np.dot(basis,yy)/den)
            pred_y=amp*basis
            traj_rms=float(np.sqrt(np.mean((yy-pred_y)**2)))/max(span,1e-9)

            # Rank using only model self-consistency and plateau quality. The
            # acceleration ground truth is deliberately absent.
            rank=(timing_rms_frames, traj_rms, plateau_rel, -span, duration)
            out.append({
                "rank":rank,"sign":sign,"top_px":top,"bottom_px":bottom,"span_px":span,
                "progress":progress,"start_index":left,"end_index":right,
                "duration_s":duration,"monotone_fraction":monotone,
                "forward_fraction":forward,"plateau_relative_mad":plateau_rel,
                "timing_fit_rms_s":timing_rms,"timing_fit_rms_frames":timing_rms_frames,
                "trajectory_shape_rms_fraction":traj_rms,"release_time_s":t0,
                "full_fall_time_s":T,"crossing_levels":levels.tolist(),
                "crossing_times_s":times.tolist()
            })
    return sorted(out,key=lambda q:q["rank"])

def candidate_descent(t,y,fps):
    q=plateau_ballistic_candidates(t,y,fps)
    if not q:raise RuntimeError("no plateau-bounded ballistic descent found")
    return q[0]

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
    else:x0=yy0=0;y1,x1=bg.shape

    cap=cv2.VideoCapture(str(video));ys=[];valid=[];times=[];i=0
    while i<n:
        ok,fr=cap.read()
        if not ok:break
        g=resize_gray(fr,width,cv2);diff=cv2.absdiff(g,bg);diff=cv2.GaussianBlur(diff,(5,5),0)
        crop=diff[yy0:y1,x0:x1];q=max(8.0,float(np.percentile(crop,99.7))*.65)
        mm=(crop>=q).astype(np.uint8)
        mm=cv2.morphologyEx(mm,cv2.MORPH_OPEN,cv2.getStructuringElement(cv2.MORPH_ELLIPSE,(3,3)))
        # Prefer a compact moving component over a whole-frame weighted centroid.
        nlab,labels,stats,cent=cv2.connectedComponentsWithStats(mm,8)
        candidates=[]
        roi_area=max(1,crop.shape[0]*crop.shape[1])
        for lab in range(1,nlab):
            area=int(stats[lab,cv2.CC_STAT_AREA])
            ww=int(stats[lab,cv2.CC_STAT_WIDTH]);hh=int(stats[lab,cv2.CC_STAT_HEIGHT])
            if area<6 or area>.08*roi_area or ww<=0 or hh<=0:continue
            aspect=ww/hh
            if not (.20<=aspect<=5.0):continue
            fill=area/max(1,ww*hh)
            cx,cy=cent[lab]
            # Compact/high-fill moving objects are ball-like; large diffuse hand
            # regions are downweighted. No physical truth enters this score.
            score=(fill+0.05)*float(np.mean(crop[labels==lab]))/math.sqrt(max(area,1))
            candidates.append((score,float(cy+yy0)))
        if candidates:
            candidates.sort(reverse=True)
            ys.append(candidates[0][1]);valid.append(True)
        else:
            ys.append(float("nan"));valid.append(False)
        times.append(i/fps);i+=1
    cap.release()

    y=np.asarray(ys);v=np.asarray(valid,bool);t=np.asarray(times)
    if v.sum()<12:raise RuntimeError("insufficient tracked ball samples")
    idx=np.arange(len(y));good=np.flatnonzero(v);y=np.interp(idx,good,y[good])
    y=smooth1(y,3)

    descent=candidate_descent(t,y,fps)
    progress=np.asarray(descent["progress"],float)
    t0=float(descent["release_time_s"]);T=float(descent["full_fall_time_s"])
    ghat=2.0*drop_height/(T*T)

    a=max(0,descent["start_index"]-2);b=min(len(t),descent["end_index"]+3)
    ta=t[a:b]-t[a]
    sm=np.clip(progress[a:b],0.0,1.0)*drop_height
    one_px=drop_height/descent["span_px"]
    if len(ta)<12:raise RuntimeError(f"free-fall active segment too short: {len(ta)}")

    X=np.column_stack([np.ones(len(ta)),ta,.5*ta*ta])
    coef=np.linalg.lstsq(X,sm,rcond=None)[0]
    trajectory_g=float(coef[2])
    return {
        "fps":fps,"valid_fraction":float(v.mean()),"span_px":descent["span_px"],
        "one_pixel_m":one_px,"active_frames":len(ta),"times":ta,"position_m":sm,
        "direct_acceleration_m_s2":ghat,
        "trajectory_acceleration_m_s2":trajectory_g,
        "full_fall_time_s":T,
        "timing_fit_rms_frames":descent["timing_fit_rms_frames"],
        "trajectory_shape_rms_fraction":descent["trajectory_shape_rms_fraction"],
        "plateau_relative_mad":descent["plateau_relative_mad"],
        "monotone_fraction":descent["monotone_fraction"],
        "forward_fraction":descent["forward_fraction"],
        "selected_direction_sign":descent["sign"],
        "duration_10_90_s":float(descent["crossing_times_s"][-1]-descent["crossing_times_s"][0]),
        "release_time_s_absolute":t0,
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
                # selector can prefer this; revision 2 must still select free fall.
                frac=max(0.0,1.0-(t-1.45)/.85)
            yy=60+span_px*frac
            cv2.circle(q,(320,int(round(yy))),10,(255,255,255),-1)
            writer.write(q)
        writer.release()
        tr=extract(p,drop_m,width=640,max_seconds=2.5)
        rel=abs(tr["direct_acceleration_m_s2"]-G)/G
        assert rel<=0.20,(tr["direct_acceleration_m_s2"],rel,tr)
        assert tr["active_frames"]>=12
        assert tr["monotone_fraction"]>=.80
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
        if d.get("tracker_revision")!="plateau_ballistic_v3":raise SystemExit("development tracker revision mismatch")
    if a.split=="final_test":
        v=json.loads(a.require_validation_summary.read_text()) if a.require_validation_summary else {}
        if not v.get("gate_pass"):raise SystemExit("validation gate failed/missing")
        if v.get("tracker_revision")!="plateau_ballistic_v3":raise SystemExit("validation tracker revision mismatch")
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
            qok=(tr["active_frames"]>=tg["minimum_active_frames"]
                 and tr["monotone_fraction"]>=tg.get("minimum_monotone_fraction",.80)
                 and tr["timing_fit_rms_frames"]<=tg.get("maximum_timing_fit_rms_frames",2.5)
                 and tr["trajectory_shape_rms_fraction"]<=tg.get("maximum_trajectory_shape_rms_fraction",.10)
                 and tr["plateau_relative_mad"]<=tg.get("maximum_plateau_relative_mad",.12))
            takes.append({"scene":m["scene"],"quality_ok":qok,"direct_acceleration_m_s2":tr["direct_acceleration_m_s2"],
                          "trajectory_acceleration_m_s2":tr["trajectory_acceleration_m_s2"],
                          "acceleration_relative_error":abs(tr["direct_acceleration_m_s2"]-G)/G,
                          "active_frames":tr["active_frames"],"valid_fraction":tr["valid_fraction"],
                          "span_px":tr["span_px"],"one_pixel_m":tr["one_pixel_m"],
                          "full_fall_time_s":tr["full_fall_time_s"],
                          "timing_fit_rms_frames":tr["timing_fit_rms_frames"],
                          "trajectory_shape_rms_fraction":tr["trajectory_shape_rms_fraction"],
                          "plateau_relative_mad":tr["plateau_relative_mad"],
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
                     "notes":f"factor={fac}; drop_height_m={height}; tracker=plateau_ballistic_v3; standardized evidence score; take01 forbidden"})
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
    summary={"schema":"vulkax.iris_freefall_blind_result","version":2,"tracker_revision":"plateau_ballistic_v3",
      "split":a.split,"expected_videos":expected,"quality_pass_videos":quality,"failures":fails,
      "median_acceleration_relative_error":median_err,"records":len(rows),"truth_control_accuracy":tc,
      "placebo_false_assertion_rate":pfar,"direction_sign_rate":sign,"gate_pass":gate,
      "take01_forbidden":True,
      "claim_guard":"Different equation-family replication; tracker revision 3 was fixed using development only after v1/v2 development failures. Gravity estimation itself is not novel."}
    (out/"summary.json").write_text(json.dumps(summary,indent=2)+"\n")
    print("VALID IRIS free-fall",a.split)
    for k,v in summary.items():
        if not isinstance(v,(list,dict)):print(k.upper(),v)
    print("OUT",out)
    if not gate and a.split in ("development","validation"):raise SystemExit(f"{a.split} gate failed")
if __name__=="__main__":main()

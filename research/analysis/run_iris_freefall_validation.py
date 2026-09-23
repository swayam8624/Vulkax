#!/usr/bin/env python3
"""Blind IRIS free-fall validation on development/validation/final partitions."""
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

def longest_run(mask):
    best=(0,0);start=None
    for i,v in enumerate(mask):
        if v and start is None:start=i
        if (not v or i==len(mask)-1) and start is not None:
            end=i+1 if v and i==len(mask)-1 else i
            if end-start>best[1]-best[0]:best=(start,end)
            start=None
    return best

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
        pts=np.vstack([c.reshape(-1,2) for c in contours]);x,y,w,h=cv2.boundingRect(pts)
        mx=max(25,int(.25*w));my=max(25,int(.15*h));x0=max(0,x-mx);y0=max(0,y-my)
        x1=min(bg.shape[1],x+w+mx);y1=min(bg.shape[0],y+h+my)
    else:x0=y0=0;y1,x1=bg.shape
    cap=cv2.VideoCapture(str(video));ys=[];valid=[];times=[];i=0
    while i<n:
        ok,fr=cap.read()
        if not ok:break
        g=resize_gray(fr,width,cv2);diff=cv2.absdiff(g,bg);diff=cv2.GaussianBlur(diff,(5,5),0)
        crop=diff[y0:y1,x0:x1];q=max(8.0,float(np.percentile(crop,99.7))*.65)
        mm=(crop>=q).astype(np.uint8);mm=cv2.morphologyEx(mm,cv2.MORPH_OPEN,cv2.getStructuringElement(cv2.MORPH_ELLIPSE,(3,3)))
        yy,xx=np.nonzero(mm)
        if len(xx)>=10:
            ww=np.maximum(crop[yy,xx].astype(float)-q+1,1)
            ys.append(float(np.sum((yy+y0)*ww)/np.sum(ww)));valid.append(True)
        else:ys.append(float("nan"));valid.append(False)
        times.append(i/fps);i+=1
    cap.release()
    y=np.asarray(ys);v=np.asarray(valid,bool);t=np.asarray(times)
    if v.sum()<12:raise RuntimeError("insufficient tracked ball samples")
    idx=np.arange(len(y));good=np.flatnonzero(v);y=np.interp(idx,good,y[good])
    y=np.convolve(y,np.ones(3)/3,mode="same");y[0]=y[1];y[-1]=y[-2]
    trend=np.median(y[int(.7*len(y)):])-np.median(y[:max(2,int(.3*len(y)))])
    sign=1.0 if trend>=0 else -1.0;proj=sign*y
    lo=float(np.percentile(proj,5));hi=float(np.percentile(proj,95));span=hi-lo
    if span<8:raise RuntimeError("tracked vertical span too small")
    norm=(proj-lo)/span
    active=(norm>=.05)&(norm<=.85)
    a,b=longest_run(active)
    if b-a<12:raise RuntimeError(f"free-fall active segment too short: {b-a}")
    ta=t[a:b]-t[a];sm=(proj[a:b]-lo)*(drop_height/span);one_px=drop_height/span
    # direct acceleration estimate with free intercept/velocity
    X=np.column_stack([np.ones(len(ta)),ta,.5*ta*ta]);coef=np.linalg.lstsq(X,sm,rcond=None)[0]
    ghat=float(coef[2])
    return {"fps":fps,"valid_fraction":float(v.mean()),"span_px":span,"one_pixel_m":one_px,
            "active_frames":len(ta),"times":ta,"position_m":sm,"direct_acceleration_m_s2":ghat,
            "roi":[int(x0),int(y0),int(x1-x0),int(y1-y0)]}

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
        for i in range(int(fps*2.0)):
            t=i/fps
            q=np.zeros((h,w,3),dtype=np.uint8)
            y=60+span_px*min(1.0,0.5*G*t*t/drop_m)
            cv2.circle(q,(320,int(round(y))),10,(255,255,255),-1)
            writer.write(q)
        writer.release()
        tr=extract(p,drop_m,width=640,max_seconds=2.0)
        rel=abs(tr["direct_acceleration_m_s2"]-G)/G
        assert rel<=0.30,(tr["direct_acceleration_m_s2"],rel)
        assert tr["active_frames"]>=12
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
    if a.split=="final_test":
        v=json.loads(a.require_validation_summary.read_text()) if a.require_validation_summary else {}
        if not v.get("gate_pass"):raise SystemExit("validation gate failed/missing")
        if not a.lock:raise SystemExit("final_test requires lock")
        subprocess.run(["python3","research/analysis/freeze_iris_freefall_final_test.py","--check",str(a.lock)],check=True)
    out=a.out or pathlib.Path(f"build/publication-validation/iris-freefall-{a.split}")
    mm=manifests(a.adapted_root,a.split,cfg);expected={"development":4,"validation":4,"final_test":9}[a.split]
    if len(mm)!=expected:raise SystemExit(f"expected {expected} frozen scenes, got {len(mm)}")
    rows=[];takes=[];fails=[]
    height=cfg["physics"]["drop_heights_m"][{"development":"drop_50","validation":"drop_100","final_test":"drop_150"}[a.split]]
    for pkg,m in mm:
        try:
            tr=extract(pathlib.Path(m["video"]["path"]),height,width=cfg["tracker"]["analysis_width"],max_seconds=cfg["tracker"]["max_seconds"])
            qok=tr["active_frames"]>=cfg["tracker"]["minimum_active_frames"]
            takes.append({"scene":m["scene"],"quality_ok":qok,"direct_acceleration_m_s2":tr["direct_acceleration_m_s2"],
                          "acceleration_relative_error":abs(tr["direct_acceleration_m_s2"]-G)/G,
                          "active_frames":tr["active_frames"],"valid_fraction":tr["valid_fraction"],
                          "span_px":tr["span_px"],"one_pixel_m":tr["one_pixel_m"]})
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
                     "notes":f"factor={fac}; drop_height_m={height}; standardized evidence score; take01 forbidden"})
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
    summary={"schema":"vulkax.iris_freefall_blind_result","version":1,"split":a.split,"expected_videos":expected,
      "quality_pass_videos":quality,"failures":fails,"median_acceleration_relative_error":median_err,
      "records":len(rows),"truth_control_accuracy":tc,"placebo_false_assertion_rate":pfar,
      "direction_sign_rate":sign,"gate_pass":gate,
      "take01_forbidden":True,"claim_guard":"Different equation-family replication; gravity estimation itself is not novel."}
    (out/"summary.json").write_text(json.dumps(summary,indent=2)+"\n")
    print("VALID IRIS free-fall",a.split)
    for k,v in summary.items():
        if not isinstance(v,(list,dict)):print(k.upper(),v)
    print("OUT",out)
    if not gate and a.split in ("development","validation"):raise SystemExit(f"{a.split} gate failed")
if __name__=="__main__":main()

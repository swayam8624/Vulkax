#!/usr/bin/env python3
"""Post-final corruption robustness for the locked IRIS pendulum method.

Creates temporary corrupted videos, runs the unchanged frozen tracker/scorer, and
reports decision degradation. No corrupted run replaces the locked final result.
"""
from __future__ import annotations
import argparse,csv,json,math,pathlib,tempfile,sys
import numpy as np
ROOT=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/"research"/"analysis"))
from run_iris_pendulum_validation import extract_motion_signal,score_candidate,candidate_cases,decision  # noqa:E402

def read_csv(p):
    with p.open(newline="",encoding="utf-8") as f:return list(csv.DictReader(f))
def safe(s):return s.replace("/","__").replace(" ","_")

def corrupt_video(src,dst,kind,value,seed):
    import cv2
    rng=np.random.default_rng(seed)
    cap=cv2.VideoCapture(str(src))
    if not cap.isOpened():raise RuntimeError(f"cannot open {src}")
    fps=float(cap.get(cv2.CAP_PROP_FPS));w=int(cap.get(cv2.CAP_PROP_FRAME_WIDTH));h=int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    step=1
    outfps=fps
    if kind=="fps":
        step=max(1,int(round(fps/float(value))));outfps=fps/step
    writer=cv2.VideoWriter(str(dst),cv2.VideoWriter_fourcc(*"MJPG"),outfps,(w,h))
    if not writer.isOpened():raise RuntimeError("video writer unavailable")
    i=0;last=None
    while True:
        ok,fr=cap.read()
        if not ok:break
        if kind=="fps" and i%step!=0:
            i+=1;continue
        q=fr.copy()
        if kind=="noise":
            q=np.clip(q.astype(np.float32)+rng.normal(0,float(value),q.shape),0,255).astype(np.uint8)
        elif kind=="blur":
            k=int(value);q=cv2.GaussianBlur(q,(k,k),0)
        elif kind=="frame_drop":
            period=max(2,int(round(1/max(1e-6,float(value)))))
            if i%period==0 and last is not None:q=last.copy()
        elif kind=="occlusion":
            frac=float(value);side=math.sqrt(frac)
            ww=max(1,int(w*side));hh=max(1,int(h*side));x=(w-ww)//2;y=(h-hh)//2
            q[y:y+hh,x:x+ww]=0
        elif kind=="crop":
            frac=float(value);dx=int(w*frac);dy=int(h*frac)
            x0=min(dx,w//4);y0=min(dy,h//4)
            crop=q[y0:h-y0,x0:w-x0]
            if crop.size:q=cv2.resize(crop,(w,h),interpolation=cv2.INTER_LINEAR)
        writer.write(q);last=q;i+=1
    cap.release();writer.release()

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--campaign",type=pathlib.Path,default=pathlib.Path("build/publication-validation/iris-pendulum-final-test"))
    ap.add_argument("--out",type=pathlib.Path,default=pathlib.Path("build/publication-validation/iris-pendulum-postfinal-robustness"))
    ap.add_argument("--profile",choices=("quick","full"),default="full")
    ap.add_argument("--self-test",action="store_true")
    a=ap.parse_args()
    if a.self_test:
        assert safe("pendulum/a b/01")=="pendulum__a_b__01"
        print("VALID IRIS robustness self-test");return
    takes=read_csv(a.campaign/"take_summary.csv")
    conditions=[("clean",0)]
    conditions += [("noise",x) for x in ([15] if a.profile=="quick" else [5,15,30])]
    conditions += [("blur",x) for x in ([7] if a.profile=="quick" else [3,7,15])]
    conditions += [("frame_drop",x) for x in ([.25] if a.profile=="quick" else [.10,.25,.50])]
    conditions += [("fps",x) for x in ([30] if a.profile=="quick" else [30,15])]
    conditions += [("occlusion",x) for x in ([.15] if a.profile=="quick" else [.05,.15,.30])]
    conditions += [("crop",x) for x in ([.05] if a.profile=="quick" else [.02,.05,.10])]
    rows=[];fail=[]
    for ci,(kind,val) in enumerate(conditions):
        for ti,take in enumerate(takes):
            scene=take["scene"];summary=json.loads((a.campaign/safe(scene)/"tracking_summary.json").read_text())
            src=pathlib.Path(summary["video"]);angle=float(take["angle_deg"]);Ltrue=float(take["true_length_m"])
            try:
                if kind=="clean":
                    tr=extract_motion_signal(src,width=640,max_seconds=14.0)
                else:
                    with tempfile.TemporaryDirectory() as td:
                        dst=pathlib.Path(td)/"corrupt.avi"
                        corrupt_video(src,dst,kind,val,20260923+ci*100+ti)
                        tr=extract_motion_signal(dst,width=640,max_seconds=14.0)
                for case in candidate_cases():
                    L0=1.25*Ltrue;L1=float(case["factor"])*Ltrue
                    z,_,_,_=score_candidate(list(tr["cycle_periods_s"]),float(tr["fps"]),angle,L0,L1,finite_amplitude=True)
                    d=decision(z)
                    rows.append({"condition":kind,"value":val,"scene":scene,"truth":case["truth"],
                                 "case":case["name"],"score":z,"decision":d,"correct":str(d==case["truth"]).lower(),
                                 "valid_fraction":tr["valid_fraction"],"cycle_count":len(tr["cycle_periods_s"])})
            except Exception as e:
                fail.append({"condition":kind,"value":val,"scene":scene,"error":f"{type(e).__name__}: {e}"})
    a.out.mkdir(parents=True,exist_ok=True)
    if rows:
        with (a.out/"records.csv").open("w",newline="",encoding="utf-8") as f:
            w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
    if fail:
        with (a.out/"failures.csv").open("w",newline="",encoding="utf-8") as f:
            w=csv.DictWriter(f,fieldnames=list(fail[0]));w.writeheader();w.writerows(fail)
    summaries=[]
    for kind,val in conditions:
        q=[r for r in rows if r["condition"]==kind and str(r["value"])==str(val)]
        if not q:continue
        summaries.append({"condition":kind,"value":val,"records":len(q),
          "strict_accuracy":sum(r["correct"]=="true" for r in q)/len(q),
          "support_accuracy":sum(r["correct"]=="true" for r in q if r["truth"]=="support")/max(1,sum(r["truth"]=="support" for r in q)),
          "veto_accuracy":sum(r["correct"]=="true" for r in q if r["truth"]=="veto")/max(1,sum(r["truth"]=="veto" for r in q)),
          "placebo_accuracy":sum(r["correct"]=="true" for r in q if r["truth"]=="unresolved")/max(1,sum(r["truth"]=="unresolved" for r in q)),
          "failed_videos":sum(1 for x in fail if x["condition"]==kind and str(x["value"])==str(val))})
    with (a.out/"summary.csv").open("w",newline="",encoding="utf-8") as f:
        w=csv.DictWriter(f,fieldnames=list(summaries[0]));w.writeheader();w.writerows(summaries)
    report={"schema":"vulkax.iris_postfinal_robustness","version":1,"profile":a.profile,
            "physical_videos":len(takes),"conditions":summaries,"failures":len(fail),
            "claim_guard":"Post-final corruption study. Never replace locked final numbers with corrupted or repaired runs."}
    (a.out/"summary.json").write_text(json.dumps(report,indent=2)+"\n")
    print("VALID IRIS post-final robustness")
    for r in summaries:print(r)
    print("FAILURES",len(fail));print("OUT",a.out)
if __name__=="__main__":main()

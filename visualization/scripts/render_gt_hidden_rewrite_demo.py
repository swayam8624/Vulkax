#!/usr/bin/env python3
"""Render an evidence-driven IRIS proposal->verification storyboard and MP4."""
from __future__ import annotations
import argparse,csv,json,pathlib

def rows(p):
 with p.open(newline="",encoding="utf-8") as f:return list(csv.DictReader(f))
def safe(s):return s.replace("/","__").replace(" ","_")
def main():
 ap=argparse.ArgumentParser()
 ap.add_argument("--campaign",type=pathlib.Path,default=pathlib.Path("build/publication-validation/iris-pendulum-final-test"))
 ap.add_argument("--proposals",type=pathlib.Path,default=pathlib.Path("build/publication-validation/iris-pendulum-postfinal-proposals/evaluated_proposals.csv"))
 ap.add_argument("--out",type=pathlib.Path,default=pathlib.Path("build/visualization/reviewer-hardening"))
 ap.add_argument("--no-video",action="store_true")
 a=ap.parse_args()
 try:import cv2
 except ImportError as e:raise SystemExit("opencv-python-headless required") from e
 pp=rows(a.proposals)
 cand=[r for r in pp if r["ordinary_candidate_improves"]=="true" and r["ground_truth"]=="veto"]
 if not cand:cand=[r for r in pp if r["ordinary_candidate_improves"]=="true"]
 if not cand:
  cand=pp
  if not cand:raise SystemExit("no blind proposals available for demo")
 r=cand[0];scene=r["scene"];td=a.campaign/safe(scene)
 meta=json.loads((td/"tracking_summary.json").read_text());video=pathlib.Path(meta["video"])
 motion=rows(td/"motion_signal.csv")
 cap=cv2.VideoCapture(str(video))
 if not cap.isOpened():raise RuntimeError(video)
 fps=float(cap.get(cv2.CAP_PROP_FPS));n=int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
 def frame_at(frac):
  cap.set(cv2.CAP_PROP_POS_FRAMES,int(frac*(n-1)));ok,fr=cap.read()
  if not ok:raise RuntimeError("frame read failed")
  h,w=fr.shape[:2];scale=640/w
  return cv2.resize(fr,(640,max(1,int(h*scale))),interpolation=cv2.INTER_AREA)
 panels=[]
 stages=[(.20,"01 OBSERVE","baseline from first 20%"),(.45,"02 REPAIR","candidate from first 40%; ordinary holdout checks improvement"),(.82,"03 VERIFY","disjoint final 40% decides SUPPORT / VETO / UNRESOLVED")]
 for frac,title,sub in stages:
  fr=frame_at(frac);h,w=fr.shape[:2]
  # motion trace is already in 640-wide coordinates
  upto=min(len(motion),max(2,int(frac*len(motion))))
  pts=[]
  for q in motion[:upto]:
   try:pts.append((int(float(q["motion_x_px"])),int(float(q["motion_y_px"]))))
   except Exception:pass
  for p0,p1 in zip(pts,pts[1:]):cv2.line(fr,p0,p1,(0,255,255),2,cv2.LINE_AA)
  cv2.rectangle(fr,(0,0),(w,76),(10,15,25),-1)
  cv2.putText(fr,title,(18,30),cv2.FONT_HERSHEY_SIMPLEX,.75,(255,255,255),2,cv2.LINE_AA)
  cv2.putText(fr,sub[:78],(18,59),cv2.FONT_HERSHEY_SIMPLEX,.42,(210,220,235),1,cv2.LINE_AA)
  panels.append(fr)
 cap.release()
 # normalize heights
 H=min(p.shape[0] for p in panels);panels=[cv2.resize(p,(640,H)) for p in panels]
 sheet=cv2.hconcat(panels);band=180
 canvas=cv2.copyMakeBorder(sheet,0,band,0,0,cv2.BORDER_CONSTANT,value=(248,250,252))
 y=H+30
 lines=[
  f"scene: {scene}",
  f"blind baseline L={float(r['baseline_length_m']):.4f} m  -> candidate L={float(r['candidate_length_m']):.4f} m",
  f"ordinary residual: {float(r['ordinary_baseline_error_s']):.5f} s -> {float(r['ordinary_candidate_error_s']):.5f} s  (candidate improves={r['ordinary_candidate_improves']})",
  f"Reality Probe standardized evidence score={float(r['standardized_evidence_score']):.3f}  decision={r['decision'].upper()}",
  f"truth revealed only after proposal SHA-256 closure: {r['ground_truth'].upper()}  |  decision correct={r['decision_correct']}",
 ]
 for line in lines:
  cv2.putText(canvas,line,(24,y),cv2.FONT_HERSHEY_SIMPLEX,.55,(25,30,40),1,cv2.LINE_AA);y+=30
 a.out.mkdir(parents=True,exist_ok=True)
 cv2.imwrite(str(a.out/"fig_gt_hidden_rewrite_storyboard.png"),canvas)
 if not a.no_video:
  cap=cv2.VideoCapture(str(video));h0=int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT));w0=int(cap.get(cv2.CAP_PROP_FRAME_WIDTH));scale=640/w0;hh=max(1,int(h0*scale))
  writer=cv2.VideoWriter(str(a.out/"gt_hidden_rewrite_gate.mp4"),cv2.VideoWriter_fourcc(*"mp4v"),fps,(640,hh))
  i=0
  while True:
   ok,fr=cap.read()
   if not ok:break
   fr=cv2.resize(fr,(640,hh),interpolation=cv2.INTER_AREA);u=i/max(1,n-1)
   stage="OBSERVE" if u<.20 else ("PROPOSE / ORDINARY HOLDOUT" if u<.60 else "VERIFY ON DISJOINT FINAL 40%")
   cv2.rectangle(fr,(0,0),(640,62),(10,15,25),-1)
   cv2.putText(fr,stage,(16,27),cv2.FONT_HERSHEY_SIMPLEX,.65,(255,255,255),2,cv2.LINE_AA)
   if u>=.60:cv2.putText(fr,f"score {float(r['standardized_evidence_score']):.2f} -> {r['decision'].upper()}",(16,52),cv2.FONT_HERSHEY_SIMPLEX,.48,(0,230,255),1,cv2.LINE_AA)
   writer.write(fr);i+=1
  cap.release();writer.release()
 manifest={"schema":"vulkax.gt_hidden_rewrite_visual","version":1,"scene":scene,
  "blind_proposal_sha256":r["blind_proposal_sha256"],"truth":r["ground_truth"],"decision":r["decision"],
  "storyboard":"fig_gt_hidden_rewrite_storyboard.png","video":None if a.no_video else "gt_hidden_rewrite_gate.mp4",
  "claim_guard":"Measured IRIS video with deterministic overlays. Proposal was generated before truth join; visualization does not create evidence."}
 (a.out/"gt_hidden_rewrite_visual.json").write_text(json.dumps(manifest,indent=2)+"\n")
 print("VALID GT-hidden rewrite visual",a.out)
if __name__=="__main__":main()

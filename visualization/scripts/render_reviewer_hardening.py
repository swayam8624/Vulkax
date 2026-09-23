#!/usr/bin/env python3
"""Render deterministic reviewer-hardening figures from post-final evidence."""
from __future__ import annotations
import argparse,csv,html,json,math,pathlib,statistics

ROOT=pathlib.Path(__file__).resolve().parents[2]
INK="#111827";MUTED="#64748b";GRID="#d1d5db";BLUE="#2563eb";ORANGE="#ea580c";GREEN="#047857";RED="#b91c1c";PANEL="#f8fafc"

def esc(x):return html.escape(str(x),quote=True)
def txt(x,y,s,size=22,weight=400,anchor="start",fill=INK):
 return f'<text x="{x}" y="{y}" font-family="Inter,Arial,sans-serif" font-size="{size}" font-weight="{weight}" text-anchor="{anchor}" fill="{fill}">{esc(s)}</text>'
def rect(x,y,w,h,fill="none",stroke="none",rx=0,width=1):
 return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{width}"/>'
def line(x1,y1,x2,y2,stroke=INK,width=2,dash=None):
 d=f' stroke-dasharray="{dash}"' if dash else ""
 return f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{stroke}" stroke-width="{width}"{d}/>'
def circle(x,y,r,fill=BLUE,stroke="none",width=1):
 return f'<circle cx="{x}" cy="{y}" r="{r}" fill="{fill}" stroke="{stroke}" stroke-width="{width}"/>'
def svg(w,h,body,title):
 return "\n".join([f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">',f"<title>{esc(title)}</title>",rect(0,0,w,h,"white"),*body,"</svg>",""])
def load_json(p):return json.loads(p.read_text())
def read_csv(p):
 with p.open(newline="",encoding="utf-8") as f:return list(csv.DictReader(f))
def write(out,name,s):out.mkdir(parents=True,exist_ok=True);(out/name).write_text(s,encoding="utf-8")

def figure_locked_vs_strong(build):
 final=load_json(ROOT/"research/results/IRIS_PENDULUM_FINAL_TEST_RESULT_2026-09-23.json")
 methods=[("Locked finite-amplitude",final["primary"]["strict_accuracy"],BLUE),
          ("Locked small-angle",final["baseline"]["strict_accuracy"],RED)]
 p=build/"publication-validation/iris-pendulum-postfinal-baselines/summary.json"
 if p.is_file():
  s=load_json(p)
  for name,d in s["baselines"].items():
   methods.append((name.replace("_"," "),d["strict_accuracy"],ORANGE if "damped" in name else GREEN))
 W,H=1500,760;body=[txt(70,65,"Locked result vs stronger post-final baselines",38,800),txt(70,105,"Post-final comparators never replace the frozen confirmation",20,400,fill=MUTED)]
 left,top,pw,ph=160,160,1240,460
 for tick in [0,.25,.5,.75,1]:
  y=top+ph*(1-tick);body+=[line(left,y,left+pw,y,GRID,1),txt(left-18,y+6,f"{tick:.2f}",16,400,"end",MUTED)]
 gap=pw/(len(methods)+1);bw=min(180,gap*.62)
 for i,(name,val,color) in enumerate(methods,1):
  x=left+gap*i;y=top+ph*(1-val)
  body += [rect(x-bw/2,y,bw,top+ph-y,color,rx=8),txt(x,y-14,f"{val:.3f}",20,700,"middle",color),
           txt(x,top+ph+34,name,16,600,"middle",INK)]
 body += [txt(35,top+ph/2,"strict three-way accuracy",18,600,"middle",INK)]
 return svg(W,H,body,"IRIS locked and strong baseline comparison")

def figure_cluster(build):
 p=build/"publication-validation/iris-pendulum-final-test/clustered_statistics/per_video.csv"
 if not p.is_file():return None
 rr=read_csv(p);W,H=1500,760;body=[txt(70,65,"Physical experimental unit = video, not record row",38,800),txt(70,105,"Each line is one of the ten locked final videos",20,fill=MUTED)]
 left,top,pw,ph=150,155,1240,470
 for tick in [0,.25,.5,.75,1]:
  y=top+ph*(1-tick);body+=[line(left,y,left+pw,y,GRID,1),txt(left-15,y+6,f"{tick:.2f}",16,anchor="end",fill=MUTED)]
 gap=pw/(len(rr)+1)
 for i,r in enumerate(rr,1):
  x=left+gap*i;a=float(r["primary_accuracy"]);b=float(r["baseline_accuracy"]);ya=top+ph*(1-a);yb=top+ph*(1-b)
  body += [line(x,yb,x,ya,INK,3),circle(x,ya,7,BLUE),circle(x,yb,7,RED),txt(x,top+ph+28,str(i),15,600,"middle",MUTED)]
 body += [txt(1220,90,"finite-amplitude",17,700,fill=BLUE),txt(1370,90,"small-angle",17,700,fill=RED)]
 return svg(W,H,body,"IRIS per-video clustered accuracy")

def figure_robustness(build):
 p=build/"publication-validation/iris-pendulum-postfinal-robustness/summary.csv"
 if not p.is_file():return None
 rr=read_csv(p);W,H=1700,900;body=[txt(70,65,"Post-final robustness envelope",38,800),txt(70,105,"Every point is a stress condition; failures remain visible",20,fill=MUTED)]
 left,top,pw,ph=160,170,1450,520
 for tick in [0,.25,.5,.75,1]:
  y=top+ph*(1-tick);body+=[line(left,y,left+pw,y,GRID,1),txt(left-18,y+6,f"{tick:.2f}",16,anchor="end",fill=MUTED)]
 gap=pw/(len(rr)+1)
 for i,r in enumerate(rr,1):
  x=left+gap*i;v=float(r["strict_accuracy"]);y=top+ph*(1-v);color=GREEN if v>=.75 else (ORANGE if v>=.5 else RED)
  body += [circle(x,y,8,color),line(x,top+ph,x,y,color,2),txt(x,top+ph+27,f'{r["condition"]}\n{r["value"]}',12,600,"middle",MUTED)]
 body += [line(left,top+ph*(1-.8181818182),left+pw,top+ph*(1-.8181818182),BLUE,2,"9 7"),
          txt(left+pw-5,top+ph*(1-.8181818182)-8,"locked clean accuracy",16,700,"end",BLUE)]
 return svg(W,H,body,"IRIS robustness envelope")

def figure_proposal_gate(build):
 p=build/"publication-validation/iris-pendulum-postfinal-proposals/summary.json"
 if not p.is_file():return None
 s=load_json(p);W,H=1500,700;body=[txt(70,65,"GT-hidden proposal → verification gate",38,800),txt(70,105,"Ground truth is joined only after the blind proposal artifact is hashed",20,fill=MUTED)]
 vals=[("ordinary-improving",s["ordinary_improving_proposals"],BLUE),("physically better",s["ordinary_improving_physically_better"],GREEN),("deceptive",s["ordinary_improving_physically_worse_deceptive"],RED),("verifier correct",s["verifier_correct_on_ordinary_improving"],ORANGE)]
 mx=max(1,max(v for _,v,_ in vals));left=110
 for i,(name,v,color) in enumerate(vals):
  y=175+i*105;w=1100*v/mx;body += [txt(left,y+28,name,20,650),rect(390,y,w,52,color,rx=10),txt(410+w,y+34,v,24,800,fill=color)]
 body += [txt(750,635,f'blind proposal SHA-256: {s["blind_proposal_sha256"][:20]}…',17,500,"middle",MUTED)]
 return svg(W,H,body,"GT hidden proposal gate")

def main():
 ap=argparse.ArgumentParser();ap.add_argument("--build-root",type=pathlib.Path,default=ROOT/"build");ap.add_argument("--out",type=pathlib.Path,default=ROOT/"build/visualization/reviewer-hardening");a=ap.parse_args()
 figs={"fig_postfinal_strong_baselines.svg":figure_locked_vs_strong(a.build_root),
       "fig_postfinal_clustered_videos.svg":figure_cluster(a.build_root),
       "fig_postfinal_robustness.svg":figure_robustness(a.build_root),
       "fig_gt_hidden_proposal_gate.svg":figure_proposal_gate(a.build_root)}
 made=[]
 for n,s in figs.items():
  if s is not None:write(a.out,n,s);made.append(n);print(a.out/n)
 (a.out/"manifest.json").write_text(json.dumps({"schema":"vulkax.reviewer_hardening_visuals","version":1,"figures":made,
  "claim_guard":"Deterministic evidence visualizations only; missing experiment outputs produce no fabricated figure."},indent=2)+"\n")
if __name__=="__main__":main()

#!/usr/bin/env python3
"""Render magnified candidate-minus-truth residuals from actual VULKAX solver states.

The magnification is purely a display transform and is explicitly printed in the
figure. All residual directions and magnitudes originate from replayed solver state.
"""
from __future__ import annotations

import argparse
import csv
import html
import json
import math
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERO = ROOT / "visualization/data/hero_case_ofc_2026-09-21.json"

INK="#111827"; MUTED="#5b6472"; GRID="#d8dee8"
TRUTH="#111827"; BASE="#2563eb"; REPAIR="#ea580c"; PANEL="#f8fafc"
MAG=700.0


def esc(v): return html.escape(str(v), quote=True)


def text(x,y,s,size=20,fill=INK,weight=400,anchor="start"):
    return (
        f'<text x="{x:.1f}" y="{y:.1f}" font-family="Inter,Arial,sans-serif" '
        f'font-size="{size}" font-weight="{weight}" fill="{fill}" '
        f'text-anchor="{anchor}">{esc(s)}</text>'
    )


def rect(x,y,w,h,fill="none",stroke="none",rx=0,width=1):
    return (
        f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" '
        f'rx="{rx:.1f}" fill="{fill}" stroke="{stroke}" stroke-width="{width}"/>'
    )


def circle(x,y,r,fill,opacity=1.0):
    return f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{r:.1f}" fill="{fill}" fill-opacity="{opacity}"/>'


def line(x1,y1,x2,y2,stroke,width=2,opacity=1.0):
    return (
        f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" '
        f'stroke="{stroke}" stroke-width="{width}" opacity="{opacity}" stroke-linecap="round"/>'
    )


def project(p):
    x,y,z=p
    return (x - 0.62*y, z + 0.34*y)


def load_states(path):
    states=defaultdict(dict)
    max_frame=0
    with path.open(newline="",encoding="utf-8") as f:
        for row in csv.DictReader(f):
            key=(row["direction"],row["model"],int(row["frame"]))
            pid=int(row["particle_id"])
            states[key][pid]={
                "rest":(float(row["rest_x"]),float(row["rest_y"]),float(row["rest_z"])),
                "pos":(float(row["x"]),float(row["y"]),float(row["z"])),
                "top":row["is_top"]=="1",
            }
            max_frame=max(max_frame,int(row["frame"]))
    return states,max_frame


def add(a,b): return tuple(a[i]+b[i] for i in range(3))
def sub(a,b): return tuple(a[i]-b[i] for i in range(3))
def mul(a,s): return tuple(a[i]*s for i in range(3))


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--trajectory-dir",type=Path,required=True)
    ap.add_argument("--out",type=Path,required=True)
    ap.add_argument("--magnification",type=float,default=MAG)
    args=ap.parse_args()
    if not math.isfinite(args.magnification) or args.magnification<=0:
        raise SystemExit("magnification must be finite and positive")

    manifest=json.loads((args.trajectory_dir/"trajectory_manifest.json").read_text(encoding="utf-8"))
    hero=json.loads(HERO.read_text(encoding="utf-8"))
    if manifest["hero_truth_id"]!=hero["row"]["truth_id"]:
        raise SystemExit("trajectory/hero mismatch")

    states,last_frame=load_states(args.trajectory_dir/"particle_trajectories.csv")
    summaries={}
    with (args.trajectory_dir/"direction_summary.csv").open(newline="",encoding="utf-8") as f:
        for row in csv.DictReader(f): summaries[row["direction"]]=row

    directions=["px","nx","py","pz"]
    force_labels={"px":"+x","nx":"−x","py":"+y","pz":"+z"}

    # Establish common display bounds from truth state plus magnified residual endpoints.
    display_points=[]
    for d in directions:
        truth=states[(d,"truth",last_frame)]
        base=states[(d,"baseline_apic",last_frame)]
        repair=states[(d,"repair_pic",last_frame)]
        for pid,t in truth.items():
            tp=t["pos"]
            display_points += [
                tp,
                add(tp,mul(sub(base[pid]["pos"],tp),args.magnification)),
                add(tp,mul(sub(repair[pid]["pos"],tp),args.magnification)),
            ]
    projected=[project(p) for p in display_points]
    umin=min(x for x,_ in projected); umax=max(x for x,_ in projected)
    vmin=min(y for _,y in projected); vmax=max(y for _,y in projected)
    span=max(umax-umin,vmax-vmin)
    cu=.5*(umin+umax); cv=.5*(vmin+vmax)

    W,H=1800,1080
    body=[
        rect(0,0,W,H,"#ffffff"),
        text(70,68,"VULKAX — mechanism X-ray from raw solver state",40,weight=800),
        text(70,108,
             f"Candidate − truth residual vectors ×{args.magnification:g} for visibility • exact frozen APIC→PIC hero replay",
             22,fill=MUTED),
        text(70,140,
             "Black points are truth final particle positions. Vector directions/magnitudes are measured from replay; only display scale is magnified.",
             18,fill=MUTED),
    ]

    panel_w,panel_h=805,375
    origins=[(70,190),(925,190),(70,600),(925,600)]
    for d,(px,py) in zip(directions,origins):
        body += [
            rect(px,py,panel_w,panel_h,PANEL,GRID,22,2),
            text(px+30,py+42,f"40 N / top particle  {force_labels[d]}",22,weight=800),
        ]
        plot_l,plot_t=px+40,py+72
        plot_w,plot_h=500,255
        scale=min(plot_w,plot_h)/span*.84
        cx=plot_l+plot_w*.5; cy=plot_t+plot_h*.5
        def screen(p):
            u,v=project(p)
            return cx+(u-cu)*scale, cy-(v-cv)*scale

        truth=states[(d,"truth",last_frame)]
        base=states[(d,"baseline_apic",last_frame)]
        repair=states[(d,"repair_pic",last_frame)]

        # Draw repair first, baseline second, truth on top.
        for pid,t in truth.items():
            sx,sy=screen(t["pos"])
            rp=add(t["pos"],mul(sub(repair[pid]["pos"],t["pos"]),args.magnification))
            ex,ey=screen(rp)
            body.append(line(sx,sy,ex,ey,REPAIR,2.0,.55))
            body.append(circle(ex,ey,3.2,REPAIR,.75))
        for pid,t in truth.items():
            sx,sy=screen(t["pos"])
            bp=add(t["pos"],mul(sub(base[pid]["pos"],t["pos"]),args.magnification))
            ex,ey=screen(bp)
            body.append(line(sx,sy,ex,ey,BASE,1.8,.65))
            body.append(circle(ex,ey,2.8,BASE,.85))
        for pid,t in truth.items():
            sx,sy=screen(t["pos"])
            body.append(circle(sx,sy,2.8,TRUTH,.90))

        s=summaries[d]
        eb=float(s["baseline_raw_rms_to_truth_m"])
        er=float(s["repair_raw_rms_to_truth_m"])
        ratio=er/eb if eb>0 else math.inf
        tx=px+565
        body += [
            text(tx,py+102,"raw compliance error",18,fill=MUTED),
            text(tx,py+139,f"baseline  {eb:.3e} m",19,fill=BASE,weight=700),
            text(tx,py+171,f"repair     {er:.3e} m",19,fill=REPAIR,weight=700),
            text(tx,py+211,f"repair / baseline  {ratio:.2f}×",21,fill=REPAIR,weight=800),
            text(tx,py+257,"X-ray legend",18,fill=MUTED),
            line(tx,py+283,tx+35,py+283,BASE,3,.9),
            text(tx+45,py+290,"baseline − truth",17),
            line(tx,py+313,tx+35,py+313,REPAIR,3,.9),
            text(tx+45,py+320,"repair − truth",17),
        ]

    body += [
        rect(175,1000,1450,54,"#fff7ed","#fed7aa",15,1),
        text(900,1034,
             f"DISPLAY MAGNIFICATION ×{args.magnification:g}; numerical values are unscaled raw solver measurements.",
             18,fill="#9a3412",weight=800,anchor="middle"),
    ]
    svg="\n".join([
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">',
        "<title>VULKAX mechanism X-ray from replayed solver state</title>",
        *body,"</svg>",""
    ])
    args.out.mkdir(parents=True,exist_ok=True)
    out=args.out/"fig_solver_state_residual_xray.svg"
    out.write_text(svg,encoding="utf-8")
    print(out)


if __name__=="__main__":
    main()

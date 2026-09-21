#!/usr/bin/env python3
from __future__ import annotations
import argparse,csv,math
from pathlib import Path
from vector_composition_common import write_svg

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--proposals",type=Path,required=True)
    p.add_argument("--out",type=Path,required=True)
    a=p.parse_args()
    rows=list(csv.DictReader(a.proposals.open()))
    if len(rows)!=36:
        raise SystemExit(f"expected 36 frozen proposals, got {len(rows)}")

    W,H=2600,1700
    left,right,top,bottom=250,2460,300,1450
    xmin,xmax=0.0,45.0
    ymin,ymax=-65.0,50.0
    def sx(x): return left+(x-xmin)/(xmax-xmin)*(right-left)
    def sy(y): return bottom-(y-ymin)/(ymax-ymin)*(bottom-top)

    pts=[]
    for r in rows:
        x=float(r["ordinary_improvement_pct"])
        y=float(r["target_change_pct"])
        z=abs(float(r["force_progress_z"]))
        rad=8+8*min(z/1.35,1.0)
        deceptive=r["label"]=="deceptive"
        hero=(r["truth_id"]=="5" and r["baseline"]=="apic" and r["repair"]=="pic")
        fill="#d95c3d" if deceptive else "#2a8ba8"
        stroke="#16191d" if not hero else "#f2a93b"
        sw=1.5 if not hero else 7
        shape=(f'<rect x="{sx(x)-rad:.2f}" y="{sy(y)-rad:.2f}" width="{2*rad:.2f}" height="{2*rad:.2f}" '
               f'transform="rotate(45 {sx(x):.2f} {sy(y):.2f})" rx="2" fill="{fill}" fill-opacity=".82" stroke="{stroke}" stroke-width="{sw}"/>'
               if deceptive else
               f'<circle cx="{sx(x):.2f}" cy="{sy(y):.2f}" r="{rad:.2f}" fill="{fill}" fill-opacity=".72" stroke="{stroke}" stroke-width="{sw}"/>')
        pts.append(shape)

    x0=sx(0); y0=sy(0)
    grid=[]
    for xt in range(0,46,5):
        xx=sx(xt)
        grid.append(f'<line x1="{xx:.1f}" y1="{top}" x2="{xx:.1f}" y2="{bottom}" stroke="#dfe4e7" stroke-width="1"/>')
        grid.append(f'<text x="{xx:.1f}" y="{bottom+42}" text-anchor="middle" fill="#65717a" font-size="18">{xt}</text>')
    for yt in range(-60,51,10):
        yy=sy(yt)
        grid.append(f'<line x1="{left}" y1="{yy:.1f}" x2="{right}" y2="{yy:.1f}" stroke="#dfe4e7" stroke-width="1"/>')
        grid.append(f'<text x="{left-28}" y="{yy+7:.1f}" text-anchor="end" fill="#65717a" font-size="18">{yt}</text>')

    svg=f'''<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">
    <rect width="{W}" height="{H}" fill="#f7f8f6"/>
    <g font-family="Inter,Helvetica Neue,Arial,sans-serif">
    <text x="250" y="115" fill="#13202a" font-size="62" font-weight="780">WHEN “BETTER” BECOMES DECEPTIVE</text>
    <text x="250" y="178" fill="#5d6a74" font-size="24">All 36 frozen proposals: ordinary held-out improvement versus change on an untouched physical target.</text>

    <rect x="{left}" y="{top}" width="{right-left}" height="{y0-top:.1f}" fill="#fbebe6"/>
    <rect x="{left}" y="{y0:.1f}" width="{right-left}" height="{bottom-y0:.1f}" fill="#eaf4f6"/>
    {''.join(grid)}
    <line x1="{left}" y1="{y0:.1f}" x2="{right}" y2="{y0:.1f}" stroke="#4d5961" stroke-width="2"/>
    <line x1="{left}" y1="{top}" x2="{left}" y2="{bottom}" stroke="#4d5961" stroke-width="2"/>

    <text x="{right-20}" y="{top+45}" text-anchor="end" fill="#b04e39" font-size="25" font-weight="750">LOOKS BETTER / PHYSICS WORSE</text>
    <text x="{right-20}" y="{bottom-28}" text-anchor="end" fill="#267d94" font-size="25" font-weight="750">LOOKS BETTER / PHYSICS BETTER</text>
    {''.join(pts)}

    <text x="{(left+right)/2:.1f}" y="1555" text-anchor="middle" fill="#27323b" font-size="25" font-weight="650">ordinary held-out error improvement (%) →</text>
    <text x="75" y="{(top+bottom)/2:.1f}" transform="rotate(-90 75 {(top+bottom)/2:.1f})" text-anchor="middle" fill="#27323b" font-size="25" font-weight="650">untouched physical target change (%) → worse</text>

    <circle cx="1820" cy="1605" r="10" fill="#2a8ba8"/><text x="1845" y="1612" fill="#59656e" font-size="18">beneficial</text>
    <rect x="1980" y="1595" width="18" height="18" transform="rotate(45 1989 1604)" fill="#d95c3d"/><text x="2015" y="1612" fill="#59656e" font-size="18">deceptive</text>
    <circle cx="2170" cy="1605" r="13" fill="none" stroke="#f2a93b" stroke-width="5"/><text x="2195" y="1612" fill="#59656e" font-size="18">hero case</text>
    <text x="250" y="1640" fill="#879097" font-size="16">Point size ∝ |force/compliance z|. Labels are frozen; no threshold retuning or relabeling.</text>
    </g></svg>'''
    write_svg(a.out,svg)

if __name__=="__main__":
    main()

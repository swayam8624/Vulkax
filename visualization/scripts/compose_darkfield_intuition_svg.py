#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path
from vector_composition_common import metrics, package_png, write_svg

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--repo-root",type=Path,required=True)
    p.add_argument("--brightfield",type=Path,required=True)
    p.add_argument("--truth",type=Path,required=True)
    p.add_argument("--repair",type=Path,required=True)
    p.add_argument("--darkfield",type=Path,required=True)
    p.add_argument("--out",type=Path,required=True)
    a=p.parse_args(); m=metrics(a.repo_root)
    imgs=[
      package_png(a.brightfield,a.out,"intuition_brightfield.png"),
      package_png(a.truth,a.out,"intuition_truth_texture.png"),
      package_png(a.repair,a.out,"intuition_repair_texture.png"),
      package_png(a.darkfield,a.out,"intuition_darkfield.png")]
    cards=[
      ("01","APPEARANCE","The repaired world looks plausible",imgs[0],"#202933"),
      ("02","TRUTH RESPONSE","Rest-space grid under the known probe",imgs[1],"#167aa2"),
      ("03","REPAIR RESPONSE","Same grid, same probe, different mechanism",imgs[2],"#c95d28"),
      ("04","MECHANISM DARKFIELD","Candidate − truth response reveals the mismatch",imgs[3],"#db5d2f")]
    blocks=[]
    for i,(num,title,sub,img,accent) in enumerate(cards):
        x=120+i*910
        blocks.append(f"""<g>
        <text x="{x}" y="360" fill="{accent}" font-size="30" font-weight="800">{num}</text>
        <text x="{x+70}" y="360" fill="#17212b" font-size="30" font-weight="800">{title}</text>
        <text x="{x}" y="410" fill="#65717d" font-size="18">{sub}</text>
        <rect x="{x}" y="455" width="840" height="840" rx="18" fill="#edf1f3" stroke="#d3d9dd"/>
        </g>
        <image x="{x+30}" y="485" width="780" height="780" href="{img}"/>""")
    svg=f"""<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="3840" height="1600" viewBox="0 0 3840 1600">
    <rect width="3840" height="1600" fill="#f7f8f6"/>
    <g font-family="Inter,Helvetica Neue,Arial,sans-serif">
    <text x="120" y="130" fill="#13202a" font-size="62" font-weight="780">REALITY PROBE: FROM APPEARANCE TO HIDDEN PHYSICS</text>
    <text x="120" y="195" fill="#5d6a74" font-size="24">A diagnostic texture turns an invisible response mismatch into a visible mechanism darkfield.</text>
    {''.join(blocks)}
    <path d="M930 860 L1010 860 M1840 860 L1920 860 M2750 860 L2830 860" stroke="#a7b1b8" stroke-width="5" stroke-linecap="round"/>
    <text x="120" y="1435" fill="#167aa2" font-size="22" font-weight="700">ordinary held-out error improves {m['improve_pct']:.2f}%</text>
    <text x="1330" y="1435" fill="#c95d28" font-size="22" font-weight="700">untouched target worsens {m['worsen_pct']:.2f}%</text>
    <text x="2560" y="1435" fill="#6f7880" font-size="22">same frozen APIC → PIC deceptive repair, truth world {m['truth_id']}</text>
    <text x="120" y="1510" fill="#879097" font-size="17">Stanford Bunny is a visualization carrier; deformation is driven by exported solver state. Display magnification is presentation-only.</text>
    </g></svg>"""
    write_svg(a.out,svg)
if __name__=="__main__": main()

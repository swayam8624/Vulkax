#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path
from vector_composition_common import defs, esc, metrics, package_png, write_svg

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--repo-root",type=Path,required=True)
    p.add_argument("--observe",type=Path,required=True)
    p.add_argument("--repair",type=Path,required=True)
    p.add_argument("--interrogate",type=Path,required=True)
    p.add_argument("--motion-scale",type=float,default=400.0)
    p.add_argument("--out",type=Path,required=True)
    a=p.parse_args(); m=metrics(a.repo_root)
    images=[
      package_png(a.observe,a.out,"hero_observe.png"),
      package_png(a.repair,a.out,"hero_repair.png"),
      package_png(a.interrogate,a.out,"hero_interrogate.png")]
    panels=[
      (150,"01","OBSERVE","CAPTURED / FITTED STATE","#80e4ff",images[0]),
      (1340,"02","REPAIR",f"LOOKS BETTER  −{m['improve_pct']:.2f}% ORDINARY ERROR","#8ff0c9",images[1]),
      (2530,"03","INTERROGATE",f"PHYSICS WORSE  +{m['worsen_pct']:.2f}% HIDDEN TARGET","#ffb36c",images[2])]
    pm=[]
    for x,idx,title,sub,accent,img in panels:
        pm.append(f"""<g id="panel-{title.lower()}" transform="translate({x},0)">
        <rect x="0" y="510" width="1160" height="1300" rx="32" fill="#07131f" stroke="{accent}" stroke-opacity=".18" stroke-width="2"/>
        <rect x="0" y="510" width="1160" height="5" rx="2.5" fill="{accent}" opacity=".85"/>
        <text x="54" y="610" fill="{accent}" font-size="44" font-weight="700">{idx}</text>
        <text x="128" y="610" fill="#f1f7ff" font-size="44" font-weight="700">{esc(title)}</text>
        <text x="54" y="665" fill="{accent}" font-size="23" font-weight="600" letter-spacing="1">{esc(sub)}</text>
        <image x="40" y="690" width="1080" height="1080" preserveAspectRatio="xMidYMid meet" href="{img}" xlink:href="{img}"/></g>""")
    svg=f"""<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="3840" height="2160" viewBox="0 0 3840 2160">
    {defs()}<rect width="3840" height="2160" fill="url(#bg)"/><rect x="120" y="120" width="3600" height="2" fill="url(#cyanLine)" opacity=".75"/>
    <g font-family="Inter,Helvetica Neue,Arial,sans-serif">
    <text x="1920" y="285" text-anchor="middle" font-size="100" font-weight="620" fill="#eaf4ff"><tspan>LOOKS RIGHT. </tspan><tspan fill="#ffaf64">PHYSICS SAYS NO.</tspan></text>
    <text x="1920" y="365" text-anchor="middle" font-size="28" font-weight="600" fill="#7fdfff" letter-spacing="3">REALITY PROBE  /  COUNTERFACTUAL PHYSICAL VERIFICATION</text>
    {''.join(pm)}
    <path d="M1280 1160 L1320 1160" stroke="#7fdfff" stroke-width="8" stroke-linecap="round"/><path d="M2470 1160 L2510 1160" stroke="#ffb36c" stroke-width="8" stroke-linecap="round"/>
    <g transform="translate(2790,1828)" filter="url(#shadow)"><rect width="690" height="150" rx="22" fill="#180d12" stroke="#ff5a62" stroke-width="2"/>
    <text x="40" y="62" fill="#ff6a70" font-size="34" font-weight="800">REFUSE CERTIFICATION</text>
    <text x="40" y="108" fill="#ffd7d9" font-size="22">force |z|={abs(m['force_z']):.4f} &lt; frozen threshold {m['threshold']:.0f}</text></g>
    <text x="150" y="1990" fill="#ffad62" font-size="20" font-weight="600">DISPLAY MAGNIFICATION ×{a.motion_scale:g} / NUMERICAL VALUES UNSCALED</text>
    <text x="3690" y="1990" text-anchor="end" fill="#a8b8c9" font-size="18">Stanford Bunny visualization carrier / credit: Stanford Computer Graphics Laboratory</text>
    <text x="1920" y="2072" text-anchor="middle" fill="#75889c" font-size="18" letter-spacing="2">APPEARANCE AGREEMENT DOES NOT IMPLY PHYSICAL AGREEMENT</text>
    </g></svg>"""
    write_svg(a.out,svg)

if __name__=="__main__": main()

#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path
from vector_composition_common import defs, metrics, package_png, write_svg

def main():
    p=argparse.ArgumentParser(); p.add_argument("--repo-root",type=Path,required=True); p.add_argument("--observe",type=Path,required=True); p.add_argument("--repair",type=Path,required=True); p.add_argument("--xray",type=Path,required=True); p.add_argument("--out",type=Path,required=True)
    a=p.parse_args(); m=metrics(a.repo_root)
    obs=package_png(a.observe,a.out,"explainer_observe.png")
    rep=package_png(a.repair,a.out,"explainer_repair.png")
    xr=package_png(a.xray,a.out,"explainer_xray.png")
    cards=[(110,"01","CAPTURE","Executable world from observation","#76ddff",obs),(965,"02","REPAIR","Optimize what ordinary evidence sees","#8cefc7",rep),(1820,"03","PERTURB","Ask a mechanism-selective counterfactual","#ffba6e",xr)]
    cm=[]
    for x,num,title,sub,color,img in cards:
        metric="FIT / RECONSTRUCT" if num=="01" else (f"ORDINARY ERROR −{m['improve_pct']:.2f}%" if num=="02" else "KNOWN FORCE / COMPLIANCE")
        foot="appearance-compatible state" if num=="01" else ("candidate looks better" if num=="02" else "observe hidden physical response")
        cm.append(f"""<g transform="translate({x},0)"><rect x="0" y="440" width="760" height="1110" rx="28" fill="#071521" stroke="{color}" stroke-opacity=".28"/>
        <text x="42" y="515" fill="{color}" font-size="30" font-weight="800">{num}</text><text x="110" y="515" fill="#edf6ff" font-size="34" font-weight="800">{title}</text>
        <text x="42" y="565" fill="#9eb2c5" font-size="18">{sub}</text><image x="40" y="610" width="680" height="680" preserveAspectRatio="xMidYMid meet" href="{img}" xlink:href="{img}"/>
        <rect x="42" y="1330" width="676" height="140" rx="16" fill="#081b29"/><text x="68" y="1380" fill="{color}" font-size="19" font-weight="700">{metric}</text>
        <text x="68" y="1420" fill="#b7c6d5" font-size="17">{foot}</text></g>""")
    svg=f"""<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="3600" height="1800" viewBox="0 0 3600 1800">{defs()}<rect width="3600" height="1800" fill="url(#bg)"/>
    <g font-family="Inter,Helvetica Neue,Arial,sans-serif"><text x="110" y="160" fill="#eef7ff" font-size="70" font-weight="760">HOW REALITY PROBE INTERROGATES A REPAIR</text>
    <text x="110" y="225" fill="#80dfff" font-size="24" font-weight="600" letter-spacing="2">APPEARANCE IS AN OBSERVATION CHANNEL — NOT A CERTIFICATE OF PHYSICAL MECHANISM</text>
    {''.join(cm)}<path d="M890 985 L940 985" stroke="#7ae1ff" stroke-width="8" stroke-linecap="round"/><path d="M1745 985 L1795 985" stroke="#ffb36c" stroke-width="8" stroke-linecap="round"/>
    <g transform="translate(2675,440)"><rect width="815" height="1110" rx="28" fill="#100d13" stroke="#ff676e" stroke-opacity=".55"/><text x="44" y="75" fill="#ff757a" font-size="30" font-weight="800">04</text>
    <text x="112" y="75" fill="#f7f0f1" font-size="34" font-weight="800">DECIDE</text><text x="44" y="128" fill="#b9a9ad" font-size="18">Support / veto only when evidence clears the frozen bar</text>
    <circle cx="407" cy="375" r="180" fill="#1a0d12" stroke="#ff5f67" stroke-width="5"/><path d="M310 278 L504 472 M504 278 L310 472" stroke="#ff646b" stroke-width="28" stroke-linecap="round"/>
    <text x="407" y="635" text-anchor="middle" fill="#ff6b72" font-size="52" font-weight="850">REFUSE</text><text x="407" y="690" text-anchor="middle" fill="#f3d6d8" font-size="20">insufficient information for certification</text>
    <line x1="70" y1="775" x2="745" y2="775" stroke="#38232b"/><text x="70" y="835" fill="#9bdfff" font-size="20">ordinary fit</text><text x="520" y="835" fill="#e9f8ff" font-size="26" font-weight="700">−{m['improve_pct']:.2f}%</text>
    <text x="70" y="890" fill="#ff8d88" font-size="20">untouched target</text><text x="520" y="890" fill="#fff0ef" font-size="26" font-weight="700">+{m['worsen_pct']:.2f}%</text>
    <text x="70" y="945" fill="#ffb36c" font-size="20">force |z|</text><text x="520" y="945" fill="#fff0df" font-size="26" font-weight="700">{abs(m['force_z']):.4f}</text>
    <text x="70" y="1000" fill="#aebdcc" font-size="20">frozen threshold</text><text x="520" y="1000" fill="#f0f6fb" font-size="26" font-weight="700">{m['threshold']:.0f}</text></g></g></svg>"""
    write_svg(a.out,svg)

if __name__=="__main__": main()

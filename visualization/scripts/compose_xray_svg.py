#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path
from vector_composition_common import defs, metrics, package_png, write_svg

def main():
    p=argparse.ArgumentParser(); p.add_argument("--repo-root",type=Path,required=True); p.add_argument("--xray",type=Path,required=True); p.add_argument("--motion-scale",type=float,default=400.0); p.add_argument("--out",type=Path,required=True)
    a=p.parse_args(); m=metrics(a.repo_root); image=package_png(a.xray,a.out,"xray_plate.png")
    svg=f"""<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="3200" height="1800" viewBox="0 0 3200 1800">{defs()}<rect width="3200" height="1800" fill="url(#bg)"/>
    <g font-family="Inter,Helvetica Neue,Arial,sans-serif"><text x="150" y="175" fill="#f0f7ff" font-size="76" font-weight="700">REALITY PROBE  /  MECHANISM DARKFIELD</text>
    <text x="150" y="245" fill="#82e2ff" font-size="25" font-weight="600" letter-spacing="2">CANDIDATE − TRUTH RESPONSE UNDER A KNOWN PHYSICAL PROBE</text>
    <rect x="120" y="330" width="1940" height="1320" rx="34" fill="#07131f" stroke="#6ee2ff" stroke-opacity=".22" stroke-width="2"/>
    <image x="510" y="390" width="1160" height="1160" href="{image}"/>
    <text x="170" y="1600" fill="#ffae65" font-size="20" font-weight="600">DISPLAY ×{a.motion_scale:g} / RESIDUAL DIRECTIONS FROM RAW SOLVER STATE</text>
    <g transform="translate(2160,350)"><text x="0" y="0" fill="#f0f7ff" font-size="36" font-weight="700">THIS DECEPTIVE REPAIR</text>
    <text x="0" y="56" fill="#91a7bb" font-size="21">truth world {m['truth_id']} / frozen APIC → PIC proposal</text>
    <rect x="0" y="120" width="880" height="185" rx="22" fill="#091a27" stroke="#67dfff" stroke-opacity=".35"/><text x="34" y="175" fill="#83e5ff" font-size="24" font-weight="700">ORDINARY FIT</text>
    <text x="34" y="235" fill="#f3f8fd" font-size="54" font-weight="800">−{m['improve_pct']:.2f}%</text><text x="330" y="235" fill="#9fb3c7" font-size="21">held-out error</text>
    <rect x="0" y="335" width="880" height="185" rx="22" fill="#1b1014" stroke="#ff726f" stroke-opacity=".45"/><text x="34" y="390" fill="#ff8580" font-size="24" font-weight="700">UNTOUCHED PHYSICAL TARGET</text>
    <text x="34" y="450" fill="#fff0ef" font-size="54" font-weight="800">+{m['worsen_pct']:.2f}%</text><text x="330" y="450" fill="#c8a9aa" font-size="21">worse after repair</text>
    <text x="0" y="605" fill="#f0f7ff" font-size="30" font-weight="700">COUNTERFACTUAL EVIDENCE</text><line x1="0" y1="635" x2="880" y2="635" stroke="#284253"/>
    <text x="0" y="700" fill="#8fe7ff" font-size="22">DCS |z|</text><text x="400" y="700" fill="#e9f7ff" font-size="30" font-weight="700">{abs(m['dcs_z']):.4f}</text>
    <text x="0" y="765" fill="#ffb36c" font-size="22">Force/compliance |z|</text><text x="400" y="765" fill="#fff0de" font-size="30" font-weight="700">{abs(m['force_z']):.4f}</text>
    <text x="0" y="830" fill="#a5b7c8" font-size="22">Case signal gain</text><text x="400" y="830" fill="#ffba69" font-size="30" font-weight="700">{m['case_ratio']:.2f}×</text>
    <text x="0" y="895" fill="#a5b7c8" font-size="22">Frozen threshold</text><text x="400" y="895" fill="#f0f7ff" font-size="30" font-weight="700">|z| ≥ {m['threshold']:.0f}</text>
    <rect x="0" y="975" width="880" height="205" rx="24" fill="#210d12" stroke="#ff5962" stroke-width="3"/><text x="42" y="1045" fill="#ff6269" font-size="42" font-weight="850">UNRESOLVED / REFUSE</text>
    <text x="42" y="1100" fill="#ffd8da" font-size="21">stronger physical evidence, still below certification threshold</text><text x="42" y="1142" fill="#a99da1" font-size="18">not a validated prospective repair verifier</text></g></g></svg>"""
    write_svg(a.out,svg)

if __name__=="__main__": main()

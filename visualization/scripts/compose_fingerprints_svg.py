#!/usr/bin/env python3
from __future__ import annotations
import argparse,csv
from pathlib import Path
from vector_composition_common import package_png, write_svg

LABELS={"px":"+X","nx":"−X","py":"+Y","pz":"+Z"}

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--summary",type=Path,required=True)
    p.add_argument("--px",type=Path,required=True)
    p.add_argument("--nx",type=Path,required=True)
    p.add_argument("--py",type=Path,required=True)
    p.add_argument("--pz",type=Path,required=True)
    p.add_argument("--out",type=Path,required=True)
    a=p.parse_args()
    rows={r["direction"]:r for r in csv.DictReader(a.summary.open())}
    imgs={d:package_png(getattr(a,d),a.out,f"fingerprint_{d}.png") for d in LABELS}
    blocks=[]
    for i,d in enumerate(("px","nx","py","pz")):
        r=rows[d]
        b=float(r["baseline_raw_rms_to_truth_m"])
        q=float(r["repair_raw_rms_to_truth_m"])
        ratio=q/max(b,1e-18)
        x=120+i*910
        blocks.append(f"""<g transform="translate({x},0)">
        <text x="0" y="330" fill="#17212b" font-size="38" font-weight="800">{LABELS[d]} FORCE</text>
        <rect x="0" y="390" width="840" height="840" rx="18" fill="#07131f"/>
        <image x="25" y="415" width="790" height="790" preserveAspectRatio="xMidYMid meet" href="{imgs[d]}" xlink:href="{imgs[d]}"/>
        <text x="0" y="1285" fill="#167aa2" font-size="20">baseline RMS {b:.2e} m</text>
        <text x="0" y="1325" fill="#c95d28" font-size="20">repair RMS {q:.2e} m</text>
        <text x="0" y="1375" fill="#17212b" font-size="28" font-weight="800">{ratio:.2f}× raw response error</text>
        </g>""")
    svg=f"""<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="3840" height="1550" viewBox="0 0 3840 1550">
    <rect width="3840" height="1550" fill="#f7f8f6"/>
    <g font-family="Inter,Helvetica Neue,Arial,sans-serif">
    <text x="120" y="125" fill="#13202a" font-size="62" font-weight="780">MECHANISM FINGERPRINTS</text>
    <text x="120" y="190" fill="#5d6a74" font-size="24">The same repaired world is interrogated along four orthogonal force directions.</text>
    {''.join(blocks)}
    <text x="120" y="1470" fill="#879097" font-size="17">Raw compliance RMS is computed from exported solver trajectories before synthetic observation noise.</text>
    </g></svg>"""
    write_svg(a.out,svg)
if __name__=="__main__": main()

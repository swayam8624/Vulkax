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
    svg=f"""<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="3840" height="1615" viewBox="0 0 3840 1615">
    <defs>
      <linearGradient id="residualScale" x1="0" y1="0" x2="1" y2="0">
        <stop offset="0%" stop-color="#093f75"/>
        <stop offset="50%" stop-color="#d5e7ee"/>
        <stop offset="100%" stop-color="#ff4d0e"/>
      </linearGradient>
    </defs>
    <rect width="3840" height="1615" fill="#f7f8f6"/>
    <g font-family="Inter,Helvetica Neue,Arial,sans-serif">
    <text x="120" y="125" fill="#13202a" font-size="62" font-weight="780">MECHANISM FINGERPRINTS</text>
    <text x="120" y="190" fill="#5d6a74" font-size="24">The same repaired world is interrogated along four orthogonal force directions.</text>
    <text x="120" y="240" fill="#7a858d" font-size="19">Surface color = |u_repair − u_truth|, normalized by one common four-direction scale.</text>
    {''.join(blocks)}
    <g transform="translate(120,1460)">
      <rect x="0" y="0" width="620" height="18" rx="9" fill="url(#residualScale)"/>
      <text x="0" y="50" fill="#5f6971" font-size="17">low disagreement</text>
      <text x="620" y="50" text-anchor="end" fill="#5f6971" font-size="17">high disagreement</text>
      <text x="760" y="22" fill="#879097" font-size="17">same scale in every panel / display magnification affects geometry only, not reported RMS values</text>
    </g>
    </g></svg>"""
    write_svg(a.out,svg)
if __name__=="__main__": main()

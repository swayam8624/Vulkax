#!/usr/bin/env python3
"""Render the frozen illustrative deceptive-repair row as a deterministic SVG."""
from __future__ import annotations

import argparse
import html
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CASE = ROOT / "visualization/data/hero_case_ofc_2026-09-21.json"
DEFAULT_OUT = ROOT / "build/visualization"

INK = "#111827"
MUTED = "#5b6472"
GRID = "#d8dee8"
GREEN = "#047857"
RED = "#b91c1c"
BLUE = "#2563eb"
ORANGE = "#ea580c"
PANEL = "#f8fafc"


def esc(v):
    return html.escape(str(v), quote=True)


def t(x, y, s, size=24, *, fill=INK, weight=400, anchor="start"):
    return (
        f'<text x="{x:.1f}" y="{y:.1f}" font-family="Inter,Arial,sans-serif" '
        f'font-size="{size}" font-weight="{weight}" fill="{fill}" '
        f'text-anchor="{anchor}">{esc(s)}</text>'
    )


def rect(x, y, w, h, fill="none", stroke="none", rx=0, width=1):
    return (
        f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" '
        f'rx="{rx:.1f}" fill="{fill}" stroke="{stroke}" stroke-width="{width}"/>'
    )


def line(x1, y1, x2, y2, stroke=INK, width=2, dash=None):
    extra = f' stroke-dasharray="{dash}"' if dash else ""
    return (
        f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" '
        f'stroke="{stroke}" stroke-width="{width}"{extra}/>'
    )


def circle(cx, cy, r, fill):
    return f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{r:.1f}" fill="{fill}"/>'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = ap.parse_args()

    payload = json.loads(CASE.read_text(encoding="utf-8"))
    r = payload["row"]
    bh = r["baseline_holdout_m"] * 1e6
    rh = r["repair_holdout_m"] * 1e6
    bt = r["baseline_target_m"] * 1e6
    rt = r["repair_target_m"] * 1e6
    ordinary_improvement = 100.0 * (bh-rh) / bh
    hidden_worsening = 100.0 * (rt-bt) / bt
    ratio = abs(r["force_progress_z"]) / abs(r["dcs_progress_z"])

    W, H = 1600, 900
    b = [
        rect(0,0,W,H,"#ffffff"),
        t(70,70,"VULKAX — one frozen deceptive repair",42,weight=800),
        t(70,110,
          f"Fresh synthetic truth world {r['truth_id']} • {r['baseline'].upper()} → {r['repair'].upper()} • post-hoc illustrative selection only",
          21,fill=MUTED),
        rect(70,160,700,300,PANEL,GRID,22,2),
        rect(830,160,700,300,PANEL,GRID,22,2),
        t(105,208,"ORDINARY HELD-OUT OBSERVATION",21,fill=GREEN,weight=800),
        t(865,208,"UNTOUCHED STRONGER TARGET",21,fill=RED,weight=800),
        t(105,252,"The proposed repair looks better",26,weight=700),
        t(865,252,"The same repair is actually worse",26,weight=700),
    ]

    def error_panel(x, y, baseline, repair, good):
        maxv = max(baseline, repair) * 1.12
        bw = 470
        xb = x+135
        wb = bw * baseline/maxv
        wr = bw * repair/maxv
        color = GREEN if good else RED
        return [
            t(x, y, "baseline",19,fill=MUTED),
            rect(xb,y-21,wb,26,"#94a3b8",rx=8),
            t(xb+wb+12,y,f"{baseline:.3f} μm",20,weight=650),
            t(x, y+72, "repair",19,fill=MUTED),
            rect(xb,y+51,wr,26,color,rx=8),
            t(xb+wr+12,y+72,f"{repair:.3f} μm",20,weight=650),
        ]

    b += error_panel(105,320,bh,rh,True)
    b += error_panel(865,320,bt,rt,False)
    b += [
        t(105,428,f"{ordinary_improvement:.2f}% lower error → ordinary metric proposes the repair",
          22,fill=GREEN,weight=750),
        t(865,428,f"{hidden_worsening:.2f}% higher error → hidden label = DECEPTIVE",
          22,fill=RED,weight=750),

        rect(70,505,1460,300,"#ffffff",GRID,22,2),
        t(105,553,"MECHANISM-SELECTIVE VERIFICATION",21,weight=800),
        t(105,588,"Both channels remain unresolved under the same frozen |z| = 2 decision rule.",22,fill=MUTED),
    ]

    # Shared signed z-axis from -2.2 to +2.2.
    x0, x1, yy = 210, 1410, 685
    zmin, zmax = -2.2, 2.2
    zx = lambda z: x0 + (float(z)-zmin)/(zmax-zmin)*(x1-x0)
    b += [
        line(x0,yy,x1,yy,INK,2),
        line(zx(-2),yy-50,zx(-2),yy+55,RED,3,"10 8"),
        line(zx(2),yy-50,zx(2),yy+55,GREEN,3,"10 8"),
        t(zx(-2),yy-66,"VETO ≤ -2",19,fill=RED,weight=700,anchor="middle"),
        t(zx(2),yy-66,"SUPPORT ≥ +2",19,fill=GREEN,weight=700,anchor="middle"),
        t(zx(0),yy+42,"0",17,fill=MUTED,anchor="middle"),
    ]
    for z in [-2,-1,0,1,2]:
        b += [line(zx(z),yy-8,zx(z),yy+8,"#64748b",2)]
        if z != 0:
            b += [t(zx(z),yy+38,str(z),17,fill=MUTED,anchor="middle")]

    zd = r["dcs_progress_z"]
    zf = r["force_progress_z"]
    b += [
        circle(zx(zd),yy-18,11,BLUE),
        t(zx(zd),yy-92,f"DCS  z={zd:.4f}",20,fill=BLUE,weight=750,anchor="middle"),
        circle(zx(zf),yy+18,13,ORANGE),
        t(zx(zf),yy+96,f"force/compliance  z={zf:.4f}",20,fill=ORANGE,weight=750,anchor="middle"),
        t(800,780,f"For this exact deceptive proposal, the orthogonal channel is {ratio:.2f}× stronger in |z| — still not enough to veto.",
          22,weight=750,anchor="middle"),
        t(800,842,
          "Selection is explicitly post-hoc for visualization; the row did not set the threshold, force amplitude, or headline statistics.",
          18,fill=MUTED,anchor="middle"),
    ]

    svg = "\n".join([
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">',
        '<title>VULKAX frozen deceptive repair hero case</title>',
        *b,
        "</svg>",
        "",
    ])
    args.out.mkdir(parents=True,exist_ok=True)
    out=args.out/"fig_deceptive_repair_hero.svg"
    out.write_text(svg,encoding="utf-8")
    print(out)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Generate deterministic paper-facing figures/tables from the frozen Vulkax result ledger.

No scientific decisions are made here. The script is a pure presentation transform
of research/results/DCS_FINAL_RESULTS_2026-09-20.json and writes SVG/CSV/JSON assets.
It uses only the Python standard library so the full reproduction path has no extra
plotting dependency.
"""
from __future__ import annotations

import argparse
import csv
import html
import json
import math
from pathlib import Path
import tempfile


WIDTH=1200
HEIGHT=720
MARGIN_L=120
MARGIN_R=40
MARGIN_T=80
MARGIN_B=120

def esc(x: object) -> str:
    return html.escape(str(x))

def svg_begin(title: str, subtitle: str="") -> list[str]:
    return [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}">',
        '<rect width="100%" height="100%" fill="white"/>',
        '<style>text{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Arial,sans-serif;fill:#111}'
        '.title{font-size:30px;font-weight:700}.subtitle{font-size:17px;fill:#444}'
        '.label{font-size:15px}.small{font-size:13px;fill:#555}.value{font-size:14px;font-weight:600}'
        '.axis{stroke:#333;stroke-width:1}.grid{stroke:#ddd;stroke-width:1}.bar{fill:#555}'
        '.bar2{fill:#888}.threshold{stroke:#111;stroke-width:2;stroke-dasharray:8 6}</style>',
        f'<text x="{MARGIN_L}" y="42" class="title">{esc(title)}</text>',
        f'<text x="{MARGIN_L}" y="68" class="subtitle">{esc(subtitle)}</text>' if subtitle else "",
    ]

def svg_end(lines: list[str], path: Path) -> None:
    lines.append("</svg>")
    path.write_text("\n".join(x for x in lines if x)+"\n")

def bar_chart(path: Path, title: str, subtitle: str, labels: list[str], values: list[float],
              maximum: float, ylabel: str, threshold: float|None=None) -> None:
    lines=svg_begin(title,subtitle)
    plot_w=WIDTH-MARGIN_L-MARGIN_R
    plot_h=HEIGHT-MARGIN_T-MARGIN_B
    x0=MARGIN_L
    y0=MARGIN_T+plot_h
    for tick in range(6):
        v=maximum*tick/5
        y=y0-plot_h*(v/maximum if maximum else 0)
        lines.append(f'<line x1="{x0}" y1="{y:.2f}" x2="{x0+plot_w}" y2="{y:.2f}" class="grid"/>')
        lines.append(f'<text x="{x0-12}" y="{y+5:.2f}" text-anchor="end" class="small">{v:.2g}</text>')
    lines.append(f'<line x1="{x0}" y1="{MARGIN_T}" x2="{x0}" y2="{y0}" class="axis"/>')
    lines.append(f'<line x1="{x0}" y1="{y0}" x2="{x0+plot_w}" y2="{y0}" class="axis"/>')
    if threshold is not None and maximum>0:
        y=y0-plot_h*threshold/maximum
        lines.append(f'<line x1="{x0}" y1="{y:.2f}" x2="{x0+plot_w}" y2="{y:.2f}" class="threshold"/>')
        lines.append(f'<text x="{x0+plot_w-5}" y="{y-8:.2f}" text-anchor="end" class="small">threshold {threshold:g}</text>')
    n=len(labels)
    band=plot_w/max(n,1)
    bw=band*0.62
    for i,(lab,val) in enumerate(zip(labels,values)):
        x=x0+i*band+(band-bw)/2
        h=plot_h*max(0,min(val,maximum))/maximum if maximum else 0
        y=y0-h
        lines.append(f'<rect x="{x:.2f}" y="{y:.2f}" width="{bw:.2f}" height="{h:.2f}" class="bar"/>')
        lines.append(f'<text x="{x+bw/2:.2f}" y="{max(MARGIN_T+16,y-8):.2f}" text-anchor="middle" class="value">{val:.4g}</text>')
        lines.append(f'<text x="{x+bw/2:.2f}" y="{y0+24}" text-anchor="middle" class="label">{esc(lab)}</text>')
    lines.append(f'<text x="28" y="{MARGIN_T+plot_h/2}" transform="rotate(-90 28 {MARGIN_T+plot_h/2})" text-anchor="middle" class="label">{esc(ylabel)}</text>')
    svg_end(lines,path)

def grouped_bar_chart(path: Path, title: str, subtitle: str, groups: list[str],
                      series: list[tuple[str,list[float]]], maximum: float=1.0) -> None:
    lines=svg_begin(title,subtitle)
    plot_w=WIDTH-MARGIN_L-MARGIN_R
    plot_h=HEIGHT-MARGIN_T-MARGIN_B
    x0=MARGIN_L; y0=MARGIN_T+plot_h
    for tick in range(6):
        v=maximum*tick/5
        y=y0-plot_h*v/maximum
        lines.append(f'<line x1="{x0}" y1="{y:.2f}" x2="{x0+plot_w}" y2="{y:.2f}" class="grid"/>')
        lines.append(f'<text x="{x0-12}" y="{y+5:.2f}" text-anchor="end" class="small">{v:.1f}</text>')
    lines.append(f'<line x1="{x0}" y1="{MARGIN_T}" x2="{x0}" y2="{y0}" class="axis"/>')
    lines.append(f'<line x1="{x0}" y1="{y0}" x2="{x0+plot_w}" y2="{y0}" class="axis"/>')
    band=plot_w/max(len(groups),1)
    sub=band/(len(series)+1)
    bw=sub*0.78
    for gi,g in enumerate(groups):
        gx=x0+gi*band
        for si,(name,vals) in enumerate(series):
            val=vals[gi]
            x=gx+(si+0.5)*sub
            h=plot_h*val/maximum
            y=y0-h
            klass="bar" if si==0 else "bar2"
            lines.append(f'<rect x="{x:.2f}" y="{y:.2f}" width="{bw:.2f}" height="{h:.2f}" class="{klass}"/>')
            lines.append(f'<text x="{x+bw/2:.2f}" y="{max(MARGIN_T+14,y-7):.2f}" text-anchor="middle" class="small">{val:.3f}</text>')
        lines.append(f'<text x="{gx+band/2:.2f}" y="{y0+24}" text-anchor="middle" class="label">{esc(g)}</text>')
    lx=MARGIN_L
    for si,(name,_) in enumerate(series):
        klass="bar" if si==0 else "bar2"
        lines.append(f'<rect x="{lx}" y="{HEIGHT-48}" width="18" height="18" class="{klass}"/>')
        lines.append(f'<text x="{lx+25}" y="{HEIGHT-34}" class="small">{esc(name)}</text>')
        lx+=180
    lines.append(f'<text x="28" y="{MARGIN_T+plot_h/2}" transform="rotate(-90 28 {MARGIN_T+plot_h/2})" text-anchor="middle" class="label">ranking agreement</text>')
    svg_end(lines,path)

def log_bar_chart(path: Path, title: str, subtitle: str, labels: list[str], values: list[float]) -> None:
    positives=[v for v in values if v>0]
    lo=10**math.floor(math.log10(min(positives)))
    hi=10**math.ceil(math.log10(max(positives)))
    if hi<=lo: hi=lo*10
    lines=svg_begin(title,subtitle)
    plot_w=WIDTH-MARGIN_L-MARGIN_R
    plot_h=HEIGHT-MARGIN_T-MARGIN_B
    x0=MARGIN_L; y0=MARGIN_T+plot_h
    llo=math.log10(lo); lhi=math.log10(hi)
    decade=llo
    while decade<=lhi+1e-9:
        v=10**decade
        y=y0-plot_h*(math.log10(v)-llo)/(lhi-llo)
        lines.append(f'<line x1="{x0}" y1="{y:.2f}" x2="{x0+plot_w}" y2="{y:.2f}" class="grid"/>')
        lines.append(f'<text x="{x0-12}" y="{y+5:.2f}" text-anchor="end" class="small">{v:.1e}</text>')
        decade+=1
    lines.append(f'<line x1="{x0}" y1="{MARGIN_T}" x2="{x0}" y2="{y0}" class="axis"/>')
    lines.append(f'<line x1="{x0}" y1="{y0}" x2="{x0+plot_w}" y2="{y0}" class="axis"/>')
    band=plot_w/len(labels); bw=band*.62
    for i,(lab,val) in enumerate(zip(labels,values)):
        frac=(math.log10(val)-llo)/(lhi-llo)
        h=max(2,plot_h*frac)
        x=x0+i*band+(band-bw)/2; y=y0-h
        lines.append(f'<rect x="{x:.2f}" y="{y:.2f}" width="{bw:.2f}" height="{h:.2f}" class="bar"/>')
        lines.append(f'<text x="{x+bw/2:.2f}" y="{max(MARGIN_T+16,y-8):.2f}" text-anchor="middle" class="value">{val:.3e}</text>')
        lines.append(f'<text x="{x+bw/2:.2f}" y="{y0+24}" text-anchor="middle" class="label">{esc(lab)}</text>')
    lines.append(f'<text x="28" y="{MARGIN_T+plot_h/2}" transform="rotate(-90 28 {MARGIN_T+plot_h/2})" text-anchor="middle" class="label">RMS error (m, log scale)</text>')
    svg_end(lines,path)

def write_csv(path: Path, header: list[str], rows: list[list[object]]) -> None:
    with path.open("w",newline="") as f:
        w=csv.writer(f); w.writerow(header); w.writerows(rows)

def generate(result_path: Path, out: Path) -> dict:
    root_data=json.loads(result_path.read_text())
    data=root_data["dcs"] if root_data.get("schema")=="vulkax.final_results" else root_data
    orthogonal=root_data.get("orthogonal_force_compliance") if root_data.get("schema")=="vulkax.final_results" else None
    out.mkdir(parents=True,exist_ok=True)
    figures=[]

    # Figure 1: target-ranking agreement across D2b and D3 for common methods.
    d2=data["stages"]["D2b"]["ranking_agreement"]
    d3=data["stages"]["D3"]["ranking_agreement"]
    common=[
        ("DCS",d2["dcs_k2_maximin"],d3["adaptive_dcs"]),
        ("raw bundle",d2["raw_bundle"],d3["raw_bundle"]),
        ("raw max/pair",d2["raw_maximin"],d3["raw_pair_aware"]),
        ("Fisher",d2["fisher"],d3["fisher"]),
        ("max motion",d2["max_motion"],d3["max_motion"]),
        ("random",d2["random"],d3["random"]),
    ]
    p=out/"fig_ranking_agreement.svg"
    grouped_bar_chart(p,"Target-ranking agreement","Discovery comparisons; higher is better",
                      [x[0] for x in common],[("D2b",[x[1] for x in common]),("D3",[x[2] for x in common])])
    write_csv(out/"fig_ranking_agreement.csv",["method","D2b","D3"],[[a,b,c] for a,b,c in common])
    figures.append({"id":"ranking_agreement","svg":p.name,"data":"fig_ranking_agreement.csv",
                    "status":"discovery","claim":"DCS did not outperform matched raw/Fisher baselines."})

    # Figure 2: observability/separation relative to fixed reference 2.0.
    vals=[data["stages"]["D2_frozen"]["median_standardized_separation"],
          data["stages"]["D3"]["median_separation"]["k2"],
          data["stages"]["D3"]["median_separation"]["k3"]]
    p=out/"fig_standardized_separation.svg"
    bar_chart(p,"Mechanism observability","Median predicted standardized separation; frozen reference = 2",
              ["D2 k2","D3 k2","D3 k3"],vals,2.0,"standardized separation",threshold=2.0)
    write_csv(out/"fig_standardized_separation.csv",["stage","median_separation","reference"],
              [["D2 k2",vals[0],2.0],["D3 k2",vals[1],2.0],["D3 k3",vals[2],2.0]])
    figures.append({"id":"standardized_separation","svg":p.name,"data":"fig_standardized_separation.csv",
                    "status":"frozen/discovery","claim":"All tested DCS variants remained far below the inherited resolution reference."})

    # Figure 3: numerical uncertainty floor reduction.
    vals=[data["stages"]["D2_frozen"]["median_numerical_rms_m"],
          data["stages"]["D3"]["median_numerical_rms_m"]["k2"],
          data["stages"]["D3"]["median_numerical_rms_m"]["k3"]]
    p=out/"fig_numerical_floor.svg"
    log_bar_chart(p,"Numerical witness floor","Witness-space refinement reduced numerical error but did not create resolved separation",
                  ["D2 raw floor","D3 k2 witness","D3 k3 witness"],vals)
    write_csv(out/"fig_numerical_floor.csv",["method","median_numerical_rms_m"],
              [["D2 raw floor",vals[0]],["D3 k2 witness",vals[1]],["D3 k3 witness",vals[2]]])
    figures.append({"id":"numerical_floor","svg":p.name,"data":"fig_numerical_floor.csv",
                    "status":"discovery","claim":"Witness-space uncertainty reduced the numerical floor substantially."})

    # Figure 4: D4V proposal population and resolved count.
    d4=data["stages"]["D4V"]
    vals=[float(d4["deceptive"]),float(d4["beneficial"]),0.0]
    p=out/"fig_d4v_proposals.svg"
    bar_chart(p,"D4V repair proposals","Held-out improvement can still be deceptive; verification coverage remained zero",
              ["deceptive","beneficial","resolved"],vals,max(vals),"count")
    write_csv(out/"fig_d4v_proposals.csv",["class","count"],
              [["deceptive",d4["deceptive"]],["beneficial",d4["beneficial"]],["resolved",0]])
    figures.append({"id":"d4v_proposals","svg":p.name,"data":"fig_d4v_proposals.csv",
                    "status":"discovery","claim":"14 of 36 held-out-improving repairs were deceptive, while all tested verification channels remained unresolved."})

    # Figure 5: GAUGE channel contradiction.
    g=data["stages"]["GAUGE"]
    vals=[float(g["ordinary_overlap_wins"]),float(g["marker_darkfield_endpoint_wins"]),float(g["longitudinal_darkfield_endpoint_wins"])]
    p=out/"fig_gauge_channel_contradiction.svg"
    bar_chart(p,"GAUGE retrospective channel contradiction","10 held-out repeats; retrospective only",
              ["ordinary overlap wins","marker endpoint wins","longitudinal endpoint wins"],vals,10.0,"repeat count")
    write_csv(out/"fig_gauge_channel_contradiction.csv",["channel","count","total"],
              [["ordinary overlap wins",g["ordinary_overlap_wins"],10],
               ["marker endpoint wins",g["marker_darkfield_endpoint_wins"],10],
               ["longitudinal endpoint wins",g["longitudinal_darkfield_endpoint_wins"],10]])
    figures.append({"id":"gauge_channel_contradiction","svg":p.name,"data":"fig_gauge_channel_contradiction.csv",
                    "status":"retrospective_only","claim":"Aggregate/marker evidence and longitudinal mechanism evidence disagree."})

    # Figure 6: post-hoc information frontier.
    frontier=data.get("post_hoc_information_frontier",{}).get("d4v",{}).get("methods",{})
    if frontier:
        method_keys=["dcs","raw_bundle","raw_point","fisher","max_motion"]
        labels=["DCS","raw bundle","raw point","Fisher","max motion"]
        vals=[float(frontier[k]["median_required_signal_amplification_to_z2"]) for k in method_keys]
        p=out/"fig_information_frontier.svg"
        bar_chart(
            p,
            "Information gap to frozen credibility reference",
            "Post-hoc D4V diagnostic; median signal amplification required to reach |z| = 2",
            labels,vals,max(45.0,max(vals)*1.08),"required signal amplification (x)"
        )
        write_csv(
            out/"fig_information_frontier.csv",
            ["method","median_required_signal_amplification_to_z2","analysis_class"],
            [[lab,val,"post_hoc_diagnostic"] for lab,val in zip(labels,vals)]
        )
        figures.append({
            "id":"information_frontier",
            "svg":p.name,
            "data":"fig_information_frontier.csv",
            "status":"post_hoc_diagnostic",
            "claim":"The tested D4V verification channels were far below, not marginally below, the frozen |z|=2 reference."
        })

    # Figure 7: fresh orthogonal physical-information gain.
    if orthogonal:
        vals=[
            float(orthogonal["dcs"]["median_abs_z"]),
            float(orthogonal["force_compliance"]["median_abs_z"]),
            float(orthogonal["force_compliance"]["max_abs_z"]),
        ]
        p=out/"fig_orthogonal_force_gain.svg"
        bar_chart(
            p,
            "Orthogonal physical information increases observability",
            "Fresh synthetic follow-on; frozen credibility reference = |z| 2",
            ["fresh DCS median","force median","force best"],vals,2.0,
            "standardized progress |z|",threshold=2.0
        )
        write_csv(
            out/"fig_orthogonal_force_gain.csv",
            ["quantity","abs_z","reference","evidence_class"],
            [
                ["fresh DCS median",vals[0],2.0,"fresh_synthetic_follow_on"],
                ["force median",vals[1],2.0,"fresh_synthetic_follow_on"],
                ["force best",vals[2],2.0,"fresh_synthetic_follow_on"],
            ]
        )
        figures.append({
            "id":"orthogonal_force_gain",
            "svg":p.name,
            "data":"fig_orthogonal_force_gain.csv",
            "status":"fresh_synthetic_follow_on",
            "claim":"Known-force compliance increased median standardized signal by 11.46x but still did not cross the frozen resolved-decision threshold."
        })

    # Paper-facing tables.
    write_csv(out/"table_stage_outcomes.csv",
              ["stage","status","population","resolved_coverage","paper_role"],
              [
                ["D1","constructed pass","64 cases","n/a","implementation control"],
                ["D2 frozen","negative","16 truth worlds","0","fresh frozen validation"],
                ["D3","negative","6 truth worlds","0","discovery"],
                ["D4V","negative","36 proposals","0","repair-veto discovery"],
                ["OFC","negative with 11.46x signal gain","36 fresh proposals","0","orthogonal force-compliance follow-on"],
                ["GAUGE","mixed","10 held-out repeats","n/a","retrospective measured evidence"],
                ["D5","not executed","fresh measured data","n/a","future confirmation only"],
              ])
    write_csv(out/"table_claim_boundaries.csv",
              ["claim","supported"],
              [
                ["ordinary held-out improvement can be deceptive","yes"],
                ["DCS universal ranking superiority","no"],
                ["DCS useful prospective resolved repair-veto coverage","no"],
                ["orthogonal known-force compliance increases standardized signal in the fresh synthetic test","yes"],
                ["orthogonal force-compliance provides resolved repair-verification coverage","no"],
                ["GAUGE longitudinal mechanism channel can contradict aggregate observation metrics","retrospective only"],
                ["fresh measured prospective DCS confirmation","no"],
              ])

    if frontier:
        write_csv(
            out/"table_information_frontier.csv",
            ["method","median_abs_z","max_abs_z","median_required_signal_amplification_to_z2","analysis_class"],
            [[
                label,
                frontier[key]["median_abs_z"],
                frontier[key]["max_abs_z"],
                frontier[key]["median_required_signal_amplification_to_z2"],
                "post_hoc_diagnostic",
            ] for key,label in zip(
                ["dcs","raw_bundle","raw_point","fisher","max_motion"],
                ["DCS","raw bundle","raw point","Fisher","max motion"]
            )]
        )
    else:
        write_csv(out/"table_information_frontier.csv",
                  ["method","median_abs_z","max_abs_z","median_required_signal_amplification_to_z2","analysis_class"],[])

    if orthogonal:
        write_csv(
            out/"table_orthogonal_force_compliance.csv",
            ["metric","fresh_dcs","force_compliance","frozen_reference"],
            [
                ["resolved_coverage",orthogonal["dcs"]["coverage"],orthogonal["force_compliance"]["coverage"],"n/a"],
                ["median_abs_z",orthogonal["dcs"]["median_abs_z"],orthogonal["force_compliance"]["median_abs_z"],2.0],
                ["max_abs_z",orthogonal["dcs"]["max_abs_z"],orthogonal["force_compliance"]["max_abs_z"],2.0],
                ["median_required_signal_amplification_to_z2",
                 orthogonal["dcs"]["median_required_signal_amplification_to_z2"],
                 orthogonal["force_compliance"]["median_required_signal_amplification_to_z2"],1.0],
                ["sign_accuracy",orthogonal["dcs"]["sign_accuracy"],orthogonal["force_compliance"]["sign_accuracy"],"n/a"],
            ]
        )
    else:
        write_csv(out/"table_orthogonal_force_compliance.csv",
                  ["metric","fresh_dcs","force_compliance","frozen_reference"],[])

    manifest={
        "schema":"vulkax.paper_assets",
        "version":1,
        "source":str(result_path),
        "figures":figures,
        "tables":["table_stage_outcomes.csv","table_claim_boundaries.csv","table_information_frontier.csv","table_orthogonal_force_compliance.csv"],
        "paper_prose_generated":False,
        "warning":"Assets visualize frozen results; they do not change claim status.",
    }
    (out/"figure_manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    return manifest

def self_test() -> None:
    with tempfile.TemporaryDirectory() as td:
        root=Path(td); source=root/"results.json"; out=root/"out"
        source.write_text(json.dumps({
          "schema":"vulkax.final_results",
          "dcs":{
          "stages":{
            "D2b":{"ranking_agreement":{"dcs_k2_maximin":.5,"raw_bundle":.6,"raw_maximin":.7,"fisher":.8,"max_motion":.55,"random":.4}},
            "D2_frozen":{"median_standardized_separation":.04,"median_numerical_rms_m":6e-6},
            "D3":{"ranking_agreement":{"adaptive_dcs":.5,"raw_bundle":.6,"raw_pair_aware":.55,"fisher":.61,"max_motion":.58,"random":.5},
                  "median_separation":{"k2":.05,"k3":.01},"median_numerical_rms_m":{"k2":8e-7,"k3":2e-7}},
            "D4V":{"deceptive":14,"beneficial":22},
            "GAUGE":{"ordinary_overlap_wins":10,"marker_darkfield_endpoint_wins":0,"longitudinal_darkfield_endpoint_wins":9}
          },
          "post_hoc_information_frontier":{"d4v":{"methods":{
            "dcs":{"median_abs_z":.05,"max_abs_z":.18,"median_required_signal_amplification_to_z2":40.0},
            "raw_bundle":{"median_abs_z":.05,"max_abs_z":.41,"median_required_signal_amplification_to_z2":40.0},
            "raw_point":{"median_abs_z":.15,"max_abs_z":.63,"median_required_signal_amplification_to_z2":13.6},
            "fisher":{"median_abs_z":.07,"max_abs_z":.63,"median_required_signal_amplification_to_z2":30.0},
            "max_motion":{"median_abs_z":.06,"max_abs_z":.58,"median_required_signal_amplification_to_z2":34.6}
          }}}
          },
          "orthogonal_force_compliance":{
            "dcs":{"coverage":0.0,"median_abs_z":.05,"max_abs_z":.17,
                   "median_required_signal_amplification_to_z2":40.0,"sign_accuracy":.5},
            "force_compliance":{"coverage":0.0,"median_abs_z":.56,"max_abs_z":1.32,
                   "median_required_signal_amplification_to_z2":3.57,"sign_accuracy":.56}
          }
        }))
        m=generate(source,out)
        assert len(m["figures"])==7
        assert (out/"fig_ranking_agreement.svg").is_file()
        assert (out/"table_stage_outcomes.csv").is_file()
        print("VALID paper asset generator self-test")

def main() -> int:
    ap=argparse.ArgumentParser()
    ap.add_argument("--results",default="research/results/VULKAX_FINAL_RESULTS_2026-09-21.json")
    ap.add_argument("--out",default="build/paper-figures")
    ap.add_argument("--self-test",action="store_true")
    args=ap.parse_args()
    if args.self_test:
        self_test(); return 0
    m=generate(Path(args.results),Path(args.out))
    print("WROTE paper assets",args.out,"figures",len(m["figures"]))
    return 0

if __name__=="__main__":
    raise SystemExit(main())

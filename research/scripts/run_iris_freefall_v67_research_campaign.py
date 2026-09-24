#!/usr/bin/env python3
"""One-shot retrospective IRIS free-fall V6.7 research campaign.

Runs only on already-spent development/failed-validation evidence.  It answers:
  1. Does V6.6 candidate generation contain a physically correct event at all?
  2. If yes, is the failure only ranking?
  3. Does higher image resolution repair candidate generation?
  4. Which simple target-free ranking policies generalize across spent drop_50
     and drop_100 evidence?

Target gravity is used ONLY for retrospective scoring/oracle diagnosis after the
candidate set is generated.  None of the candidate generators or named ranking
policies below use target g, known expected fall time, or target error.

Do not use drop_150 with this script.
"""
from __future__ import annotations

import argparse
import csv
import importlib.util
import json
import math
from pathlib import Path
from collections import defaultdict

import numpy as np

ROOT=Path(__file__).resolve().parents[2]
ANALYZER=ROOT/"research/analysis/run_iris_freefall_validation.py"
TRACK_DIAG=ROOT/"research/scripts/diagnose_iris_freefall_v66.py"
G=9.80665


def load(path,name):
    spec=importlib.util.spec_from_file_location(name,path)
    mod=importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


def all_candidates_v66(mod,tracks,fps,cfg):
    minimum_interval_frames=int(cfg.get("minimum_active_frames",12))
    chunks=mod._identity_track_chunks(tracks,fps,cfg)
    out=[]
    for chunk in chunks:
        sign=float(cfg.get("expected_image_gravity_sign",1.0))
        z=sign*np.asarray(chunk["y"],float)
        coarse=mod._causal_median_v66(z,3)
        scale=max(float(np.ptp(coarse)),1.0)
        progress=(coarse-float(np.min(coarse)))/scale
        for run_id,(aa,bb) in enumerate(mod._causal_monotone_runs_v66(progress)):
            upper=min(len(z)-minimum_interval_frames,aa+7)
            starts=range(max(0,aa-3),upper)
            end_cap=min(len(z)-1,bb+12)
            for s in starts:
                for e in range(s+minimum_interval_frames,end_cap+1):
                    q=mod._evaluate_prefix_v66(chunk,s,e,fps,cfg)
                    if q is not None:
                        q=dict(q)
                        q["run_id"]=int(run_id)
                        q["start_frame"]=int(chunk["frames"][s])
                        q["end_frame"]=int(chunk["frames"][e-1])
                        q["chunk_id"]=int(chunk["chunk_id"])
                        out.append(q)
                        break
    return out


def current_rank(q):
    return (
        float(q["trajectory_shape_rms_fraction"]),
        float(q.get("acceleration_stability",0.0)),
        abs(float(q.get("release_phase",0.0))),
        float(q["x_drift_fraction"]),
        -float(q.get("impact_persistent_samples",0)),
        -float(q["identity_score"]),
        -float(q["detected_window_fraction"]),
        -float(q["local_span_px"]),
    )


POLICIES={
    "v66_current":current_rank,
    "earliest_release":lambda q:(
        float(q["t0_s"]),
        float(q["trajectory_shape_rms_fraction"]),
        float(q.get("acceleration_stability",0.0)),
    ),
    "earliest_impact":lambda q:(
        int(q.get("impact_frame",10**9)),
        float(q["trajectory_shape_rms_fraction"]),
        float(q.get("acceleration_stability",0.0)),
    ),
    "longest_duration":lambda q:(
        -float(q["full_fall_time_s"]),
        float(q["trajectory_shape_rms_fraction"]),
        float(q.get("acceleration_stability",0.0)),
    ),
    "largest_span":lambda q:(
        -float(q["local_span_px"]),
        float(q["trajectory_shape_rms_fraction"]),
        float(q.get("acceleration_stability",0.0)),
    ),
    "release_then_duration":lambda q:(
        abs(float(q.get("release_phase",0.0))),
        -float(q["full_fall_time_s"]),
        float(q["trajectory_shape_rms_fraction"]),
    ),
    "identity_then_duration":lambda q:(
        -float(q["identity_score"]),
        -float(q["full_fall_time_s"]),
        float(q["trajectory_shape_rms_fraction"]),
    ),
    "terminal_strength":lambda q:(
        -int(q.get("impact_persistent_samples",0)),
        -float(q.get("impact_innovation_sigma",0.0)),
        float(q["trajectory_shape_rms_fraction"]),
    ),
    "earliest_start":lambda q:(
        int(q.get("start_frame",10**9)),
        float(q["trajectory_shape_rms_fraction"]),
        float(q.get("acceleration_stability",0.0)),
    ),
}


def eval_candidate(q,height):
    T=float(q["full_fall_time_s"])
    g=2.0*height/(T*T)
    return g,abs(g-G)/G


def qsummary(q,height):
    g,err=eval_candidate(q,height)
    return {
        "track":int(q["track_id"]),
        "chunk":int(q.get("chunk_id",-1)),
        "start":int(q.get("start_frame",-1)),
        "impact":int(q.get("impact_frame",-1)),
        "T":float(q["full_fall_time_s"]),
        "g":g,
        "err":err,
        "shape":float(q["trajectory_shape_rms_fraction"]),
        "identity":float(q["identity_score"]),
        "span":float(q["local_span_px"]),
        "release_phase":float(q.get("release_phase",0.0)),
    }


def med(vals):
    vals=[float(x) for x in vals if math.isfinite(float(x))]
    return float(np.median(vals)) if vals else float("nan")


def run_take(mod,diag,video,cfg,height,width):
    cc=dict(cfg);cc["analysis_width"]=int(width)
    fps,tracks=diag.build_tracks(mod,video,cc)
    cand=all_candidates_v66(mod,tracks,fps,cc)
    if not cand:
        return {
            "candidate_count":0,"raw_tracks":len(tracks),
            "policy":{},"oracle":None,
        }

    oracle=min(cand,key=lambda q:eval_candidate(q,height)[1])
    selected={}
    for name,key in POLICIES.items():
        q=min(cand,key=key)
        selected[name]=qsummary(q,height)
    return {
        "candidate_count":len(cand),
        "raw_tracks":len(tracks),
        "policy":selected,
        "oracle":qsummary(oracle,height),
    }


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument(
        "--config",type=Path,
        default=ROOT/"research/validation/iris_freefall_rescue_v6.json"
    )
    ap.add_argument("--data-root",type=Path,required=True)
    ap.add_argument("--widths",type=int,nargs="+",default=[640,960,1280])
    ap.add_argument("--out-dir",type=Path,default=Path("build/v67-campaign"))
    args=ap.parse_args()

    mod=load(ANALYZER,"iris_v67_campaign")
    diag=load(TRACK_DIAG,"iris_v67_campaign_diag")
    config=json.loads(args.config.read_text())
    base_cfg=dict(config["tracker"])
    specs=[
        ("development",config["dataset"]["development"]),
        ("validation",config["dataset"]["validation"]),
    ]

    rows=[]
    detailed={}
    print("="*100)
    print("V6.7 ONE-SHOT RETROSPECTIVE CAMPAIGN")
    print("drop_50 + failed drop_100 only; drop_150 is forbidden here")
    print("Target g is used only after candidate generation for retrospective scoring.")
    print("="*100)

    for width in args.widths:
        print(f"\n--- ANALYSIS WIDTH {width} ---")
        for split,spec in specs:
            setting=spec["setting"]
            height=float(config["physics"]["drop_heights_m"][setting])
            for take in spec["takes"]:
                video=args.data_root/"iris"/"Dropping_ball"/setting/f"{take}.mp4"
                key=f"{width}:{split}:{setting}:{take}"
                if not video.is_file():
                    print(f"{setting}/{take} MISSING {video}")
                    continue
                try:
                    r=run_take(mod,diag,video,base_cfg,height,width)
                except Exception as exc:
                    print(f"{setting}/{take} ERROR {type(exc).__name__}: {exc}")
                    continue
                detailed[key]=r
                oracle=r["oracle"]
                cur=r["policy"].get("v66_current")
                if oracle is None:
                    print(
                        f"{setting}/{take} candidates=0 tracks={r['raw_tracks']} "
                        "NO_VALID_CANDIDATE"
                    )
                    continue
                gap=(cur["err"]-oracle["err"]) if cur else float("nan")
                print(
                    f"{setting}/{take} cand={r['candidate_count']:3d} "
                    f"CURRENT err={cur['err']:.3f} T={cur['T']:.3f} "
                    f"ORACLE err={oracle['err']:.3f} T={oracle['T']:.3f} "
                    f"oracle_frames={oracle['start']}->{oracle['impact']} "
                    f"rank_gap={gap:.3f}"
                )
                for pname,p in r["policy"].items():
                    rows.append({
                        "width":width,"split":split,"setting":setting,"take":take,
                        "policy":pname,"candidate_count":r["candidate_count"],
                        **p,
                    })
                rows.append({
                    "width":width,"split":split,"setting":setting,"take":take,
                    "policy":"ORACLE_RETROSPECTIVE_ONLY",
                    "candidate_count":r["candidate_count"],**oracle,
                })

    # Aggregate fixed policies by width and split.
    groups=defaultdict(list)
    for row in rows:
        groups[(int(row["width"]),row["policy"],row["split"])].append(row)

    print("\n"+"="*100)
    print("POLICY SUMMARY")
    print("="*100)
    table=[]
    all_policies=list(POLICIES)+["ORACLE_RETROSPECTIVE_ONLY"]
    for width in args.widths:
        for pname in all_policies:
            dev=groups.get((width,pname,"development"),[])
            val=groups.get((width,pname,"validation"),[])
            if not dev and not val:
                continue
            dmed=med([r["err"] for r in dev])
            vmed=med([r["err"] for r in val])
            dmax=max([r["err"] for r in dev],default=float("nan"))
            vmax=max([r["err"] for r in val],default=float("nan"))
            rec={
                "width":width,"policy":pname,
                "dev_n":len(dev),"dev_median_err":dmed,"dev_max_err":dmax,
                "val_n":len(val),"val_median_err":vmed,"val_max_err":vmax,
            }
            table.append(rec)
            print(
                f"w={width:4d} {pname:28s} "
                f"DEV n={len(dev)} med={dmed:.3f} max={dmax:.3f} | "
                f"VAL n={len(val)} med={vmed:.3f} max={vmax:.3f}"
            )

    # Diagnose whether ranking alone can possibly rescue the candidate generator.
    print("\n"+"="*100)
    print("DECISION")
    print("="*100)
    oracle_rows=[r for r in table if r["policy"]=="ORACLE_RETROSPECTIVE_ONLY"]
    if oracle_rows:
        best_oracle=min(
            oracle_rows,
            key=lambda r:(
                float("inf") if math.isnan(r["val_median_err"]) else r["val_median_err"],
                float("inf") if math.isnan(r["dev_median_err"]) else r["dev_median_err"],
            )
        )
        print(
            "BEST_ORACLE_WIDTH",
            best_oracle["width"],
            "DEV_MEDIAN",best_oracle["dev_median_err"],
            "VAL_MEDIAN",best_oracle["val_median_err"],
            "VAL_MAX",best_oracle["val_max_err"],
        )
        if (
            best_oracle["val_n"]>=4
            and best_oracle["val_median_err"]<=0.20
        ):
            print(
                "DIAGNOSIS ranking_or_candidate_choice_problem: "
                "candidate generation contains usable events; inspect target-free policies."
            )
        else:
            print(
                "DIAGNOSIS candidate_generation_or_projection_problem: "
                "even retrospective oracle candidates do not meet the 20% median validation criterion."
            )

    # Find simple fixed policy with best cross-setting worst median.  This is
    # retrospective model development only, never confirmatory evidence.
    fixed=[r for r in table if r["policy"]!="ORACLE_RETROSPECTIVE_ONLY"
           and r["dev_n"]>=4 and r["val_n"]>=4]
    if fixed:
        best=min(
            fixed,
            key=lambda r:(
                max(r["dev_median_err"],r["val_median_err"]),
                r["val_median_err"],
                r["dev_median_err"],
            )
        )
        print(
            "BEST_FIXED_TARGET_FREE_POLICY",
            best["policy"],"width",best["width"],
            "DEV_MEDIAN",best["dev_median_err"],
            "VAL_MEDIAN",best["val_median_err"],
            "DEV_MAX",best["dev_max_err"],
            "VAL_MAX",best["val_max_err"],
        )
        if best["dev_median_err"]<=.20 and best["val_median_err"]<=.20:
            print(
                "POLICY_SIGNAL promising retrospective selector; "
                "freeze only after inspecting per-take failure modes and then validate on NEW evidence."
            )
        else:
            print(
                "POLICY_SIGNAL no simple target-free ranking rule solves both spent settings."
            )

    args.out_dir.mkdir(parents=True,exist_ok=True)
    if rows:
        fields=sorted({k for r in rows for k in r})
        with (args.out_dir/"candidate_policy_rows.csv").open(
            "w",newline="",encoding="utf-8"
        ) as f:
            w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(rows)
    (args.out_dir/"policy_summary.json").write_text(
        json.dumps(table,indent=2,sort_keys=True)+"\n",encoding="utf-8"
    )
    (args.out_dir/"details.json").write_text(
        json.dumps(detailed,indent=2,sort_keys=True)+"\n",encoding="utf-8"
    )
    print("OUT",args.out_dir)
    return 0


if __name__=="__main__":
    raise SystemExit(main())

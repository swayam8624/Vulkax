#!/usr/bin/env python3
"""Retrospective V6.7 diagnostic: stitch fragmented ball tracks, then replay V6.6.

Purpose
-------
Test the hypothesis exposed by failed prospective V6.6 validation: longer falls
are split into multiple temporal tracks, so the unchanged V6.6 selector sees only
a late partial flight and incorrectly binds the full known height to it.

Stitch proposal is target-free.  Edges use only:
  - short temporal gap,
  - image-space position/velocity continuity,
  - expected gravity direction,
  - component size continuity.

No target g, expected fall time, drop height, or validation outcome enters edge
construction or V6.6 selection.  Ground-truth quantities are printed only after
selection for retrospective diagnosis.

This script is diagnostic only.  drop_100/06..10 is permanently retrospective.
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import math
from pathlib import Path

import numpy as np

ROOT=Path(__file__).resolve().parents[2]
ANALYZER=ROOT/"research/analysis/run_iris_freefall_validation.py"
TRACK_DIAG=ROOT/"research/scripts/diagnose_iris_freefall_v66.py"
G=9.80665


def _load(path,name):
    spec=importlib.util.spec_from_file_location(name,path)
    mod=importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(mod)
    return mod


def _endpoint_velocity(pts,tail=True):
    q=pts[-3:] if tail else pts[:3]
    if len(q)<2:
        return 0.0,0.0
    a=q[0];b=q[-1]
    df=max(1,int(b["frame"])-int(a["frame"]))
    return (
        (float(b["x"])-float(a["x"]))/df,
        (float(b["y"])-float(a["y"]))/df,
    )


def _ratio(a,b):
    a=max(float(a),1e-6);b=max(float(b),1e-6)
    return max(a,b)/min(a,b)


def _edge(a,b,sign,max_gap=5):
    """Return target-free stitch cost or None."""
    ap=a["pts"];bp=b["pts"]
    if len(ap)<4 or len(bp)<4:
        return None
    last=ap[-1];first=bp[0]
    gap=int(first["frame"])-int(last["frame"])
    if gap<1 or gap>max_gap:
        return None

    vx,vy=_endpoint_velocity(ap,tail=True)
    bx,by=_endpoint_velocity(bp,tail=False)
    predx=float(last["x"])+vx*gap
    predy=float(last["y"])+vy*gap
    dx=float(first["x"])-predx
    dy=float(first["y"])-predy
    pred_err=math.hypot(dx,dy)

    # A new fragment may begin slightly above the last centroid because component
    # extraction jitters, but a large opposite-gravity jump is not a continuation.
    signed_step=sign*(float(first["y"])-float(last["y"]))
    if signed_step < -12.0:
        return None

    speed=math.hypot(vx,vy)
    gate=max(24.0,12.0*gap+3.0*speed*gap)
    if pred_err>gate:
        return None

    xjump=abs(float(first["x"])-float(last["x"]))
    if xjump>max(38.0,2.0*abs(vx)*gap+18.0):
        return None

    area_ratio=_ratio(first.get("area",1.0),last.get("area",1.0))
    radius_ratio=_ratio(first.get("radius",1.0),last.get("radius",1.0))
    if area_ratio>4.5 or radius_ratio>3.0:
        return None

    # Directional velocity may grow under acceleration; only reject an obvious
    # reversal at the join.  No expected acceleration magnitude is imposed.
    if sign*vy>1.0 and sign*by < -max(2.0,.75*abs(vy)):
        return None

    cost=(
        pred_err/max(gap,1)
        +.35*xjump/max(gap,1)
        +3.0*abs(math.log(area_ratio))
        +2.0*abs(math.log(radius_ratio))
    )
    return float(cost)


def _candidate_chains(tracks,sign,max_gap=5,max_nodes=4,max_out=3):
    tracks=sorted(tracks,key=lambda t:(t["pts"][0]["frame"],t["pts"][-1]["frame"],t["id"]))
    by_id={int(t["id"]):t for t in tracks}
    outgoing={int(t["id"]):[] for t in tracks}
    for i,a in enumerate(tracks):
        aend=int(a["pts"][-1]["frame"])
        for b in tracks[i+1:]:
            bstart=int(b["pts"][0]["frame"])
            if bstart-aend>max_gap:
                break
            cost=_edge(a,b,sign,max_gap=max_gap)
            if cost is not None:
                outgoing[int(a["id"])].append((cost,int(b["id"])))
        outgoing[int(a["id"])].sort()
        outgoing[int(a["id"])]=outgoing[int(a["id"])][:max_out]

    chains=set()
    def dfs(path):
        if len(path)>=2:
            chains.add(tuple(path))
        if len(path)>=max_nodes:
            return
        for _cost,nxt in outgoing.get(path[-1],[]):
            if nxt in path:
                continue
            dfs(path+[nxt])
    for t in tracks:
        dfs([int(t["id"])])

    # Prefer chains with more observations and larger gravity-direction span;
    # this only limits diagnostic combinatorics, not V6.6 ranking.
    def prescore(chain):
        pts=[]
        for tid in chain:pts.extend(by_id[tid]["pts"])
        pts=sorted(pts,key=lambda p:p["frame"])
        z=[sign*float(p["y"]) for p in pts]
        return (-len(pts),-max(0.0,max(z)-min(z)),len(chain))
    return sorted(chains,key=prescore)[:250],by_id,outgoing


def _merge_chain(chain,by_id,new_id):
    seen={}
    for tid in chain:
        for p in by_id[tid]["pts"]:
            ff=int(p["frame"])
            # If two fragments overlap unexpectedly, keep the stronger appearance.
            old=seen.get(ff)
            if old is None or float(p.get("appearance_score",0.0))>float(old.get("appearance_score",0.0)):
                seen[ff]=dict(p)
    pts=[seen[k] for k in sorted(seen)]
    return {"id":int(new_id),"pts":pts,"missed":0,"source_track_ids":list(chain)}


def _run_selector(mod,tracks,fps,cfg):
    chosen,audit=mod.choose_ballistic_track_v66(
        tracks,fps,
        minimum_interval_frames=int(cfg.get("minimum_active_frames",12)),
        identity_cfg=cfg,
    )
    return chosen,audit


def diagnose_take(mod,diag,video,cfg,height):
    fps,tracks=diag.build_tracks(mod,video,cfg)
    baseline=None
    try:
        baseline,_=_run_selector(mod,tracks,fps,cfg)
    except Exception:
        baseline=None

    sign=float(cfg.get("expected_image_gravity_sign",1.0))
    chains,by_id,outgoing=_candidate_chains(
        tracks,sign,
        max_gap=int(cfg.get("v67_stitch_max_gap_frames",5)),
        max_nodes=int(cfg.get("v67_stitch_max_nodes",4)),
        max_out=int(cfg.get("v67_stitch_max_outgoing",3)),
    )
    merged=[]
    chain_map={}
    base_id=100000
    for k,chain in enumerate(chains):
        q=_merge_chain(chain,by_id,base_id+k)
        if len(q["pts"])<6:
            continue
        merged.append(q);chain_map[int(q["id"])]=list(chain)

    chosen,audit=_run_selector(mod,tracks+merged,fps,cfg)
    tid=int(chosen["track_id"])
    source=chain_map.get(tid,[tid])
    T=float(chosen["full_fall_time_s"])
    truth_T=math.sqrt(2.0*height/G)
    g_eval=2.0*height/(T*T)
    out={
        "fps":fps,
        "raw_tracks":len(tracks),
        "proposed_chains":len(chains),
        "merged_tracks":len(merged),
        "selected_track_id":tid,
        "selected_source_tracks":source,
        "selected_is_stitched":tid in chain_map,
        "selected_T_s":T,
        "truth_T_s":truth_T,
        "T_fraction_truth":T/truth_T,
        "g_eval_m_s2":g_eval,
        "g_relative_error":abs(g_eval-G)/G,
        "selected_interval_frames":int(chosen["interval_frames"]),
        "selected_inferred_frames":float(chosen["inferred_full_fall_frames"]),
        "selected_shape":float(chosen["trajectory_shape_rms_fraction"]),
        "selected_identity":float(chosen["identity_score"]),
        "selected_impact_frame":int(chosen.get("impact_frame",-1)),
    }
    if baseline is not None:
        bT=float(baseline["full_fall_time_s"])
        out.update({
            "baseline_T_s":bT,
            "baseline_T_fraction_truth":bT/truth_T,
            "baseline_g_relative_error":abs(2.0*height/(bT*bT)-G)/G,
            "baseline_track_id":int(baseline["track_id"]),
        })
    return out


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument(
        "--config",type=Path,
        default=ROOT/"research/validation/iris_freefall_rescue_v6.json"
    )
    ap.add_argument("--data-root",type=Path,required=True)
    ap.add_argument("--split",choices=("development","validation"),default="validation")
    ap.add_argument("--take",action="append")
    args=ap.parse_args()

    mod=_load(ANALYZER,"iris_ff_v67_stitch")
    diag=_load(TRACK_DIAG,"iris_ff_v67_stitch_tracks")
    config=json.loads(args.config.read_text())
    cfg=dict(config["tracker"])
    spec=config["dataset"][args.split]
    setting=spec["setting"]
    height=float(config["physics"]["drop_heights_m"][setting])
    takes=args.take or list(spec["takes"])

    rows=[]
    print(
        "RETROSPECTIVE_ONLY V6.7 TRACK-STITCH DIAGNOSIS -- "
        "stitching/selection use no target g or expected duration"
    )
    for take in takes:
        video=args.data_root/"iris"/"Dropping_ball"/setting/f"{take}.mp4"
        try:
            r=diagnose_take(mod,diag,video,cfg,height)
            r["take"]=take;rows.append(r)
            print(
                f"{setting}/{take} raw={r['raw_tracks']} chains={r['proposed_chains']} "
                f"selected={'STITCH' if r['selected_is_stitched'] else 'RAW'}"
                f"{r['selected_source_tracks']} "
                f"T={r['selected_T_s']:.4f}s "
                f"T/Ttruth={r['T_fraction_truth']:.3f} "
                f"frames={r['selected_inferred_frames']:.2f} "
                f"shape={r['selected_shape']:.4f} "
                f"gerr={r['g_relative_error']:.3f}"
                +(
                    f" baseline_T/Ttruth={r['baseline_T_fraction_truth']:.3f} "
                    f"baseline_gerr={r['baseline_g_relative_error']:.3f}"
                    if "baseline_T_fraction_truth" in r else
                    " baseline=NO_EVENT"
                )
            )
        except Exception as exc:
            print(f"{setting}/{take} FAIL {type(exc).__name__}: {exc}")

    if rows:
        print("SUMMARY",json.dumps({
            "takes":len(rows),
            "stitched_selected":sum(bool(r["selected_is_stitched"]) for r in rows),
            "median_T_fraction_truth":float(np.median([
                r["T_fraction_truth"] for r in rows
            ])),
            "median_g_relative_error":float(np.median([
                r["g_relative_error"] for r in rows
            ])),
            "max_g_relative_error":float(np.max([
                r["g_relative_error"] for r in rows
            ])),
        },sort_keys=True))
    return 0


if __name__=="__main__":
    raise SystemExit(main())

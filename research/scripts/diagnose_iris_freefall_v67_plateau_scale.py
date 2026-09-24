#!/usr/bin/env python3
"""Retrospective endpoint-anchor scale diagnosis for IRIS free-fall V6.7.

V6.6 event timing is replayed unchanged.  This diagnostic asks whether stationary
release/impact plateaus can provide the missing metric scale:

    g_hat = c_px * (known_drop_height_m / anchored_pixel_span)

where c_px is the quadratic image-space curvature of the V6.6-selected event.

The already-opened drop_50/drop_100 takes are retrospective only.  Ground-truth g
is printed only for diagnosis and never used to choose plateaus or event timing.
"""
from __future__ import annotations

import argparse
import csv
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


def causal_median(x,width=3):
    q=np.asarray(x,float);out=np.empty_like(q)
    for i in range(len(q)):
        out[i]=float(np.median(q[max(0,i-width+1):i+1]))
    return out


def plateau_segments(chunks,cfg):
    """Find stationary identity-consistent plateaus with no future smoothing."""
    vmax=float(cfg.get("plateau_max_speed_px_per_frame",0.75))
    nmin=int(cfg.get("plateau_min_frames",4))
    out=[]
    for q in chunks:
        frames=np.asarray(q["frames"],int)
        y=causal_median(np.asarray(q["y"],float),3)
        x=causal_median(np.asarray(q["x"],float),3)
        if len(frames)<nmin:
            continue
        # Dense chunks are consecutive after interpolation.  Mark a sample
        # stationary only when both adjacent causal displacements are small.
        dy=np.zeros(len(y),float)
        if len(y)>1:
            dy[1:]=np.abs(np.diff(y))
            dy[0]=dy[1]
        dx=np.zeros(len(x),float)
        if len(x)>1:
            dx[1:]=np.abs(np.diff(x))
            dx[0]=dx[1]
        stationary=(dy<=vmax)&(dx<=max(1.5*vmax,1.0))
        i=0
        while i<len(stationary):
            if not stationary[i]:
                i+=1;continue
            a=i
            while i+1<len(stationary) and stationary[i+1]:
                i+=1
            b=i+1
            if b-a>=nmin:
                out.append({
                    "track_id":int(q["track_id"]),
                    "chunk_id":int(q["chunk_id"]),
                    "frame_start":int(frames[a]),
                    "frame_end":int(frames[b-1]),
                    "frames":int(frames[b-1]-frames[a]+1),
                    "x":float(np.median(x[a:b])),
                    "y":float(np.median(y[a:b])),
                    "identity":float(q["identity_score"]),
                })
            i+=1
    return out


def choose_anchor_pair(plateaus,chosen,chunk,cfg):
    """Choose anchors from event-relative geometry only; no g or expected T."""
    sign=float(cfg.get("expected_image_gravity_sign",1.0))
    fps_frames=np.asarray(chunk["frames"],int)
    z=sign*np.asarray(chunk["y"],float)
    ids=np.asarray(chosen["window_indices"],int)
    event_frames=fps_frames[ids]
    event_x=np.asarray(chunk["x"],float)[ids]
    event_z=z[ids]
    release_frame=float(chosen["t0_s"])*float(cfg["_fps"])
    impact_frame=int(event_frames[-1])
    xm=float(np.median(event_x))
    max_gap=int(cfg.get("plateau_max_gap_frames",24))
    max_x=float(cfg.get("plateau_max_x_delta_px",45.0))
    overlap=int(cfg.get("plateau_overlap_frames",6))

    prior=[]
    post=[]
    for p in plateaus:
        pz=sign*float(p["y"])
        if (
            p["frame_end"]<=release_frame+overlap
            and release_frame-p["frame_end"]<=max_gap
            and abs(float(p["x"])-xm)<=max_x
            and pz<=float(event_z[0])+12.0
        ):
            q=dict(p);q["z"]=pz
            prior.append(q)
        if (
            p["frame_start"]>=impact_frame-overlap
            and p["frame_start"]-impact_frame<=max_gap
            and abs(float(p["x"])-xm)<=max_x
            and pz>=float(event_z[-1])-12.0
        ):
            q=dict(p);q["z"]=pz
            post.append(q)

    pairs=[]
    for top in prior:
        for bottom in post:
            span=float(bottom["z"]-top["z"])
            if not math.isfinite(span) or span<18.0:
                continue
            pre_gap=max(0.0,release_frame-float(top["frame_end"]))
            post_gap=max(0.0,float(bottom["frame_start"])-impact_frame)
            x_cost=abs(float(top["x"])-xm)+abs(float(bottom["x"])-xm)
            # Rank only by temporal adjacency, x continuity, identity, span.
            rank=(
                pre_gap+post_gap,
                x_cost,
                -min(float(top["identity"]),float(bottom["identity"])),
                -span,
            )
            pairs.append((rank,top,bottom,span))
    pairs.sort(key=lambda q:q[0])
    return (pairs[0] if pairs else None),len(prior),len(post),len(pairs)


def diagnose_take(analyzer,diag,video,cfg,height):
    fps,tracks=diag.build_tracks(analyzer,video,cfg)
    chunks=analyzer._identity_track_chunks(tracks,fps,cfg)
    chosen,_audit=analyzer.choose_ballistic_track_v66(
        tracks,fps,
        minimum_interval_frames=int(cfg.get("minimum_active_frames",12)),
        identity_cfg=cfg,
    )
    chunk=next(
        q for q in chunks if int(q["chunk_id"])==int(chosen["chunk_id"])
    )
    frames=np.asarray(chunk["frames"],int)
    sign=float(cfg.get("expected_image_gravity_sign",1.0))
    z=sign*np.asarray(chunk["y"],float)
    ids=np.asarray(chosen["window_indices"],int)
    fr=frames[ids];zz=z[ids]
    _t,_a,_b,c,_pred=analyzer._quad_fit_v66(fr,zz,fps)

    pcfg=dict(cfg);pcfg["_fps"]=fps
    plats=plateau_segments(chunks,pcfg)
    picked,nprior,npost,npairs=choose_anchor_pair(plats,chosen,chunk,pcfg)

    row={
        "fps":fps,
        "track_id":int(chosen["track_id"]),
        "chunk_id":int(chosen["chunk_id"]),
        "event_frame_start":int(fr[0]),
        "event_frame_end":int(fr[-1]),
        "event_T_s":float(chosen["full_fall_time_s"]),
        "event_range_px":float(np.ptp(zz)),
        "curvature_px_s2":float(c),
        "plateau_count":len(plats),
        "prior_plateaus":nprior,
        "post_plateaus":npost,
        "anchor_pairs":npairs,
    }
    if picked is None:
        row["anchor_status"]="none"
        return row

    _rank,top,bottom,span=picked
    scale=height/span
    g_anchor=float(c)*scale
    row.update({
        "anchor_status":"selected",
        "release_anchor_track":top["track_id"],
        "release_anchor_chunk":top["chunk_id"],
        "release_anchor_frames":f"{top['frame_start']}-{top['frame_end']}",
        "release_anchor_y_px":top["y"],
        "release_anchor_x_px":top["x"],
        "impact_anchor_track":bottom["track_id"],
        "impact_anchor_chunk":bottom["chunk_id"],
        "impact_anchor_frames":f"{bottom['frame_start']}-{bottom['frame_end']}",
        "impact_anchor_y_px":bottom["y"],
        "impact_anchor_x_px":bottom["x"],
        "anchor_span_px":span,
        "event_fraction_of_anchor_span":float(np.ptp(zz))/span,
        "metric_scale_m_per_px":scale,
        "g_anchor_m_s2":g_anchor,
        "g_anchor_relative_error":abs(g_anchor-G)/G,
    })
    return row


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument(
        "--config",type=Path,
        default=ROOT/"research/validation/iris_freefall_rescue_v6.json"
    )
    ap.add_argument("--data-root",type=Path,required=True)
    ap.add_argument("--split",choices=("development","validation"),default="validation")
    ap.add_argument("--take",action="append")
    ap.add_argument("--out",type=Path)
    args=ap.parse_args()

    analyzer=_load(ANALYZER,"iris_ff_v67_anchor")
    diag=_load(TRACK_DIAG,"iris_ff_v67_track")
    config=json.loads(args.config.read_text())
    cfg=config["tracker"]
    spec=config["dataset"][args.split]
    setting=spec["setting"]
    height=float(config["physics"]["drop_heights_m"][setting])
    takes=args.take or list(spec["takes"])

    rows=[]
    print(
        "RETROSPECTIVE_ONLY V6.7 ENDPOINT-ANCHOR SCALE DIAGNOSIS -- "
        "anchor selection does not use target g"
    )
    for take in takes:
        video=args.data_root/"iris"/"Dropping_ball"/setting/f"{take}.mp4"
        row={"split":args.split,"setting":setting,"take":take,"height_m":height}
        try:
            row.update(diagnose_take(analyzer,diag,video,cfg,height))
            row["status"]="ok"
        except Exception as exc:
            row["status"]="no_event"
            row["error"]=f"{type(exc).__name__}: {exc}"
        rows.append(row)
        if row["status"]!="ok":
            print(f"{setting}/{take} EVENT_FAIL {row.get('error','')}")
        elif row.get("anchor_status")!="selected":
            print(
                f"{setting}/{take} ANCHOR_FAIL plateaus={row.get('plateau_count',0)} "
                f"prior={row.get('prior_plateaus',0)} post={row.get('post_plateaus',0)}"
            )
        else:
            print(
                f"{setting}/{take} "
                f"event={row['event_frame_start']}-{row['event_frame_end']} "
                f"anchors={row['release_anchor_frames']}->{row['impact_anchor_frames']} "
                f"span={row['anchor_span_px']:.2f}px "
                f"event/span={row['event_fraction_of_anchor_span']:.3f} "
                f"c={row['curvature_px_s2']:.2f}px/s2 "
                f"g_anchor={row['g_anchor_m_s2']:.3f} "
                f"relerr={row['g_anchor_relative_error']:.3f}"
            )

    anchored=[r for r in rows if r.get("anchor_status")=="selected"]
    if anchored:
        vals=[float(r["g_anchor_relative_error"]) for r in anchored]
        print(
            "SUMMARY",
            json.dumps({
                "events":sum(r["status"]=="ok" for r in rows),
                "anchored":len(anchored),
                "median_anchor_g_relative_error":float(np.median(vals)),
                "max_anchor_g_relative_error":float(np.max(vals)),
                "median_event_fraction_of_anchor_span":float(np.median([
                    float(r["event_fraction_of_anchor_span"]) for r in anchored
                ])),
            },sort_keys=True)
        )

    if args.out:
        args.out.parent.mkdir(parents=True,exist_ok=True)
        fields=sorted({k for r in rows for k in r})
        with args.out.open("w",newline="",encoding="utf-8") as f:
            w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(rows)
        print("OUT",args.out)
    return 0


if __name__=="__main__":
    raise SystemExit(main())

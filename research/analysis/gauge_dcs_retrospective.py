#!/usr/bin/env python3
"""Retrospective DCS analysis of the already-known GAUGE fixture metric mirage.

Discovery-only: the finite-support failure is already known and therefore cannot
serve as confirmatory evidence for DCS. No material parameters are fit.
"""
import argparse,csv,json,math,pathlib,statistics,tempfile

from gauge_fixture_overlap_heldout import (
    derive_overlap, unique_faces, write_inputs, run_arm, read_pred,
    measured_frames, arm_metrics, v3, sub, norm
)

def dot(a,b): return sum(x*y for x,y in zip(a,b))

def response_vector(frames,ids,frame):
    out=[]
    for mid in ids:
        out.extend(frames[frame][mid])
    return out

def second_difference(v0,vm,v1):
    return [v1[i]-2.0*vm[i]+v0[i] for i in range(len(v0))]

def rms_diff(a,b):
    return math.sqrt(statistics.fmean((x-y)*(x-y) for x,y in zip(a,b)))

def longitudinal_strain(frames,ids,faces,frame):
    support_count=max(max(f) for f in faces)+1
    topo_ids=ids[:support_count]
    half=support_count//2
    rows=(list(range(half)),list(range(half,support_count)))
    rest=[frames[0][m] for m in topo_ids]
    pts=[frames[frame][m] for m in topo_ids]
    strains=[]
    for row in rows:
        for a,b in zip(row[:-1],row[1:]):
            l0=norm(sub(rest[b],rest[a]))
            l1=norm(sub(pts[b],pts[a]))
            strains.append(l1/max(l0,1e-15)-1.0)
    return statistics.fmean(strains)

def select_mid_frame(d,n):
    base=d["base"]
    b0=v3(base,0)
    final=sub(v3(base,n-1),b0)
    denom=dot(final,final)
    if denom<=1e-14:
        raise RuntimeError("GAUGE DCS driver final displacement is too small")
    final_norm=math.sqrt(denom)
    candidates=[]
    maximum_orth=0.0
    for frame in range(n):
        disp=sub(v3(base,frame),b0)
        progress=dot(disp,final)/denom
        projected=[progress*x for x in final]
        orth=norm(sub(disp,projected))/final_norm
        maximum_orth=max(maximum_orth,orth)
        if 0<frame<n-1:
            candidates.append((abs(progress-0.5),frame,progress))
    _,mid,mid_progress=min(candidates)
    return mid,mid_progress,maximum_orth

def darkfield_errors(measured,pred,ids,faces,n,mid):
    mv=[response_vector(measured,ids,f) for f in (0,mid,n-1)]
    pv=[response_vector(pred,ids,f) for f in (0,mid,n-1)]
    measured_marker=second_difference(*mv)
    predicted_marker=second_difference(*pv)
    marker_error=rms_diff(measured_marker,predicted_marker)

    ms=[longitudinal_strain(measured,ids,faces,f) for f in (0,mid,n-1)]
    ps=[longitudinal_strain(pred,ids,faces,f) for f in (0,mid,n-1)]
    measured_long=ms[2]-2.0*ms[1]+ms[0]
    predicted_long=ps[2]-2.0*ps[1]+ps[0]
    return {
        "marker_second_difference_error_m":marker_error,
        "measured_longitudinal_second_difference":measured_long,
        "predicted_longitudinal_second_difference":predicted_long,
        "longitudinal_second_difference_abs_error":abs(predicted_long-measured_long),
    }

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--root",required=True)
    ap.add_argument("--exe",required=True)
    ap.add_argument("--effective-span-dir",required=True)
    ap.add_argument("--out",required=True)
    a=ap.parse_args()

    root=pathlib.Path(a.root)
    out=pathlib.Path(a.out); out.mkdir(parents=True,exist_ok=True)
    validation,effective_span,overlap=derive_overlap(a.effective_span_dir)
    if overlap is None:
        raise RuntimeError("effective-span prerequisite failed")

    md=json.loads((root/"metadata"/"foam shearing.json").read_text())
    faces=unique_faces(md)
    rows=[]
    n_cross,n_long,dt=7,49,1.0/48000.0

    with tempfile.TemporaryDirectory(prefix="vulkax-gauge-dcs-") as t:
        td=pathlib.Path(t)
        for material in ("soft","hard"):
            mat=md["assets"]["foam"]["material"][material]
            for trial in (2,4,6,8,10):
                d=json.loads((root/"data"/"foam shearing"/material/f"{trial}.json").read_text())
                marker=td/f"{material}_{trial}_markers.csv"
                driver=td/f"{material}_{trial}_driver.csv"
                ids,n=write_inputs(d,marker,driver)
                mid,mid_progress,max_orth=select_mid_frame(d,n)

                endpoint_path=td/f"{material}_{trial}_endpoint.csv"
                overlap_path=td/f"{material}_{trial}_overlap.csv"
                run_arm(a.exe,marker,driver,endpoint_path,mat,dt,n_cross,n_long,
                        f"dcs-{material}-{trial}-endpoint")
                run_arm(a.exe,marker,driver,overlap_path,mat,dt,n_cross,n_long,
                        f"dcs-{material}-{trial}-overlap",overlap)

                measured=measured_frames(d,ids,n)
                endpoint=read_pred(endpoint_path)
                overlap_pred=read_pred(overlap_path)

                ordinary_endpoint=arm_metrics(d,endpoint,ids,faces,n)
                ordinary_overlap=arm_metrics(d,overlap_pred,ids,faces,n)
                dcs_endpoint=darkfield_errors(measured,endpoint,ids,faces,n,mid)
                dcs_overlap=darkfield_errors(measured,overlap_pred,ids,faces,n,mid)

                rows.append({
                    "material":material,
                    "trial":trial,
                    "mid_frame":mid,
                    "mid_progress":mid_progress,
                    "driver_max_orthogonal_fraction":max_orth,
                    "endpoint_face_nrmse":ordinary_endpoint["face_nrmse"],
                    "overlap_face_nrmse":ordinary_overlap["face_nrmse"],
                    "endpoint_marker_rmse_m":ordinary_endpoint["marker_rmse_m"],
                    "overlap_marker_rmse_m":ordinary_overlap["marker_rmse_m"],
                    "endpoint_dcs_marker_error_m":dcs_endpoint["marker_second_difference_error_m"],
                    "overlap_dcs_marker_error_m":dcs_overlap["marker_second_difference_error_m"],
                    "endpoint_dcs_long_error":dcs_endpoint["longitudinal_second_difference_abs_error"],
                    "overlap_dcs_long_error":dcs_overlap["longitudinal_second_difference_abs_error"],
                    "ordinary_dual_prefers_overlap":(
                        ordinary_overlap["face_nrmse"]<ordinary_endpoint["face_nrmse"] and
                        ordinary_overlap["marker_rmse_m"]<ordinary_endpoint["marker_rmse_m"]),
                    "dcs_marker_prefers_endpoint":(
                        dcs_endpoint["marker_second_difference_error_m"]<
                        dcs_overlap["marker_second_difference_error_m"]),
                    "dcs_long_prefers_endpoint":(
                        dcs_endpoint["longitudinal_second_difference_abs_error"]<
                        dcs_overlap["longitudinal_second_difference_abs_error"]),
                })

    with (out/"per_trial.csv").open("w",newline="") as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0]))
        w.writeheader(); w.writerows(rows)

    summary={
        "schema":"vulkax.dcs.gauge_retrospective",
        "version":1,
        "provenance":"known-GAUGE-metric-mirage-retrospective-discovery-only",
        "fit_performed":False,
        "confirmatory_evidence":False,
        "trial_count":len(rows),
        "ordinary_dual_overlap_wins":sum(bool(r["ordinary_dual_prefers_overlap"]) for r in rows),
        "dcs_marker_endpoint_wins":sum(bool(r["dcs_marker_prefers_endpoint"]) for r in rows),
        "dcs_long_endpoint_wins":sum(bool(r["dcs_long_prefers_endpoint"]) for r in rows),
        "median_driver_mid_progress":statistics.median(r["mid_progress"] for r in rows),
        "maximum_driver_orthogonal_fraction":max(r["driver_max_orthogonal_fraction"] for r in rows),
        "median_endpoint_dcs_marker_error_m":statistics.median(r["endpoint_dcs_marker_error_m"] for r in rows),
        "median_overlap_dcs_marker_error_m":statistics.median(r["overlap_dcs_marker_error_m"] for r in rows),
        "median_endpoint_dcs_long_error":statistics.median(r["endpoint_dcs_long_error"] for r in rows),
        "median_overlap_dcs_long_error":statistics.median(r["overlap_dcs_long_error"] for r in rows),
        "interpretation_rule":(
            "A retrospective dark-field signal is interesting only if ordinary metrics "
            "prefer overlap while an independently constructed lower-order-annihilating "
            "response prefers endpoint. This cannot validate DCS because the failure "
            "was already known before this analysis."
        ),
    }
    (out/"summary.json").write_text(json.dumps(summary,indent=2)+"\n")
    print("VALID GAUGE DCS retrospective")
    print("ORDINARY_OVERLAP_WINS",summary["ordinary_dual_overlap_wins"],"/",len(rows))
    print("DCS_MARKER_ENDPOINT_WINS",summary["dcs_marker_endpoint_wins"],"/",len(rows))
    print("DCS_LONG_ENDPOINT_WINS",summary["dcs_long_endpoint_wins"],"/",len(rows))
    print("DRIVER_MAX_ORTHOGONAL_FRACTION",summary["maximum_driver_orthogonal_fraction"])

if __name__=="__main__":
    main()

#!/usr/bin/env python3
"""Held-out GAUGE fixture-support experiment.

Calibration uses only measured kinematics from odd repeats. Evaluation uses only
even repeats. Material metadata is never fit to Vulkax trajectory error.

The pilot mode is a lower-cost mechanism screen. The definitive mode implements
the frozen 7x7x49 / 1/48000 s protocol.
"""
import argparse
import csv
import json
import math
import pathlib
import statistics
import subprocess
import tempfile

def v3(d,i,s=1e-3):
    return [float(d["x"][i])*s,float(d["y"][i])*s,float(d["z"][i])*s]

def sub(a,b):
    return [a[i]-b[i] for i in range(3)]

def add(a,b):
    return [a[i]+b[i] for i in range(3)]

def smul(a,s):
    return [x*s for x in a]

def norm(a):
    return math.sqrt(sum(x*x for x in a))

def cross(a,b):
    return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]

def area(a,b,c):
    return 0.5*norm(cross(sub(b,a),sub(c,a)))

def rms(xs):
    return math.sqrt(statistics.fmean(x*x for x in xs)) if xs else 0.0

def unique_faces(md):
    out=[]
    seen=set()
    for f in md["markers"]["faces"]:
        k=tuple(map(int,f))
        if k not in seen:
            seen.add(k)
            out.append(k)
    return out

def write_inputs(d,marker_path,driver_path):
    foam=d["foam"]
    ids=sorted(foam)
    base=d["base"]
    n=min([len(base[a]) for a in ("x","y","z")]+[
        len(foam[m][a]) for m in ids for a in ("x","y","z")])
    with marker_path.open("w",newline="") as f:
        w=csv.writer(f)
        w.writerow(["marker_id","x_m","y_m","z_m"])
        for mid in ids:
            w.writerow([mid,*v3(foam[mid],0)])
    b0=v3(base,0)
    fps=float(d["FPS"])
    with driver_path.open("w",newline="") as f:
        w=csv.writer(f)
        w.writerow(["frame","time_s","dx_m","dy_m","dz_m","magnitude_m"])
        for frame in range(n):
            disp=sub(v3(base,frame),b0)
            w.writerow([frame,frame/fps,*disp,norm(disp)])
    return ids,n

def read_pred(path):
    frames={}
    with path.open(newline="") as f:
        for r in csv.DictReader(f):
            frames.setdefault(int(r["frame"]),{})[r["marker_id"]]=[
                float(r["x_m"]),float(r["y_m"]),float(r["z_m"])]
    return frames

def face_curve(points_by_frame,ids,faces):
    keys=sorted(points_by_frame)
    p0=[points_by_frame[keys[0]][m] for m in ids]
    a0=[area(p0[i],p0[j],p0[k]) for i,j,k in faces]
    curve=[]
    for frame in keys:
        pts=[points_by_frame[frame][m] for m in ids]
        aa=[area(pts[i],pts[j],pts[k]) for i,j,k in faces]
        curve.append(rms([(x-y)/max(y,1e-15) for x,y in zip(aa,a0)]))
    return curve

def measured_frames(d,ids,n):
    foam=d["foam"]
    return {
        frame:{mid:v3(foam[mid],frame) for mid in ids}
        for frame in range(n)
    }

def marker_rmse(pred,measured,ids,n):
    es=[]
    for frame in range(n):
        for mid in ids:
            es.append(norm(sub(pred[frame][mid],measured[frame][mid])))
    return rms(es)

def longitudinal_final_mean_strain(frames,ids,faces):
    support_count=max(max(f) for f in faces)+1
    topo_ids=ids[:support_count]
    if support_count<6 or support_count%2:
        raise ValueError("unexpected GAUGE face-topology support")
    half=support_count//2
    rows=(list(range(half)),list(range(half,support_count)))
    rest=[frames[0][m] for m in topo_ids]
    last=frames[max(frames)]
    pts=[last[m] for m in topo_ids]
    strains=[]
    for row in rows:
        for a,b in zip(row[:-1],row[1:]):
            l0=norm(sub(rest[b],rest[a]))
            l1=norm(sub(pts[b],pts[a]))
            strains.append(l1/max(l0,1e-15)-1.0)
    return statistics.fmean(strains)

def run_arm(exe,marker,driver,pred,mat,dt,n_cross,n_long,label,boundary_thickness_m=None):
    cmd=[
        exe,str(marker),str(driver),str(pred),
        str(float(mat["young"])),str(float(mat["poisson"])),
        str(float(mat["density"])),str(float(mat["mass"])),
        repr(dt),label,
        str(n_cross),str(n_long),"1","APIC","released_asset_aspect","zero","neo_hookean_log_j",
    ]
    if boundary_thickness_m is not None:
        cmd.append(repr(boundary_thickness_m))
    cp=subprocess.run(cmd,text=True,capture_output=True)
    if cp.returncode:
        raise RuntimeError("forward failed: "+" ".join(cmd)+"\n"+cp.stderr+"\n"+cp.stdout)
    summary_path=pred.parent/(pred.stem+"_summary.json")
    return json.loads(summary_path.read_text())

def derive_overlap(effective_span_dir):
    edir=pathlib.Path(effective_span_dir)
    validation=json.loads((edir/"validation.json").read_text())
    if not validation.get("survives",False):
        return validation,None,None
    rows=[]
    with (edir/"per_trial.csv").open(newline="") as f:
        for r in csv.DictReader(f):
            if abs(float(r["threshold"])-0.50)<1e-9 and int(r["trial"])%2==1:
                rows.append(float(r["effective_span_m"]))
    if len(rows)!=10:
        raise RuntimeError(f"expected 10 odd-repeat calibration spans, found {len(rows)}")
    effective_span=statistics.median(rows)
    overlap=0.5*(0.200-effective_span)
    if not (0.0<overlap<0.050):
        raise RuntimeError(f"derived overlap outside conservative physical range: {overlap}")
    return validation,effective_span,overlap

def arm_metrics(d,pred,ids,faces,n):
    measured=measured_frames(d,ids,n)
    common=min(n,len(pred))
    measured={f:measured[f] for f in range(common)}
    pred={f:pred[f] for f in range(common)}
    for f in range(common):
        if set(pred[f])!=set(ids):
            raise RuntimeError(f"predicted marker identity mismatch at frame {f}")
    mc=face_curve(measured,ids,faces)
    pc=face_curve(pred,ids,faces)
    peak=max(mc)
    face_rmse=rms([x-y for x,y in zip(pc,mc)])
    measured_long=longitudinal_final_mean_strain(measured,ids,faces)
    predicted_long=longitudinal_final_mean_strain(pred,ids,faces)
    return {
        "frames":common,
        "face_nrmse":face_rmse/max(peak,1e-15),
        "marker_rmse_m":marker_rmse(pred,measured,ids,common),
        "face_peak_ratio":max(pc)/max(peak,1e-15),
        "measured_longitudinal_final_mean_strain":measured_long,
        "predicted_longitudinal_final_mean_strain":predicted_long,
        "longitudinal_final_abs_error":abs(predicted_long-measured_long),
    }

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--root",required=True)
    ap.add_argument("--exe",required=True)
    ap.add_argument("--effective-span-dir",required=True)
    ap.add_argument("--out",required=True)
    ap.add_argument("--mode",choices=("pilot","definitive"),default="pilot")
    a=ap.parse_args()

    root=pathlib.Path(a.root)
    out=pathlib.Path(a.out)
    out.mkdir(parents=True,exist_ok=True)

    validation,effective_span,overlap=derive_overlap(a.effective_span_dir)
    if overlap is None:
        result={
            "schema":"vulkax.gauge_fixture_overlap_heldout",
            "version":1,
            "provenance":"measured-calibration-only",
            "fit_performed":False,
            "mode":a.mode,
            "decision":"prerequisite_effective_span_failed",
            "effective_span_validation":validation,
            "inverse_fitting_unlocked":False,
        }
        (out/"summary.json").write_text(json.dumps(result,indent=2)+"\n")
        print("GAUGE_FIXTURE_OVERLAP prerequisite_effective_span_failed")
        return

    if a.mode=="definitive":
        n_cross,n_long,dt=7,49,1.0/48000.0
    else:
        n_cross,n_long,dt=5,25,1.0/24000.0

    md=json.loads((root/"metadata"/"foam shearing.json").read_text())
    faces=unique_faces(md)
    rows=[]

    with tempfile.TemporaryDirectory(prefix="vulkax-gauge-overlap-") as t:
        td=pathlib.Path(t)
        for material in ("soft","hard"):
            mat=md["assets"]["foam"]["material"][material]
            for trial in (2,4,6,8,10):
                d=json.loads((root/"data"/"foam shearing"/material/f"{trial}.json").read_text())
                marker=td/f"{material}_{trial}_markers.csv"
                driver=td/f"{material}_{trial}_driver.csv"
                ids,n=write_inputs(d,marker,driver)

                endpoint_pred=td/f"{material}_{trial}_endpoint.csv"
                overlap_pred=td/f"{material}_{trial}_overlap.csv"
                endpoint_ev=run_arm(
                    a.exe,marker,driver,endpoint_pred,mat,dt,n_cross,n_long,
                    f"{material}-{trial}-endpoint")
                overlap_ev=run_arm(
                    a.exe,marker,driver,overlap_pred,mat,dt,n_cross,n_long,
                    f"{material}-{trial}-overlap",overlap)

                endpoint=arm_metrics(d,read_pred(endpoint_pred),ids,faces,n)
                overlap_metrics=arm_metrics(d,read_pred(overlap_pred),ids,faces,n)
                stable=(
                    float(endpoint_ev["minimum_J"])>0.0 and
                    float(overlap_ev["minimum_J"])>0.0
                )
                row={
                    "material":material,
                    "trial":trial,
                    "effective_span_calibration_m":effective_span,
                    "fixture_overlap_each_end_m":overlap,
                    "endpoint_face_nrmse":endpoint["face_nrmse"],
                    "overlap_face_nrmse":overlap_metrics["face_nrmse"],
                    "endpoint_marker_rmse_m":endpoint["marker_rmse_m"],
                    "overlap_marker_rmse_m":overlap_metrics["marker_rmse_m"],
                    "endpoint_longitudinal_abs_error":endpoint["longitudinal_final_abs_error"],
                    "overlap_longitudinal_abs_error":overlap_metrics["longitudinal_final_abs_error"],
                    "endpoint_face_peak_ratio":endpoint["face_peak_ratio"],
                    "overlap_face_peak_ratio":overlap_metrics["face_peak_ratio"],
                    "endpoint_min_J":endpoint_ev["minimum_J"],
                    "overlap_min_J":overlap_ev["minimum_J"],
                    "endpoint_prescribed_particles":endpoint_ev["maximum_prescribed_particles"],
                    "overlap_prescribed_particles":overlap_ev["maximum_prescribed_particles"],
                    "face_improved":overlap_metrics["face_nrmse"]<endpoint["face_nrmse"],
                    "marker_improved":overlap_metrics["marker_rmse_m"]<endpoint["marker_rmse_m"],
                    "longitudinal_improved":overlap_metrics["longitudinal_final_abs_error"]<endpoint["longitudinal_final_abs_error"],
                    "dual_metric_win":(
                        overlap_metrics["face_nrmse"]<endpoint["face_nrmse"] and
                        overlap_metrics["marker_rmse_m"]<endpoint["marker_rmse_m"]
                    ),
                    "stable":stable,
                }
                rows.append(row)

    fields=list(rows[0])
    with (out/"per_trial.csv").open("w",newline="") as f:
        w=csv.DictWriter(f,fieldnames=fields)
        w.writeheader()
        w.writerows(rows)

    groups={}
    for material in ("soft","hard"):
        g=[r for r in rows if r["material"]==material]
        groups[material]={
            "trials":len(g),
            "dual_metric_wins":sum(bool(r["dual_metric_win"]) for r in g),
            "longitudinal_wins":sum(bool(r["longitudinal_improved"]) for r in g),
            "mean_endpoint_face_nrmse":statistics.fmean(r["endpoint_face_nrmse"] for r in g),
            "mean_overlap_face_nrmse":statistics.fmean(r["overlap_face_nrmse"] for r in g),
            "mean_endpoint_marker_rmse_m":statistics.fmean(r["endpoint_marker_rmse_m"] for r in g),
            "mean_overlap_marker_rmse_m":statistics.fmean(r["overlap_marker_rmse_m"] for r in g),
            "mean_endpoint_longitudinal_abs_error":statistics.fmean(r["endpoint_longitudinal_abs_error"] for r in g),
            "mean_overlap_longitudinal_abs_error":statistics.fmean(r["overlap_longitudinal_abs_error"] for r in g),
        }

    overall_dual=sum(bool(r["dual_metric_win"]) for r in rows)
    stable=all(bool(r["stable"]) for r in rows)
    face_mean_improves=statistics.fmean(r["overlap_face_nrmse"] for r in rows) < statistics.fmean(r["endpoint_face_nrmse"] for r in rows)
    marker_mean_improves=statistics.fmean(r["overlap_marker_rmse_m"] for r in rows) < statistics.fmean(r["endpoint_marker_rmse_m"] for r in rows)
    long_groups_improve=all(
        groups[m]["mean_overlap_longitudinal_abs_error"] <
        groups[m]["mean_endpoint_longitudinal_abs_error"]
        for m in ("soft","hard")
    )

    if a.mode=="definitive":
        survives=(
            stable and overall_dual>=8 and
            groups["soft"]["dual_metric_wins"]>=4 and
            groups["hard"]["dual_metric_wins"]>=4 and
            face_mean_improves and marker_mean_improves and long_groups_improve
        )
        decision=(
            "fixture_overlap_survives_heldout_gate"
            if survives else
            "kill_fixture_overlap_as_primary_explanation"
        )
    else:
        survives=(
            stable and overall_dual>=6 and
            face_mean_improves and marker_mean_improves
        )
        decision=(
            "pilot_supports_definitive_fixture_overlap_gate"
            if survives else
            "pilot_does_not_support_fixture_overlap"
        )

    summary={
        "schema":"vulkax.gauge_fixture_overlap_heldout",
        "version":1,
        "provenance":"odd-repeat-measured-calibration+even-repeat-no-fit-model-evaluation",
        "fit_performed":False,
        "mode":a.mode,
        "calibration":{
            "source":"odd repeats only; 50% driver-motion threshold",
            "shared_effective_span_m":effective_span,
            "released_body_length_m":0.200,
            "fixture_overlap_each_end_m":overlap,
        },
        "numerics":{
            "n_cross":n_cross,
            "n_long":n_long,
            "requested_dt_s":dt,
            "transfer":"APIC",
            "geometry":"released_asset_aspect",
            "constitutive":"neo_hookean_log_j",
            "gravity":"zero",
        },
        "evaluation":{
            "repeats":"even only: 2,4,6,8,10 for each material",
            "trial_count":len(rows),
            "overall_dual_metric_wins":overall_dual,
            "all_stable":stable,
            "mean_face_metric_improves":face_mean_improves,
            "mean_marker_metric_improves":marker_mean_improves,
            "group_longitudinal_error_improves":long_groups_improve,
            "groups":groups,
        },
        "decision":decision,
        "inverse_fitting_unlocked":False,
        "guardrails":[
            "No material parameter or support thickness is selected from target simulation error.",
            "Odd repeats calibrate the kinematic support summary; even repeats alone evaluate the model.",
            "Pilot mode is a mechanism screen and cannot satisfy the frozen definitive protocol.",
            "Even a definitive positive result would only authorize a fresh no-fit adequacy comparison against the affine null.",
        ],
    }
    (out/"summary.json").write_text(json.dumps(summary,indent=2)+"\n")
    print("VALID GAUGE held-out fixture-support experiment")
    print("MODE",a.mode)
    print("CALIBRATION effective_span_m",effective_span,"overlap_each_end_m",overlap)
    print("DUAL_WINS",overall_dual,"/",len(rows),"stable",stable)
    print("DECISION",decision)

if __name__=="__main__":
    main()

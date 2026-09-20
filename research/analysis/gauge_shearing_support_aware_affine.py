#!/usr/bin/env python3
"""Held-out support-aware affine null for GAUGE foam shearing.

Calibrate one effective longitudinal span from odd repeats using the measured-only
effective-span diagnostic. Evaluate unchanged on even repeats against the historical
marker-envelope affine null. No material or simulator parameter is used.
"""
import argparse
import csv
import json
import math
import pathlib
import statistics

def v3(series,i):
    return [float(series[a][i])*1.0e-3 for a in ("x","y","z")]
def sub(a,b): return [a[i]-b[i] for i in range(3)]
def add(a,b): return [a[i]+b[i] for i in range(3)]
def scale(a,s): return [x*s for x in a]
def dot(a,b): return sum(x*y for x,y in zip(a,b))
def norm(a): return math.sqrt(dot(a,a))
def rms(xs): return math.sqrt(statistics.fmean(x*x for x in xs)) if xs else 0.0
def corr(a,b):
    ma=statistics.fmean(a); mb=statistics.fmean(b)
    da=[x-ma for x in a]; db=[x-mb for x in b]
    den=math.sqrt(sum(x*x for x in da)*sum(x*x for x in db))
    return sum(x*y for x,y in zip(da,db))/den if den>0 else 0.0
def cross(a,b):
    return [a[1]*b[2]-a[2]*b[1],
            a[2]*b[0]-a[0]*b[2],
            a[0]*b[1]-a[1]*b[0]]
def area(a,b,c): return 0.5*norm(cross(sub(b,a),sub(c,a)))

def unique_faces(metadata):
    seen=set(); out=[]
    for f in metadata["markers"]["faces"]:
        k=tuple(int(x) for x in f)
        if k not in seen:
            seen.add(k); out.append(k)
    return out

def calibration_span(path):
    rows=list(csv.DictReader(path.open()))
    vals=[]
    for r in rows:
        if int(r["trial"])%2!=1: continue
        if abs(float(r["threshold"])-0.50)>1.0e-9: continue
        vals.append(float(r["effective_span_m"]))
    if len(vals)!=10:
        raise RuntimeError(f"expected 10 odd-repeat calibration spans, got {len(vals)}")
    return statistics.median(vals),vals

def evaluate_trial(path,faces,L):
    d=json.loads(path.read_text())
    foam=d["foam"]; ids=sorted(foam); base=d["base"]
    n=min([len(base[a]) for a in ("x","y","z")]+[
        len(foam[m][a]) for m in ids for a in ("x","y","z")])
    p0=[v3(foam[m],0) for m in ids]
    lo=[min(p[a] for p in p0) for a in range(3)]
    hi=[max(p[a] for p in p0) for a in range(3)]
    axis=max(range(3),key=lambda a:hi[a]-lo[a])
    marker_span=hi[axis]-lo[axis]
    center=0.5*(hi[axis]+lo[axis])
    support_lo=center-0.5*L

    alpha_marker=[(p[axis]-lo[axis])/marker_span for p in p0]
    alpha_support=[(p[axis]-support_lo)/L for p in p0]
    max_extrap=max(max(0.0,-a,a-1.0) for a in alpha_support)

    a0=[area(p0[i],p0[j],p0[k]) for i,j,k in faces]
    if any(x<=1e-15 for x in a0):
        raise RuntimeError("degenerate GAUGE face in support-aware affine test")

    b0=v3(base,0)
    measured_curve=[]; marker_curve=[]; support_curve=[]
    marker_errors=[]; support_errors=[]

    for frame in range(n):
        disp=sub(v3(base,frame),b0)
        measured=[v3(foam[m],frame) for m in ids]
        pred_marker=[add(p,scale(disp,a)) for p,a in zip(p0,alpha_marker)]
        pred_support=[add(p,scale(disp,a)) for p,a in zip(p0,alpha_support)]

        marker_errors.extend(norm(sub(a,b)) for a,b in zip(pred_marker,measured))
        support_errors.extend(norm(sub(a,b)) for a,b in zip(pred_support,measured))

        am=[area(measured[i],measured[j],measured[k]) for i,j,k in faces]
        aa=[area(pred_marker[i],pred_marker[j],pred_marker[k]) for i,j,k in faces]
        sa=[area(pred_support[i],pred_support[j],pred_support[k]) for i,j,k in faces]

        measured_curve.append(rms((x-y)/y for x,y in zip(am,a0)))
        marker_curve.append(rms((x-y)/y for x,y in zip(aa,a0)))
        support_curve.append(rms((x-y)/y for x,y in zip(sa,a0)))

    peak=max(measured_curve)
    marker_face_rmse=rms(x-y for x,y in zip(marker_curve,measured_curve))
    support_face_rmse=rms(x-y for x,y in zip(support_curve,measured_curve))
    return {
        "frames":n,
        "long_axis":"xyz"[axis],
        "marker_span_m":marker_span,
        "calibrated_support_span_m":L,
        "support_max_alpha_extrapolation":max_extrap,
        "marker_affine_marker_rmse_m":rms(marker_errors),
        "support_affine_marker_rmse_m":rms(support_errors),
        "marker_affine_face_nrmse":marker_face_rmse/max(peak,1e-15),
        "support_affine_face_nrmse":support_face_rmse/max(peak,1e-15),
        "marker_affine_face_corr":corr(measured_curve,marker_curve),
        "support_affine_face_corr":corr(measured_curve,support_curve),
    }

def summarize(rows,material=None):
    g=[r for r in rows if material is None or r["material"]==material]
    dual=sum(
        r["support_affine_marker_rmse_m"]<r["marker_affine_marker_rmse_m"] and
        r["support_affine_face_nrmse"]<r["marker_affine_face_nrmse"]
        for r in g
    )
    return {
        "trials":len(g),
        "dual_metric_wins":dual,
        "mean_marker_affine_marker_rmse_m":statistics.fmean(
            r["marker_affine_marker_rmse_m"] for r in g),
        "mean_support_affine_marker_rmse_m":statistics.fmean(
            r["support_affine_marker_rmse_m"] for r in g),
        "mean_marker_affine_face_nrmse":statistics.fmean(
            r["marker_affine_face_nrmse"] for r in g),
        "mean_support_affine_face_nrmse":statistics.fmean(
            r["support_affine_face_nrmse"] for r in g),
        "max_support_alpha_extrapolation":max(
            r["support_max_alpha_extrapolation"] for r in g),
    }

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--root",required=True)
    ap.add_argument("--effective-span-dir",required=True)
    ap.add_argument("--out",required=True)
    a=ap.parse_args()
    root=pathlib.Path(a.root)
    out=pathlib.Path(a.out); out.mkdir(parents=True,exist_ok=True)
    eff=pathlib.Path(a.effective_span_dir)

    L,calibration_values=calibration_span(eff/"per_trial.csv")
    metadata=json.loads((root/"metadata"/"foam shearing.json").read_text())
    faces=unique_faces(metadata)

    rows=[]
    for material in ("soft","hard"):
        for trial in (2,4,6,8,10):
            r=evaluate_trial(
                root/"data"/"foam shearing"/material/f"{trial}.json",
                faces,L)
            r.update({"material":material,"trial":trial})
            rows.append(r)

    fields=list(rows[0].keys())
    with (out/"heldout_trials.csv").open("w",newline="") as f:
        w=csv.DictWriter(f,fieldnames=fields)
        w.writeheader(); w.writerows(rows)

    overall=summarize(rows)
    by_material={m:summarize(rows,m) for m in ("soft","hard")}
    primary_improves=(
        overall["mean_support_affine_marker_rmse_m"] <
        overall["mean_marker_affine_marker_rmse_m"] and
        overall["mean_support_affine_face_nrmse"] <
        overall["mean_marker_affine_face_nrmse"]
    )
    support_valid=(
        overall["dual_metric_wins"]>=8 and
        all(by_material[m]["dual_metric_wins"]>=4 for m in ("soft","hard")) and
        primary_improves and
        overall["max_support_alpha_extrapolation"]<=0.15
    )

    result={
        "schema":"vulkax.gauge_support_aware_affine_heldout",
        "version":1,
        "provenance":"measured-only+analytic",
        "fit_performed":False,
        "simulation_performed":False,
        "split":{
            "calibration":"odd repeats, both materials",
            "heldout":"even repeats, both materials",
        },
        "calibration":{
            "motion_threshold":0.50,
            "shared_effective_span_m":L,
            "odd_repeat_span_values_m":calibration_values,
        },
        "overall":overall,
        "by_material":by_material,
        "survives":support_valid,
        "decision":(
            "retain_support_aware_affine_as_stronger_null"
            if support_valid else
            "scalar_support_correction_insufficient"
        ),
        "warning":(
            "This held-out analytic test only asks whether observation support was "
            "mis-specified in the historical affine null. It does not identify "
            "fixture mechanics or unlock inverse material fitting."
        ),
    }
    (out/"summary.json").write_text(json.dumps(result,indent=2)+"\n")

    print("VALID GAUGE support-aware affine heldout test")
    print("CALIBRATED_SPAN_M",L)
    print("OVERALL",overall)
    for material,data in by_material.items():
        print("MATERIAL",material,data)
    print("SURVIVES",support_valid)
    print("DECISION",result["decision"])

if __name__=="__main__":
    main()

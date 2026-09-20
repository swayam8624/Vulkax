#!/usr/bin/env python3
"""Measured-only effective deforming-span diagnostic for GAUGE foam shearing.

For each trial, project marker displacement onto the measured driver direction and
fit a simple affine displacement-vs-longitudinal-position field. Extrapolate the
positions at which that field reaches 0x and 1x driver displacement. The distance
between those virtual planes is an *effective deforming span* diagnostic.

This is not a material fit and is not used to minimize Vulkax trajectory error.
It tests whether the observed marker support is consistent with either the marker
envelope, the full released asset length, or an intermediate effective free span.
"""
import argparse
import csv
import json
import math
import pathlib
import statistics

THRESHOLDS=(0.30,0.50,0.70)

def v3(series,i):
    return [float(series[a][i])*1.0e-3 for a in ("x","y","z")]
def sub(a,b): return [a[i]-b[i] for i in range(3)]
def dot(a,b): return sum(x*y for x,y in zip(a,b))
def norm(a): return math.sqrt(dot(a,a))
def mean(xs): return statistics.fmean(xs)

def linear_fit(x,y):
    mx,my=mean(x),mean(y)
    sxx=sum((q-mx)**2 for q in x)
    if sxx<=1e-18:
        raise ValueError("degenerate longitudinal coordinate")
    slope=sum((a-mx)*(b-my) for a,b in zip(x,y))/sxx
    intercept=my-slope*mx
    yhat=[slope*a+intercept for a in x]
    sse=sum((a-b)**2 for a,b in zip(y,yhat))
    sst=sum((a-my)**2 for a in y)
    r2=1.0-sse/sst if sst>1e-18 else 1.0
    return slope,intercept,r2

def trial_stats(path,threshold):
    d=json.loads(path.read_text())
    if d.get("translation unit")!="mm":
        raise ValueError(f"expected millimetres: {path}")
    foam=d["foam"]; ids=sorted(foam)
    p0=[v3(foam[m],0) for m in ids]
    spans=[max(p[a] for p in p0)-min(p[a] for p in p0) for a in range(3)]
    axis=max(range(3),key=lambda a:spans[a])
    x=[p[axis] for p in p0]
    base=d["base"]
    n=min([len(base[a]) for a in ("x","y","z")]+[
        len(foam[m][a]) for m in ids for a in ("x","y","z")])
    b0=v3(base,0)
    driver=[sub(v3(base,i),b0) for i in range(n)]
    mags=[norm(q) for q in driver]
    peak=max(mags)
    if not peak>0.0:
        raise ValueError(f"zero driver motion: {path}")
    frames=[i for i,m in enumerate(mags) if m>=threshold*peak]

    eff=[]; r2s=[]; centers=[]
    for frame in frames:
        dv=driver[frame]; dm=norm(dv)
        if dm<=1e-12: continue
        direction=[q/dm for q in dv]
        y=[]
        for mid,pinit in zip(ids,p0):
            pt=v3(foam[mid],frame)
            y.append(dot(sub(pt,pinit),direction)/dm)
        slope,intercept,r2=linear_fit(x,y)
        if abs(slope)<=1e-9: continue
        x0=-intercept/slope
        x1=(1.0-intercept)/slope
        lo,hi=sorted((x0,x1))
        eff.append(hi-lo)
        centers.append(0.5*(lo+hi))
        r2s.append(r2)

    if not eff:
        raise ValueError(f"no usable frames: {path}")
    marker_center=0.5*(min(x)+max(x))
    return {
        "threshold":threshold,
        "marker_long_axis":"xyz"[axis],
        "marker_span_m":max(x)-min(x),
        "effective_span_m":statistics.median(eff),
        "effective_span_frame_sd_m":statistics.stdev(eff) if len(eff)>1 else 0.0,
        "median_r2":statistics.median(r2s),
        "effective_center_m":statistics.median(centers),
        "marker_center_m":marker_center,
        "center_offset_m":statistics.median(centers)-marker_center,
        "frames_used":len(eff),
    }

def group(rows,material,threshold):
    g=[r for r in rows if r["material"]==material and r["threshold"]==threshold]
    spans=[r["effective_span_m"] for r in g]
    mu=mean(spans); sd=statistics.stdev(spans)
    odd=[r["effective_span_m"] for r in g if r["trial"]%2==1]
    even=[r["effective_span_m"] for r in g if r["trial"]%2==0]
    odd_median=statistics.median(odd)
    even_mae=mean(abs(x-odd_median) for x in even)
    return {
        "trials":len(g),
        "effective_span_median_m":statistics.median(spans),
        "effective_span_mean_m":mu,
        "effective_span_sd_m":sd,
        "effective_span_cv":sd/mu,
        "marker_span_median_m":statistics.median(r["marker_span_m"] for r in g),
        "median_r2":statistics.median(r["median_r2"] for r in g),
        "center_offset_median_m":statistics.median(r["center_offset_m"] for r in g),
        "odd_trial_train_median_m":odd_median,
        "even_trial_holdout_mae_m":even_mae,
        "even_trial_holdout_relative_mae":even_mae/odd_median,
    }

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--root",required=True)
    ap.add_argument("--out",required=True)
    a=ap.parse_args()
    root=pathlib.Path(a.root); out=pathlib.Path(a.out)
    out.mkdir(parents=True,exist_ok=True)

    rows=[]
    for material in ("soft","hard"):
        for trial in range(1,11):
            path=root/"data"/"foam shearing"/material/f"{trial}.json"
            for threshold in THRESHOLDS:
                r=trial_stats(path,threshold)
                r.update({"material":material,"trial":trial})
                rows.append(r)

    fields=list(rows[0].keys())
    with (out/"per_trial.csv").open("w",newline="") as f:
        w=csv.DictWriter(f,fieldnames=fields)
        w.writeheader(); w.writerows(rows)

    groups={}
    cross={}
    for threshold in THRESHOLDS:
        key=f"{threshold:.2f}"
        groups[key]={m:group(rows,m,threshold) for m in ("soft","hard")}
        sm=groups[key]["soft"]["effective_span_median_m"]
        hm=groups[key]["hard"]["effective_span_median_m"]
        mid=0.5*(sm+hm)
        cross[key]={
            "soft_median_m":sm,
            "hard_median_m":hm,
            "soft_hard_abs_difference_m":abs(sm-hm),
            "pooled_midpoint_m":mid,
            "released_asset_length_m":0.200,
            "implied_total_fixture_overlap_m":0.200-mid,
            "implied_overlap_each_end_if_symmetric_m":0.5*(0.200-mid),
        }

    result={
        "schema":"vulkax.gauge_effective_deforming_span",
        "version":1,
        "provenance":"measured-only",
        "fit_performed":False,
        "simulation_performed":False,
        "trial_count":20,
        "thresholds":list(THRESHOLDS),
        "method":{
            "field":"marker displacement projected onto instantaneous measured driver direction",
            "regression":"ordinary least squares displacement/driver vs initial longitudinal marker coordinate",
            "virtual_planes":"positions where fitted field reaches 0x and 1x measured driver displacement",
            "effective_span":"distance between virtual planes",
            "holdout":"odd repeats define median span; even repeats report absolute prediction error",
        },
        "groups":groups,
        "cross_material":cross,
        "interpretation_guard":(
            "Effective span is a kinematic observation-support diagnostic. It is not a clamp-contact "
            "identification, constitutive parameter, or permission to tune the simulator. Stability across "
            "repeats only justifies testing an independently specified boundary-support model."
        ),
    }
    (out/"summary.json").write_text(json.dumps(result,indent=2)+"\n")

    print("VALID GAUGE effective deforming-span diagnostic")
    for threshold in THRESHOLDS:
        k=f"{threshold:.2f}"
        for material in ("soft","hard"):
            g=groups[k][material]
            print("EFFECTIVE_SPAN",material,k,
                  "median_m",g["effective_span_median_m"],
                  "sd_m",g["effective_span_sd_m"],
                  "cv",g["effective_span_cv"],
                  "r2",g["median_r2"],
                  "holdout_rel_mae",g["even_trial_holdout_relative_mae"])
        print("CROSS_EFFECTIVE_SPAN",k,cross[k])

if __name__=="__main__":
    main()

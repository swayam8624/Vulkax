#!/usr/bin/env python3
"""Geometry-aware GAUGE foam-shearing boundary compatibility diagnostic.

The released foam asset is 50 x 50 x 200 mm while the mocap markers cover only
~75% of the long axis, so the outermost marker rows are interior points and
cannot be equated with fixture planes.

This measured-only diagnostic:
1. reads the released foam OBJ and task translation;
2. finds the physical long-axis end planes;
3. clusters marker rows by initial long-axis coordinate;
4. projects marker motion onto the measured driver direction;
5. extrapolates the first/last marker rows to the actual asset ends using
   local 2-row and local 3-row linear fits;
6. compares those extrapolated end motions with the released fixed=0 and
   moving=driver kinematics.

No simulation residual, material parameter, or Vulkax prediction is consulted.
"""
import argparse,csv,json,math,pathlib,statistics
from collections import defaultdict

MM=1e-3

def v3(series,i):
    return tuple(float(series[a][i])*MM for a in ("x","y","z"))
def sub(a,b): return tuple(a[i]-b[i] for i in range(3))
def dot(a,b): return sum(x*y for x,y in zip(a,b))
def norm(a): return math.sqrt(dot(a,a))
def scale(a,s): return tuple(x*s for x in a)
def rms(xs):
    xs=list(xs)
    return math.sqrt(statistics.fmean(x*x for x in xs)) if xs else 0.0

def obj_bbox(path):
    lo=[math.inf]*3;hi=[-math.inf]*3;n=0
    for line in path.read_text().splitlines():
        if not line.startswith("v "): continue
        p=[float(x) for x in line.split()[1:4]]
        for a,x in enumerate(p):lo[a]=min(lo[a],x);hi[a]=max(hi[a],x)
        n+=1
    if n<8: raise RuntimeError("foam OBJ has too few vertices")
    span=[hi[a]-lo[a] for a in range(3)]
    return lo,hi,span

def cluster_rows(initial,axis):
    items=sorted((p[axis],mid) for mid,p in initial.items())
    total=items[-1][0]-items[0][0]
    tol=max(1e-6,total*1e-4)
    groups=[]
    for z,mid in items:
        if not groups or abs(z-statistics.fmean(x[0] for x in groups[-1]))>tol:
            groups.append([])
        groups[-1].append((z,mid))
    if len(groups)<4:
        raise RuntimeError(f"too few longitudinal marker rows: {len(groups)}")
    rows=[]
    for g in groups:
        rows.append((statistics.fmean(z for z,_ in g),[mid for _,mid in g]))
    spacings=[rows[i+1][0]-rows[i][0] for i in range(len(rows)-1)]
    if min(spacings)<=0: raise RuntimeError("non-increasing marker row coordinates")
    return rows,statistics.median(spacings)

def linear_predict(points,x):
    # Least-squares y=a+b*x over 2 or 3 local row means.
    xm=statistics.fmean(p[0] for p in points)
    ym=statistics.fmean(p[1] for p in points)
    den=sum((px-xm)**2 for px,_ in points)
    if den<=1e-20: raise RuntimeError("degenerate endpoint extrapolation")
    b=sum((px-xm)*(py-ym) for px,py in points)/den
    return ym+b*(x-xm)

def analyze_trial(md,obj_span,path,material,trial_no):
    d=json.loads(path.read_text())
    if d.get("translation unit")!="mm": raise RuntimeError("expected millimetres")
    foam=d["foam"];base=d["base"];ids=sorted(foam)
    n=min(min(len(foam[mid][a]) for a in ("x","y","z")) for mid in ids)
    n=min(n,min(len(base[a]) for a in ("x","y","z")))
    initial={mid:v3(foam[mid],0) for mid in ids}
    marker_span=[
        max(p[a] for p in initial.values())-min(p[a] for p in initial.values())
        for a in range(3)
    ]
    marker_axis=max(range(3),key=lambda a:marker_span[a])
    asset_axis=max(range(3),key=lambda a:obj_span[a])
    if marker_axis!=asset_axis:
        raise RuntimeError(
            f"asset/marker long axes differ ({asset_axis} vs {marker_axis}); "
            "refusing to invent a rotation"
        )
    center=float(md["tasks"]["default"]["translation"]["foam"][marker_axis])
    half=0.5*obj_span[asset_axis]
    physical_lo=center-half;physical_hi=center+half
    rows,row_spacing=cluster_rows(initial,marker_axis)
    lo_margin=rows[0][0]-physical_lo
    hi_margin=physical_hi-rows[-1][0]
    if lo_margin<0 or hi_margin<0:
        raise RuntimeError("markers extend beyond released foam asset")
    # Extrapolation is intentionally limited to <=1.5 row spacings.
    if lo_margin>1.5*row_spacing or hi_margin>1.5*row_spacing:
        raise RuntimeError(
            f"fixture extrapolation too far: margins={lo_margin},{hi_margin}, spacing={row_spacing}"
        )

    b0=v3(base,0)
    driver=[sub(v3(base,i),b0) for i in range(n)]
    peak=max(range(n),key=lambda i:norm(driver[i]))
    peak_vec=driver[peak]
    peak_norm=norm(peak_vec)
    if peak_norm<=0: raise RuntimeError("zero driver")
    direction=scale(peak_vec,1.0/peak_norm)
    driver_scalar=[dot(x,direction) for x in driver]

    fixed2=[];fixed3=[];moving2=[];moving3=[]
    row_nonlin=[]
    for frame in range(n):
        row_y=[]
        for z,mids in rows:
            vals=[]
            for mid in mids:
                disp=sub(v3(foam[mid],frame),initial[mid])
                vals.append(dot(disp,direction))
            row_y.append((z,statistics.fmean(vals)))
        fixed2.append(linear_predict(row_y[:2],physical_lo))
        fixed3.append(linear_predict(row_y[:3],physical_lo))
        moving2.append(linear_predict(row_y[-2:],physical_hi))
        moving3.append(linear_predict(row_y[-3:],physical_hi))
        # local-fit disagreement is a measured extrapolation-stability indicator.
        row_nonlin.append(abs(fixed2[-1]-fixed3[-1])+abs(moving2[-1]-moving3[-1]))

    driver_peak=max(abs(x) for x in driver_scalar)
    def residual_stats(fixed,moving):
        return {
            "fixed_end_rms_m":rms(fixed),
            "fixed_end_rms_fraction_driver":rms(fixed)/driver_peak,
            "moving_end_driver_rms_m":rms(moving[i]-driver_scalar[i] for i in range(n)),
            "moving_end_driver_rms_fraction":rms(moving[i]-driver_scalar[i] for i in range(n))/driver_peak,
            "moving_peak_gain":max(abs(x) for x in moving)/driver_peak,
        }
    return {
      "material":material,"trial":trial_no,"frames":n,
      "long_axis":"xyz"[marker_axis],"marker_rows":len(rows),
      "row_spacing_m":row_spacing,
      "physical_end_low_m":physical_lo,"physical_end_high_m":physical_hi,
      "first_marker_row_m":rows[0][0],"last_marker_row_m":rows[-1][0],
      "low_extrapolation_margin_m":lo_margin,"high_extrapolation_margin_m":hi_margin,
      "driver_peak_m":driver_peak,
      "local2":residual_stats(fixed2,moving2),
      "local3":residual_stats(fixed3,moving3),
      "local2_local3_disagreement_rms_m":rms(row_nonlin),
      "local2_local3_disagreement_fraction_driver":rms(row_nonlin)/driver_peak,
    }

def group_summary(rows,material):
    g=[r for r in rows if r["material"]==material]
    def avg(path):
        return statistics.fmean(r[path[0]][path[1]] for r in g)
    return {
      "trials":len(g),
      "marker_rows":sorted(set(r["marker_rows"] for r in g)),
      "long_axis":sorted(set(r["long_axis"] for r in g)),
      "low_margin_mm_mean":1000*statistics.fmean(r["low_extrapolation_margin_m"] for r in g),
      "high_margin_mm_mean":1000*statistics.fmean(r["high_extrapolation_margin_m"] for r in g),
      "local2_fixed_fraction_mean":avg(("local2","fixed_end_rms_fraction_driver")),
      "local2_moving_residual_fraction_mean":avg(("local2","moving_end_driver_rms_fraction")),
      "local3_fixed_fraction_mean":avg(("local3","fixed_end_rms_fraction_driver")),
      "local3_moving_residual_fraction_mean":avg(("local3","moving_end_driver_rms_fraction")),
      "extrapolation_model_disagreement_fraction_mean":
        statistics.fmean(r["local2_local3_disagreement_fraction_driver"] for r in g),
    }

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--root",required=True);ap.add_argument("--out",required=True)
    a=ap.parse_args();root=pathlib.Path(a.root);out=pathlib.Path(a.out)
    out.mkdir(parents=True,exist_ok=True)
    md=json.loads((root/"metadata"/"foam shearing.json").read_text())
    _,_,span=obj_bbox(root/"assets"/"foam.obj")
    rows=[]
    for material in ("soft","hard"):
        for trial in range(1,11):
            rows.append(analyze_trial(
                md,span,root/"data"/"foam shearing"/material/f"{trial}.json",
                material,trial))
    result={
      "schema":"vulkax.gauge_boundary_end_extrapolation","version":1,
      "provenance":"released-asset+measured-trajectories-only",
      "fit_performed":False,"simulation_performed":False,
      "asset_bbox_span_m":span,
      "method":"local linear extrapolation from nearest 2 and 3 measured longitudinal marker rows to released asset end planes",
      "materials":{m:group_summary(rows,m) for m in ("soft","hard")},
      "guardrails":[
        "No Vulkax prediction or trajectory residual is used.",
        "No compliance parameter is fitted.",
        "End extrapolation is rejected if the asset end is farther than 1.5 marker-row spacings from measured support.",
        "Disagreement between 2-row and 3-row extrapolations is reported as a stability warning.",
        "Compatibility with endpoint kinematics does not prove a contact law or identify material parameters."
      ],
      "decision":"boundary_compatibility_diagnostic_only"
    }
    (out/"summary.json").write_text(json.dumps(result,indent=2)+"\n")
    with (out/"trials.csv").open("w",newline="") as f:
        w=csv.writer(f);w.writerow([
            "material","trial","rows","row_spacing_m","lo_margin_m","hi_margin_m",
            "local2_fixed_frac","local2_moving_resid_frac",
            "local3_fixed_frac","local3_moving_resid_frac","extrapolation_disagreement_frac"])
        for r in rows:w.writerow([
            r["material"],r["trial"],r["marker_rows"],r["row_spacing_m"],
            r["low_extrapolation_margin_m"],r["high_extrapolation_margin_m"],
            r["local2"]["fixed_end_rms_fraction_driver"],
            r["local2"]["moving_end_driver_rms_fraction"],
            r["local3"]["fixed_end_rms_fraction_driver"],
            r["local3"]["moving_end_driver_rms_fraction"],
            r["local2_local3_disagreement_fraction_driver"]])
    print("VALID GAUGE geometry-aware boundary extrapolation")
    for m,s in result["materials"].items():
        print("BOUNDARY_EXTRAP",m,
              "lo_margin_mm",s["low_margin_mm_mean"],
              "hi_margin_mm",s["high_margin_mm_mean"],
              "fixed2",s["local2_fixed_fraction_mean"],
              "moving2",s["local2_moving_residual_fraction_mean"],
              "fixed3",s["local3_fixed_fraction_mean"],
              "moving3",s["local3_moving_residual_fraction_mean"],
              "fit_disagreement",s["extrapolation_model_disagreement_fraction_mean"])

if __name__=="__main__":main()

#!/usr/bin/env python3
import argparse,csv,hashlib,json,math,pathlib,statistics
from itertools import combinations
TASKS=("foam stretching","foam compression","foam shearing")
MATERIALS=("soft","hard")
def sha256(p):
    h=hashlib.sha256()
    with open(p,"rb") as f:
        for b in iter(lambda:f.read(1<<20),b""):h.update(b)
    return h.hexdigest()
def vec3(d,i,scale=1e-3):return(float(d["x"][i])*scale,float(d["y"][i])*scale,float(d["z"][i])*scale)
def sub(a,b):return tuple(x-y for x,y in zip(a,b))
def norm(a):return math.sqrt(sum(x*x for x in a))
def dist(a,b):return norm(sub(a,b))
def cross(a,b):return(a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
def tri_area(a,b,c):return .5*norm(cross(sub(b,a),sub(c,a)))
def mean(x):return statistics.fmean(x) if x else float("nan")
def stdev(x):return statistics.stdev(x) if len(x)>1 else 0.
def percentile(xs,q):
    ys=sorted(xs)
    if not ys:return float("nan")
    x=(len(ys)-1)*q;lo=int(math.floor(x));hi=int(math.ceil(x))
    return ys[lo] if lo==hi else ys[lo]*(hi-x)+ys[hi]*(x-lo)
def unique_faces(metadata):
    seen=set();out=[]
    for face in metadata["markers"]["faces"]:
        key=tuple(int(x) for x in face)
        if key not in seen:seen.add(key);out.append(key)
    return out
def trial_metrics(path,faces):
    d=json.loads(path.read_text())
    if d["translation unit"]!="mm":raise ValueError("expected GAUGE mm")
    fps=float(d["FPS"]);markers=d["foam"];ids=sorted(markers)
    n=min(min(len(markers[m][a]) for a in ("x","y","z")) for m in ids)
    base=d["base"];n=min(n,min(len(base[a]) for a in ("x","y","z")))
    pts0=[vec3(markers[m],0) for m in ids]
    pairs=[]
    for ia,ib in combinations(range(len(ids)),2):
        d0=dist(pts0[ia],pts0[ib])
        if d0>1e-6:pairs.append((ia,ib,d0))
    area0=[tri_area(pts0[a],pts0[b],pts0[c]) for a,b,c in faces]
    if any(a<=1e-12 for a in area0):raise ValueError("degenerate GAUGE face")
    base0=vec3(base,0);driver=[norm(sub(vec3(base,i),base0)) for i in range(n)];driver_max=max(driver)
    pair_rms=[];pair_p95=[];shape=[];area_abs_rms=[];area_rel_rms=[]
    c0=tuple(mean([p[k] for p in pts0]) for k in range(3))
    for i in range(n):
        pts=[vec3(markers[m],i) for m in ids]
        rel=[(dist(pts[a],pts[b])-d0)/d0 for a,b,d0 in pairs]
        pair_rms.append(math.sqrt(mean([x*x for x in rel])));pair_p95.append(percentile([abs(x) for x in rel],.95))
        c=tuple(mean([p[k] for p in pts]) for k in range(3))
        centered=[sub(sub(p,c),sub(p0,c0)) for p,p0 in zip(pts,pts0)]
        shape.append(math.sqrt(mean([sum(x*x for x in q) for q in centered])))
        areas=[tri_area(pts[a],pts[b],pts[c]) for a,b,c in faces]
        da=[x-y for x,y in zip(areas,area0)]
        dr=[(x-y)/y for x,y in zip(areas,area0)]
        area_abs_rms.append(math.sqrt(mean([x*x for x in da])))
        area_rel_rms.append(math.sqrt(mean([x*x for x in dr])))
    pre_idx=[i for i,x in enumerate(driver) if x<=.05*driver_max]
    pair_noise=math.sqrt(mean([pair_rms[i]**2 for i in pre_idx])) if pre_idx else pair_rms[0]
    area_noise=math.sqrt(mean([area_rel_rms[i]**2 for i in pre_idx])) if pre_idx else area_rel_rms[0]
    peak=max(range(n),key=lambda i:area_rel_rms[i])
    return{"frames":n,"duration_s":(n-1)/fps,"marker_count":len(ids),"unique_face_count":len(faces),
      "driver_max_m":driver_max,"driver_final_m":driver[-1],"max_pair_strain_rms":max(pair_rms),
      "max_pair_abs_strain_p95":max(pair_p95),"max_shape_rms_m":max(shape),
      "max_face_area_change_rms_m2":max(area_abs_rms),"final_face_area_change_rms_m2":area_abs_rms[-1],
      "max_face_area_relative_change_rms":max(area_rel_rms),"final_face_area_relative_change_rms":area_rel_rms[-1],
      "pre_motion_pair_noise_rms":pair_noise,"pre_motion_area_relative_noise_rms":area_noise,
      "area_signal_to_noise":max(area_rel_rms)/max(area_noise,1e-12),"area_peak_time_s":peak/fps}
def main():
    ap=argparse.ArgumentParser();ap.add_argument("--root",required=True);ap.add_argument("--out",required=True);a=ap.parse_args()
    root=pathlib.Path(a.root);out=pathlib.Path(a.out);out.mkdir(parents=True,exist_ok=True);rows=[];metadata={}
    for task in TASKS:
        mp=root/"metadata"/f"{task}.json";md=json.loads(mp.read_text());faces=unique_faces(md)
        metadata[task]={"sha256":sha256(mp),"materials":md["assets"]["foam"]["material"],
                        "face_entries":len(md["markers"]["faces"]),"unique_faces":len(faces)}
        for mat in MATERIALS:
            for trial in range(1,11):
                p=root/"data"/task/mat/f"{trial}.json";m=trial_metrics(p,faces)
                m.update({"task":task,"material":mat,"trial":trial,"source_sha256":sha256(p)});rows.append(m)
    fields=["task","material","trial","frames","duration_s","marker_count","unique_face_count","driver_max_m","driver_final_m",
      "max_pair_strain_rms","max_pair_abs_strain_p95","max_shape_rms_m","max_face_area_change_rms_m2",
      "final_face_area_change_rms_m2","max_face_area_relative_change_rms","final_face_area_relative_change_rms",
      "pre_motion_pair_noise_rms","pre_motion_area_relative_noise_rms","area_signal_to_noise","area_peak_time_s","source_sha256"]
    with(out/"trial_metrics.csv").open("w",newline="")as f:w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(rows)
    summaries=[]
    for task in TASKS:
        for mat in MATERIALS:
            g=[r for r in rows if r["task"]==task and r["material"]==mat];md=metadata[task]["materials"][mat]
            summaries.append({"task":task,"material":mat,"n":len(g),"young_pa":md["young"],"poisson":md["poisson"],
              "density_kg_m3":md["density"],"mass_kg":md["mass"],"driver_mean_m":mean([r["driver_max_m"] for r in g]),
              "driver_sd_m":stdev([r["driver_max_m"] for r in g]),"area_rel_mean":mean([r["max_face_area_relative_change_rms"] for r in g]),
              "area_rel_sd":stdev([r["max_face_area_relative_change_rms"] for r in g]),
              "area_abs_mean_m2":mean([r["max_face_area_change_rms_m2"] for r in g]),
              "area_abs_sd_m2":stdev([r["max_face_area_change_rms_m2"] for r in g]),
              "area_noise_mean":mean([r["pre_motion_area_relative_noise_rms"] for r in g]),
              "area_snr_median":statistics.median(r["area_signal_to_noise"] for r in g)})
    with(out/"group_summary.csv").open("w",newline="")as f:w=csv.DictWriter(f,fieldnames=list(summaries[0]));w.writeheader();w.writerows(summaries)
    comps=[]
    for task in TASKS:
        s=[r["max_face_area_relative_change_rms"] for r in rows if r["task"]==task and r["material"]=="soft"]
        h=[r["max_face_area_relative_change_rms"] for r in rows if r["task"]==task and r["material"]=="hard"]
        pooled=math.sqrt(((len(s)-1)*stdev(s)**2+(len(h)-1)*stdev(h)**2)/max(len(s)+len(h)-2,1));delta=mean(h)-mean(s)
        sd=next(x for x in summaries if x["task"]==task and x["material"]=="soft")
        hd=next(x for x in summaries if x["task"]==task and x["material"]=="hard")
        comps.append({"task":task,"soft_area_rel_mean":mean(s),"hard_area_rel_mean":mean(h),"hard_minus_soft":delta,
          "standardized_difference":delta/pooled if pooled>0 else None,
          "driver_ratio_hard_to_soft":hd["driver_mean_m"]/sd["driver_mean_m"],
          "important_confound":"soft and hard differ simultaneously in E, nu, density and mass; stretching/compression driver travel also differs"})
    findings={"schema":"vulkax.gauge_foam_descriptive","version":2,"provenance":"measured","trial_count":len(rows),
      "observable":"RMS change in local triangular marker-face areas, following GAUGE volumetric-deformable generalized trajectory",
      "metadata":metadata,"comparisons":comps,
      "limitations":["No Vulkax simulation has been fit in this artifact.","Soft and hard differ in E, nu, density and mass.",
        "Stretching and compression also have materially different driver travel across the two foam variants.",
        "Shearing has the closest driver amplitudes and is therefore the cleanest first sim-to-real task, but material attribution remains multivariate."]}
    (out/"findings.json").write_text(json.dumps(findings,indent=2)+"\n")
    print("VALID GAUGE benchmark-native measured analysis",len(rows),"trials")
    for x in summaries:print("AREA_GROUP",x["task"],x["material"],"rel",f'{x["area_rel_mean"]:.6g}',"+/-",f'{x["area_rel_sd"]:.3g}',"driver",f'{x["driver_mean_m"]:.6g}',"snr",f'{x["area_snr_median"]:.3g}')
    for x in comps:print("AREA_COMPARE",x["task"],"hard_minus_soft",f'{x["hard_minus_soft"]:.6g}',"std_diff",x["standardized_difference"],"driver_ratio",x["driver_ratio_hard_to_soft"])
if __name__=="__main__":main()

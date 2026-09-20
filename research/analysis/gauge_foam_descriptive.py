#!/usr/bin/env python3
import argparse,csv,hashlib,json,math,pathlib,statistics
from itertools import combinations
TASKS=("foam stretching","foam compression","foam shearing")
MATERIALS=("soft","hard")
def sha256(p):
    h=hashlib.sha256()
    with open(p,"rb") as f:
        for b in iter(lambda:f.read(1<<20),b""): h.update(b)
    return h.hexdigest()
def vec3(d,i,scale=1e-3): return (float(d["x"][i])*scale,float(d["y"][i])*scale,float(d["z"][i])*scale)
def dist(a,b): return math.sqrt(sum((x-y)**2 for x,y in zip(a,b)))
def norm(a): return math.sqrt(sum(x*x for x in a))
def sub(a,b): return tuple(x-y for x,y in zip(a,b))
def mean(xs): return statistics.fmean(xs) if xs else float("nan")
def stdev(xs): return statistics.stdev(xs) if len(xs)>1 else 0.0
def percentile(xs,q):
    ys=sorted(xs)
    if not ys:return float("nan")
    x=(len(ys)-1)*q;lo=int(math.floor(x));hi=int(math.ceil(x))
    return ys[lo] if lo==hi else ys[lo]*(hi-x)+ys[hi]*(x-lo)
def trial_metrics(path):
    d=json.loads(path.read_text())
    if d["translation unit"]!="mm": raise ValueError("expected GAUGE mm trajectories")
    fps=float(d["FPS"]);markers=d["foam"];ids=sorted(markers)
    n=min(min(len(markers[m][a]) for a in ("x","y","z")) for m in ids)
    base=d["base"];n=min(n,min(len(base[a]) for a in ("x","y","z")))
    pts0={m:vec3(markers[m],0) for m in ids};pairs=[]
    for a,b in combinations(ids,2):
        d0=dist(pts0[a],pts0[b])
        if d0>1e-6:pairs.append((a,b,d0))
    base0=vec3(base,0);driver=[norm(sub(vec3(base,i),base0)) for i in range(n)];driver_max=max(driver)
    strain_rms=[];strain_p95=[];shape=[]
    c0=tuple(mean([pts0[m][k] for m in ids]) for k in range(3))
    for i in range(n):
        pts={m:vec3(markers[m],i) for m in ids}
        rel=[(dist(pts[a],pts[b])-d0)/d0 for a,b,d0 in pairs]
        strain_rms.append(math.sqrt(mean([x*x for x in rel])))
        strain_p95.append(percentile([abs(x) for x in rel],.95))
        c=tuple(mean([pts[m][k] for m in ids]) for k in range(3))
        centered=[sub(sub(pts[m],c),sub(pts0[m],c0)) for m in ids]
        shape.append(math.sqrt(mean([sum(x*x for x in q) for q in centered])))
    pre=[strain_rms[i] for i,x in enumerate(driver) if x<=.05*driver_max]
    noise=math.sqrt(mean([x*x for x in pre])) if pre else strain_rms[0]
    peak=max(range(n),key=lambda i:strain_rms[i])
    return {"frames":n,"duration_s":(n-1)/fps,"marker_count":len(ids),"driver_max_m":driver_max,
      "driver_final_m":driver[-1],"max_pair_strain_rms":max(strain_rms),"final_pair_strain_rms":strain_rms[-1],
      "max_pair_abs_strain_p95":max(strain_p95),"max_shape_rms_m":max(shape),"final_shape_rms_m":shape[-1],
      "pre_motion_strain_noise_rms":noise,"strain_snr":max(strain_rms)/max(noise,1e-12),"peak_time_s":peak/fps}
def main():
    ap=argparse.ArgumentParser();ap.add_argument("--root",required=True);ap.add_argument("--out",required=True);a=ap.parse_args()
    root=pathlib.Path(a.root);out=pathlib.Path(a.out);out.mkdir(parents=True,exist_ok=True);rows=[];metadata={}
    for task in TASKS:
        mp=root/"metadata"/f"{task}.json";md=json.loads(mp.read_text())
        metadata[task]={"sha256":sha256(mp),"materials":md["assets"]["foam"]["material"]}
        for mat in MATERIALS:
            for trial in range(1,11):
                p=root/"data"/task/mat/f"{trial}.json";m=trial_metrics(p)
                m.update({"task":task,"material":mat,"trial":trial,"source_sha256":sha256(p)});rows.append(m)
    fields=["task","material","trial","frames","duration_s","marker_count","driver_max_m","driver_final_m","max_pair_strain_rms",
      "final_pair_strain_rms","max_pair_abs_strain_p95","max_shape_rms_m","final_shape_rms_m","pre_motion_strain_noise_rms",
      "strain_snr","peak_time_s","source_sha256"]
    with (out/"trial_metrics.csv").open("w",newline="") as f:
        w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(rows)
    summaries=[]
    for task in TASKS:
        for mat in MATERIALS:
            g=[r for r in rows if r["task"]==task and r["material"]==mat];md=metadata[task]["materials"][mat]
            summaries.append({"task":task,"material":mat,"n":len(g),"young_pa":md["young"],"poisson":md["poisson"],
              "density_kg_m3":md["density"],"mass_kg":md["mass"],"driver_max_mean_m":mean([r["driver_max_m"] for r in g]),
              "driver_max_sd_m":stdev([r["driver_max_m"] for r in g]),"strain_mean":mean([r["max_pair_strain_rms"] for r in g]),
              "strain_sd":stdev([r["max_pair_strain_rms"] for r in g]),"shape_mean_m":mean([r["max_shape_rms_m"] for r in g]),
              "shape_sd_m":stdev([r["max_shape_rms_m"] for r in g]),"noise_mean":mean([r["pre_motion_strain_noise_rms"] for r in g]),
              "snr_median":statistics.median(r["strain_snr"] for r in g)})
    with (out/"group_summary.csv").open("w",newline="") as f:
        w=csv.DictWriter(f,fieldnames=list(summaries[0]));w.writeheader();w.writerows(summaries)
    comps=[]
    for task in TASKS:
        soft=[r["max_pair_strain_rms"] for r in rows if r["task"]==task and r["material"]=="soft"]
        hard=[r["max_pair_strain_rms"] for r in rows if r["task"]==task and r["material"]=="hard"]
        pooled=math.sqrt(((len(soft)-1)*stdev(soft)**2+(len(hard)-1)*stdev(hard)**2)/max(len(soft)+len(hard)-2,1))
        delta=mean(hard)-mean(soft)
        comps.append({"task":task,"soft_mean":mean(soft),"hard_mean":mean(hard),"hard_minus_soft":delta,
          "standardized_difference":delta/pooled if pooled>0 else None,
          "important_confound":"soft and hard differ simultaneously in E, nu, density and mass"})
    findings={"schema":"vulkax.gauge_foam_descriptive","version":1,"provenance":"measured","trial_count":len(rows),
      "tasks":list(TASKS),"materials":list(MATERIALS),"metadata":metadata,"comparisons":comps,
      "limitations":["Descriptive only: no Vulkax fit or causal material attribution.",
        "Soft and hard foams differ simultaneously in Young's modulus, Poisson ratio, density and mass.",
        "Pairwise-distance strain is a generic marker-shape proxy, not the benchmark's official task metric.",
        "A later solver comparison must reproduce geometry, boundary conditions and prescribed driver trajectory."]}
    (out/"findings.json").write_text(json.dumps(findings,indent=2)+"\n")
    print("VALID GAUGE measured descriptive analysis",len(rows),"trials")
    for x in summaries: print("GROUP",x["task"],x["material"],"strain",f'{x["strain_mean"]:.6g}',"+/-",f'{x["strain_sd"]:.3g}',"driver_m",f'{x["driver_max_mean_m"]:.6g}',"snr_med",f'{x["snr_median"]:.3g}')
    for x in comps: print("COMPARE",x["task"],"hard_minus_soft",f'{x["hard_minus_soft"]:.6g}',"std_diff",x["standardized_difference"])
if __name__=="__main__":main()

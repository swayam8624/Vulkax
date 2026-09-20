#!/usr/bin/env python3
import argparse,csv,json,math,pathlib,statistics

def v3(d,i,s=1e-3): return [float(d["x"][i])*s,float(d["y"][i])*s,float(d["z"][i])*s]
def sub(a,b): return [a[i]-b[i] for i in range(3)]
def add(a,b): return [a[i]+b[i] for i in range(3)]
def scale(a,s): return [x*s for x in a]
def norm(a): return math.sqrt(sum(x*x for x in a))
def cross(a,b): return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def area(a,b,c): return .5*norm(cross(sub(b,a),sub(c,a)))
def rms(xs): return math.sqrt(statistics.fmean(x*x for x in xs))
def corr(a,b):
    ma=statistics.fmean(a);mb=statistics.fmean(b)
    da=[x-ma for x in a];db=[x-mb for x in b]
    den=math.sqrt(sum(x*x for x in da)*sum(x*x for x in db))
    return sum(x*y for x,y in zip(da,db))/den if den>0 else 0.0
def unique_faces(md):
    seen=set();out=[]
    for f in md["markers"]["faces"]:
        k=tuple(int(x) for x in f)
        if k not in seen:seen.add(k);out.append(k)
    return out
def choose_trial(root,material):
    rec=[]
    for trial in range(1,11):
        p=root/"data"/"foam shearing"/material/f"{trial}.json";d=json.loads(p.read_text())
        base=d["base"];n=min(len(base[a]) for a in ("x","y","z"));p0=v3(base,0)
        mags=[norm(sub(v3(base,i),p0)) for i in range(n)]
        rec.append((max(mags),trial,d))
    med=statistics.median(x[0] for x in rec)
    return min(rec,key=lambda x:(abs(x[0]-med),x[1]))
def evaluate(d,faces):
    foam=d["foam"];ids=sorted(foam);n=min(min(len(foam[m][a]) for a in ("x","y","z")) for m in ids)
    base=d["base"];n=min(n,min(len(base[a]) for a in ("x","y","z")))
    pts0=[v3(foam[m],0) for m in ids];a0=[area(pts0[i],pts0[j],pts0[k]) for i,j,k in faces]
    lo=[min(p[q] for p in pts0) for q in range(3)];hi=[max(p[q] for p in pts0) for q in range(3)]
    axis=max(range(3),key=lambda q:hi[q]-lo[q]);span=hi[axis]-lo[axis]
    b0=v3(base,0)
    measured=[];affine=[];driver=[]
    for frame in range(n):
        disp=sub(v3(base,frame),b0);driver.append(norm(disp))
        pts=[v3(foam[m],frame) for m in ids]
        am=[area(pts[i],pts[j],pts[k]) for i,j,k in faces]
        measured.append(rms([(x-y)/y for x,y in zip(am,a0)]))
        pred=[]
        for p in pts0:
            alpha=(p[axis]-lo[axis])/span if span>0 else 0.0
            pred.append(add(p,scale(disp,alpha)))
        ap=[area(pred[i],pred[j],pred[k]) for i,j,k in faces]
        affine.append(rms([(x-y)/y for x,y in zip(ap,a0)]))
    peak=max(measured);err=rms([x-y for x,y in zip(affine,measured)])
    return {"axis":axis,"span_m":span,"frames":n,"measured":measured,"affine":affine,"driver":driver,
            "measured_peak":peak,"affine_peak":max(affine),"rmse":err,
            "nrmse_to_measured_peak":err/max(peak,1e-15),"correlation":corr(measured,affine)}
def main():
    ap=argparse.ArgumentParser();ap.add_argument("--root",required=True);ap.add_argument("--out",required=True);a=ap.parse_args()
    root=pathlib.Path(a.root);out=pathlib.Path(a.out);out.mkdir(parents=True,exist_ok=True)
    md=json.loads((root/"metadata"/"foam shearing.json").read_text());faces=unique_faces(md)
    results={}
    for mat in ("soft","hard"):
        peak,trial,d=choose_trial(root,mat);r=evaluate(d,faces);r["selected_trial"]=trial;r["driver_peak_m"]=peak;results[mat]=r
        with (out/f"curve_{mat}.csv").open("w",newline="") as f:
            w=csv.writer(f);w.writerow(["frame","driver_m","measured_area_rel_rms","affine_area_rel_rms"])
            for i,(dr,m,p) in enumerate(zip(r["driver"],r["measured"],r["affine"])):w.writerow([i,dr,m,p])
    summary={"schema":"vulkax.gauge_affine_null","version":1,"provenance":"measured+analytic",
      "model":"zero-parameter linear-through-thickness affine displacement field from measured base motion",
      "faces":len(faces),
      "soft":{k:v for k,v in results["soft"].items() if k not in ("measured","affine","driver")},
      "hard":{k:v for k,v in results["hard"].items() if k not in ("measured","affine","driver")},
      "measured_peak_soft_minus_hard":results["soft"]["measured_peak"]-results["hard"]["measured_peak"],
      "affine_peak_soft_minus_hard":results["soft"]["affine_peak"]-results["hard"]["affine_peak"],
      "interpretation_rule":"If affine NRMSE is large while measured soft-hard separation is much larger than affine separation, kinematics alone are insufficient; this does not prove a constitutive model is correct.",
      "warning":"No material parameter fitting is performed."}
    (out/"summary.json").write_text(json.dumps(summary,indent=2)+"\n")
    print("VALID GAUGE affine null baseline")
    for m in ("soft","hard"):
        r=results[m];print("AFFINE",m,"trial",r["selected_trial"],"measured_peak",r["measured_peak"],
            "affine_peak",r["affine_peak"],"nrmse",r["nrmse_to_measured_peak"],"corr",r["correlation"],"axis",r["axis"])
    print("SEPARATION measured",summary["measured_peak_soft_minus_hard"],"affine",summary["affine_peak_soft_minus_hard"])
if __name__=="__main__":main()

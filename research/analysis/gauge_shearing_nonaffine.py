#!/usr/bin/env python3
import argparse,csv,json,math,pathlib,statistics

def v3(d,i): return [float(d[a][i])*1e-3 for a in ("x","y","z")]
def sub(a,b): return [x-y for x,y in zip(a,b)]
def add(a,b): return [x+y for x,y in zip(a,b)]
def smul(a,s): return [x*s for x in a]
def norm(a): return math.sqrt(sum(x*x for x in a))
def cross(a,b): return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def area(a,b,c): return 0.5*norm(cross(sub(b,a),sub(c,a)))
def rms(xs): return math.sqrt(statistics.fmean(x*x for x in xs)) if xs else 0.0
def faces(md):
    out=[]; seen=set()
    for f in md["markers"]["faces"]:
        k=tuple(map(int,f))
        if k not in seen: seen.add(k); out.append(k)
    return out

def analyze(path,fs):
    d=json.loads(path.read_text()); foam=d["foam"]; ids=sorted(foam); base=d["base"]
    n=min([len(base[a]) for a in ("x","y","z")]+[len(foam[m][a]) for m in ids for a in ("x","y","z")])
    p0=[v3(foam[m],0) for m in ids]
    lo=[min(p[k] for p in p0) for k in range(3)]; hi=[max(p[k] for p in p0) for k in range(3)]
    axis=max(range(3),key=lambda k:hi[k]-lo[k]); span=hi[axis]-lo[axis]; b0=v3(base,0)
    a0=[area(p0[i],p0[j],p0[k]) for i,j,k in fs]
    driver=[]; marker=[]; face=[]; measured=[]; affine=[]
    for t in range(n):
        disp=sub(v3(base,t),b0); driver.append(norm(disp))
        pm=[v3(foam[m],t) for m in ids]
        pp=[add(p,smul(disp,(p[axis]-lo[axis])/span)) for p in p0]
        marker.append(rms([norm(sub(x,y)) for x,y in zip(pm,pp)]))
        am=[area(pm[i],pm[j],pm[k]) for i,j,k in fs]
        ap=[area(pp[i],pp[j],pp[k]) for i,j,k in fs]
        rm=[(x-y)/y for x,y in zip(am,a0)]; rp=[(x-y)/y for x,y in zip(ap,a0)]
        measured.append(rms(rm)); affine.append(rms(rp)); face.append(rms([x-y for x,y in zip(rm,rp)]))
    peak=max(driver); pre=[i for i,x in enumerate(driver) if x<=0.05*peak]; noise=rms([face[i] for i in pre])
    return {"driver_peak_m":peak,"axis":axis,"span_m":span,"marker_nonaffine_rms_m":rms(marker),
            "marker_nonaffine_peak_m":max(marker),"face_nonaffine_rms":rms(face),"face_nonaffine_peak":max(face),
            "face_nonaffine_snr":max(face)/max(noise,1e-12),
            "affine_face_nrmse":rms([x-y for x,y in zip(measured,affine)])/max(max(measured),1e-15)}

def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--root",required=True); ap.add_argument("--out",required=True); a=ap.parse_args()
    root=pathlib.Path(a.root); out=pathlib.Path(a.out); out.mkdir(parents=True,exist_ok=True)
    md=json.loads((root/"metadata"/"foam shearing.json").read_text()); fs=faces(md); rows=[]
    for material in ("soft","hard"):
        for trial in range(1,11):
            r=analyze(root/"data"/"foam shearing"/material/(str(trial)+".json"),fs)
            r.update({"material":material,"trial":trial}); rows.append(r)
    fields=["material","trial","driver_peak_m","axis","span_m","marker_nonaffine_rms_m","marker_nonaffine_peak_m","face_nonaffine_rms","face_nonaffine_peak","face_nonaffine_snr","affine_face_nrmse"]
    with (out/"trial_nonaffine.csv").open("w",newline="") as f:
        w=csv.DictWriter(f,fieldnames=fields); w.writeheader(); w.writerows(rows)
    groups={}
    for material in ("soft","hard"):
        g=[r for r in rows if r["material"]==material]; groups[material]={}
        for k in fields[2:]:
            if k=="axis": continue
            vals=[float(r[k]) for r in g]; groups[material][k+"_mean"]=statistics.fmean(vals); groups[material][k+"_sd"]=statistics.stdev(vals)
    s=[r["face_nonaffine_rms"] for r in rows if r["material"]=="soft"]; h=[r["face_nonaffine_rms"] for r in rows if r["material"]=="hard"]
    ps=statistics.stdev(s); ph=statistics.stdev(h); pooled=math.sqrt((9*ps*ps+9*ph*ph)/18); delta=statistics.fmean(s)-statistics.fmean(h)
    result={"schema":"vulkax.gauge_shearing_nonaffine","version":1,"provenance":"measured+analytic-null","trial_count":20,
            "null_model":"measured base displacement linearly distributed through longest initial marker axis; zero fitted parameters",
            "groups":groups,"soft_minus_hard_face_nonaffine_rms":delta,"standardized_soft_minus_hard":delta/pooled if pooled else None,
            "warning":"Residual is not a Young's-modulus estimate; GAUGE soft/hard differ in multiple physical properties."}
    (out/"summary.json").write_text(json.dumps(result,indent=2)+"\n")
    print("VALID GAUGE non-affine residual analysis")
    for m in ("soft","hard"): print("NONAFFINE",m,groups[m])
    print("NONAFFINE_SEPARATION",delta,result["standardized_soft_minus_hard"])
if __name__=="__main__": main()

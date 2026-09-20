#!/usr/bin/env python3
import argparse,csv,json,math,pathlib,statistics,subprocess,tempfile

def v3(d,i,s=1e-3): return [float(d["x"][i])*s,float(d["y"][i])*s,float(d["z"][i])*s]
def sub(a,b): return [a[i]-b[i] for i in range(3)]
def add(a,b): return [a[i]+b[i] for i in range(3)]
def smul(a,s): return [x*s for x in a]
def norm(a): return math.sqrt(sum(x*x for x in a))
def cross(a,b): return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def area(a,b,c): return .5*norm(cross(sub(b,a),sub(c,a)))
def rms(xs): return math.sqrt(statistics.fmean(x*x for x in xs)) if xs else 0.0
def corr(a,b):
    ma=statistics.fmean(a);mb=statistics.fmean(b)
    aa=[x-ma for x in a];bb=[x-mb for x in b]
    den=math.sqrt(sum(x*x for x in aa)*sum(x*x for x in bb))
    return sum(x*y for x,y in zip(aa,bb))/den if den else 0.0

def unique_faces(md):
    out=[];seen=set()
    for f in md["markers"]["faces"]:
        k=tuple(map(int,f))
        if k not in seen: seen.add(k);out.append(k)
    return out

def write_inputs(d,marker_path,driver_path):
    foam=d["foam"]; ids=sorted(foam); base=d["base"]
    n=min([len(base[a]) for a in ("x","y","z")]+[len(foam[m][a]) for m in ids for a in ("x","y","z")])
    with marker_path.open("w",newline="") as f:
        w=csv.writer(f);w.writerow(["marker_id","x_m","y_m","z_m"])
        for mid in ids:w.writerow([mid,*v3(foam[mid],0)])
    b0=v3(base,0);fps=float(d["FPS"])
    with driver_path.open("w",newline="") as f:
        w=csv.writer(f);w.writerow(["frame","time_s","dx_m","dy_m","dz_m","magnitude_m"])
        for frame in range(n):
            disp=sub(v3(base,frame),b0)
            w.writerow([frame,frame/fps,*disp,norm(disp)])
    return ids,n

def read_pred(path):
    frames={}
    with path.open(newline="") as f:
        for r in csv.DictReader(f):
            frames.setdefault(int(r["frame"]),{})[r["marker_id"]]=[float(r["x_m"]),float(r["y_m"]),float(r["z_m"])]
    return frames

def curves(d,pred,faces):
    foam=d["foam"];ids=sorted(foam);base=d["base"]
    n=min(len(pred),*[len(base[a]) for a in ("x","y","z")],*[len(foam[m][a]) for m in ids for a in ("x","y","z")])
    p0=[v3(foam[m],0) for m in ids]
    a0=[area(p0[i],p0[j],p0[k]) for i,j,k in faces]
    lo=[min(p[q] for p in p0) for q in range(3)];hi=[max(p[q] for p in p0) for q in range(3)]
    axis=max(range(3),key=lambda q:hi[q]-lo[q]);span=hi[axis]-lo[axis];b0=v3(base,0)
    measured=[];affine=[];vulkax=[]
    for frame in range(n):
        pm=[v3(foam[m],frame) for m in ids]
        aa=[area(pm[i],pm[j],pm[k]) for i,j,k in faces]
        measured.append(rms([(x-y)/max(y,1e-15) for x,y in zip(aa,a0)]))
        disp=sub(v3(base,frame),b0)
        pa=[add(p,smul(disp,(p[axis]-lo[axis])/span)) for p in p0]
        aaa=[area(pa[i],pa[j],pa[k]) for i,j,k in faces]
        affine.append(rms([(x-y)/max(y,1e-15) for x,y in zip(aaa,a0)]))
        pp=[pred[frame][m] for m in ids]
        ap=[area(pp[i],pp[j],pp[k]) for i,j,k in faces]
        vulkax.append(rms([(x-y)/max(y,1e-15) for x,y in zip(ap,a0)]))
    return measured,affine,vulkax

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--root",required=True)
    ap.add_argument("--exe",required=True)
    ap.add_argument("--out",required=True)
    ap.add_argument("--dt",type=float,default=1.0/12000.0)
    a=ap.parse_args()
    root=pathlib.Path(a.root);out=pathlib.Path(a.out);out.mkdir(parents=True,exist_ok=True)
    md=json.loads((root/"metadata"/"foam shearing.json").read_text());faces=unique_faces(md)
    rows=[]
    with tempfile.TemporaryDirectory(prefix="vulkax-gauge-repeat-") as td:
        td=pathlib.Path(td)
        for material in ("soft","hard"):
            mat=md["assets"]["foam"]["material"][material]
            for trial in range(1,11):
                raw=root/"data"/"foam shearing"/material/f"{trial}.json"
                d=json.loads(raw.read_text())
                marker=td/f"{material}_{trial}_markers.csv";driver=td/f"{material}_{trial}_driver.csv"
                pred=td/f"{material}_{trial}_pred.csv"
                write_inputs(d,marker,driver)
                cmd=[a.exe,str(marker),str(driver),str(pred),str(float(mat["young"])),str(float(mat["poisson"])),
                     str(float(mat["density"])),str(float(mat["mass"])),repr(a.dt),f"{material}-{trial}"]
                cp=subprocess.run(cmd,text=True,capture_output=True)
                if cp.returncode:
                    raise SystemExit(f"forward failed {material} trial {trial}: {cp.stderr}\n{cp.stdout}")
                p=read_pred(pred);meas,aff,vkx=curves(d,p,faces)
                peak=max(meas)
                vr=rms([x-y for x,y in zip(vkx,meas)]);ar=rms([x-y for x,y in zip(aff,meas)])
                rows.append({
                    "material":material,"trial":trial,"frames":len(meas),"measured_peak":peak,
                    "vulkax_peak":max(vkx),"affine_peak":max(aff),
                    "vulkax_rmse":vr,"affine_rmse":ar,
                    "vulkax_nrmse":vr/max(peak,1e-15),"affine_nrmse":ar/max(peak,1e-15),
                    "vulkax_corr":corr(meas,vkx),"affine_corr":corr(meas,aff),
                    "beats_affine":int(vr<ar)
                })
    fields=list(rows[0])
    with (out/"per_trial.csv").open("w",newline="") as f:
        w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(rows)
    groups={}
    for material in ("soft","hard"):
        g=[r for r in rows if r["material"]==material]
        groups[material]={
            "trials":len(g),
            "vulkax_wins":sum(r["beats_affine"] for r in g),
            "mean_vulkax_nrmse":statistics.fmean(r["vulkax_nrmse"] for r in g),
            "sd_vulkax_nrmse":statistics.stdev(r["vulkax_nrmse"] for r in g),
            "mean_affine_nrmse":statistics.fmean(r["affine_nrmse"] for r in g),
            "sd_affine_nrmse":statistics.stdev(r["affine_nrmse"] for r in g),
            "mean_vulkax_corr":statistics.fmean(r["vulkax_corr"] for r in g),
            "mean_affine_corr":statistics.fmean(r["affine_corr"] for r in g),
        }
    summary={
      "schema":"vulkax.gauge_shearing_repeat_forward","version":1,
      "provenance":"measured+model-prediction","fit_performed":False,
      "dt_s":a.dt,"trial_count":len(rows),"groups":groups,
      "overall_vulkax_wins":sum(r["beats_affine"] for r in rows),
      "decision":"model_family_still_inadequate" if sum(r["beats_affine"] for r in rows)<16 else "repeatability_gate_passed",
      "warning":"No inverse fitting. Same GAUGE metadata-only material model is evaluated independently on all 20 measured shearing repeats."
    }
    (out/"summary.json").write_text(json.dumps(summary,indent=2)+"\n")
    print("VALID GAUGE all-repeat metadata-only forward test")
    for m,g in groups.items():
        print("REPEATS",m,"wins",g["vulkax_wins"],"/",g["trials"],
              "vulkax_nrmse",g["mean_vulkax_nrmse"],"+/-",g["sd_vulkax_nrmse"],
              "affine_nrmse",g["mean_affine_nrmse"],"+/-",g["sd_affine_nrmse"])
    print("DECISION",summary["decision"])

if __name__=="__main__": main()

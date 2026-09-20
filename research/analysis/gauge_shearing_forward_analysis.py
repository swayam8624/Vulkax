#!/usr/bin/env python3
import argparse,csv,json,math,pathlib,statistics

def vsub(a,b): return [x-y for x,y in zip(a,b)]
def norm(a): return math.sqrt(sum(x*x for x in a))
def cross(a,b): return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def area(a,b,c): return .5*norm(cross(vsub(b,a),vsub(c,a)))
def rms(xs): return math.sqrt(statistics.fmean(x*x for x in xs)) if xs else 0.0
def corr(a,b):
    ma=statistics.fmean(a);mb=statistics.fmean(b)
    aa=[x-ma for x in a];bb=[x-mb for x in b]
    den=math.sqrt(sum(x*x for x in aa)*sum(x*x for x in bb))
    return sum(x*y for x,y in zip(aa,bb))/den if den else 0.0

def read_markers(path):
    frames={}
    with open(path,newline="") as f:
        r=csv.DictReader(f)
        for row in r:
            k=int(row["frame"]);frames.setdefault(k,{})[row["marker_id"]]=[
                float(row["x_m"]),float(row["y_m"]),float(row["z_m"])]
    return frames

def read_affine(path):
    with open(path,newline="") as f:
        return [float(r["affine_area_rel_rms"]) for r in csv.DictReader(f)]

def face_curve(frames,faces,ids):
    keys=sorted(frames)
    p0=[frames[keys[0]][m] for m in ids]
    a0=[area(p0[i],p0[j],p0[k]) for i,j,k in faces]
    curve=[]
    for frame in keys:
        pts=[frames[frame][m] for m in ids]
        aa=[area(pts[i],pts[j],pts[k]) for i,j,k in faces]
        curve.append(rms([(x-y)/max(y,1e-15) for x,y in zip(aa,a0)]))
    return curve

def marker_rmse(pred,measured,ids,n):
    es=[]
    for f in range(n):
        for m in ids:
            es.append(norm(vsub(pred[f][m],measured[f][m])))
    return rms(es)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--contract-dir",required=True)
    ap.add_argument("--forward-dir",required=True)
    ap.add_argument("--affine-dir",required=True)
    ap.add_argument("--out",required=True)
    a=ap.parse_args()
    cdir=pathlib.Path(a.contract_dir); fdir=pathlib.Path(a.forward_dir); adir=pathlib.Path(a.affine_dir); out=pathlib.Path(a.out)
    out.mkdir(parents=True,exist_ok=True)
    contract=json.loads((cdir/"contract.json").read_text())
    faces=[tuple(map(int,x)) for x in contract["unique_faces"]]
    results={}
    curves={}
    for material in ("soft","hard"):
        measured=read_markers(cdir/f"markers_measured_{material}.csv")
        pred=read_markers(fdir/f"predicted_{material}.csv")
        ids=sorted(measured[0])
        common=min(len(measured),len(pred))
        if common<10: raise SystemExit("too few common GAUGE frames")
        for f in range(common):
            if set(pred[f])!=set(ids) or set(measured[f])!=set(ids):
                raise SystemExit(f"marker identity mismatch {material} frame {f}")
        measured={k:measured[k] for k in range(common)}
        pred={k:pred[k] for k in range(common)}
        mc=face_curve(measured,faces,ids); pc=face_curve(pred,faces,ids)
        ac=read_affine(adir/f"curve_{material}.csv")[:common]
        peak=max(mc)
        prmse=rms([x-y for x,y in zip(pc,mc)])
        armse=rms([x-y for x,y in zip(ac,mc)])
        results[material]={
          "frames":common,
          "measured_peak_face_area_rms":peak,
          "predicted_peak_face_area_rms":max(pc),
          "affine_peak_face_area_rms":max(ac),
          "forward_face_rmse":prmse,
          "affine_face_rmse":armse,
          "forward_nrmse":prmse/max(peak,1e-15),
          "affine_nrmse":armse/max(peak,1e-15),
          "forward_vs_affine_improvement_fraction":1.0-prmse/max(armse,1e-15),
          "forward_correlation":corr(mc,pc),
          "affine_correlation":corr(mc,ac),
          "marker_position_rmse_m":marker_rmse(pred,measured,ids,common),
        }
        curves[material]=(mc,pc,ac)
        with (out/f"curve_{material}.csv").open("w",newline="") as f:
            w=csv.writer(f);w.writerow(["frame","measured_face_rel_rms","vulkax_forward_face_rel_rms","affine_face_rel_rms"])
            for i,row in enumerate(zip(mc,pc,ac)):w.writerow([i,*row])

    measured_sep=max(curves["soft"][0])-max(curves["hard"][0])
    pred_sep=max(curves["soft"][1])-max(curves["hard"][1])
    affine_sep=max(curves["soft"][2])-max(curves["hard"][2])
    beats_both=all(results[m]["forward_face_rmse"]<results[m]["affine_face_rmse"] for m in ("soft","hard"))
    separation_sign=(measured_sep==0) or (pred_sep*measured_sep>0)
    decision="proceed_to_controlled_inverse_fitting" if beats_both and separation_sign else "do_not_fit_material_parameters"
    summary={
      "schema":"vulkax.gauge_shearing_no_fit_forward","version":1,
      "provenance":"measured+model-prediction",
      "fit_performed":False,
      "materials":results,
      "soft_minus_hard_peak":{"measured":measured_sep,"vulkax_forward":pred_sep,"affine":affine_sep},
      "beats_affine_both_materials":beats_both,
      "material_separation_sign_agrees":separation_sign,
      "decision":decision,
      "decision_rule":"Permit inverse fitting only if the metadata-only Vulkax forward model has lower benchmark-native face-area RMSE than the zero-parameter affine null for both materials and predicts the observed soft-hard peak-separation sign.",
      "limitations":[
        "Vulkax uses a mass/density volume-consistent rectangular-prism geometry proxy because the selected marker set does not span the full cross-section.",
        "Gravity is disabled because a verified GAUGE-to-world gravity-axis transform has not yet been established.",
        "The prescribed-particle boundary is an auditable projection approximation, not a calibrated fixture/contact model.",
        "Soft and hard differ jointly in E, nu, density and mass; this experiment does not isolate Young's modulus."
      ]
    }
    (out/"summary.json").write_text(json.dumps(summary,indent=2)+"\n")
    print("VALID GAUGE metadata-only forward sanity test")
    for m in ("soft","hard"):
        r=results[m]
        print("FORWARD",m,"nrmse",r["forward_nrmse"],"affine",r["affine_nrmse"],
              "improvement",r["forward_vs_affine_improvement_fraction"],
              "marker_rmse_m",r["marker_position_rmse_m"],"corr",r["forward_correlation"])
    print("SEPARATION measured",measured_sep,"forward",pred_sep,"affine",affine_sep)
    print("DECISION",decision)

if __name__=="__main__": main()

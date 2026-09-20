#!/usr/bin/env python3
import argparse,csv,json,math,pathlib,statistics,subprocess,sys

def rms(xs): return math.sqrt(statistics.fmean(x*x for x in xs)) if xs else 0.0
def read_curve(path):
    with path.open(newline="") as f:
        rows=list(csv.DictReader(f))
    return [float(r["measured_face_rel_rms"]) for r in rows],[float(r["vulkax_forward_face_rel_rms"]) for r in rows]

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--exe",required=True)
    ap.add_argument("--contract-dir",required=True)
    ap.add_argument("--affine-dir",required=True)
    ap.add_argument("--out",required=True)
    a=ap.parse_args()
    out=pathlib.Path(a.out);out.mkdir(parents=True,exist_ok=True)
    here=pathlib.Path(__file__).resolve().parent
    dts=[1.0/6000.0,1.0/12000.0,1.0/24000.0]
    labels=["coarse","baseline","fine"]
    summaries={}
    curves={}
    for label,dt in zip(labels,dts):
        fd=out/f"forward_{label}";ad=out/f"analysis_{label}"
        subprocess.run([sys.executable,str(here/"run_gauge_shearing_forward.py"),"--exe",a.exe,
                        "--contract-dir",a.contract_dir,"--out",str(fd),"--dt",repr(dt)],check=True)
        subprocess.run([sys.executable,str(here/"gauge_shearing_forward_analysis.py"),"--contract-dir",a.contract_dir,
                        "--forward-dir",str(fd),"--affine-dir",a.affine_dir,"--out",str(ad)],check=True)
        summaries[label]=json.loads((ad/"summary.json").read_text())
        curves[label]={}
        for material in ("soft","hard"):
            curves[label][material]=read_curve(ad/f"curve_{material}.csv")
    rows=[]
    for material in ("soft","hard"):
        measured=curves["baseline"][material][0]
        peak=max(measured)
        for label,dt in zip(labels,dts):
            r=summaries[label]["materials"][material]
            rows.append({"material":material,"level":label,"dt_s":dt,
                         "forward_nrmse":r["forward_nrmse"],"affine_nrmse":r["affine_nrmse"],
                         "forward_rmse":r["forward_face_rmse"],"correlation":r["forward_correlation"]})
        pcoarse=curves["coarse"][material][1];pbase=curves["baseline"][material][1];pfine=curves["fine"][material][1]
        n=min(len(pcoarse),len(pbase),len(pfine))
        cb=rms([pcoarse[i]-pbase[i] for i in range(n)])/max(peak,1e-15)
        bf=rms([pbase[i]-pfine[i] for i in range(n)])/max(peak,1e-15)
        summaries.setdefault("convergence",{})[material]={
            "coarse_to_baseline_prediction_nrmse":cb,
            "baseline_to_fine_prediction_nrmse":bf,
            "baseline_data_misfit_nrmse":summaries["baseline"]["materials"][material]["forward_nrmse"],
            "fine_data_misfit_nrmse":summaries["fine"]["materials"][material]["forward_nrmse"],
            "fine_change_to_misfit_ratio":bf/max(summaries["baseline"]["materials"][material]["forward_nrmse"],1e-15)
        }
    with (out/"levels.csv").open("w",newline="") as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
    result={
      "schema":"vulkax.gauge_shearing_timestep_forensics","version":1,
      "provenance":"measured+model-prediction","fit_performed":False,
      "dts_s":dict(zip(labels,dts)),
      "convergence":summaries["convergence"],
      "interpretation_rule":"If baseline-to-fine prediction change is small relative to baseline data misfit, timestep error is unlikely to explain the dominant sim-to-real miss.",
      "warning":"This isolates temporal-step sensitivity only; it does not validate spatial resolution, geometry, boundaries, gravity, or constitutive choice."
    }
    (out/"summary.json").write_text(json.dumps(result,indent=2)+"\n")
    print("VALID GAUGE timestep forensics")
    for m,d in result["convergence"].items():
        print("TIMESTEP",m,"coarse_base",d["coarse_to_baseline_prediction_nrmse"],
              "base_fine",d["baseline_to_fine_prediction_nrmse"],
              "misfit",d["baseline_data_misfit_nrmse"],
              "fine_change/misfit",d["fine_change_to_misfit_ratio"])

if __name__=="__main__": main()

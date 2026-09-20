#!/usr/bin/env python3
import argparse,csv,json,math,pathlib,statistics,subprocess,sys

def rms(xs): return math.sqrt(statistics.fmean(x*x for x in xs)) if xs else 0.0
def read_curve(path):
    with path.open(newline="") as f: rows=list(csv.DictReader(f))
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
    levels=[("coarse",1.0/6000.0),("baseline",1.0/12000.0),("fine",1.0/24000.0)]
    summaries={};curves={};run_status={}
    for label,dt in levels:
        fd=out/f"forward_{label}";ad=out/f"analysis_{label}"
        cp=subprocess.run([sys.executable,str(here/"run_gauge_shearing_forward.py"),"--exe",a.exe,
                           "--contract-dir",a.contract_dir,"--out",str(fd),"--dt",repr(dt)],
                          text=True,capture_output=True)
        if cp.stdout: print(cp.stdout,end="")
        if cp.stderr: print(cp.stderr,end="",file=sys.stderr)
        if cp.returncode:
            run_status[label]={"status":"failed","dt_s":dt,"returncode":cp.returncode,
                               "failure_tail":(cp.stderr or cp.stdout)[-1000:]}
            print("TIMESTEP_LEVEL_FAILED",label,dt,cp.returncode)
            continue
        subprocess.run([sys.executable,str(here/"gauge_shearing_forward_analysis.py"),"--contract-dir",a.contract_dir,
                        "--forward-dir",str(fd),"--affine-dir",a.affine_dir,"--out",str(ad)],check=True)
        summaries[label]=json.loads((ad/"summary.json").read_text())
        curves[label]={m:read_curve(ad/f"curve_{m}.csv") for m in ("soft","hard")}
        run_status[label]={"status":"success","dt_s":dt}
    if "baseline" not in summaries or "fine" not in summaries:
        raise SystemExit("baseline and fine timestep levels must succeed for convergence evidence")
    rows=[];convergence={}
    for material in ("soft","hard"):
        measured=curves["baseline"][material][0];peak=max(measured)
        for label,dt in levels:
            if label not in summaries: continue
            r=summaries[label]["materials"][material]
            rows.append({"material":material,"level":label,"dt_s":dt,
                         "forward_nrmse":r["forward_nrmse"],"affine_nrmse":r["affine_nrmse"],
                         "forward_rmse":r["forward_face_rmse"],"correlation":r["forward_correlation"]})
        pbase=curves["baseline"][material][1];pfine=curves["fine"][material][1]
        n=min(len(pbase),len(pfine))
        bf=rms([pbase[i]-pfine[i] for i in range(n)])/max(peak,1e-15)
        d={
          "baseline_to_fine_prediction_nrmse":bf,
          "baseline_data_misfit_nrmse":summaries["baseline"]["materials"][material]["forward_nrmse"],
          "fine_data_misfit_nrmse":summaries["fine"]["materials"][material]["forward_nrmse"],
          "fine_change_to_misfit_ratio":bf/max(summaries["baseline"]["materials"][material]["forward_nrmse"],1e-15)
        }
        if "coarse" in curves:
            pc=curves["coarse"][material][1]
            n2=min(len(pc),len(pbase))
            d["coarse_to_baseline_prediction_nrmse"]=rms([pc[i]-pbase[i] for i in range(n2)])/max(peak,1e-15)
        convergence[material]=d
    with (out/"levels.csv").open("w",newline="") as f:
        w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
    result={
      "schema":"vulkax.gauge_shearing_timestep_forensics","version":2,
      "provenance":"measured+model-prediction","fit_performed":False,
      "levels":run_status,"convergence":convergence,
      "coarse_instability_is_evidence":run_status.get("coarse",{}).get("status")=="failed",
      "interpretation_rule":"Treat an inverted coarse step as a stability failure, not missing data. If baseline-to-fine prediction change is small relative to baseline data misfit, the dominant sim-to-real miss is not explained by temporal-step error.",
      "warning":"This isolates temporal-step sensitivity only; it does not validate spatial resolution, geometry, boundary conditions, gravity, transfer dissipation or constitutive choice."
    }
    (out/"summary.json").write_text(json.dumps(result,indent=2)+"\n")
    print("VALID GAUGE timestep forensics")
    for m,d in convergence.items():
        print("TIMESTEP",m,"base_fine",d["baseline_to_fine_prediction_nrmse"],
              "misfit",d["baseline_data_misfit_nrmse"],
              "fine_misfit",d["fine_data_misfit_nrmse"],
              "fine_change/misfit",d["fine_change_to_misfit_ratio"])
    print("COARSE_STATUS",run_status.get("coarse"))
if __name__=="__main__": main()

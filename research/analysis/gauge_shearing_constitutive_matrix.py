#!/usr/bin/env python3
"""Factorial no-fit GAUGE constitutive/transfer adequacy probe.

Every world uses the frozen measured material metadata and driver trajectory.
No trajectory error is used to tune E, nu, density, mass, geometry, boundary
thickness, timestep, or model parameters. This is a model-family falsification
matrix, not an inverse-fit search.
"""
import argparse,json,pathlib,subprocess,sys

TRANSFERS=("APIC","PIC","FLIP")
MODELS=("neo_hookean_log_j","neo_hookean_quadratic_j","st_venant_kirchhoff")

def run(cmd):
    cp=subprocess.run(cmd,text=True,capture_output=True)
    if cp.stdout: print(cp.stdout,end="")
    if cp.stderr: print(cp.stderr,end="",file=sys.stderr)
    if cp.returncode:
        raise SystemExit(f"command failed ({cp.returncode}): {' '.join(map(str,cmd))}")
    return cp

def dominates(a,b):
    return (a["mean_face_nrmse"]<=b["mean_face_nrmse"] and
            a["mean_marker_rmse_m"]<=b["mean_marker_rmse_m"] and
            (a["mean_face_nrmse"]<b["mean_face_nrmse"] or
             a["mean_marker_rmse_m"]<b["mean_marker_rmse_m"]))

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--exe",required=True)
    p.add_argument("--contract-dir",required=True)
    p.add_argument("--affine-dir",required=True)
    p.add_argument("--out",required=True)
    a=p.parse_args()
    out=pathlib.Path(a.out);out.mkdir(parents=True,exist_ok=True)
    records=[]

    for transfer in TRANSFERS:
        for model in MODELS:
            name=f"{transfer.lower()}__{model}"
            fdir=out/name/"forward"
            adir=out/name/"analysis"
            run([
                sys.executable,"research/analysis/run_gauge_shearing_forward.py",
                "--exe",a.exe,
                "--contract-dir",a.contract_dir,
                "--out",str(fdir),
                "--transfer",transfer,
                "--constitutive",model,
            ])
            run([
                sys.executable,"research/analysis/gauge_shearing_forward_analysis.py",
                "--contract-dir",a.contract_dir,
                "--forward-dir",str(fdir),
                "--affine-dir",a.affine_dir,
                "--out",str(adir),
            ])
            summary=json.loads((adir/"summary.json").read_text())
            invocation=json.loads((fdir/"invocation.json").read_text())
            soft=summary["materials"]["soft"];hard=summary["materials"]["hard"]
            rec={
                "name":name,
                "transfer":transfer,
                "constitutive":model,
                "fit_performed":False,
                "young_nu_density_mass_source":"frozen measured GAUGE metadata",
                "mean_face_nrmse":0.5*(soft["forward_nrmse"]+hard["forward_nrmse"]),
                "soft_face_nrmse":soft["forward_nrmse"],
                "hard_face_nrmse":hard["forward_nrmse"],
                "mean_marker_rmse_m":0.5*(soft["marker_position_rmse_m"]+hard["marker_position_rmse_m"]),
                "soft_marker_rmse_m":soft["marker_position_rmse_m"],
                "hard_marker_rmse_m":hard["marker_position_rmse_m"],
                "beats_affine_both_materials":summary["beats_affine_both_materials"],
                "material_separation_sign_agrees":summary["material_separation_sign_agrees"],
                "soft_minus_hard_peak":summary["soft_minus_hard_peak"],
                "forward_analysis_decision":summary["decision"],
                "structural_controls":invocation["structural_controls"],
            }
            records.append(rec)
            print("MODEL_MATRIX",name,
                  "face",rec["mean_face_nrmse"],
                  "marker_m",rec["mean_marker_rmse_m"],
                  "beats_affine",rec["beats_affine_both_materials"],
                  "sep_sign",rec["material_separation_sign_agrees"])

    baseline=next(r for r in records
                  if r["transfer"]=="APIC" and r["constitutive"]=="neo_hookean_log_j")
    for r in records:
        r["face_delta_vs_historical_baseline"]=r["mean_face_nrmse"]-baseline["mean_face_nrmse"]
        r["marker_delta_vs_historical_baseline_m"]=r["mean_marker_rmse_m"]-baseline["mean_marker_rmse_m"]
        r["dual_metric_improvement_vs_historical_baseline"]=(
            r["mean_face_nrmse"]<baseline["mean_face_nrmse"] and
            r["mean_marker_rmse_m"]<baseline["mean_marker_rmse_m"])
        r["pareto_nondominated"]=not any(dominates(other,r) for other in records if other is not r)

    ranking=sorted(records,key=lambda r:(r["mean_face_nrmse"],r["mean_marker_rmse_m"]))
    pareto=[r["name"] for r in records if r["pareto_nondominated"]]
    affine_pass=[r["name"] for r in records if r["beats_affine_both_materials"] and r["material_separation_sign_agrees"]]

    result={
        "schema":"vulkax.gauge_constitutive_transfer_matrix",
        "version":1,
        "provenance":"measured-input+no-fit-model-prediction",
        "fit_performed":False,
        "factorial_design":{"transfers":list(TRANSFERS),"constitutive_models":list(MODELS),"worlds":len(records)},
        "frozen_controls":{
            "material_parameters":"GAUGE measured metadata",
            "geometry":"historical measured-aspect proxy",
            "gravity":"zero",
            "boundary_layers":1,
            "particle_resolution":"5x5x13 along inferred long axis",
            "requested_dt_s":8.333333333333333e-5,
        },
        "historical_baseline":baseline["name"],
        "records":records,
        "pareto_nondominated":pareto,
        "affine_gate_passers":affine_pass,
        "diagnostic_ranking":[r["name"] for r in ranking],
        "inverse_fitting_unlocked":False,
        "decision":"diagnostic_only_no_inverse_unlock",
        "warning":"Even a matrix winner is only a no-fit model-adequacy diagnostic. It cannot unlock inverse material fitting without an independently justified fresh validation gate. Generic constitutive model selection is established prior art and is not a Vulkax novelty claim.",
    }
    (out/"summary.json").write_text(json.dumps(result,indent=2)+"\n")
    print("VALID GAUGE constitutive-transfer matrix")
    print("PARETO",pareto)
    print("AFFINE_GATE_PASSERS",affine_pass)
    print("RANKING",[(r["name"],r["mean_face_nrmse"],r["mean_marker_rmse_m"]) for r in ranking])

if __name__=="__main__":
    main()

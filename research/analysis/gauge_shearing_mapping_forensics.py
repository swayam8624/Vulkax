#!/usr/bin/env python3
"""Isolate GAUGE marker-observation mapping support from the MPM physical state.

Each arm reruns the identical frozen physical world and changes only the fixed
affine-MLS neighbor count used to reconstruct surface markers. The reconstructed
markers do not feed back into MPM in this path.
"""
import argparse
import json
import math
import pathlib
import subprocess
import sys

SUPPORTS=(8,12,16,24,32,48)
CONTROL=24
PHYSICAL_KEYS=(
    "minimum_J",
    "maximum_actual_dt_s",
    "substeps",
    "grid",
    "grid_cell_m",
    "max_momentum_accounting_error",
    "max_position_correction_m",
    "accumulated_constraint_impulse_magnitude",
)

def run(cmd):
    cp=subprocess.run(cmd,text=True,capture_output=True)
    if cp.stdout: print(cp.stdout,end="")
    if cp.stderr: print(cp.stderr,end="",file=sys.stderr)
    if cp.returncode:
        raise SystemExit(f"command failed ({cp.returncode}): {' '.join(map(str,cmd))}")

def load(path):
    return json.loads(pathlib.Path(path).read_text())

def close(a,b):
    if isinstance(a,list) and isinstance(b,list):
        return len(a)==len(b) and all(close(x,y) for x,y in zip(a,b))
    if isinstance(a,(int,float)) and isinstance(b,(int,float)):
        return abs(float(a)-float(b))<=1.0e-12*max(1.0,abs(float(a)),abs(float(b)))
    return a==b

def distance_from_one(x):
    return abs(float(x)-1.0)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--exe",required=True)
    ap.add_argument("--contract-dir",required=True)
    ap.add_argument("--affine-dir",required=True)
    ap.add_argument("--out",required=True)
    a=ap.parse_args()
    out=pathlib.Path(a.out); out.mkdir(parents=True,exist_ok=True)
    here=pathlib.Path(__file__).resolve().parent

    records={}
    raw_physics={}
    for neighbors in SUPPORTS:
        name=f"n{neighbors}"
        fdir=out/name/"forward"
        adir=out/name/"analysis"
        ddir=out/name/"decomposition"

        run([
            sys.executable,str(here/"run_gauge_shearing_forward.py"),
            "--exe",a.exe,
            "--contract-dir",a.contract_dir,
            "--out",str(fdir),
            "--mapping-neighbors",str(neighbors),
        ])
        run([
            sys.executable,str(here/"gauge_shearing_forward_analysis.py"),
            "--contract-dir",a.contract_dir,
            "--forward-dir",str(fdir),
            "--affine-dir",a.affine_dir,
            "--out",str(adir),
        ])
        run([
            sys.executable,str(here/"gauge_shearing_affine_nonaffine.py"),
            "--contract-dir",a.contract_dir,
            "--forward-dir",str(fdir),
            "--out",str(ddir),
        ])

        ana=load(adir/"summary.json")
        dec=load(ddir/"summary.json")
        inv=load(fdir/"invocation.json")
        phys={}
        mats={}
        for material in ("soft","hard"):
            fs=load(fdir/f"predicted_{material}_summary.json")
            phys[material]={k:fs[k] for k in PHYSICAL_KEYS}
            mats[material]={
                "face_nrmse":ana["materials"][material]["forward_nrmse"],
                "marker_rmse_m":ana["materials"][material]["marker_position_rmse_m"],
                "median_global_F_error_normalized":
                    dec["materials"][material]["median_global_F_error_normalized"],
                "nonaffine_marker_ratio":
                    dec["materials"][material]["predicted_to_measured_nonaffine_marker_ratio"],
                "nonaffine_face_ratio":
                    dec["materials"][material]["predicted_to_measured_nonaffine_face_ratio"],
                "nonaffine_marker_vector_rmse_m":
                    dec["materials"][material]["mean_nonaffine_marker_vector_rmse_m"],
                "nonaffine_face_vector_rmse":
                    dec["materials"][material]["mean_nonaffine_face_vector_rmse"],
                "mapping_max_partition_error":fs["mapping_max_partition_error"],
                "mapping_max_affine_reproduction_error_m":
                    fs["mapping_max_affine_reproduction_error_m"],
            }
        raw_physics[name]=phys
        records[name]={
            "mapping_neighbors":neighbors,
            "structural_controls":inv["structural_controls"],
            "materials":mats,
        }

    control=records[f"n{CONTROL}"]
    invariant=True
    invariance_checks={}
    for name,phys in raw_physics.items():
        per_mat={}
        for material in ("soft","hard"):
            checks={k:close(phys[material][k],raw_physics[f"n{CONTROL}"][material][k])
                    for k in PHYSICAL_KEYS}
            per_mat[material]=checks
            invariant &= all(checks.values())
        invariance_checks[name]=per_mat

    candidate_supports=[]
    for name,rec in records.items():
        if rec["mapping_neighbors"]==CONTROL:
            rec["relative_to_control"]={"control":True}
            continue
        checks={}
        for material in ("soft","hard"):
            b=control["materials"][material]
            q=rec["materials"][material]
            checks[material]={
                "nonaffine_marker_ratio_closer_to_one":
                    distance_from_one(q["nonaffine_marker_ratio"]) <
                    distance_from_one(b["nonaffine_marker_ratio"]),
                "nonaffine_face_ratio_closer_to_one":
                    distance_from_one(q["nonaffine_face_ratio"]) <
                    distance_from_one(b["nonaffine_face_ratio"]),
                "macro_F_relative_change":abs(
                    q["median_global_F_error_normalized"]-
                    b["median_global_F_error_normalized"]
                )/max(abs(b["median_global_F_error_normalized"]),1.0e-15),
            }
        all_local=all(
            checks[m]["nonaffine_marker_ratio_closer_to_one"] and
            checks[m]["nonaffine_face_ratio_closer_to_one"]
            for m in ("soft","hard"))
        macro_stable=all(
            checks[m]["macro_F_relative_change"]<=0.10
            for m in ("soft","hard"))
        rec["relative_to_control"]={
            "control":False,
            "materials":checks,
            "local_ratios_improve_both_materials":all_local,
            "macro_stable_both_materials":macro_stable,
        }
        if all_local and macro_stable:
            candidate_supports.append(rec["mapping_neighbors"])

    result={
        "schema":"vulkax.gauge_observation_mapping_forensics",
        "version":1,
        "provenance":"measured-input+no-fit-model-prediction",
        "fit_performed":False,
        "supports":list(SUPPORTS),
        "historical_control_neighbors":CONTROL,
        "physical_solver_evidence_invariant":invariant,
        "physical_invariance_checks":invariance_checks,
        "records":records,
        "supports_meeting_frozen_local_repair_signature":candidate_supports,
        "classification":(
            "observation_mapping_sensitive_local_failure"
            if invariant and candidate_supports else
            "observation_mapping_not_primary_under_tested_supports"
            if invariant else
            "harness_failure_physical_state_changed"
        ),
        "adoption_authorized":False,
        "warning":(
            "This is a layer-isolation forensic sweep, not hyperparameter tuning. "
            "No support size is promoted from this result; any correspondence change "
            "requires an independently justified rule and held-out evaluation."
        ),
    }
    (out/"summary.json").write_text(json.dumps(result,indent=2)+"\n")

    print("VALID GAUGE observation-mapping forensics")
    print("PHYSICAL_INVARIANT",invariant)
    print("CANDIDATE_SUPPORTS",candidate_supports)
    print("CLASSIFICATION",result["classification"])
    for name,rec in records.items():
        print("MAPPING",name,rec["materials"],rec.get("relative_to_control"))


if __name__=="__main__":
    main()

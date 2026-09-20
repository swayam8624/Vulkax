#!/usr/bin/env python3
"""Controlled synthetic test of the GAUGE observation-support/fixture hypothesis.

Truth and misspecified worlds share:
- the same frozen GAUGE initial marker positions and driver,
- the same measured metadata E, nu, density and mass,
- the same APIC transfer, constitutive law and timestep.

The only planted difference is body/fixture support:
- truth: released_asset_aspect (1:1:4, volume from mass/density)
- misspecified: measured_aspect (historical marker-envelope long support)

This is a post-hoc mechanism test motivated by the real-data failure signature.
It cannot serve as independent confirmation of that signature.
"""
import argparse
import csv
import json
import math
import pathlib
import shutil
import subprocess
import sys


def run(cmd):
    cp=subprocess.run(cmd,text=True,capture_output=True)
    if cp.stdout:
        print(cp.stdout,end="")
    if cp.stderr:
        print(cp.stderr,end="",file=sys.stderr)
    if cp.returncode:
        raise SystemExit(f"command failed ({cp.returncode}): {' '.join(map(str,cmd))}")


def read_positions(path):
    frames={}
    with open(path,newline="") as f:
        for row in csv.DictReader(f):
            frames.setdefault(int(row["frame"]),{})[row["marker_id"]]=(
                float(row["x_m"]),float(row["y_m"]),float(row["z_m"])
            )
    return frames


def norm(v):
    return math.sqrt(sum(x*x for x in v))


def marker_rmse(a,b):
    errs=[]
    common=sorted(set(a)&set(b))
    for frame in common:
        ids=sorted(set(a[frame])&set(b[frame]))
        for mid in ids:
            pa=a[frame][mid]; pb=b[frame][mid]
            errs.append(norm(tuple(pa[i]-pb[i] for i in range(3))))
    return math.sqrt(sum(x*x for x in errs)/len(errs)) if errs else 0.0


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--exe",required=True)
    ap.add_argument("--contract-dir",required=True)
    ap.add_argument("--real-mode-summary",required=True)
    ap.add_argument("--out",required=True)
    a=ap.parse_args()

    out=pathlib.Path(a.out)
    out.mkdir(parents=True,exist_ok=True)
    here=pathlib.Path(__file__).resolve().parent
    truth=out/"truth_released_asset"
    bad=out/"misspecified_marker_envelope"

    common=[
        sys.executable,str(here/"run_gauge_shearing_forward.py"),
        "--exe",a.exe,
        "--contract-dir",a.contract_dir,
        "--dt",str(1.0/12000.0),
        "--transfer","APIC",
        "--gravity","zero",
        "--constitutive","neo_hookean_log_j",
    ]
    run(common+["--geometry-mode","released_asset_aspect","--out",str(truth)])
    run(common+["--geometry-mode","measured_aspect","--out",str(bad)])

    synthetic_contract=out/"synthetic_truth_contract"
    synthetic_contract.mkdir(exist_ok=True)
    shutil.copy2(pathlib.Path(a.contract_dir)/"contract.json",synthetic_contract/"contract.json")
    for material in ("soft","hard"):
        shutil.copy2(
            truth/f"predicted_{material}.csv",
            synthetic_contract/f"markers_measured_{material}.csv"
        )

    mode_out=out/"mode_misspecified_vs_truth"
    run([
        sys.executable,str(here/"gauge_shearing_mode_diagnostic.py"),
        "--contract-dir",str(synthetic_contract),
        "--forward-dir",str(bad),
        "--out",str(mode_out),
    ])
    modes=json.loads((mode_out/"summary.json").read_text())
    real_modes=json.loads(pathlib.Path(a.real_mode_summary).read_text())

    def sign(x,eps=1.0e-12):
        if abs(x)<=eps:
            return 0
        return 1 if x>0.0 else -1

    materials={}
    all_long_direction_match=True
    all_area_over=True
    all_shear_over=True
    for material in ("soft","hard"):
        md=modes["materials"][material]["mode_discrepancy"]
        longm=md["longitudinal_strain_mean"]
        area=md["face_area_rel_rms"]
        shear=md["shear_cos_delta_rms"]
        tr=read_positions(truth/f"predicted_{material}.csv")
        br=read_positions(bad/f"predicted_{material}.csv")
        mr=marker_rmse(tr,br)
        real_md=real_modes["materials"][material]["mode_discrepancy"]
        real_long=real_md["longitudinal_strain_mean"]
        real_area=real_md["face_area_rel_rms"]
        real_shear=real_md["shear_cos_delta_rms"]
        long_sign_mismatch=not bool(longm["final_sign_agrees"])
        long_direction_match=(
            long_sign_mismatch and
            sign(longm["measured_final"])==sign(real_long["measured_final"]) and
            sign(longm["predicted_final"])==sign(real_long["predicted_final"])
        )
        area_over=(
            area["predicted_to_measured_peak_ratio"]>1.0 and
            real_area["predicted_to_measured_peak_ratio"]>1.0
        )
        shear_over=(
            shear["predicted_to_measured_peak_ratio"]>1.0 and
            real_shear["predicted_to_measured_peak_ratio"]>1.0
        )
        all_long_direction_match &= long_direction_match
        all_area_over &= area_over
        all_shear_over &= shear_over
        materials[material]={
            "marker_rmse_m":mr,
            "longitudinal_final_truth":longm["measured_final"],
            "longitudinal_final_marker_envelope":longm["predicted_final"],
            "longitudinal_final_sign_mismatch":long_sign_mismatch,
            "longitudinal_sign_direction_matches_real":long_direction_match,
            "real_longitudinal_final_measured":real_long["measured_final"],
            "real_longitudinal_final_baseline":real_long["predicted_final"],
            "longitudinal_mean_nrmse":longm["nrmse_to_measured_peak_abs"],
            "face_area_rms_nrmse":area["nrmse_to_measured_peak_abs"],
            "face_area_rms_peak_ratio":area["predicted_to_measured_peak_ratio"],
            "shear_rms_nrmse":shear["nrmse_to_measured_peak_abs"],
            "shear_rms_peak_ratio":shear["predicted_to_measured_peak_ratio"],
        }

    recreates=all_long_direction_match and all_area_over and all_shear_over
    result={
        "schema":"vulkax.gauge_fixture_envelope_synthetic_control",
        "version":1,
        "provenance":"synthetic-control-from-frozen-GAUGE-inputs",
        "fit_performed":False,
        "truth_geometry":"released_asset_aspect",
        "planted_misspecification":"measured_aspect",
        "all_else_equal":{
            "material_metadata":True,
            "initial_markers":True,
            "driver":True,
            "transfer":"APIC",
            "constitutive":"neo_hookean_log_j",
            "gravity":"zero",
            "requested_dt_s":1.0/12000.0,
        },
        "materials":materials,
        "qualitative_signature_checks":{
            "longitudinal_sign_reversal_matches_real_direction_both":all_long_direction_match,
            "face_area_rms_overamplified_both":all_area_over,
            "shear_rms_overamplified_both":all_shear_over,
        },
        "recreates_key_real_failure_signature":recreates,
        "interpretation":(
            "If true, body/fixture support misspecification is sufficient inside the "
            "same simulator/material family to create the key observed failure signature. "
            "It is not proof that this is the sole or real-world cause."
            if recreates else
            "The planted body/fixture support error does not reproduce all key real-data "
            "failure modes, so it cannot be promoted as the primary explanation."
        ),
        "independence_guard":(
            "This control was designed after observing the real GAUGE mode signature. "
            "Any positive result requires fresh real-task or all-repeat confirmation."
        ),
        "inverse_fitting_unlocked":False,
    }
    (out/"summary.json").write_text(json.dumps(result,indent=2)+"\n")
    print("VALID GAUGE fixture-envelope synthetic control")
    print("SIGNATURE",result["qualitative_signature_checks"])
    print("RECREATES",recreates)


if __name__=="__main__":
    main()

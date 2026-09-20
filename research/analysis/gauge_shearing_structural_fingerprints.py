#!/usr/bin/env python3
"""Re-analyze existing GAUGE structural arms as macro/local mechanism fingerprints.

No physical simulation is launched here. This script consumes the prediction files
already produced by gauge_shearing_structural_forensics.py.
"""
import argparse
import json
import pathlib
import subprocess
import sys


def load(path):
    return json.loads(pathlib.Path(path).read_text())


def d1(x):
    return abs(float(x)-1.0)


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--contract-dir",required=True)
    ap.add_argument("--structural-dir",required=True)
    ap.add_argument("--out",required=True)
    a=ap.parse_args()

    cdir=pathlib.Path(a.contract_dir)
    sdir=pathlib.Path(a.structural_dir)
    out=pathlib.Path(a.out); out.mkdir(parents=True,exist_ok=True)
    structural=load(sdir/"summary.json")
    here=pathlib.Path(__file__).resolve().parent

    successful=[
        name for name,row in structural["variants"].items()
        if row.get("status")=="success"
    ]
    if "baseline" not in successful:
        raise SystemExit("structural fingerprint requires successful baseline")

    decompositions={}
    for name in successful:
        ddir=out/f"decomposition_{name}"
        cp=subprocess.run([
            sys.executable,
            str(here/"gauge_shearing_affine_nonaffine.py"),
            "--contract-dir",str(cdir),
            "--forward-dir",str(sdir/f"forward_{name}"),
            "--out",str(ddir),
        ],text=True,capture_output=True)
        if cp.stdout: print(cp.stdout,end="")
        if cp.stderr: print(cp.stderr,end="",file=sys.stderr)
        if cp.returncode:
            raise SystemExit(f"decomposition failed for {name}: {cp.returncode}")
        decompositions[name]=load(ddir/"summary.json")

    baseline=decompositions["baseline"]
    records={}
    for name in successful:
        dec=decompositions[name]
        struct=structural["variants"][name]
        per_material={}
        macro_both=True
        local_both=True
        any_move=False
        for material in ("soft","hard"):
            b=baseline["materials"][material]
            q=dec["materials"][material]
            macro=q["median_global_F_error_normalized"] < b["median_global_F_error_normalized"]
            local_marker=d1(q["predicted_to_measured_nonaffine_marker_ratio"]) < d1(
                b["predicted_to_measured_nonaffine_marker_ratio"])
            local_face=d1(q["predicted_to_measured_nonaffine_face_ratio"]) < d1(
                b["predicted_to_measured_nonaffine_face_ratio"])
            det_improve=abs(q["final_predicted_det_F"]-q["final_measured_det_F"]) < abs(
                b["final_predicted_det_F"]-b["final_measured_det_F"])
            per_material[material]={
                "macro_F_improved":macro,
                "det_F_error_improved":det_improve,
                "local_marker_ratio_improved":local_marker,
                "local_face_ratio_improved":local_face,
                "baseline":{
                    "median_global_F_error_normalized":b["median_global_F_error_normalized"],
                    "nonaffine_marker_ratio":b["predicted_to_measured_nonaffine_marker_ratio"],
                    "nonaffine_face_ratio":b["predicted_to_measured_nonaffine_face_ratio"],
                },
                "variant":{
                    "median_global_F_error_normalized":q["median_global_F_error_normalized"],
                    "nonaffine_marker_ratio":q["predicted_to_measured_nonaffine_marker_ratio"],
                    "nonaffine_face_ratio":q["predicted_to_measured_nonaffine_face_ratio"],
                },
            }
            macro_both &= macro
            local_both &= (local_marker and local_face)
            any_move |= macro or local_marker or local_face or det_improve

        face_delta=struct.get("mean_delta_vs_baseline",0.0)
        marker_delta=struct.get("mean_marker_delta_vs_baseline_m",0.0)
        metric_tradeoff=(face_delta<0.0 and marker_delta>0.0) or (
            face_delta>0.0 and marker_delta<0.0)

        if name=="baseline":
            category="control"
        elif macro_both and local_both:
            category="macro_and_local_change"
        elif macro_both:
            category="macro_dominant_change"
        elif local_both:
            category="local_dominant_change"
        elif any_move:
            category="mixed_or_material_specific"
        else:
            category="no_useful_movement"
        if metric_tradeoff:
            category=category+"+ordinary_metric_tradeoff"

        records[name]={
            "options":struct.get("options",{}),
            "ordinary_mean_face_nrmse":struct.get("mean_nrmse"),
            "ordinary_mean_marker_rmse_m":struct.get("mean_marker_rmse_m"),
            "ordinary_face_delta_vs_baseline":face_delta,
            "ordinary_marker_delta_vs_baseline_m":marker_delta,
            "ordinary_dual_metric_improvement":
                bool(struct.get("dual_metric_consistent_improvement",False)),
            "ordinary_metric_tradeoff":metric_tradeoff,
            "macro_improves_both_materials":macro_both,
            "local_marker_and_face_ratios_improve_both_materials":local_both,
            "materials":per_material,
            "fingerprint":category,
        }

    result={
        "schema":"vulkax.gauge_structural_mechanism_fingerprints",
        "version":1,
        "provenance":"measured+existing-no-fit-model-predictions",
        "new_physical_simulations_launched":False,
        "baseline":"baseline",
        "records":records,
        "warning":(
            "Fingerprints localize which kinematic scale changes under existing "
            "one-factor structural arms. They do not select a model, identify a "
            "cause, or establish novelty."
        ),
    }
    (out/"summary.json").write_text(json.dumps(result,indent=2)+"\n")

    print("VALID GAUGE structural mechanism fingerprints")
    for name,row in records.items():
        print("FINGERPRINT",name,row["fingerprint"],
              "dual",row["ordinary_dual_metric_improvement"],
              "macro",row["macro_improves_both_materials"],
              "local",row["local_marker_and_face_ratios_improve_both_materials"])


if __name__=="__main__":
    main()

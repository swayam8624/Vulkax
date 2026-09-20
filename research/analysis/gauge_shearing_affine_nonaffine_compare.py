#!/usr/bin/env python3
"""Compare GAUGE baseline vs released-asset geometry at macro/local scales.

The comparison rule is frozen in GAUGE_AFFINE_NONAFFINE_PROTOCOL.md.
This script performs no physical-parameter fitting and no threshold tuning.
"""
import argparse
import json
import pathlib


def load(path):
    return json.loads(pathlib.Path(path).read_text())


def strictly_better(candidate, baseline):
    return candidate < baseline


def ratio_distance_from_one(value):
    return abs(value - 1.0)


def material_comparison(base, cand):
    bF=base["median_global_F_error_normalized"]
    cF=cand["median_global_F_error_normalized"]

    bdet=abs(base["final_predicted_det_F"]-base["final_measured_det_F"])
    cdet=abs(cand["final_predicted_det_F"]-cand["final_measured_det_F"])

    bface=ratio_distance_from_one(base["predicted_to_measured_nonaffine_face_ratio"])
    cface=ratio_distance_from_one(cand["predicted_to_measured_nonaffine_face_ratio"])

    bmarker=ratio_distance_from_one(base["predicted_to_measured_nonaffine_marker_ratio"])
    cmarker=ratio_distance_from_one(cand["predicted_to_measured_nonaffine_marker_ratio"])

    checks={
        "median_global_F_error_improved": strictly_better(cF,bF),
        "final_det_F_error_improved": strictly_better(cdet,bdet),
        "nonaffine_face_amplitude_ratio_improved": strictly_better(cface,bface),
        "nonaffine_marker_amplitude_ratio_improved": strictly_better(cmarker,bmarker),
        "nonaffine_face_vector_rmse_improved": strictly_better(
            cand["mean_nonaffine_face_vector_rmse"],
            base["mean_nonaffine_face_vector_rmse"]),
        "nonaffine_marker_vector_rmse_improved": strictly_better(
            cand["mean_nonaffine_marker_vector_rmse_m"],
            base["mean_nonaffine_marker_vector_rmse_m"]),
    }
    return {
        "checks":checks,
        "baseline":{
            "median_global_F_error_normalized":bF,
            "final_det_F_abs_error":bdet,
            "nonaffine_face_ratio":base["predicted_to_measured_nonaffine_face_ratio"],
            "nonaffine_face_ratio_distance_from_one":bface,
            "nonaffine_marker_ratio":base["predicted_to_measured_nonaffine_marker_ratio"],
            "nonaffine_marker_ratio_distance_from_one":bmarker,
            "nonaffine_face_vector_rmse":base["mean_nonaffine_face_vector_rmse"],
            "nonaffine_marker_vector_rmse_m":base["mean_nonaffine_marker_vector_rmse_m"],
        },
        "candidate":{
            "median_global_F_error_normalized":cF,
            "final_det_F_abs_error":cdet,
            "nonaffine_face_ratio":cand["predicted_to_measured_nonaffine_face_ratio"],
            "nonaffine_face_ratio_distance_from_one":cface,
            "nonaffine_marker_ratio":cand["predicted_to_measured_nonaffine_marker_ratio"],
            "nonaffine_marker_ratio_distance_from_one":cmarker,
            "nonaffine_face_vector_rmse":cand["mean_nonaffine_face_vector_rmse"],
            "nonaffine_marker_vector_rmse_m":cand["mean_nonaffine_marker_vector_rmse_m"],
        },
    }


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--baseline",required=True)
    ap.add_argument("--candidate",required=True)
    ap.add_argument("--structural-summary",required=True)
    ap.add_argument("--out",required=True)
    a=ap.parse_args()

    base=load(a.baseline)
    cand=load(a.candidate)
    structural=load(a.structural_summary)
    out=pathlib.Path(a.out)
    out.mkdir(parents=True,exist_ok=True)

    if base.get("schema")!="vulkax.gauge_affine_nonaffine_decomposition":
        raise SystemExit("unexpected baseline decomposition schema")
    if cand.get("schema")!="vulkax.gauge_affine_nonaffine_decomposition":
        raise SystemExit("unexpected candidate decomposition schema")

    variant=structural.get("variants",{}).get("released_asset_aspect")
    ordinary_dual=bool(
        variant and variant.get("status")=="success"
        and variant.get("dual_metric_consistent_improvement",False)
    )

    materials={}
    for material in ("soft","hard"):
        materials[material]=material_comparison(
            base["materials"][material],
            cand["materials"][material])

    macro_both=all(
        materials[m]["checks"]["median_global_F_error_improved"]
        for m in ("soft","hard"))
    local_face_both=all(
        materials[m]["checks"]["nonaffine_face_amplitude_ratio_improved"]
        for m in ("soft","hard"))
    local_marker_both=all(
        materials[m]["checks"]["nonaffine_marker_amplitude_ratio_improved"]
        for m in ("soft","hard"))
    local_both=local_face_both and local_marker_both

    if ordinary_dual and macro_both and local_both:
        classification="macro_and_local_repair"
    elif macro_both and not local_both:
        classification="macro_only_repair"
    elif local_both and not macro_both:
        classification="local_only_repair"
    else:
        any_improvement=any(
            any(v for v in materials[m]["checks"].values())
            for m in ("soft","hard"))
        classification="partial_or_mixed" if any_improvement else "no_mechanistic_repair"

    result={
        "schema":"vulkax.gauge_affine_nonaffine_repair_comparison",
        "version":1,
        "provenance":"measured+no-fit-model-prediction",
        "fit_performed":False,
        "candidate":"released_asset_aspect",
        "ordinary_dual_metric_improvement":ordinary_dual,
        "macro_improves_both_materials":macro_both,
        "local_face_ratio_improves_both_materials":local_face_both,
        "local_marker_ratio_improves_both_materials":local_marker_both,
        "local_amplitude_ratios_improve_both_materials":local_both,
        "materials":materials,
        "classification":classification,
        "inverse_fitting_unlocked":False,
        "warning":(
            "This comparison only localizes which kinematic scale changes under a "
            "provenance-backed geometry correction. It is not causal proof, material "
            "identification, or a novelty claim."
        ),
    }
    (out/"summary.json").write_text(json.dumps(result,indent=2)+"\n")
    print("VALID GAUGE affine/non-affine repair comparison")
    print("CLASSIFICATION",classification)
    print("ORDINARY_DUAL",ordinary_dual)
    for m,d in materials.items():
        print("MATERIAL",m,d["checks"])


if __name__=="__main__":
    main()

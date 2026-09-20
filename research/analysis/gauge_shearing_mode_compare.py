#!/usr/bin/env python3
"""Pre-registered comparison of GAUGE baseline vs released-asset geometry.

This analysis does not fit or select parameters. It asks whether the physically
justified body-envelope repair improves both ordinary trajectory metrics and the
specific deformation-mode failures diagnosed before seeing the repair result.
"""
import argparse
import json
import pathlib


def load(path):
    return json.loads(pathlib.Path(path).read_text())


def get_mode(summary, material, key):
    return summary["materials"][material]["mode_discrepancy"][key]


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--baseline",required=True)
    ap.add_argument("--candidate",required=True)
    ap.add_argument("--structural-summary",required=True)
    ap.add_argument("--out",required=True)
    a=ap.parse_args()

    baseline=load(a.baseline)
    candidate=load(a.candidate)
    structural=load(a.structural_summary)
    out=pathlib.Path(a.out)
    out.mkdir(parents=True,exist_ok=True)

    variant=structural["variants"].get("released_asset_aspect")
    if not variant or variant.get("status")!="success":
        result={
            "schema":"vulkax.gauge_fixture_envelope_mode_comparison",
            "version":1,
            "provenance":"measured+no-fit-model-prediction",
            "fit_performed":False,
            "decision":"candidate_unavailable",
            "warning":"Released-asset structural variant did not produce a successful forward result."
        }
        (out/"summary.json").write_text(json.dumps(result,indent=2)+"\n")
        print("GAUGE_FIXTURE_ENVELOPE candidate_unavailable")
        return

    materials={}
    all_area=True
    all_long_error=True
    all_shear_dispersion=True
    all_sign=True

    for material in ("soft","hard"):
        b_area=get_mode(baseline,material,"face_area_rel_rms")
        c_area=get_mode(candidate,material,"face_area_rel_rms")
        b_long=get_mode(baseline,material,"longitudinal_strain_mean")
        c_long=get_mode(candidate,material,"longitudinal_strain_mean")
        b_shear=get_mode(baseline,material,"shear_cos_delta_rms")
        c_shear=get_mode(candidate,material,"shear_cos_delta_rms")

        b_long_signed_error=abs(b_long["predicted_final"]-b_long["measured_final"])
        c_long_signed_error=abs(c_long["predicted_final"]-c_long["measured_final"])
        b_shear_amp_error=abs(b_shear["predicted_to_measured_peak_ratio"]-1.0)
        c_shear_amp_error=abs(c_shear["predicted_to_measured_peak_ratio"]-1.0)

        checks={
            "face_area_nrmse_improved":(
                c_area["nrmse_to_measured_peak_abs"] <
                b_area["nrmse_to_measured_peak_abs"]
            ),
            "longitudinal_signed_error_improved":(
                c_long_signed_error < b_long_signed_error
            ),
            "longitudinal_final_sign_agrees":bool(c_long["final_sign_agrees"]),
            "shear_rms_amplitude_error_improved":(
                c_shear_amp_error < b_shear_amp_error
            ),
        }
        all_area &= checks["face_area_nrmse_improved"]
        all_long_error &= checks["longitudinal_signed_error_improved"]
        all_sign &= checks["longitudinal_final_sign_agrees"]
        all_shear_dispersion &= checks["shear_rms_amplitude_error_improved"]

        materials[material]={
            "checks":checks,
            "baseline":{
                "face_area_rms_nrmse":b_area["nrmse_to_measured_peak_abs"],
                "longitudinal_final_measured":b_long["measured_final"],
                "longitudinal_final_predicted":b_long["predicted_final"],
                "longitudinal_signed_error":b_long_signed_error,
                "shear_rms_peak_ratio":b_shear["predicted_to_measured_peak_ratio"],
            },
            "released_asset":{
                "face_area_rms_nrmse":c_area["nrmse_to_measured_peak_abs"],
                "longitudinal_final_measured":c_long["measured_final"],
                "longitudinal_final_predicted":c_long["predicted_final"],
                "longitudinal_signed_error":c_long_signed_error,
                "shear_rms_peak_ratio":c_shear["predicted_to_measured_peak_ratio"],
            },
        }

    dual_metric=bool(variant.get("dual_metric_consistent_improvement",False))
    strong=dual_metric and all_area and all_long_error and all_shear_dispersion and all_sign
    partial=dual_metric and all_area and all_long_error

    if strong:
        decision="fixture_envelope_hypothesis_supported_for_further_validation"
    elif partial:
        decision="fixture_envelope_hypothesis_partially_supported"
    else:
        decision="fixture_envelope_hypothesis_not_supported_as_primary_repair"

    result={
        "schema":"vulkax.gauge_fixture_envelope_mode_comparison",
        "version":1,
        "provenance":"measured+no-fit-model-prediction",
        "fit_performed":False,
        "candidate":"released_asset_aspect",
        "pre_registered_checks":{
            "ordinary_dual_metric_improvement":dual_metric,
            "face_area_nrmse_improves_both":all_area,
            "longitudinal_signed_error_improves_both":all_long_error,
            "longitudinal_final_sign_repaired_both":all_sign,
            "shear_rms_overamplification_improves_both":all_shear_dispersion,
        },
        "materials":materials,
        "decision":decision,
        "inverse_fitting_unlocked":False,
        "warning":(
            "A positive result supports a specific structural explanation only. "
            "It does not identify material parameters, prove a physical fixture model, "
            "or establish novelty. A fresh all-repeat/no-fit gate remains required."
        ),
    }
    (out/"summary.json").write_text(json.dumps(result,indent=2)+"\n")
    print("VALID GAUGE fixture-envelope mode comparison")
    print("DECISION",decision)
    print("CHECKS",result["pre_registered_checks"])


if __name__=="__main__":
    main()

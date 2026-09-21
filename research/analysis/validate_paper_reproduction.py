#!/usr/bin/env python3
"""Validate a fresh Vulkax paper-evidence run against the frozen canonical ledger.

The goal is reproduction integrity, not threshold tuning. Counts and categorical
outcomes are checked exactly; floating metrics use declared tolerances to allow
small cross-platform numerical variation.
"""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import tempfile


def load(path: Path) -> dict:
    if not path.is_file():
        raise RuntimeError(f"missing required analysis: {path}")
    value=json.loads(path.read_text())
    if not isinstance(value,dict):
        raise RuntimeError(f"expected JSON object: {path}")
    return value

def near(actual: float, expected: float, *, rel: float=0.02, abs_tol: float=1e-9) -> bool:
    return math.isclose(float(actual),float(expected),rel_tol=rel,abs_tol=abs_tol)

def check(result: dict, name: str, actual, expected, *, rel=None, abs_tol=1e-9) -> None:
    if rel is None:
        passed=actual==expected
    else:
        passed=near(float(actual),float(expected),rel=rel,abs_tol=abs_tol)
    result["checks"].append({
        "name":name,
        "actual":actual,
        "expected":expected,
        "passed":passed,
        "relative_tolerance":rel,
        "absolute_tolerance":abs_tol if rel is not None else None,
    })
    if not passed:
        result["passed"]=False

def validate(repo: Path, build: Path, allow_missing_gauge: bool) -> dict:
    canonical=load(repo/"research/results/VULKAX_FINAL_RESULTS_2026-09-21.json")
    stages=canonical["dcs"]["stages"]
    ofc_ref=canonical["orthogonal_force_compliance"]

    result={
        "schema":"vulkax.paper_reproduction_validation",
        "version":1,
        "passed":True,
        "allow_missing_gauge":allow_missing_gauge,
        "checks":[],
        "warning":"Validation compares against frozen canonical results; it does not create a new scientific claim.",
    }

    d1=load(build/"dcs-positive-control/analysis.json")
    ref=stages["D1"]
    check(result,"D1 case_count",d1["case_count"],ref["cases"])
    check(result,"D1 ordinary_accept",d1["ordinary_metric_accept_count"],ref["ordinary_accept"])
    check(result,"D1 deceptive",d1["deceptive_repair_count"],ref["deceptive"])
    check(result,"D1 dcs_accept_deceptive",d1["dcs_accept_count"],ref["dcs_accept_deceptive"])
    check(result,"D1 median observation improvement",
          d1["median_observation_improvement_fraction"],ref["median_observation_improvement"],rel=0.01,abs_tol=1e-6)
    check(result,"D1 median witness degradation",
          d1["median_witness_degradation_ratio"],ref["median_witness_degradation_ratio"],rel=0.01,abs_tol=1e-6)

    d2a=load(build/"dcs-solver-native/analysis.json")
    ref=stages["D2a"]
    check(result,"D2a heldout agreement",d2a["heldout_pairwise_target_agreement"],
          ref["ranking_agreement"]["heldout_rmse"],rel=0.002,abs_tol=1e-6)
    check(result,"D2a DCS agreement",d2a["dcs_pairwise_target_agreement"],
          ref["ranking_agreement"]["dcs_fixed_k2"],rel=0.002,abs_tol=1e-6)
    check(result,"D2a mirage count",d2a["ordinary_metric_mirage_pairs"],ref["ordinary_mirage_pairs"])
    check(result,"D2a corrected mirages",d2a["dcs_corrected_mirage_pairs"],ref["dcs_corrected_mirages"])

    active=load(build/"dcs-active-selection/analysis.json")
    ref=stages["D2b"]["ranking_agreement"]
    mapping={
        "dcs_k2_maximin":"dcs",
        "raw_bundle":"raw_bundle_same_cost",
        "raw_maximin":"raw_maximin",
        "fisher":"fisher_sensitivity",
        "max_motion":"max_motion",
        "random":"random",
    }
    for rk,gk in mapping.items():
        check(result,f"D2b {rk} agreement",
              active["methods"][gk]["target_ranking_agreement"],ref[rk],rel=0.002,abs_tol=1e-6)

    d2=load(build/"dcs-d2-validation/analysis.json")
    ref=stages["D2_frozen"]
    check(result,"D2 frozen truth worlds",d2["truth_count"],ref["truth_worlds"])
    check(result,"D2 frozen resolved worlds",d2["resolved_truth_count"],ref["resolved_worlds"])
    check(result,"D2 frozen coverage",d2["coverage"],ref["coverage"],rel=0.0,abs_tol=1e-12)
    check(result,"D2 frozen median separation",d2["standardized_separation_median"],
          ref["median_standardized_separation"],rel=0.03,abs_tol=1e-5)
    check(result,"D2 frozen median numerical RMS",d2["numerical_rms_median"],
          ref["median_numerical_rms_m"],rel=0.05,abs_tol=1e-8)
    check(result,"D2 frozen moment residual contract",d2["maximum_moment_residual"]<=1e-9,True)

    d3=load(build/"dcs-d3-discovery/analysis.json")
    ref=stages["D3"]
    check(result,"D3 truth worlds",d3["truth_count"],ref["truth_worlds"])
    check(result,"D3 adaptive resolved",d3["observability"]["adaptive_resolved_worlds"],ref["resolved_worlds"])
    check(result,"D3 selected k2",int(d3["adaptive_order_counts"].get("2",0)),int(ref["adaptive_order_counts"]["2"]))
    check(result,"D3 selected k3",int(d3["adaptive_order_counts"].get("3",0)),int(ref["adaptive_order_counts"]["3"]))
    check(result,"D3 median k2 separation",d3["observability"]["median_order2_separation"],
          ref["median_separation"]["k2"],rel=0.03,abs_tol=1e-5)
    check(result,"D3 median k3 separation",d3["observability"]["median_order3_separation"],
          ref["median_separation"]["k3"],rel=0.03,abs_tol=1e-5)
    d3map={
        "adaptive_dcs":"adaptive_dcs",
        "k2_dcs":"order2_dcs",
        "k3_dcs":"order3_dcs",
        "raw_bundle":"raw_bundle_same_cost",
        "raw_pair_aware":"raw_pair_aware",
        "fisher":"fisher_sensitivity",
        "max_motion":"max_motion",
        "random":"random",
    }
    for rk,gk in d3map.items():
        check(result,f"D3 {rk} agreement",d3["methods"][gk]["target_ranking_agreement"],
              ref["ranking_agreement"][rk],rel=0.002,abs_tol=1e-6)

    d4=load(build/"dcs-d4v-discovery/analysis.json")
    ref=stages["D4V"]
    check(result,"D4V proposals",d4["proposal_count"],ref["proposals"])
    check(result,"D4V deceptive",d4["deceptive_count"],ref["deceptive"])
    check(result,"D4V beneficial",d4["beneficial_count"],ref["beneficial"])
    d4map={"dcs":"dcs","raw_bundle":"raw_bundle","raw_pair":"raw_point","fisher":"fisher","max_motion":"max_motion"}
    for rk,gk in d4map.items():
        check(result,f"D4V {rk} coverage",d4["methods"][gk]["coverage"],
              ref["resolved_coverage"][rk],rel=0.0,abs_tol=1e-12)

    gauge_path=build/"gauge-dcs-retrospective/summary.json"
    if gauge_path.is_file():
        gauge=load(gauge_path); ref=stages["GAUGE"]
        check(result,"GAUGE trial count",gauge["trial_count"],ref["trials"])
        check(result,"GAUGE ordinary overlap wins",gauge["ordinary_dual_overlap_wins"],ref["ordinary_overlap_wins"])
        check(result,"GAUGE marker endpoint wins",gauge["dcs_marker_endpoint_wins"],ref["marker_darkfield_endpoint_wins"])
        check(result,"GAUGE longitudinal endpoint wins",gauge["dcs_long_endpoint_wins"],ref["longitudinal_darkfield_endpoint_wins"])
        check(result,"GAUGE endpoint marker median",
              gauge["median_endpoint_dcs_marker_error_m"],ref["median_marker_error_m"]["endpoint"],rel=0.02,abs_tol=2e-6)
        check(result,"GAUGE overlap marker median",
              gauge["median_overlap_dcs_marker_error_m"],ref["median_marker_error_m"]["overlap"],rel=0.02,abs_tol=2e-6)
        check(result,"GAUGE endpoint longitudinal median",
              gauge["median_endpoint_dcs_long_error"],ref["median_longitudinal_error"]["endpoint"],rel=0.03,abs_tol=2e-5)
        check(result,"GAUGE overlap longitudinal median",
              gauge["median_overlap_dcs_long_error"],ref["median_longitudinal_error"]["overlap"],rel=0.03,abs_tol=2e-5)
    elif not allow_missing_gauge:
        result["passed"]=False
        result["checks"].append({
            "name":"GAUGE result present","actual":False,"expected":True,"passed":False,
            "relative_tolerance":None,"absolute_tolerance":None,
        })

    ofc=load(build/"orthogonal-force-compliance/analysis.json")
    check(result,"OFC proposal count",ofc["proposal_count"],ofc_ref["proposal_count"])
    check(result,"OFC deceptive count",ofc["deceptive_count"],ofc_ref["deceptive_count"])
    check(result,"OFC beneficial count",ofc["beneficial_count"],ofc_ref["beneficial_count"])
    check(result,"OFC DCS coverage",ofc["dcs"]["coverage"],ofc_ref["dcs"]["coverage"],rel=0.0,abs_tol=1e-12)
    check(result,"OFC force coverage",ofc["force"]["coverage"],ofc_ref["force_compliance"]["coverage"],rel=0.0,abs_tol=1e-12)
    check(result,"OFC force median abs z",ofc["force"]["median_abs_z"],
          ofc_ref["force_compliance"]["median_abs_z"],rel=0.03,abs_tol=1e-4)
    check(result,"OFC force max abs z",ofc["force"]["max_abs_z"],
          ofc_ref["force_compliance"]["max_abs_z"],rel=0.03,abs_tol=1e-4)
    check(result,"OFC force/DCS median-z ratio",ofc["force_to_dcs_median_abs_z_ratio"],
          ofc_ref["force_to_dcs_median_abs_z_ratio"],rel=0.04,abs_tol=1e-3)
    check(result,"OFC advancement gate",ofc["advancement_gate_pass"],ofc_ref["advancement_gate_pass"])

    cert=load(build/"paper-captured-world-run/certificate.json")
    check(result,"captured-world certificate schema",cert.get("schema"),"vulkax_captured_world_run")
    check(result,"captured-world run completed",cert.get("run_status"),"completed")

    result["check_count"]=len(result["checks"])
    result["failed_count"]=sum(not c["passed"] for c in result["checks"])
    return result

def self_test() -> None:
    assert near(1.001,1.0,rel=0.01)
    assert not near(1.2,1.0,rel=0.01)
    r={"passed":True,"checks":[]}
    check(r,"x",1,1)
    check(r,"y",1.001,1.0,rel=.01)
    assert r["passed"] and len(r["checks"])==2
    print("VALID paper reproduction validator self-test")

def main() -> int:
    ap=argparse.ArgumentParser()
    ap.add_argument("--repo-root",default=".")
    ap.add_argument("--build-root",default="build")
    ap.add_argument("--out",default="build/paper-evidence/generated/reproduction-validation.json")
    ap.add_argument("--allow-missing-gauge",action="store_true")
    ap.add_argument("--self-test",action="store_true")
    args=ap.parse_args()
    if args.self_test:
        self_test(); return 0
    repo=Path(args.repo_root).resolve()
    build=(repo/args.build_root).resolve() if not Path(args.build_root).is_absolute() else Path(args.build_root).resolve()
    out=(repo/args.out).resolve() if not Path(args.out).is_absolute() else Path(args.out).resolve()
    result=validate(repo,build,args.allow_missing_gauge)
    out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(json.dumps(result,indent=2)+"\n")
    print("PAPER_REPRODUCTION", "PASS" if result["passed"] else "FAIL")
    print("CHECKS",result["check_count"],"FAILED",result["failed_count"])
    print("OUT",out)
    if not result["passed"]:
        for c in result["checks"]:
            if not c["passed"]:
                print("FAILED",c["name"],"actual",c["actual"],"expected",c["expected"])
        return 1
    return 0

if __name__=="__main__":
    raise SystemExit(main())

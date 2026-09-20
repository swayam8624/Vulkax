#!/usr/bin/env python3
"""Post-hoc information-frontier diagnostics for the frozen D4V and GAUGE artifacts.

This analysis does not change any preregistered gate. It quantifies:
- how far unresolved D4V progress statistics are from the inherited |z|=2 reference;
- sign-direction agreement with hidden beneficial/deceptive labels;
- paired GAUGE channel differences and exact sign-test summaries.

All outputs are diagnostic and must be labelled post-hoc/retrospective where used.
"""
from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
import statistics
import tempfile


METHODS={
    "dcs":"dcs_progress_z",
    "raw_bundle":"raw_bundle_progress_z",
    "raw_point":"raw_point_progress_z",
    "fisher":"fisher_progress_z",
    "max_motion":"maxmotion_progress_z",
}

def quantile(values:list[float],q:float)->float:
    xs=sorted(values)
    if not xs:
        raise ValueError("empty quantile input")
    if len(xs)==1:
        return xs[0]
    pos=(len(xs)-1)*q
    lo=math.floor(pos); hi=math.ceil(pos)
    if lo==hi:
        return xs[lo]
    w=pos-lo
    return xs[lo]*(1-w)+xs[hi]*w

def exact_two_sided_sign_p(positive:int,negative:int)->float|None:
    n=positive+negative
    if n==0:
        return None
    m=min(positive,negative)
    p=2.0*sum(math.comb(n,i) for i in range(m+1))/(2**n)
    return min(1.0,p)

def analyze_d4v(path:Path,threshold:float=2.0)->dict:
    rows=list(csv.DictReader(path.open()))
    if not rows:
        raise RuntimeError("D4V proposal table is empty")
    out={"proposal_count":len(rows),"threshold_abs_z":threshold,"methods":{}}
    for name,col in METHODS.items():
        zs=[float(r[col]) for r in rows]
        absz=[abs(v) for v in zs]
        nonzero=[v for v in absz if v>0]
        deceptive=[float(r[col]) for r in rows if r["label"]=="deceptive"]
        beneficial=[float(r[col]) for r in rows if r["label"]=="beneficial"]
        correct=sum(
            (r["label"]=="deceptive" and float(r[col])<0) or
            (r["label"]=="beneficial" and float(r[col])>0)
            for r in rows
        )
        med=statistics.median(absz)
        out["methods"][name]={
            "median_abs_z":med,
            "p90_abs_z":quantile(absz,.9),
            "max_abs_z":max(absz),
            "resolved_count":sum(v>=threshold for v in absz),
            "median_required_signal_amplification_to_threshold":
                statistics.median(threshold/v for v in nonzero),
            "minimum_required_signal_amplification_to_threshold":
                min(threshold/v for v in nonzero),
            "median_required_sigma_fraction_to_threshold":med/threshold,
            "median_required_variance_fraction_to_threshold":(med/threshold)**2,
            "sign_accuracy_all":correct/len(rows),
            "deceptive_negative_fraction":
                sum(v<0 for v in deceptive)/len(deceptive) if deceptive else None,
            "beneficial_positive_fraction":
                sum(v>0 for v in beneficial)/len(beneficial) if beneficial else None,
            "median_z_deceptive":statistics.median(deceptive) if deceptive else None,
            "median_z_beneficial":statistics.median(beneficial) if beneficial else None,
        }
    return out

def analyze_gauge(path:Path)->dict:
    rows=list(csv.DictReader(path.open()))
    if not rows:
        raise RuntimeError("GAUGE per-trial table is empty")
    specs={
        "face_nrmse":("endpoint_face_nrmse","overlap_face_nrmse"),
        "marker_rmse_m":("endpoint_marker_rmse_m","overlap_marker_rmse_m"),
        "dcs_marker_error_m":("endpoint_dcs_marker_error_m","overlap_dcs_marker_error_m"),
        "dcs_longitudinal_error":("endpoint_dcs_long_error","overlap_dcs_long_error"),
    }
    out={"trial_count":len(rows),"paired":{}}
    for name,(a,b) in specs.items():
        diffs=[float(r[a])-float(r[b]) for r in rows]
        endpoint_better=sum(d<0 for d in diffs)
        overlap_better=sum(d>0 for d in diffs)
        ties=sum(d==0 for d in diffs)
        out["paired"][name]={
            "difference_definition":"endpoint_minus_overlap",
            "median_difference":statistics.median(diffs),
            "mean_difference":statistics.fmean(diffs),
            "endpoint_better_count":endpoint_better,
            "overlap_better_count":overlap_better,
            "ties":ties,
            "exact_two_sided_sign_test_p":
                exact_two_sided_sign_p(endpoint_better,overlap_better),
        }
    out["warning"]="Exact sign tests are retrospective diagnostics; they are not a preregistered confirmatory analysis."
    return out

def write_csv(path:Path,d4:dict)->None:
    with path.open("w",newline="") as f:
        fields=["method","median_abs_z","p90_abs_z","max_abs_z","resolved_count",
                "median_required_signal_amplification_to_threshold",
                "minimum_required_signal_amplification_to_threshold",
                "median_required_sigma_fraction_to_threshold",
                "median_required_variance_fraction_to_threshold",
                "sign_accuracy_all","deceptive_negative_fraction",
                "beneficial_positive_fraction","median_z_deceptive",
                "median_z_beneficial"]
        w=csv.DictWriter(f,fieldnames=fields)
        w.writeheader()
        for method,data in d4["methods"].items():
            w.writerow({"method":method,**data})

def run(d4v:Path,gauge:Path|None,out:Path,allow_missing_gauge:bool)->dict:
    out.mkdir(parents=True,exist_ok=True)
    d4=analyze_d4v(d4v)
    gauge_result=None
    if gauge and gauge.is_file():
        gauge_result=analyze_gauge(gauge)
    elif not allow_missing_gauge:
        raise RuntimeError("GAUGE per-trial table missing")
    result={
        "schema":"vulkax.paper_information_frontier",
        "version":1,
        "analysis_class":"post_hoc_diagnostic",
        "d4v":d4,
        "gauge":gauge_result,
        "warning":"These diagnostics do not modify D2/D3/D4V gates or convert GAUGE into prospective confirmation."
    }
    (out/"information_frontier.json").write_text(json.dumps(result,indent=2)+"\n")
    write_csv(out/"d4v_information_frontier.csv",d4)
    if gauge_result:
        with (out/"gauge_paired_diagnostics.csv").open("w",newline="") as f:
            fields=["channel","median_difference","mean_difference","endpoint_better_count",
                    "overlap_better_count","ties","exact_two_sided_sign_test_p"]
            w=csv.DictWriter(f,fieldnames=fields)
            w.writeheader()
            for channel,data in gauge_result["paired"].items():
                w.writerow({"channel":channel,**{k:data[k] for k in fields if k!="channel"}})
    return result

def self_test()->None:
    with tempfile.TemporaryDirectory() as td:
        root=Path(td)
        d4=root/"d4.csv"
        d4.write_text(
            "label,dcs_progress_z,raw_bundle_progress_z,raw_point_progress_z,fisher_progress_z,maxmotion_progress_z\n"
            "deceptive,-0.5,-0.2,-1.0,-0.3,-0.4\n"
            "beneficial,0.6,0.3,1.2,0.4,0.5\n"
        )
        g=root/"g.csv"
        g.write_text(
            "endpoint_face_nrmse,overlap_face_nrmse,endpoint_marker_rmse_m,overlap_marker_rmse_m,"
            "endpoint_dcs_marker_error_m,overlap_dcs_marker_error_m,endpoint_dcs_long_error,overlap_dcs_long_error\n"
            "2,1,2,1,2,1,1,2\n"
            "3,1,3,1,3,1,1,2\n"
        )
        out=root/"out"
        r=run(d4,g,out,False)
        assert r["d4v"]["methods"]["dcs"]["resolved_count"]==0
        assert r["gauge"]["paired"]["dcs_longitudinal_error"]["endpoint_better_count"]==2
        assert (out/"d4v_information_frontier.csv").is_file()
        print("VALID information-frontier diagnostic self-test")

def main()->int:
    ap=argparse.ArgumentParser()
    ap.add_argument("--d4v",default="build/dcs-d4v-discovery/proposals.csv")
    ap.add_argument("--gauge",default="build/gauge-dcs-retrospective/per_trial.csv")
    ap.add_argument("--out",default="build/paper-diagnostics")
    ap.add_argument("--allow-missing-gauge",action="store_true")
    ap.add_argument("--self-test",action="store_true")
    args=ap.parse_args()
    if args.self_test:
        self_test(); return 0
    gauge=Path(args.gauge)
    result=run(Path(args.d4v),gauge if gauge.exists() else None,Path(args.out),args.allow_missing_gauge)
    print("WROTE post-hoc information frontier",args.out)
    print("D4V_DCS_MEDIAN_ABS_Z",result["d4v"]["methods"]["dcs"]["median_abs_z"])
    print("D4V_DCS_MEDIAN_REQUIRED_AMPLIFICATION",
          result["d4v"]["methods"]["dcs"]["median_required_signal_amplification_to_threshold"])
    if result["gauge"]:
        print("GAUGE_LONG_SIGN_P",
              result["gauge"]["paired"]["dcs_longitudinal_error"]["exact_two_sided_sign_test_p"])
    return 0

if __name__=="__main__":
    raise SystemExit(main())

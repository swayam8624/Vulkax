#!/usr/bin/env python3
"""Analyze D3 witness-space/adaptive-order DCS discovery.

Discovery only: this script reports the result; it does not create a confirmatory
claim or tune a threshold on this partition.
"""
import csv,itertools,json,pathlib,statistics,sys

root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/dcs-d3-discovery")
rows=list(csv.DictReader((root/"cases.csv").open()))
stencils=list(csv.DictReader((root/"stencils.csv").open()))
meta=json.loads((root/"summary.json").read_text())
if meta.get("provenance")!="synthetic-dcs-d3-discovery":
    raise SystemExit("unexpected D3 provenance")
if meta.get("protocol")!="research/benchmarks/DCS_D3_DISCOVERY_PROTOCOL.md":
    raise SystemExit("D3 protocol marker missing")

def f(r,k): return float(r[k])

truth_ids=sorted({int(r["truth_id"]) for r in rows})
if len(truth_ids)!=6:
    raise SystemExit(f"expected 6 D3 truth worlds, found {len(truth_ids)}")
if len(rows)!=24:
    raise SystemExit(f"expected 24 D3 candidate rows, found {len(rows)}")

methods={
  "adaptive_dcs":"adaptive_error_m",
  "order2_dcs":"order2_error_m",
  "order3_dcs":"order3_error_m",
  "raw_bundle_same_cost":"raw_bundle_error_m",
  "raw_pair_aware":"raw_error_m",
  "fisher_sensitivity":"fisher_error_m",
  "max_motion":"maxmotion_error_m",
  "random":"random_error_m",
}

result={
  "schema":"vulkax.dcs.d3_discovery_analysis",
  "version":1,
  "provenance":"synthetic-dcs-d3-discovery",
  "truth_count":len(truth_ids),
  "candidate_count":len(rows),
  "methods":{},
  "adaptive_order_counts":{},
  "observability":{},
  "stencil_contract":{},
  "warning":"Discovery only. Do not treat this partition as confirmatory evidence."
}

pairs=[]
for tid in truth_ids:
    g=[r for r in rows if int(r["truth_id"])==tid]
    adaptive_orders={int(r["adaptive_order"]) for r in g}
    if len(adaptive_orders)!=1:
        raise SystemExit(f"inconsistent adaptive order in truth {tid}")
    order=next(iter(adaptive_orders))
    result["adaptive_order_counts"][str(order)]=result["adaptive_order_counts"].get(str(order),0)+1
    for a,b in itertools.combinations(g,2):
        gap=f(a,"target_error_m")-f(b,"target_error_m")
        if abs(gap)<1e-15:
            continue
        target="a" if gap<0 else "b"
        rec={"truth_id":tid,"a":a,"b":b,"target":target}
        for name,key in methods.items():
            pref="a" if f(a,key)<f(b,key) else "b"
            rec[name+"_correct"]=pref==target
        pairs.append(rec)

raw_mirages=[p for p in pairs if not p["raw_pair_aware_correct"]]
raw_correct=[p for p in pairs if p["raw_pair_aware_correct"]]

for name,key in methods.items():
    correct=sum(p[name+"_correct"] for p in pairs)
    corrected=sum(p[name+"_correct"] for p in raw_mirages)
    new_errors=sum(not p[name+"_correct"] for p in raw_correct)
    result["methods"][name]={
      "pair_count":len(pairs),
      "target_ranking_agreement":correct/len(pairs) if pairs else None,
      "raw_mirage_pairs":len(raw_mirages),
      "corrected_raw_mirages":corrected,
      "raw_mirage_correction_rate":corrected/len(raw_mirages) if raw_mirages else None,
      "new_errors_on_raw_correct_pairs":new_errors,
      "median_selector_error_m":statistics.median(f(r,key) for r in rows),
    }

per_truth=[]
for tid in truth_ids:
    g=[r for r in rows if int(r["truth_id"])==tid]
    r0=g[0]
    per_truth.append({
      "truth_id":tid,
      "adaptive_order":int(r0["adaptive_order"]),
      "adaptive_separation":f(r0,"adaptive_separation"),
      "order2_separation":f(r0,"order2_separation"),
      "order3_separation":f(r0,"order3_separation"),
      "order2_numerical_rms_m":f(r0,"order2_numerical_rms_m"),
      "order3_numerical_rms_m":f(r0,"order3_numerical_rms_m"),
    })
result["observability"]={
  "reference_threshold":2.0,
  "adaptive_resolved_worlds":sum(x["adaptive_separation"]>=2.0 for x in per_truth),
  "order2_resolved_worlds":sum(x["order2_separation"]>=2.0 for x in per_truth),
  "order3_resolved_worlds":sum(x["order3_separation"]>=2.0 for x in per_truth),
  "median_adaptive_separation":statistics.median(x["adaptive_separation"] for x in per_truth),
  "median_order2_separation":statistics.median(x["order2_separation"] for x in per_truth),
  "median_order3_separation":statistics.median(x["order3_separation"] for x in per_truth),
  "median_order2_numerical_rms_m":statistics.median(x["order2_numerical_rms_m"] for x in per_truth),
  "median_order3_numerical_rms_m":statistics.median(x["order3_numerical_rms_m"] for x in per_truth),
  "per_truth":per_truth,
}
result["stencil_contract"]={
  "maximum_moment_residual":max(float(r["moment_residual"]) for r in stencils),
  "all_le_1e_9":all(float(r["moment_residual"])<=1e-9 for r in stencils),
}

adaptive=result["methods"]["adaptive_dcs"]
bundle=result["methods"]["raw_bundle_same_cost"]
raw=result["methods"]["raw_pair_aware"]
obs=result["observability"]
result["discovery_assessment"]={
  "any_observable_world":obs["adaptive_resolved_worlds"]>0,
  "adaptive_beats_fixed_order2":
      adaptive["target_ranking_agreement"]>result["methods"]["order2_dcs"]["target_ranking_agreement"],
  "adaptive_beats_raw_pair_aware":
      adaptive["target_ranking_agreement"]>raw["target_ranking_agreement"],
  "adaptive_beats_same_cost_raw_bundle":
      adaptive["target_ranking_agreement"]>bundle["target_ranking_agreement"],
  "corrects_some_raw_mirage":
      adaptive["corrected_raw_mirages"]>0,
  "net_correction_positive":
      adaptive["corrected_raw_mirages"]>adaptive["new_errors_on_raw_correct_pairs"],
}
result["discovery_assessment"]["promising_for_new_validation"]=(
    result["discovery_assessment"]["any_observable_world"]
    and result["discovery_assessment"]["adaptive_beats_fixed_order2"]
    and result["discovery_assessment"]["corrects_some_raw_mirage"]
    and result["stencil_contract"]["all_le_1e_9"]
)

(root/"analysis.json").write_text(json.dumps(result,indent=2)+"\n")
print("VALID DCS D3 witness-space discovery")
for name,data in result["methods"].items():
    print("METHOD",name,
          "agreement",data["target_ranking_agreement"],
          "mirage_correction",data["raw_mirage_correction_rate"],
          "new_errors",data["new_errors_on_raw_correct_pairs"])
print("ADAPTIVE_ORDER_COUNTS",result["adaptive_order_counts"])
print("OBSERVABILITY",result["observability"])
print("STENCIL_CONTRACT",result["stencil_contract"])
print("DISCOVERY_ASSESSMENT",result["discovery_assessment"])
print("DECISION",
      "freeze_d4_validation_if_method_configuration_is_now_fixed"
      if result["discovery_assessment"]["promising_for_new_validation"]
      else "d3_not_yet_strong_enough_for_validation")

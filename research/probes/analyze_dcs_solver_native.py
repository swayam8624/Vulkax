#!/usr/bin/env python3
"""Analyze solver-native DCS discovery without tuning thresholds."""
import csv,itertools,json,pathlib,statistics,sys

root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/dcs-solver-native")
rows=list(csv.DictReader((root/"cases.csv").open()))
meta=json.loads((root/"summary.json").read_text())
if meta.get("provenance")!="synthetic-dcs-solver-native-discovery":
    raise SystemExit("unexpected solver-native DCS provenance")

def f(r,k): return float(r[k])
truth_ids=sorted({int(r["truth_id"]) for r in rows})
pair_records=[]
mirages=[]
for tid in truth_ids:
    g=[r for r in rows if int(r["truth_id"])==tid]
    for a,b in itertools.combinations(g,2):
        target_gap=f(a,"target_error_m")-f(b,"target_error_m")
        if abs(target_gap)<1e-12:
            continue
        target_pref="a" if target_gap<0 else "b"
        held_pref="a" if f(a,"heldout_rmse_m")<f(b,"heldout_rmse_m") else "b"
        dcs_pref="a" if f(a,"witness_error_m")<f(b,"witness_error_m") else "b"
        rec={
          "truth_id":tid,
          "a":a["variant"],"b":b["variant"],
          "target_pref":target_pref,
          "heldout_pref":held_pref,
          "dcs_pref":dcs_pref,
          "heldout_correct":held_pref==target_pref,
          "dcs_correct":dcs_pref==target_pref,
        }
        pair_records.append(rec)
        if held_pref!=target_pref:
            mirages.append(rec)

result={
 "schema":"vulkax.dcs.solver_native_discovery_analysis",
 "version":1,
 "provenance":"synthetic-dcs-solver-native-discovery",
 "truth_count":len(truth_ids),
 "candidate_count":len(rows),
 "pair_count":len(pair_records),
 "heldout_pairwise_target_agreement":
    sum(r["heldout_correct"] for r in pair_records)/len(pair_records) if pair_records else None,
 "dcs_pairwise_target_agreement":
    sum(r["dcs_correct"] for r in pair_records)/len(pair_records) if pair_records else None,
 "ordinary_metric_mirage_pairs":len(mirages),
 "dcs_corrected_mirage_pairs":sum(r["dcs_correct"] for r in mirages),
 "dcs_correction_rate_on_mirages":
    sum(r["dcs_correct"] for r in mirages)/len(mirages) if mirages else None,
 "by_variant":{},
 "warning":"Discovery only. No thresholds or method choices may be validated on this same dataset."
}
for v in sorted({r["variant"] for r in rows}):
    g=[r for r in rows if r["variant"]==v]
    result["by_variant"][v]={
      "median_fit_objective_m":statistics.median(f(r,"fit_objective_m") for r in g),
      "median_heldout_rmse_m":statistics.median(f(r,"heldout_rmse_m") for r in g),
      "median_witness_error_m":statistics.median(f(r,"witness_error_m") for r in g),
      "median_target_error_m":statistics.median(f(r,"target_error_m") for r in g),
    }

(root/"analysis.json").write_text(json.dumps(result,indent=2)+"\n")
print("VALID DCS solver-native discovery")
print("HELDOUT_PAIRWISE_TARGET_AGREEMENT",result["heldout_pairwise_target_agreement"])
print("DCS_PAIRWISE_TARGET_AGREEMENT",result["dcs_pairwise_target_agreement"])
print("MIRAGE_PAIRS",result["ordinary_metric_mirage_pairs"])
print("DCS_CORRECTED_MIRAGES",result["dcs_corrected_mirage_pairs"])
for v,d in result["by_variant"].items():
    print("VARIANT",v,d)

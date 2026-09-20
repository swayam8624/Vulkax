#!/usr/bin/env python3
"""Analyze solver-native DCS active-selection discovery without threshold fitting."""
import csv,itertools,json,pathlib,statistics,sys

root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/dcs-active-selection")
rows=list(csv.DictReader((root/"cases.csv").open()))
meta=json.loads((root/"summary.json").read_text())
if meta.get("provenance")!="synthetic-dcs-active-discovery":
    raise SystemExit("unexpected DCS active-selection provenance")

def f(r,k): return float(r[k])
truth_ids=sorted({int(r["truth_id"]) for r in rows})
methods={
    "dcs":"dcs_error_m",
    "raw_bundle_same_cost":"raw_bundle_error_m",
    "raw_maximin":"raw_error_m",
    "max_motion":"maxmotion_error_m",
    "random":"random_error_m",
}
summary={
    "schema":"vulkax.dcs.active_selection_discovery_analysis",
    "version":1,
    "provenance":"synthetic-dcs-active-discovery",
    "truth_count":len(truth_ids),
    "candidate_count":len(rows),
    "methods":{},
    "warning":"Discovery only. Do not set confirmatory thresholds or claim superiority from this dataset."
}

for name,key in methods.items():
    pair_count=0
    correct=0
    mirage_pairs=0
    corrected_mirages=0
    for tid in truth_ids:
        g=[r for r in rows if int(r["truth_id"])==tid]
        for a,b in itertools.combinations(g,2):
            target_gap=f(a,"target_error_m")-f(b,"target_error_m")
            if abs(target_gap)<1e-12:
                continue
            target_pref="a" if target_gap<0 else "b"
            method_pref="a" if f(a,key)<f(b,key) else "b"
            raw_pref="a" if f(a,"raw_error_m")<f(b,"raw_error_m") else "b"
            pair_count+=1
            correct+=method_pref==target_pref
            if raw_pref!=target_pref:
                mirage_pairs+=1
                corrected_mirages+=method_pref==target_pref
    summary["methods"][name]={
        "pair_count":pair_count,
        "target_ranking_agreement":correct/pair_count if pair_count else None,
        "raw_metric_mirage_pair_count":mirage_pairs,
        "correction_rate_on_raw_mirages":
            corrected_mirages/mirage_pairs if mirage_pairs else None,
        "median_error_m":statistics.median(f(r,key) for r in rows),
    }

summary["dcs_selector"]={
    "median_predicted_worst_case_standardized_separation":
      statistics.median(f(r,"dcs_maximin_separation") for r in rows),
    "selected_weight_rows":sum(1 for _ in csv.DictReader((root/"stencils.csv").open())),
}

(root/"analysis.json").write_text(json.dumps(summary,indent=2)+"\n")
print("VALID DCS active-selection discovery")
for name,data in summary["methods"].items():
    print("METHOD",name,data)
print("DCS_SELECTOR",summary["dcs_selector"])

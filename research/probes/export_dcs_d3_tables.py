#!/usr/bin/env python3
"""Create paper-ready CSV summaries from DCS D3 discovery outputs.

This script does not alter or reinterpret the scientific decision; it only
materializes the already-computed analysis into flat tables for plotting/paper use.
"""
import csv,json,pathlib,sys

root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/dcs-d3-discovery")
analysis=json.loads((root/"analysis.json").read_text())
rows=list(csv.DictReader((root/"cases.csv").open()))

methods=analysis["methods"]
with (root/"method_summary.csv").open("w",newline="") as f:
    w=csv.writer(f)
    w.writerow([
        "method","pair_count","target_ranking_agreement","raw_mirage_pairs",
        "corrected_raw_mirages","raw_mirage_correction_rate",
        "new_errors_on_raw_correct_pairs","median_selector_error_m"
    ])
    for name,data in methods.items():
        w.writerow([
            name,data["pair_count"],data["target_ranking_agreement"],
            data["raw_mirage_pairs"],data["corrected_raw_mirages"],
            data["raw_mirage_correction_rate"],
            data["new_errors_on_raw_correct_pairs"],
            data["median_selector_error_m"],
        ])

with (root/"truth_summary.csv").open("w",newline="") as f:
    w=csv.writer(f)
    w.writerow([
        "truth_id","adaptive_order","adaptive_separation","order2_separation",
        "order3_separation","order2_numerical_rms_m","order3_numerical_rms_m"
    ])
    for item in analysis["observability"]["per_truth"]:
        w.writerow([
            item["truth_id"],item["adaptive_order"],item["adaptive_separation"],
            item["order2_separation"],item["order3_separation"],
            item["order2_numerical_rms_m"],item["order3_numerical_rms_m"]
        ])

print("WROTE",root/"method_summary.csv")
print("WROTE",root/"truth_summary.csv")

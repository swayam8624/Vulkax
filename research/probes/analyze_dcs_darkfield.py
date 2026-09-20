#!/usr/bin/env python3
"""Preregistered analysis for the constructed DCS deceptive-repair positive control."""
import csv,json,pathlib,statistics,sys

root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/dcs-darkfield-synthetic")
rows=list(csv.DictReader((root/"cases.csv").open()))
meta=json.loads((root/"summary.json").read_text())
if meta.get("provenance")!="synthetic-dcs-positive-control":
    raise SystemExit("unexpected DCS positive-control provenance")
if len(rows)!=int(meta["case_count"]) or len(rows)!=64:
    raise SystemExit("DCS positive control must contain the frozen 64 cases")

def b(r,k): return int(r[k])!=0
def f(r,k): return float(r[k])

ordinary_accept=[r for r in rows if f(r,"repair_loss") < f(r,"baseline_loss")]
deceptive=[r for r in rows if b(r,"deceptive")]
dcs_accept=[r for r in ordinary_accept if not b(r,"deceptive")]

result={
  "schema":"vulkax.dcs.synthetic_positive_control_analysis",
  "version":1,
  "provenance":"synthetic-dcs-positive-control",
  "case_count":len(rows),
  "ordinary_metric_accept_count":len(ordinary_accept),
  "deceptive_repair_count":len(deceptive),
  "dcs_accept_count":len(dcs_accept),
  "ordinary_metric_false_accept_rate":
      sum(b(r,"deceptive") for r in ordinary_accept)/len(ordinary_accept)
      if ordinary_accept else None,
  "median_observation_improvement_fraction":
      statistics.median((f(r,"baseline_loss")-f(r,"repair_loss"))/
                        max(f(r,"baseline_loss"),1e-15) for r in rows),
  "median_witness_degradation_ratio":
      statistics.median(f(r,"repair_witness_error")/
                        max(f(r,"baseline_witness_error"),1e-15) for r in rows),
  "predeclared_positive_control_gate":{
      "required_deceptive_detected_at_least":60,
      "required_ordinary_metric_accept_at_least":60,
      "pass":len(deceptive)>=60 and len(ordinary_accept)>=60,
  },
  "warning":"Constructed positive control only. Passing this gate demonstrates implementation correctness, not scientific validity."
}
(root/"analysis.json").write_text(json.dumps(result,indent=2)+"\n")
print("VALID DCS deceptive-repair positive control")
print("ORDINARY_ACCEPT",len(ordinary_accept),"/",len(rows))
print("DECEPTIVE",len(deceptive),"/",len(rows))
print("DCS_ACCEPT",len(dcs_accept),"/",len(rows))
print("MEDIAN_OBSERVATION_IMPROVEMENT",result["median_observation_improvement_fraction"])
print("MEDIAN_WITNESS_DEGRADATION_RATIO",result["median_witness_degradation_ratio"])
print("DECISION","pass_positive_control" if result["predeclared_positive_control_gate"]["pass"] else "kill_dcs_implementation")
if not result["predeclared_positive_control_gate"]["pass"]:
    raise SystemExit("DCS constructed positive control failed")

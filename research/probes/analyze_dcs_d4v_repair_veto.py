#!/usr/bin/env python3
import csv,json,pathlib,sys
root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/dcs-d4v-discovery")
rows=list(csv.DictReader((root/"proposals.csv").open()))
meta=json.loads((root/"summary.json").read_text())
if meta.get("provenance")!="synthetic-dcs-d4v-discovery":
    raise SystemExit("unexpected D4V provenance")
if meta.get("protocol")!="research/benchmarks/DCS_D4V_REPAIR_VETO_DISCOVERY_PROTOCOL.md":
    raise SystemExit("protocol marker missing")
methods={
 "dcs":("dcs_progress_z","dcs_decision"),
 "raw_bundle":("raw_bundle_progress_z","raw_bundle_decision"),
 "raw_point":("raw_point_progress_z","raw_point_decision"),
 "fisher":("fisher_progress_z","fisher_decision"),
 "max_motion":("maxmotion_progress_z","maxmotion_decision"),
}
deceptive=[r for r in rows if r["label"]=="deceptive"]
beneficial=[r for r in rows if r["label"]=="beneficial"]
out={"schema":"vulkax.dcs.d4v_repair_veto_analysis","version":1,
     "proposal_count":len(rows),"deceptive_count":len(deceptive),
     "beneficial_count":len(beneficial),"methods":{}}
for name,(_,dk) in methods.items():
    resolved=[r for r in rows if r[dk]!="unresolved"]
    vetoed=[r for r in rows if r[dk]=="veto"]
    supported=[r for r in rows if r[dk]=="support"]
    tp=sum(r["label"]=="deceptive" for r in vetoed)
    fp=sum(r["label"]=="beneficial" for r in vetoed)
    resolved_deceptive=[r for r in deceptive if r[dk]!="unresolved"]
    resolved_beneficial=[r for r in beneficial if r[dk]!="unresolved"]
    out["methods"][name]={
      "coverage":len(resolved)/len(rows) if rows else 0,
      "veto_precision":tp/len(vetoed) if vetoed else None,
      "deceptive_veto_recall_all":tp/len(deceptive) if deceptive else None,
      "deceptive_veto_recall_resolved":
        sum(r[dk]=="veto" for r in resolved_deceptive)/len(resolved_deceptive) if resolved_deceptive else None,
      "false_veto_rate_beneficial_resolved":
        sum(r[dk]=="veto" for r in resolved_beneficial)/len(resolved_beneficial) if resolved_beneficial else None,
      "beneficial_support_rate_resolved":
        sum(r[dk]=="support" for r in resolved_beneficial)/len(resolved_beneficial) if resolved_beneficial else None,
      "veto_count":len(vetoed),"support_count":len(supported),
      "unresolved_count":len(rows)-len(resolved),
    }
d=out["methods"]["dcs"]; rb=out["methods"]["raw_bundle"]
gate={
 "at_least_4_deceptive":len(deceptive)>=4,
 "coverage_at_least_30pct":d["coverage"]>=0.30,
 "resolved_deceptive_recall_at_least_60pct":
    d["deceptive_veto_recall_resolved"] is not None and d["deceptive_veto_recall_resolved"]>=0.60,
 "resolved_beneficial_false_veto_at_most_20pct":
    d["false_veto_rate_beneficial_resolved"] is not None and d["false_veto_rate_beneficial_resolved"]<=0.20,
 "veto_precision_at_least_70pct":
    d["veto_precision"] is not None and d["veto_precision"]>=0.70,
 "raw_bundle_not_strictly_dominant":
    not (
      rb["deceptive_veto_recall_resolved"] is not None and
      d["deceptive_veto_recall_resolved"] is not None and
      rb["false_veto_rate_beneficial_resolved"] is not None and
      d["false_veto_rate_beneficial_resolved"] is not None and
      rb["deceptive_veto_recall_resolved"]>d["deceptive_veto_recall_resolved"] and
      rb["false_veto_rate_beneficial_resolved"]<d["false_veto_rate_beneficial_resolved"]
    ),
 "moment_contract":max(float(r["moment_residual"]) for r in rows)<=1e-9 if rows else False,
}
gate["pass"]=all(gate.values())
out["advancement_gate"]=gate
(root/"analysis.json").write_text(json.dumps(out,indent=2)+"\n")
print("VALID D4V repair-veto discovery")
print("PROPOSALS",len(rows),"deceptive",len(deceptive),"beneficial",len(beneficial))
for n,d in out["methods"].items(): print("METHOD",n,d)
print("GATE",gate)
print("DECISION","freeze_new_validation" if gate["pass"] else "d4v_not_strong_enough")

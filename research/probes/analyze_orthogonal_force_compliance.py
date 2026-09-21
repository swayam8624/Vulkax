#!/usr/bin/env python3
import csv,json,math,pathlib,statistics,sys

root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/orthogonal-force-compliance")
rows=list(csv.DictReader((root/"proposals.csv").open()))
meta=json.loads((root/"summary.json").read_text())
if meta.get("provenance")!="synthetic-orthogonal-force-compliance":
    raise SystemExit("unexpected provenance")
if meta.get("protocol")!="research/benchmarks/ORTHOGONAL_FORCE_COMPLIANCE_PROTOCOL_2026-09-21.md":
    raise SystemExit("protocol marker missing")

def stats(prefix):
    decisions=[r[prefix+"_decision"] for r in rows]
    zs=[float(r[prefix+"_progress_z"]) for r in rows]
    deceptive=[r for r in rows if r["label"]=="deceptive"]
    beneficial=[r for r in rows if r["label"]=="beneficial"]
    resolved=[r for r in rows if r[prefix+"_decision"]!="unresolved"]
    vetoed=[r for r in rows if r[prefix+"_decision"]=="veto"]
    supported=[r for r in rows if r[prefix+"_decision"]=="support"]
    tp=sum(r["label"]=="deceptive" for r in vetoed)
    fp=sum(r["label"]=="beneficial" for r in vetoed)
    sign_ok=sum((r["label"]=="deceptive" and float(r[prefix+"_progress_z"])<0) or
                (r["label"]=="beneficial" and float(r[prefix+"_progress_z"])>0) for r in rows)
    return {
      "coverage":len(resolved)/len(rows) if rows else 0.0,
      "veto_count":len(vetoed),
      "support_count":len(supported),
      "unresolved_count":len(rows)-len(resolved),
      "deceptive_veto_recall_all":tp/len(deceptive) if deceptive else None,
      "beneficial_support_rate_all":sum(r[prefix+"_decision"]=="support" for r in beneficial)/len(beneficial) if beneficial else None,
      "beneficial_false_veto_rate_all":fp/len(beneficial) if beneficial else None,
      "sign_accuracy":sign_ok/len(rows) if rows else 0.0,
      "median_abs_z":statistics.median(abs(z) for z in zs) if zs else None,
      "max_abs_z":max(abs(z) for z in zs) if zs else None,
    }

dcs=stats("dcs")
force=stats("force")
deceptive=sum(r["label"]=="deceptive" for r in rows)
beneficial=sum(r["label"]=="beneficial" for r in rows)
only_force=sum(r["force_decision"]!="unresolved" and r["dcs_decision"]=="unresolved" for r in rows)
only_dcs=sum(r["dcs_decision"]!="unresolved" and r["force_decision"]=="unresolved" for r in rows)
both=sum(r["dcs_decision"]!="unresolved" and r["force_decision"]!="unresolved" for r in rows)
finite_num=all(math.isfinite(float(r["force_num_baseline_m"])) and math.isfinite(float(r["force_num_repair_m"])) for r in rows)
ratio=(force["median_abs_z"]/dcs["median_abs_z"]) if dcs["median_abs_z"] and dcs["median_abs_z"]>0 else None

gate={
 "at_least_4_deceptive":deceptive>=4,
 "force_coverage_at_least_30pct":force["coverage"]>=0.30,
 "force_deceptive_veto_recall_at_least_50pct":force["deceptive_veto_recall_all"] is not None and force["deceptive_veto_recall_all"]>=0.50,
 "force_beneficial_false_veto_at_most_25pct":force["beneficial_false_veto_rate_all"] is not None and force["beneficial_false_veto_rate_all"]<=0.25,
 "force_coverage_exceeds_dcs":force["coverage"]>dcs["coverage"],
 "force_median_abs_z_exceeds_dcs":force["median_abs_z"]>dcs["median_abs_z"],
 "finite_numerical_comparison":finite_num,
}

out={
 "schema":"vulkax.orthogonal_force_compliance_analysis","version":1,
 "proposal_count":len(rows),"deceptive_count":deceptive,"beneficial_count":beneficial,
 "dcs":dcs,"force":force,
 "force_to_dcs_median_abs_z_ratio":ratio,
 "resolved_only_by_force":only_force,"resolved_only_by_dcs":only_dcs,"resolved_by_both":both,
 "advancement_gate":gate,
 "advancement_gate_pass":all(gate.values()),
 "decision":"orthogonal_information_helps" if all(gate.values()) else "orthogonal_information_gate_failed",
 "warning":"Fresh synthetic follow-on only; do not reinterpret frozen D2/D3/D4V or claim measured-world confirmation."
}
(root/"analysis.json").write_text(json.dumps(out,indent=2)+"\n")
print(json.dumps(out,indent=2))

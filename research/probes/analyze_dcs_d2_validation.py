#!/usr/bin/env python3
"""Frozen analysis for fresh D2 solver-native DCS validation."""
import csv,itertools,json,pathlib,statistics,sys

root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/dcs-d2-validation")
rows=list(csv.DictReader((root/"cases.csv").open()))
meta=json.loads((root/"summary.json").read_text())
if meta.get("provenance")!="synthetic-dcs-d2-validation":
    raise SystemExit("unexpected D2 validation provenance")
if meta.get("protocol")!="research/benchmarks/DCS_D2_VALIDATION_PROTOCOL.md":
    raise SystemExit("D2 validation protocol marker missing")

def f(r,k): return float(r[k])
def b(r,k): return int(r[k])!=0

truth_ids=sorted({int(r["truth_id"]) for r in rows})
if len(truth_ids)!=16:
    raise SystemExit(f"expected 16 frozen truth worlds, found {len(truth_ids)}")

methods={
  "dcs":"dcs_error_m",
  "raw_bundle_same_cost":"raw_bundle_error_m",
  "raw_maximin":"raw_error_m",
  "fisher_sensitivity":"fisher_error_m",
  "max_motion":"maxmotion_error_m",
  "random":"random_error_m",
}

result={
  "schema":"vulkax.dcs.d2_validation_analysis",
  "version":1,
  "provenance":"synthetic-dcs-d2-validation",
  "truth_count":len(truth_ids),
  "candidate_count":len(rows),
  "resolved_truth_count":0,
  "coverage":None,
  "methods":{},
  "dcs_gate":{},
  "warning":"Frozen validation result. Do not tune this protocol from these labels."
}

resolved_ids=[]
for tid in truth_ids:
    g=[r for r in rows if int(r["truth_id"])==tid]
    states={b(r,"dcs_resolved") for r in g}
    if len(states)!=1:
        raise SystemExit(f"inconsistent DCS resolution state within truth {tid}")
    if True in states:
        resolved_ids.append(tid)
result["resolved_truth_count"]=len(resolved_ids)
result["coverage"]=len(resolved_ids)/len(truth_ids)

# Raw-maximin defines the "ordinary/raw metric mirage" set for the frozen gate.
raw_mirages=[]
raw_correct_pairs=[]
for tid in truth_ids:
    g=[r for r in rows if int(r["truth_id"])==tid]
    for a,brow in itertools.combinations(g,2):
        target_gap=f(a,"target_error_m")-f(brow,"target_error_m")
        if abs(target_gap)<1e-12: continue
        target_pref="a" if target_gap<0 else "b"
        raw_pref="a" if f(a,"raw_error_m")<f(brow,"raw_error_m") else "b"
        rec=(tid,a,brow,target_pref,raw_pref)
        if raw_pref!=target_pref: raw_mirages.append(rec)
        else: raw_correct_pairs.append(rec)

for name,key in methods.items():
    pair_count=correct=0
    family_correct={}
    new_errors=0
    corrected_mirages=0
    considered_mirages=0

    ids=resolved_ids if name=="dcs" else truth_ids
    for tid in ids:
        g=[r for r in rows if int(r["truth_id"])==tid]
        for a,brow in itertools.combinations(g,2):
            target_gap=f(a,"target_error_m")-f(brow,"target_error_m")
            if abs(target_gap)<1e-12: continue
            target_pref="a" if target_gap<0 else "b"
            method_pref="a" if f(a,key)<f(brow,key) else "b"
            pair_count+=1
            correct+=method_pref==target_pref
            family=tuple(sorted((a["variant"],brow["variant"])))
            fam=family_correct.setdefault("|".join(family),[0,0])
            fam[1]+=1
            fam[0]+=method_pref==target_pref

    for tid,a,brow,target_pref,raw_pref in raw_mirages:
        if name=="dcs" and tid not in resolved_ids: continue
        considered_mirages+=1
        method_pref="a" if f(a,key)<f(brow,key) else "b"
        corrected_mirages+=method_pref==target_pref

    for tid,a,brow,target_pref,raw_pref in raw_correct_pairs:
        if name=="dcs" and tid not in resolved_ids: continue
        method_pref="a" if f(a,key)<f(brow,key) else "b"
        new_errors+=method_pref!=target_pref

    result["methods"][name]={
      "truth_worlds_considered":len(ids),
      "pair_count":pair_count,
      "target_ranking_agreement":correct/pair_count if pair_count else None,
      "mirage_pairs_considered":considered_mirages,
      "corrected_mirage_pairs":corrected_mirages,
      "correction_rate_on_raw_mirages":
          corrected_mirages/considered_mirages if considered_mirages else None,
      "new_errors_on_raw_correct_pairs":new_errors,
      "median_selector_error_m":statistics.median(f(r,key) for r in rows if int(r["truth_id"]) in ids) if ids else None,
      "family_agreement":{
          fam:(v[0]/v[1] if v[1] else None) for fam,v in family_correct.items()
      }
    }

dcs=result["methods"]["dcs"]
raw=result["methods"]["raw_maximin"]
bundle=result["methods"]["raw_bundle_same_cost"]

family_success_counts=[]
for fam,acc in dcs["family_agreement"].items():
    if acc is not None:
        family_success_counts.append((fam,acc))
dominance=False
if family_success_counts:
    # Proxy for "not carried by one family": require at least 3 pair families
    # with >=50% agreement among those actually evaluated.
    dominance=sum(acc>=0.5 for _,acc in family_success_counts)>=3

gate={
  "coverage_at_least_50pct":result["coverage"]>=0.50,
  "beats_raw_maximin":
      dcs["target_ranking_agreement"] is not None and
      raw["target_ranking_agreement"] is not None and
      dcs["target_ranking_agreement"]>raw["target_ranking_agreement"],
  "beats_same_cost_raw_bundle":
      dcs["target_ranking_agreement"] is not None and
      bundle["target_ranking_agreement"] is not None and
      dcs["target_ranking_agreement"]>bundle["target_ranking_agreement"],
  "corrects_at_least_60pct_raw_mirages":
      dcs["correction_rate_on_raw_mirages"] is not None and
      dcs["correction_rate_on_raw_mirages"]>=0.60,
  "net_positive_corrections":
      dcs["corrected_mirage_pairs"]>dcs["new_errors_on_raw_correct_pairs"],
  "not_single_family_only":dominance,
  "all_moment_contracts_assumed_from_probe":True,
}
gate["pass"]=all(gate.values())
result["dcs_gate"]=gate
result["numerical_rms_median"]=statistics.median(f(r,"numerical_rms_m") for r in rows)
result["standardized_separation_median"]=statistics.median(f(r,"dcs_maximin_separation") for r in rows)

(root/"analysis.json").write_text(json.dumps(result,indent=2)+"\n")
print("VALID frozen DCS D2 validation")
print("COVERAGE",result["coverage"],"resolved",len(resolved_ids),"/",len(truth_ids))
for name,data in result["methods"].items():
    print("METHOD",name,
          "agreement",data["target_ranking_agreement"],
          "mirage_correction",data["correction_rate_on_raw_mirages"],
          "new_errors",data["new_errors_on_raw_correct_pairs"])
print("NUMERICAL_RMS_MEDIAN",result["numerical_rms_median"])
print("STANDARDIZED_SEPARATION_MEDIAN",result["standardized_separation_median"])
print("D2_GATE",gate)
print("DECISION","advance_dcs" if gate["pass"] else "freeze_failure_and_do_not_tune_this_partition")

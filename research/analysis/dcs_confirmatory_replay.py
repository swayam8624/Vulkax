#!/usr/bin/env python3
"""Frozen-format DCS confirmatory replay runner.

This tool intentionally does NOT fit models or select stencils. It consumes a
pre-frozen signed stencil plus measured/baseline/repair response tables and emits
support/veto/unresolved using the threshold stored in the manifest.

Directory contract:
  manifest.json
  stencil.csv
  measured.csv
  baseline_nominal.csv
  baseline_refined.csv
  repair_nominal.csv
  repair_refined.csv

Response CSV columns: experiment_id,component,value
Stencil CSV columns: experiment_id,weight
"""
import argparse,csv,json,math,pathlib,tempfile

FILES=("measured","baseline_nominal","baseline_refined","repair_nominal","repair_refined")

def read_stencil(path):
    rows=list(csv.DictReader(path.open()))
    if not rows or set(rows[0])!={"experiment_id","weight"}:
        raise ValueError("stencil.csv must contain experiment_id,weight")
    out={}
    for r in rows:
        key=r["experiment_id"]
        if key in out: raise ValueError("duplicate stencil experiment_id")
        out[key]=float(r["weight"])
    if not out: raise ValueError("empty stencil")
    return out

def read_response(path):
    rows=list(csv.DictReader(path.open()))
    required={"experiment_id","component","value"}
    if not rows or not required.issubset(rows[0]):
        raise ValueError(f"{path.name} must contain experiment_id,component,value")
    out={}
    for r in rows:
        key=(r["experiment_id"],r["component"])
        if key in out: raise ValueError(f"duplicate response key in {path.name}: {key}")
        out[key]=float(r["value"])
    return out

def witness(table,stencil):
    components=sorted({c for e,c in table if e in stencil})
    experiments=set(stencil)
    if not components: raise ValueError("no response components overlap stencil")
    result=[]
    for comp in components:
        acc=0.0
        for exp,w in stencil.items():
            key=(exp,comp)
            if key not in table: raise ValueError(f"missing response {key}")
            acc+=w*table[key]
        result.append(acc)
    return components,result

def rms(a,b):
    if len(a)!=len(b) or not a: raise ValueError("witness dimensions mismatch")
    return math.sqrt(sum((x-y)**2 for x,y in zip(a,b))/len(a))

def analyze(root):
    manifest=json.loads((root/"manifest.json").read_text())
    if manifest.get("schema")!="vulkax.dcs.confirmatory_replay":
        raise ValueError("unexpected manifest schema")
    if manifest.get("version")!=1:
        raise ValueError("unsupported manifest version")
    if manifest.get("stencil_frozen_before_measurement") is not True:
        raise ValueError("confirmatory replay requires a pre-frozen stencil")
    threshold=float(manifest["decision_threshold_abs_z"])
    if not math.isfinite(threshold) or threshold<=0: raise ValueError("invalid decision threshold")
    measurement_var=float(manifest["measurement_variance_per_intervention"])
    repeat_var=float(manifest["repeat_variance_per_intervention"])
    if min(measurement_var,repeat_var)<0: raise ValueError("negative uncertainty")

    stencil=read_stencil(root/"stencil.csv")
    tables={name:read_response(root/f"{name}.csv") for name in FILES}
    keys=set(tables["measured"])
    for name,t in tables.items():
        if set(t)!=keys: raise ValueError(f"response key mismatch: {name}")

    components={}
    witnesses={}
    for name,t in tables.items():
        components[name],witnesses[name]=witness(t,stencil)
    if len({tuple(v) for v in components.values()})!=1:
        raise ValueError("component ordering mismatch")

    e_base=rms(witnesses["baseline_nominal"],witnesses["measured"])
    e_repair=rms(witnesses["repair_nominal"],witnesses["measured"])
    n_base=rms(witnesses["baseline_nominal"],witnesses["baseline_refined"])
    n_repair=rms(witnesses["repair_nominal"],witnesses["repair_refined"])
    weight_l2_sq=sum(w*w for w in stencil.values())
    obs_var=(measurement_var+repeat_var)*weight_l2_sq
    sigma=math.sqrt(max(obs_var+n_base*n_base+n_repair*n_repair,1e-30))
    z=(e_base-e_repair)/sigma
    decision="support" if z>=threshold else ("veto" if z<=-threshold else "unresolved")

    result={
      "schema":"vulkax.dcs.confirmatory_replay_result",
      "version":1,
      "proposal_id":manifest.get("proposal_id"),
      "decision":decision,
      "progress_z":z,
      "decision_threshold_abs_z":threshold,
      "baseline_witness_error":e_base,
      "repair_witness_error":e_repair,
      "baseline_numerical_witness_rms":n_base,
      "repair_numerical_witness_rms":n_repair,
      "observation_variance_after_stencil":obs_var,
      "stencil_l2_squared":weight_l2_sq,
      "component_count":len(witnesses["measured"]),
      "fit_performed":False,
      "stencil_selected_from_measurement":False,
    }
    (root/"analysis.json").write_text(json.dumps(result,indent=2)+"\n")
    return result

def self_test():
    with tempfile.TemporaryDirectory() as td:
        root=pathlib.Path(td)
        (root/"manifest.json").write_text(json.dumps({
          "schema":"vulkax.dcs.confirmatory_replay","version":1,
          "proposal_id":"self-test","decision_threshold_abs_z":2.0,
          "measurement_variance_per_intervention":1e-8,
          "repeat_variance_per_intervention":1e-8,
          "stencil_frozen_before_measurement":True})+"\n")
        with (root/"stencil.csv").open("w",newline="") as f:
            w=csv.writer(f);w.writerow(["experiment_id","weight"])
            w.writerows([["p11",.25],["p1m1",-.25],["pm11",-.25],["pm1m1",.25]])
        def write(name,vals):
            with (root/f"{name}.csv").open("w",newline="") as f:
                w=csv.writer(f);w.writerow(["experiment_id","component","value"])
                for exp,value in zip(("p11","p1m1","pm11","pm1m1"),vals):
                    w.writerow([exp,"x",value])
        write("measured",[1,-1,-1,1])
        write("baseline_nominal",[.2,-.2,-.2,.2])
        write("baseline_refined",[.201,-.201,-.201,.201])
        write("repair_nominal",[.95,-.95,-.95,.95])
        write("repair_refined",[.951,-.951,-.951,.951])
        result=analyze(root)
        if result["decision"]!="support" or result["progress_z"]<=2:
            raise RuntimeError(f"confirmatory self-test failed: {result}")
        print("VALID DCS confirmatory replay self-test",result["progress_z"])

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("root",nargs="?")
    ap.add_argument("--self-test",action="store_true")
    a=ap.parse_args()
    if a.self_test:
        self_test();return
    if not a.root: raise SystemExit("root directory required unless --self-test")
    result=analyze(pathlib.Path(a.root))
    print(json.dumps(result,indent=2))

if __name__=="__main__":
    main()

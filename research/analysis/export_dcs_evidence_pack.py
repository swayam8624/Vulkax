#!/usr/bin/env python3
"""Build a flat evidence index for DCS experiment artifacts.

Usage:
  export_dcs_evidence_pack.py OUT.csv result1.json result2.json ...

The exporter never changes scientific decisions. It records schema, provenance,
decision/gate fields, and key counts/metrics when present.
"""
import argparse,csv,json,pathlib,tempfile

FIELDS=[
 "source","schema","provenance","decision","proposal_count","deceptive_count",
 "beneficial_count","truth_count","candidate_count","trial_count",
 "ordinary_dual_overlap_wins","dcs_marker_endpoint_wins","dcs_long_endpoint_wins",
 "resolved_worlds","coverage","notes"
]

def flatten(path):
    p=pathlib.Path(path); d=json.loads(p.read_text())
    row={k:"" for k in FIELDS}; row["source"]=str(p)
    row["schema"]=d.get("schema",""); row["provenance"]=d.get("provenance","")
    row["decision"]=d.get("decision","")
    for k in ("proposal_count","deceptive_count","beneficial_count","truth_count",
              "candidate_count","trial_count","ordinary_dual_overlap_wins",
              "dcs_marker_endpoint_wins","dcs_long_endpoint_wins"):
        if k in d: row[k]=d[k]
    mr=d.get("mechanism_resolution",{})
    if "resolved_truths" in mr: row["resolved_worlds"]=mr["resolved_truths"]
    methods=d.get("methods",{})
    if "dcs" in methods and isinstance(methods["dcs"],dict):
        if "coverage" in methods["dcs"]: row["coverage"]=methods["dcs"]["coverage"]
    gate=d.get("advancement_gate") or d.get("frozen_gate")
    if isinstance(gate,dict):
        row["notes"]="gate_pass="+str(gate.get("pass"))
    return row

def export(out,inputs):
    rows=[flatten(p) for p in inputs]
    with pathlib.Path(out).open("w",newline="") as f:
        w=csv.DictWriter(f,fieldnames=FIELDS);w.writeheader();w.writerows(rows)
    return len(rows)

def self_test():
    with tempfile.TemporaryDirectory() as td:
        root=pathlib.Path(td)
        j=root/"x.json"; j.write_text(json.dumps({
          "schema":"vulkax.dcs.test","provenance":"self-test","truth_count":3,
          "methods":{"dcs":{"coverage":0.5}},"advancement_gate":{"pass":False}}))
        out=root/"evidence.csv"
        n=export(out,[j])
        txt=out.read_text()
        if n!=1 or "vulkax.dcs.test" not in txt or "gate_pass=False" not in txt:
            raise RuntimeError("evidence exporter self-test failed")
        print("VALID DCS evidence-pack exporter self-test",n)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("out",nargs="?")
    ap.add_argument("inputs",nargs="*")
    ap.add_argument("--self-test",action="store_true")
    a=ap.parse_args()
    if a.self_test: self_test(); return
    if not a.out or not a.inputs: raise SystemExit("out and at least one input required")
    print("WROTE",a.out,"rows",export(a.out,a.inputs))

if __name__=="__main__": main()

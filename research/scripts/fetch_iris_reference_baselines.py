#!/usr/bin/env python3
"""Fetch pinned official IRIS published parameter-recovery reference tables.

These are contextual baselines from the upstream benchmark, not verification
baselines on the Vulkax three-way task. The distinction is written into outputs.
"""
from __future__ import annotations
import argparse,csv,hashlib,json,pathlib,urllib.request

COMMIT="0688a57e0e8b5a7306d67c5e22d999bec7566a70"
BASE="https://raw.githubusercontent.com/KurbanIntelligenceLab/iris-bench/"+COMMIT+"/"
FILES={
 "iris_baseline":"Results/iris_comparison/parameter_errors_baseline.csv",
 "iris_unified":"Results/iris_comparison/parameter_errors_unified.csv",
 "iris_unified_multistep":"Results/iris_comparison/parameter_errors_unified_multistep.csv",
}
def sha(p):
 h=hashlib.sha256();h.update(p.read_bytes());return h.hexdigest()
def rows(p):
 with p.open(newline="",encoding="utf-8") as f:return list(csv.DictReader(f))
def main():
 ap=argparse.ArgumentParser()
 ap.add_argument("--out",type=pathlib.Path,default=pathlib.Path("build/publication-validation/iris-official-reference"))
 ap.add_argument("--self-test",action="store_true")
 a=ap.parse_args()
 if a.self_test:
  assert COMMIT and all(v.endswith(".csv") for v in FILES.values())
  print("VALID IRIS official reference fetcher self-test");return
 a.out.mkdir(parents=True,exist_ok=True)
 downloaded=[]
 for name,rel in FILES.items():
  dst=a.out/f"{name}.csv"
  urllib.request.urlretrieve(BASE+rel,dst)
  downloaded.append({"name":name,"upstream_path":rel,"sha256":sha(dst),"bytes":dst.stat().st_size})
 selected=[]
 for item in downloaded:
  for r in rows(a.out/f"{item['name']}.csv"):
   if (r["dynamics"],r["setting"],r["param"]) in {
      ("pendulum","pendulum_90","rope_length"),
      ("dropping_ball","drop_150","g"),
      ("pendulum","pendulum_45","rope_length"),
      ("dropping_ball","drop_100","g")}:
    selected.append({"source":item["name"],**r})
 with (a.out/"selected_reference_rows.csv").open("w",newline="",encoding="utf-8") as f:
  w=csv.DictWriter(f,fieldnames=list(selected[0]));w.writeheader();w.writerows(selected)
 report={"schema":"vulkax.iris_official_reference","version":1,"upstream_commit":COMMIT,
  "files":downloaded,"selected_rows":selected,
  "claim_guard":"Upstream IRIS tables measure parameter-recovery error, not Vulkax SUPPORT/VETO/UNRESOLVED verification. Do not compare these numbers as if the tasks were identical."}
 (a.out/"manifest.json").write_text(json.dumps(report,indent=2)+"\n")
 print("VALID pinned IRIS official reference tables")
 for r in selected:print(r)
 print("OUT",a.out)
if __name__=="__main__":main()

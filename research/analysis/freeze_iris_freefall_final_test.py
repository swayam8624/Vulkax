#!/usr/bin/env python3
"""Create/check lock for the blind IRIS free-fall final domain."""
from __future__ import annotations
import argparse,csv,importlib.metadata,json,pathlib,subprocess,sys

EXPECTED_REV="c253822f55431ca80ef2084de4bc5e79d1a488f1"
RUNTIME=pathlib.Path("build/publication-validation/iris-freefall-runtime.json")
REPO_FILES=[
 pathlib.Path("research/validation/protocol_v1.json"),
 pathlib.Path("research/validation/record_schema_v1.json"),
 pathlib.Path("research/validation/iris_freefall_blind_v1.json"),
 pathlib.Path("research/validation/iris_freefall_final_world_manifest_v1.csv"),
 pathlib.Path("research/benchmarks/IRIS_FREEFALL_BLIND_REPLICATION_PROTOCOL_2026-09-23.md"),
 pathlib.Path("research/scripts/fetch_iris_freefall_blind.py"),
 pathlib.Path("research/analysis/prepare_public_validation_datasets.py"),
 pathlib.Path("research/analysis/adapt_public_validation_inputs.py"),
 pathlib.Path("research/analysis/run_iris_freefall_validation.py"),
 pathlib.Path("research/analysis/freeze_iris_freefall_final_test.py"),
 pathlib.Path("research/scripts/run_iris_freefall_blind_validation.sh"),
 pathlib.Path("research/scripts/run_iris_freefall_final_test.sh"),
]
VAL_FILES=[
 pathlib.Path("build/publication-validation/iris-freefall-validation/summary.json"),
 pathlib.Path("build/publication-validation/iris-freefall-validation/validation_records.csv"),
 pathlib.Path("build/publication-validation/iris-freefall-validation/take_summary.csv"),
 RUNTIME,
]
def runtime():
    return {"python":sys.version.split()[0],"numpy":importlib.metadata.version("numpy"),
            "opencv_python_headless":importlib.metadata.version("opencv-python-headless"),
            "huggingface_hub":importlib.metadata.version("huggingface_hub")}
def write_runtime():
    RUNTIME.parent.mkdir(parents=True,exist_ok=True);RUNTIME.write_text(json.dumps({"schema":"vulkax.freefall_runtime","version":1,"packages":runtime()},indent=2,sort_keys=True)+"\n")
def check_runtime():
    d=json.loads(RUNTIME.read_text())
    if d.get("packages")!=runtime():raise RuntimeError(f"runtime mismatch frozen={d.get('packages')} current={runtime()}")
def validate_gate():
    p=VAL_FILES[0]
    if not p.is_file():raise FileNotFoundError(p)
    s=json.loads(p.read_text())
    if s.get("split")!="validation" or not s.get("gate_pass"):raise RuntimeError("free-fall validation gate did not pass")
    if s.get("take01_forbidden") is not True:raise RuntimeError("take01 leakage guard absent")
def check_manifest():
    cfg=json.loads(REPO_FILES[2].read_text());assert cfg["dataset"]["revision"]==EXPECTED_REV
    with REPO_FILES[3].open(newline="",encoding="utf-8") as f:q=list(csv.DictReader(f))
    want=[f"dropping_ball/drop_150/{i:02d}" for i in range(2,11)]
    if [r["scene"] for r in q]!=want:raise RuntimeError("final free-fall world manifest mismatch")
    if any("/01" in r["scene"] for r in q):raise RuntimeError("take01 leaked into final manifest")
def create(out,require_clean):
    validate_gate();check_manifest();write_runtime()
    miss=[str(p) for p in REPO_FILES+VAL_FILES if not p.is_file()]
    if miss:raise RuntimeError("missing freeze inputs:\n"+"\n".join(miss))
    cmd=[sys.executable,"research/analysis/freeze_publication_validation.py","--protocol",str(REPO_FILES[0]),"--schema",str(REPO_FILES[1]),"--out",str(out)]
    for p in REPO_FILES[2:]+VAL_FILES:cmd+=["--extra",str(p)]
    if require_clean:cmd.append("--require-clean")
    subprocess.run(cmd,check=True)
    d=json.loads(out.read_text());d["purpose"]="IRIS free-fall blind final test";d["take01_forbidden"]=True;d["dataset_revision"]=EXPECTED_REV
    out.write_text(json.dumps(d,indent=2,sort_keys=True)+"\n")
    check(out)
def check(out):
    d=json.loads(out.read_text())
    if d.get("purpose")!="IRIS free-fall blind final test" or not d.get("take01_forbidden"):raise RuntimeError("not a valid free-fall blind lock")
    validate_gate();check_manifest();check_runtime()
    subprocess.run([sys.executable,"research/analysis/freeze_publication_validation.py","--check",str(out)],check=True)
    locked={x["path"] for x in d["files"]};need={str(p) for p in REPO_FILES+VAL_FILES}
    if need-locked:raise RuntimeError("lock coverage missing: "+",".join(sorted(need-locked)))
    print("VALID IRIS free-fall blind final lock",out)
def main():
    ap=argparse.ArgumentParser();ap.add_argument("--out",type=pathlib.Path,default=pathlib.Path("build/publication-validation/iris-freefall-final-lock.json"))
    ap.add_argument("--check",type=pathlib.Path);ap.add_argument("--require-clean",action="store_true");ap.add_argument("--self-test",action="store_true");a=ap.parse_args()
    if a.self_test:
        check_manifest();print("VALID free-fall final-freeze self-test");return
    if a.check:check(a.check);return
    create(a.out,a.require_clean);print("VALID free-fall final freeze",a.out)
if __name__=="__main__":main()

#!/usr/bin/env python3
"""Create/check the IRIS free-fall V6 final lock after fresh validation."""
from __future__ import annotations
import argparse,csv,importlib.metadata,json,pathlib,subprocess,sys

EXPECTED_REV="c253822f55431ca80ef2084de4bc5e79d1a488f1"
CONFIG=pathlib.Path("research/validation/iris_freefall_rescue_v6.json")
WORLD=pathlib.Path("research/validation/iris_freefall_final_world_manifest_v1.csv")
PROTOCOL=pathlib.Path("research/benchmarks/IRIS_FREEFALL_V6_FRESH_RESCUE_PROTOCOL_2026-09-23.md")
RUNTIME=pathlib.Path("build/publication-validation/iris-freefall-v6-runtime.json")
VAL_DIR=pathlib.Path("build/publication-validation/iris-freefall-v6-validation")

REPO_FILES=[
 pathlib.Path("research/validation/protocol_v1.json"),
 pathlib.Path("research/validation/record_schema_v1.json"),
 CONFIG,
 WORLD,
 PROTOCOL,
 pathlib.Path("research/scripts/fetch_iris_freefall_rescue_v6.py"),
 pathlib.Path("research/scripts/fetch_iris_freefall_v6_final.py"),
 pathlib.Path("research/analysis/prepare_public_validation_datasets.py"),
 pathlib.Path("research/analysis/adapt_public_validation_inputs.py"),
 pathlib.Path("research/analysis/run_iris_freefall_validation.py"),
 pathlib.Path("research/analysis/freeze_iris_freefall_v6_final.py"),
 pathlib.Path("research/scripts/run_iris_freefall_rescue_v6.sh"),
 pathlib.Path("research/scripts/run_iris_freefall_v6_final_test.sh"),
]
VAL_FILES=[
 VAL_DIR/"summary.json",
 VAL_DIR/"validation_records.csv",
 VAL_DIR/"take_summary.csv",
 RUNTIME,
]

def runtime():
    return {
      "python":sys.version.split()[0],
      "numpy":importlib.metadata.version("numpy"),
      "opencv_python_headless":importlib.metadata.version("opencv-python-headless"),
      "huggingface_hub":importlib.metadata.version("huggingface_hub"),
    }

def write_runtime():
    RUNTIME.parent.mkdir(parents=True,exist_ok=True)
    RUNTIME.write_text(json.dumps({
      "schema":"vulkax.freefall_v6_runtime","version":1,"packages":runtime()
    },indent=2,sort_keys=True)+"\n")

def check_runtime():
    d=json.loads(RUNTIME.read_text())
    if d.get("packages")!=runtime():
        raise RuntimeError(f"runtime mismatch frozen={d.get('packages')} current={runtime()}")

def validate_gate():
    p=VAL_FILES[0]
    if not p.is_file():
        raise FileNotFoundError(p)
    s=json.loads(p.read_text())
    if s.get("split")!="validation" or not s.get("gate_pass"):
        raise RuntimeError("V6 fresh validation gate did not pass")
    if s.get("tracker_revision")!="ball_identity_v6":
        raise RuntimeError("V6 tracker revision mismatch")
    if int(s.get("version",0))!=6:
        raise RuntimeError("V6 summary version mismatch")
    if s.get("take01_forbidden") is not True:
        raise RuntimeError("take01 leakage guard absent")

def check_config_manifest():
    cfg=json.loads(CONFIG.read_text())
    if int(cfg.get("version",0))!=6:
        raise RuntimeError("not V6 config")
    if cfg["dataset"]["revision"]!=EXPECTED_REV:
        raise RuntimeError("dataset revision drift")
    failed=set(cfg["dataset"]["prior_failed_validation"]["takes"])
    fresh=set(cfg["dataset"]["validation"]["takes"])
    if failed & fresh:
        raise RuntimeError("fresh validation overlaps failed V5 validation")
    with WORLD.open(newline="",encoding="utf-8") as f:
        q=list(csv.DictReader(f))
    want=[f"dropping_ball/drop_150/{i:02d}" for i in range(2,11)]
    if [r["scene"] for r in q]!=want:
        raise RuntimeError("V6 final world manifest mismatch")
    if any("/01" in r["scene"] for r in q):
        raise RuntimeError("take01 leaked into V6 final manifest")

def create(out,require_clean):
    validate_gate();check_config_manifest();write_runtime()
    miss=[str(p) for p in REPO_FILES+VAL_FILES if not p.is_file()]
    if miss:
        raise RuntimeError("missing V6 freeze inputs:\n"+"\n".join(miss))
    cmd=[
      sys.executable,"research/analysis/freeze_publication_validation.py",
      "--protocol",str(REPO_FILES[0]),
      "--schema",str(REPO_FILES[1]),
      "--out",str(out),
    ]
    for p in REPO_FILES[2:]+VAL_FILES:
        cmd+=["--extra",str(p)]
    if require_clean:
        cmd.append("--require-clean")
    subprocess.run(cmd,check=True)
    d=json.loads(out.read_text())
    d["purpose"]="IRIS free-fall V6 fresh-split final test"
    d["take01_forbidden"]=True
    d["dataset_revision"]=EXPECTED_REV
    d["failed_v5_validation_reused"]=False
    out.write_text(json.dumps(d,indent=2,sort_keys=True)+"\n")
    check(out)

def check(out):
    d=json.loads(out.read_text())
    if d.get("purpose")!="IRIS free-fall V6 fresh-split final test":
        raise RuntimeError("not a V6 final lock")
    if not d.get("take01_forbidden") or d.get("failed_v5_validation_reused") is not False:
        raise RuntimeError("V6 lock provenance guard failed")
    validate_gate();check_config_manifest();check_runtime()
    subprocess.run([
      sys.executable,"research/analysis/freeze_publication_validation.py",
      "--check",str(out)
    ],check=True)
    locked={x["path"] for x in d["files"]}
    need={str(p) for p in REPO_FILES+VAL_FILES}
    if need-locked:
        raise RuntimeError("V6 lock coverage missing: "+",".join(sorted(need-locked)))
    print("VALID IRIS free-fall V6 final lock",out)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--out",type=pathlib.Path,
                    default=pathlib.Path("build/publication-validation/iris-freefall-v6-final-lock.json"))
    ap.add_argument("--check",type=pathlib.Path)
    ap.add_argument("--require-clean",action="store_true")
    ap.add_argument("--self-test",action="store_true")
    a=ap.parse_args()
    if a.self_test:
        check_config_manifest()
        print("VALID V6 final-freeze self-test")
        return
    if a.check:
        check(a.check);return
    create(a.out,a.require_clean)
    print("VALID V6 final freeze",a.out)

if __name__=="__main__":
    main()

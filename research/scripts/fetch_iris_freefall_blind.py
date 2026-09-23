#!/usr/bin/env python3
"""Download only the frozen IRIS free-fall blind-replication takes."""
from __future__ import annotations
import argparse,hashlib,json,subprocess
from datetime import datetime,timezone
from pathlib import Path

REPO="rasulkhanbayov/IRIS"
REV="c253822f55431ca80ef2084de4bc5e79d1a488f1"
DEV={"drop_50":["02","03","04","05"],"drop_100":["02","03","04","05"]}
FINAL={"drop_150":["02","03","04","05","06","07","08","09","10"]}

def hf():
    try:from huggingface_hub import snapshot_download,HfApi
    except ImportError as e:raise SystemExit("huggingface_hub required") from e
    return snapshot_download,HfApi
def sha(p):
    h=hashlib.sha256()
    with p.open("rb") as f:
        for q in iter(lambda:f.read(1<<20),b""):h.update(q)
    return h.hexdigest()
def manifest(root,phase,patterns):
    files=sorted(p for p in root.rglob("*") if p.is_file() and ".cache/huggingface" not in p.as_posix() and p.name!="vulkax_download_manifest.json")
    data={"schema":"vulkax.public_dataset_download","version":1,"created_utc":datetime.now(timezone.utc).isoformat(),
      "dataset":"iris","repo_id":REPO,"revision":REV,"license":"CC-BY-NC-4.0",
      "citation_url":"https://huggingface.co/datasets/rasulkhanbayov/IRIS",
      "profile":f"core+freefall_blind_{phase}","requested_patterns":patterns,
      "file_count":len(files),"total_bytes":sum(p.stat().st_size for p in files),
      "files":[{"path":p.relative_to(root).as_posix(),"bytes":p.stat().st_size,"sha256":sha(p)} for p in files]}
    (root/"vulkax_download_manifest.json").write_text(json.dumps(data,indent=2,sort_keys=True)+"\n")
    return data

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--data-root",type=Path,default=Path("build/public-datasets"))
    ap.add_argument("--phase",choices=("development_validation","final_test"),default="development_validation")
    ap.add_argument("--lock",type=Path)
    ap.add_argument("--self-test",action="store_true")
    a=ap.parse_args()
    if a.self_test:
        assert "01" not in sum(DEV.values(),[]) and "01" not in sum(FINAL.values(),[])
        print("VALID free-fall blind downloader self-test");return
    if a.phase=="final_test":
        if not a.lock:raise SystemExit("final_test download requires --lock")
        subprocess.run(["python3","research/analysis/freeze_iris_freefall_final_test.py","--check",str(a.lock)],check=True)
        selection=FINAL
    else:selection=DEV
    snapshot_download,HfApi=hf()
    info=HfApi().dataset_info(REPO)
    if str(info.sha)!=REV:raise SystemExit(f"IRIS revision drift: expected {REV}, got {info.sha}")
    patterns=["README.md","parameters.json"]
    for setting,takes in selection.items():
        for take in takes:patterns.append(f"Dropping_ball/{setting}/{take}.mp4")
    if any("/01.mp4" in p for p in patterns):raise RuntimeError("take01 leakage")
    root=a.data_root/"iris";root.mkdir(parents=True,exist_ok=True)
    print("[freefall] phase",a.phase,"revision",REV)
    snapshot_download(repo_id=REPO,repo_type="dataset",revision=REV,local_dir=str(root),allow_patterns=patterns)
    expected=[root/"Dropping_ball"/s/f"{t}.mp4" for s,ts in selection.items() for t in ts]
    miss=[str(p) for p in expected if not p.is_file()]
    if miss:raise SystemExit(f"incomplete free-fall download: {len(miss)} missing")
    m=manifest(root,a.phase,patterns)
    print("VALID IRIS free-fall blind download",len(expected),"phase files")
    print("TOTAL_MANIFEST_FILES",m["file_count"])
if __name__=="__main__":main()

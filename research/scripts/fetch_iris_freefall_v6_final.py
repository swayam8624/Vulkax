#!/usr/bin/env python3
"""Download the V6 final IRIS free-fall population only after a valid V6 lock."""
from __future__ import annotations
import argparse,hashlib,json,subprocess
from datetime import datetime,timezone
from pathlib import Path

REPO="rasulkhanbayov/IRIS"
REV="c253822f55431ca80ef2084de4bc5e79d1a488f1"

def hf():
    try:
        from huggingface_hub import snapshot_download,HfApi
    except ImportError as e:
        raise SystemExit("huggingface_hub required") from e
    return snapshot_download,HfApi

def sha256(path):
    h=hashlib.sha256()
    with path.open("rb") as f:
        for q in iter(lambda:f.read(1<<20),b""):
            h.update(q)
    return h.hexdigest()

def write_manifest(root,patterns,takes):
    files=sorted(
        p for p in root.rglob("*")
        if p.is_file()
        and ".cache/huggingface" not in p.as_posix()
        and p.name!="vulkax_download_manifest.json"
    )
    data={
      "schema":"vulkax.public_dataset_download",
      "version":2,
      "created_utc":datetime.now(timezone.utc).isoformat(),
      "dataset":"iris",
      "repo_id":REPO,
      "revision":REV,
      "license":"CC-BY-NC-4.0",
      "citation_url":"https://huggingface.co/datasets/rasulkhanbayov/IRIS",
      "profile":"freefall_rescue_v6_final_test",
      "selection":{"setting":"drop_150","takes":takes},
      "requested_patterns":patterns,
      "file_count":len(files),
      "total_bytes":sum(p.stat().st_size for p in files),
      "files":[
        {"path":p.relative_to(root).as_posix(),
         "bytes":p.stat().st_size,
         "sha256":sha256(p)}
        for p in files
      ],
    }
    (root/"vulkax_download_manifest.json").write_text(
        json.dumps(data,indent=2,sort_keys=True)+"\n"
    )
    return data

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--data-root",type=Path,default=Path("build/public-datasets"))
    ap.add_argument("--config",type=Path,default=Path("research/validation/iris_freefall_rescue_v6.json"))
    ap.add_argument("--lock",type=Path,required=True)
    ap.add_argument("--self-test",action="store_true")
    a=ap.parse_args()

    cfg=json.loads(a.config.read_text())
    spec=cfg["dataset"]["final_test"]
    setting=str(spec["setting"])
    takes=[str(x) for x in spec["takes"]]
    if int(cfg.get("version",0))!=6 or "01" in takes:
        raise RuntimeError("invalid V6 final config")

    if a.self_test:
        assert setting=="drop_150"
        assert takes==[f"{i:02d}" for i in range(2,11)]
        required=("schema","dataset","repo_id","revision","license","citation_url")
        probe={
          "schema":"vulkax.public_dataset_download",
          "dataset":"iris","repo_id":REPO,"revision":REV,
          "license":"CC-BY-NC-4.0",
          "citation_url":"https://huggingface.co/datasets/rasulkhanbayov/IRIS",
        }
        assert all(probe.get(k) for k in required)
        print("VALID V6 final downloader self-test")
        return

    subprocess.run([
      "python3","research/analysis/freeze_iris_freefall_v6_final.py",
      "--check",str(a.lock)
    ],check=True)

    snapshot_download,HfApi=hf()
    info=HfApi().dataset_info(REPO)
    if str(info.sha)!=REV:
        raise SystemExit(f"IRIS revision drift: expected {REV}, got {info.sha}")

    patterns=["README.md","parameters.json"]+[
      f"Dropping_ball/{setting}/{t}.mp4" for t in takes
    ]
    root=a.data_root/"iris"
    root.mkdir(parents=True,exist_ok=True)
    print("[freefall-v6-final] lock valid; downloading",setting,"takes",",".join(takes))
    snapshot_download(
      repo_id=REPO,repo_type="dataset",revision=REV,
      local_dir=str(root),allow_patterns=patterns
    )
    expected=[root/"Dropping_ball"/setting/f"{t}.mp4" for t in takes]
    miss=[str(p) for p in expected if not p.is_file()]
    if miss:
        raise SystemExit(f"incomplete V6 final download: {len(miss)} missing")
    m=write_manifest(root,patterns,takes)
    print("VALID IRIS free-fall V6 final download",len(expected),"videos")
    print("TOTAL_MANIFEST_FILES",m["file_count"])

if __name__=="__main__":
    main()

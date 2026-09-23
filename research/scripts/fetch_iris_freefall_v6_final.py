#!/usr/bin/env python3
"""Download the V6 final IRIS free-fall population only after a valid V6 lock."""
from __future__ import annotations
import argparse,json,subprocess
from pathlib import Path

REPO="rasulkhanbayov/IRIS"
REV="c253822f55431ca80ef2084de4bc5e79d1a488f1"

def hf():
    try:
        from huggingface_hub import snapshot_download,HfApi
    except ImportError as e:
        raise SystemExit("huggingface_hub required") from e
    return snapshot_download,HfApi

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
    print("VALID IRIS free-fall V6 final download",len(expected),"videos")

if __name__=="__main__":
    main()

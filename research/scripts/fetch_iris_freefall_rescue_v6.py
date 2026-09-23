#!/usr/bin/env python3
"""Download only the fresh V6 IRIS free-fall rescue development/validation takes.

This downloader intentionally has no final-test mode. The locked final population
remains behind the separate final-lock workflow until V6 validation succeeds.
"""
from __future__ import annotations
import argparse,hashlib,json
from datetime import datetime,timezone
from pathlib import Path

REPO="rasulkhanbayov/IRIS"
REV="c253822f55431ca80ef2084de4bc5e79d1a488f1"
DEFAULT_CONFIG=Path("research/validation/iris_freefall_rescue_v6.json")

def hf():
    try:
        from huggingface_hub import snapshot_download,HfApi
    except ImportError as e:
        raise SystemExit("huggingface_hub required") from e
    return snapshot_download,HfApi

def sha(p):
    h=hashlib.sha256()
    with p.open("rb") as f:
        for q in iter(lambda:f.read(1<<20),b""):h.update(q)
    return h.hexdigest()

def manifest(root,phase,patterns,selection):
    files=sorted(
        p for p in root.rglob("*")
        if p.is_file() and ".cache/huggingface" not in p.as_posix()
        and p.name!="vulkax_download_manifest.json"
    )
    data={
      "schema":"vulkax.public_dataset_download","version":2,
      "created_utc":datetime.now(timezone.utc).isoformat(),
      "dataset":"iris","repo_id":REPO,"revision":REV,"license":"CC-BY-NC-4.0",
      "citation_url":"https://huggingface.co/datasets/rasulkhanbayov/IRIS",
      "profile":f"freefall_rescue_v6_{phase}",
      "selection":selection,
      "requested_patterns":patterns,
      "file_count":len(files),"total_bytes":sum(p.stat().st_size for p in files),
      "files":[
        {"path":p.relative_to(root).as_posix(),"bytes":p.stat().st_size,"sha256":sha(p)}
        for p in files
      ]
    }
    (root/"vulkax_download_manifest.json").write_text(
        json.dumps(data,indent=2,sort_keys=True)+"\n"
    )
    return data

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--data-root",type=Path,default=Path("build/public-datasets"))
    ap.add_argument("--config",type=Path,default=DEFAULT_CONFIG)
    ap.add_argument("--phase",choices=("development","validation"),default="development")
    ap.add_argument("--self-test",action="store_true")
    a=ap.parse_args()

    cfg=json.loads(a.config.read_text())
    if int(cfg.get("version",0))!=6:
        raise SystemExit("V6 downloader requires version-6 rescue config")
    spec=cfg["dataset"][a.phase]
    setting=str(spec["setting"])
    takes=[str(x) for x in spec["takes"]]

    if a.self_test:
        assert takes
        assert "01" not in takes
        old=set(cfg["dataset"]["prior_failed_validation"]["takes"])
        if a.phase=="validation":
            assert not (old & set(takes))
        final=set(cfg["dataset"]["final_test"]["takes"])
        assert "01" not in final
        probe={
          "schema":"vulkax.public_dataset_download",
          "dataset":"iris",
          "repo_id":REPO,
          "revision":REV,
          "license":"CC-BY-NC-4.0",
          "citation_url":"https://huggingface.co/datasets/rasulkhanbayov/IRIS",
        }
        for key in ("schema","dataset","repo_id","revision","license","citation_url"):
            assert probe.get(key), key
        print("VALID free-fall V6 rescue downloader self-test")
        return

    if "01" in takes:
        raise RuntimeError("take01 leakage")
    if a.phase=="validation":
        old=set(cfg["dataset"]["prior_failed_validation"]["takes"])
        overlap=old & set(takes)
        if overlap:
            raise RuntimeError(f"fresh validation overlaps failed V5 validation: {sorted(overlap)}")

    snapshot_download,HfApi=hf()
    info=HfApi().dataset_info(REPO)
    if str(info.sha)!=REV:
        raise SystemExit(f"IRIS revision drift: expected {REV}, got {info.sha}")

    patterns=["README.md","parameters.json"] + [
        f"Dropping_ball/{setting}/{take}.mp4" for take in takes
    ]
    root=a.data_root/"iris"
    root.mkdir(parents=True,exist_ok=True)
    print("[freefall-v6] phase",a.phase,"revision",REV,
          "setting",setting,"takes",",".join(takes))
    snapshot_download(
        repo_id=REPO,repo_type="dataset",revision=REV,
        local_dir=str(root),allow_patterns=patterns
    )
    expected=[root/"Dropping_ball"/setting/f"{t}.mp4" for t in takes]
    miss=[str(p) for p in expected if not p.is_file()]
    if miss:
        raise SystemExit(f"incomplete V6 free-fall download: {len(miss)} missing")
    m=manifest(root,a.phase,patterns,{"setting":setting,"takes":takes})
    print("VALID IRIS free-fall V6 rescue download",len(expected),"phase files")
    print("TOTAL_MANIFEST_FILES",m["file_count"])

if __name__=="__main__":
    main()

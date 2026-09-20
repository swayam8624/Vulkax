#!/usr/bin/env python3
import argparse,hashlib,json,pathlib,time,urllib.parse,urllib.request

BASE="https://huggingface.co/datasets/InternRobotics/GAUGE-Dataset/resolve/main"
ASSETS={
  "foam_obj":"assets/obj/foam.obj",
  "foam_mjcf":"assets/mjcf/foam.xml",
  "foam_shearing_usd":"assets/usd/foam shearing.usd",
}
EXPECTED_SHA256={
  "foam_shearing_usd":"064df7954b43ec5aaba34678762479558d7eb3af0a3ff85aa500105e85a83ac5",
}

def sha256(path):
    h=hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda:f.read(1<<20),b""): h.update(chunk)
    return h.hexdigest()

def download(rel,path):
    url=BASE+"/"+urllib.parse.quote(rel,safe="/")
    last=None
    for attempt in range(4):
        try:
            req=urllib.request.Request(url,headers={"User-Agent":"Vulkax-research-probe/1"})
            with urllib.request.urlopen(req,timeout=120) as r, path.open("wb") as f:
                while True:
                    b=r.read(1<<20)
                    if not b: break
                    f.write(b)
            if path.stat().st_size==0: raise RuntimeError("empty download")
            return url
        except Exception as e:
            last=e
            if path.exists(): path.unlink()
            time.sleep(1+attempt)
    raise RuntimeError(f"download failed {rel}: {last}")

def main():
    p=argparse.ArgumentParser();p.add_argument("out");a=p.parse_args()
    out=pathlib.Path(a.out);out.mkdir(parents=True,exist_ok=True)
    records={}
    for key,rel in ASSETS.items():
        target=out/pathlib.Path(rel).name
        url=download(rel,target);digest=sha256(target)
        expected=EXPECTED_SHA256.get(key)
        if expected and digest!=expected:
            raise SystemExit(f"hash mismatch for {key}: {digest} != {expected}")
        records[key]={"repo_path":rel,"resolved_url":url,"local_name":target.name,
                      "bytes":target.stat().st_size,"sha256":digest,"expected_sha256":expected}
        print("ASSET",key,target.stat().st_size,digest)
    manifest={"schema":"vulkax.gauge_sim_assets","version":1,
      "provenance":"released-public-benchmark-assets","repository":"InternRobotics/GAUGE-Dataset",
      "revision_request":"main","assets":records,
      "warning":"Raw assets are ephemeral CI inputs. Derived geometry/provenance only should be uploaded as Vulkax evidence."}
    (out/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    print("VALID GAUGE simulation assets",len(records))

if __name__=="__main__": main()

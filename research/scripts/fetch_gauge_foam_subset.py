#!/usr/bin/env python3
import json,pathlib,sys,time,urllib.parse,urllib.request
root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/gauge-download")
(root/"metadata").mkdir(parents=True,exist_ok=True)
base="https://huggingface.co/datasets/InternRobotics/GAUGE-Dataset/raw/main"
tasks=(("foam stretching","foam stretching"),("foam compression","foam compressing"),("foam shearing","foam shearing"))
materials=("soft","hard")
def get(url,path):
    path.parent.mkdir(parents=True,exist_ok=True)
    last=None
    for attempt in range(4):
        try:
            req=urllib.request.Request(url,headers={"User-Agent":"Vulkax-research-probe/1"})
            with urllib.request.urlopen(req,timeout=60) as r:
                path.write_bytes(r.read())
            return
        except Exception as e:
            last=e
            time.sleep(1+attempt)
    raise RuntimeError(f"download failed {url}: {last}")
for data_task,metadata_task in tasks:
    data_enc=urllib.parse.quote(data_task,safe="")
    meta_enc=urllib.parse.quote(metadata_task,safe="")
    get(f"{base}/metadata/deformable/{meta_enc}.json",root/"metadata"/f"{data_task}.json")
    for material in materials:
        for trial in range(1,11):
            get(f"{base}/data/deformable/{data_enc}/json/{material}/{trial}.json",
                root/"data"/data_task/material/f"{trial}.json")
files=list((root/"data").glob("*/*/*.json"))
if len(files)!=60: raise SystemExit(f"expected 60 GAUGE trials, got {len(files)}")
for p in files:
    d=json.loads(p.read_text())
    if d.get("FPS")!=30 or d.get("translation unit")!="mm" or not d.get("foam"):
        raise SystemExit(f"unexpected GAUGE schema: {p}")
print("VALID downloaded GAUGE foam subset",len(files),"trials")

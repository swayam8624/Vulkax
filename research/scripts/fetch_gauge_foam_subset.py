#!/usr/bin/env python3
import json,pathlib,sys,time,urllib.parse,urllib.request
root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/gauge-download")
(root/"metadata").mkdir(parents=True,exist_ok=True)
base="https://huggingface.co/datasets/InternRobotics/GAUGE-Dataset/raw/main"
tasks=("foam stretching","foam compressing","foam shearing")
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
for task in tasks:
    enc=urllib.parse.quote(task,safe="")
    get(f"{base}/metadata/deformable/{enc}.json",root/"metadata"/f"{task}.json")
    for material in materials:
        for trial in range(1,11):
            get(f"{base}/data/deformable/{enc}/json/{material}/{trial}.json",
                root/"data"/task/material/f"{trial}.json")
files=list((root/"data").glob("*/*/*.json"))
if len(files)!=60: raise SystemExit(f"expected 60 GAUGE trials, got {len(files)}")
for p in files:
    d=json.loads(p.read_text())
    if d.get("FPS")!=30 or d.get("translation unit")!="mm" or not d.get("foam"):
        raise SystemExit(f"unexpected GAUGE schema: {p}")
print("VALID downloaded GAUGE foam subset",len(files),"trials")

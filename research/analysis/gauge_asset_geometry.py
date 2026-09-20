#!/usr/bin/env python3
"""Audit the published GAUGE foam mesh geometry without using trajectory error."""
import argparse,hashlib,json,math,pathlib

def sha256(path):
    h=hashlib.sha256()
    with open(path,"rb") as f:
        for chunk in iter(lambda:f.read(1<<20),b""): h.update(chunk)
    return h.hexdigest()

def bbox_obj(path):
    lo=[math.inf]*3; hi=[-math.inf]*3; count=0
    with open(path,"r",errors="strict") as f:
        for line in f:
            if not line.startswith("v "): continue
            p=line.split()
            if len(p)<4: raise ValueError("malformed OBJ vertex")
            xyz=[float(p[i]) for i in range(1,4)]
            if not all(math.isfinite(v) for v in xyz): raise ValueError("non-finite OBJ vertex")
            for a,v in enumerate(xyz):
                lo[a]=min(lo[a],v); hi[a]=max(hi[a],v)
            count+=1
    if count<8: raise ValueError("foam OBJ has too few vertices")
    extent=[hi[a]-lo[a] for a in range(3)]
    if not all(e>0 for e in extent): raise ValueError("degenerate foam OBJ bounding box")
    return count,lo,hi,extent

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--root",required=True)
    ap.add_argument("--out",required=True)
    a=ap.parse_args()
    root=pathlib.Path(a.root); out=pathlib.Path(a.out); out.mkdir(parents=True,exist_ok=True)
    obj=root/"assets"/"foam.obj"
    meta=root/"metadata"/"foam shearing.json"
    md=json.loads(meta.read_text())
    n,lo,hi,extent=bbox_obj(obj)
    bbox_volume=math.prod(extent)
    task_translation=[float(x) for x in md["tasks"]["default"]["translation"]["foam"]]
    marker_xyz=[[float(x) for x in p] for p in md["markers"]["translation"]]
    marker_lo=[min(p[k] for p in marker_xyz) for k in range(3)]
    marker_hi=[max(p[k] for p in marker_xyz) for k in range(3)]
    marker_span=[marker_hi[k]-marker_lo[k] for k in range(3)]
    expected={}
    for material in ("soft","hard"):
        m=md["assets"]["foam"]["material"][material]
        v=float(m["mass"])/float(m["density"])
        expected[material]={
          "mass_over_density_m3":v,
          "bbox_volume_relative_difference":abs(bbox_volume-v)/v
        }
    result={
      "schema":"vulkax.gauge_published_asset_geometry","version":1,
      "provenance":"published-asset+measured-metadata",
      "fit_performed":False,
      "asset_source":"GAUGE-Dataset/assets/obj/foam.obj",
      "asset_sha256":sha256(obj),
      "metadata_sha256":sha256(meta),
      "obj_vertex_count":n,
      "coordinate_unit_interpretation":"m",
      "unit_basis":"GAUGE publishes the mesh as a simulation-ready MuJoCo asset and task translations are in scene coordinates; no trajectory residual is used to infer scale.",
      "asset_bbox":{"min_m":lo,"max_m":hi,"extent_m":extent,"bbox_volume_m3":bbox_volume},
      "task_translation_m":task_translation,
      "marker_support":{"min_m":marker_lo,"max_m":marker_hi,"span_m":marker_span,
                        "span_over_asset_extent":[marker_span[k]/extent[k] for k in range(3)]},
      "mass_density_volume_check":expected,
      "warning":"This test uses the published asset bounding box, not the full surface mesh as an MPM discretization. It is a structural adequacy probe, not material fitting."
    }
    (out/"geometry.json").write_text(json.dumps(result,indent=2)+"\n")
    print("VALID GAUGE published foam asset geometry")
    print("ASSET_BBOX extent_m",extent,"volume_m3",bbox_volume,"task_translation_m",task_translation)
    print("MARKER_SUPPORT span_m",marker_span,"ratio",result["marker_support"]["span_over_asset_extent"])
    print("VOLUME_CHECK",expected)

if __name__=="__main__":
    main()

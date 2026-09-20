#!/usr/bin/env python3
import argparse,json,pathlib,xml.etree.ElementTree as ET

def parse_obj(path):
    vertices=[];faces=[]
    with path.open("r",errors="replace") as f:
        for line in f:
            if line.startswith("v "):
                q=line.split()
                if len(q)>=4: vertices.append(tuple(map(float,q[1:4])))
            elif line.startswith("f "):
                idx=[]
                for tok in line.split()[1:]:
                    raw=tok.split("/")[0]
                    if not raw: continue
                    i=int(raw);idx.append(i-1 if i>0 else len(vertices)+i)
                if len(idx)>=3:
                    for j in range(1,len(idx)-1): faces.append((idx[0],idx[j],idx[j+1]))
    if not vertices: raise SystemExit("foam.obj contains no vertices")
    lo=[min(v[i] for v in vertices) for i in range(3)]
    hi=[max(v[i] for v in vertices) for i in range(3)]
    ext=[hi[i]-lo[i] for i in range(3)]
    signed=0.0
    for ia,ib,ic in faces:
        a,b,c=vertices[ia],vertices[ib],vertices[ic]
        signed+=(a[0]*(b[1]*c[2]-b[2]*c[1])-a[1]*(b[0]*c[2]-b[2]*c[0])+a[2]*(b[0]*c[1]-b[1]*c[0]))/6.0
    return {"vertices":len(vertices),"triangles":len(faces),"bounds_min_raw":lo,"bounds_max_raw":hi,
            "extents_raw":ext,"bbox_volume_raw":ext[0]*ext[1]*ext[2],
            "signed_mesh_volume_raw":signed,"absolute_mesh_volume_raw":abs(signed)}

def parse_mjcf(path):
    root=ET.fromstring(path.read_text(errors="replace"))
    nodes=[{"tag":e.tag,"attributes":dict(e.attrib)} for e in root.iter() if e.attrib]
    return {"root_tag":root.tag,"attribute_nodes":nodes,
      "foam_references":[n for n in nodes if any("foam" in str(v).lower() for v in n["attributes"].values())],
      "scale_like_attributes":[n for n in nodes if any(k in n["attributes"] for k in ("scale","size","pos","quat"))]}

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--asset-dir",required=True);p.add_argument("--metadata",required=True);p.add_argument("--out",required=True)
    a=p.parse_args();ad=pathlib.Path(a.asset_dir);out=pathlib.Path(a.out);out.mkdir(parents=True,exist_ok=True)
    obj=parse_obj(ad/"foam.obj");mjcf=parse_mjcf(ad/"foam.xml")
    md=json.loads(pathlib.Path(a.metadata).read_text());mats=md["assets"]["foam"]["material"]
    vols={name:{"mass_kg":float(m["mass"]),"density_kg_m3":float(m["density"]),
                "volume_m3":float(m["mass"])/float(m["density"])} for name,m in mats.items()}
    target=sum(x["volume_m3"] for x in vols.values())/len(vols)
    raw_vol=obj["absolute_mesh_volume_raw"];bbox=obj["bbox_volume_raw"]
    if raw_vol>1e-18:
        scale=(target/raw_vol)**(1/3);source="closed_mesh_signed_volume"
    elif bbox>1e-18:
        scale=(target/bbox)**(1/3);source="bbox_volume_fallback"
    else: raise SystemExit("degenerate foam OBJ")
    obj.update({"volume_matching_scale_to_metres":scale,"volume_scale_source":source,
      "volume_matched_extents_m":[x*scale for x in obj["extents_raw"]],
      "target_volume_m3_from_metadata":target,"scaled_bbox_volume_m3":bbox*scale**3,
      "scaled_mesh_volume_m3":raw_vol*scale**3})
    result={"schema":"vulkax.gauge_asset_geometry","version":1,
      "provenance":"released-asset+measured-metadata","fit_performed":False,
      "material_volumes":vols,"foam_obj":obj,"foam_mjcf":mjcf,
      "interpretation_rule":"Replace marker-span proxy only after released asset units/scale are reconciled from benchmark assets and physical metadata; never choose scale from trajectory error.",
      "warning":"Volume-matching scale is a unit/asset-consistency diagnostic, not a fitted mechanical parameter."}
    (out/"geometry.json").write_text(json.dumps(result,indent=2)+"\n")
    print("VALID GAUGE released foam geometry analysis")
    print("OBJ",obj["vertices"],obj["triangles"],"RAW_EXTENTS",obj["extents_raw"],"RAW_VOLUME",raw_vol)
    print("METADATA_VOLUME",target,"SCALE_TO_M",scale,"EXTENTS_M",obj["volume_matched_extents_m"])
    print("MJCF_FOAM_REFS",mjcf["foam_references"])

if __name__=="__main__": main()

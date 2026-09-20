#!/usr/bin/env python3
"""Export DCS spatial witness localization to an ASCII PLY for visualization.

positions.csv: region,x,y,z
residuals.csv: region,rms_error,standardized_error,resolved

Multiple positions may share one region; each inherits that region's scalar fields.
"""
import argparse,csv,pathlib,tempfile

def export(root,out_path):
    root=pathlib.Path(root)
    pos=list(csv.DictReader((root/"positions.csv").open()))
    res=list(csv.DictReader((root/"residuals.csv").open()))
    by_region={int(r["region"]):r for r in res}
    if not pos or not res: raise ValueError("positions/residuals must be non-empty")
    rows=[]
    for p in pos:
        region=int(p["region"])
        if region not in by_region: raise ValueError(f"missing residual region {region}")
        r=by_region[region]
        rows.append((
            float(p["x"]),float(p["y"]),float(p["z"]),region,
            float(r["rms_error"]),float(r["standardized_error"]),
            1 if str(r["resolved"]).lower() in ("1","true","yes") else 0))
    with pathlib.Path(out_path).open("w") as f:
        f.write("ply\nformat ascii 1.0\n")
        f.write(f"element vertex {len(rows)}\n")
        f.write("property float x\nproperty float y\nproperty float z\n")
        f.write("property int region\nproperty float dcs_rms_error\n")
        f.write("property float dcs_standardized_error\nproperty uchar dcs_resolved\n")
        f.write("end_header\n")
        for row in rows: f.write(" ".join(map(str,row))+"\n")
    return len(rows)

def self_test():
    with tempfile.TemporaryDirectory() as td:
        root=pathlib.Path(td)
        (root/"positions.csv").write_text(
            "region,x,y,z\n0,0,0,0\n0,1,0,0\n1,0,1,0\n")
        (root/"residuals.csv").write_text(
            "region,rms_error,standardized_error,resolved\n"
            "0,0.01,2.5,1\n1,0.02,0.5,0\n")
        out=root/"map.ply"
        n=export(root,out)
        text=out.read_text()
        if n!=3 or "dcs_standardized_error" not in text:
            raise RuntimeError("spatial exporter self-test failed")
        print("VALID DCS spatial map exporter self-test",n)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("root",nargs="?")
    ap.add_argument("--out",default="dcs_spatial_map.ply")
    ap.add_argument("--self-test",action="store_true")
    a=ap.parse_args()
    if a.self_test: self_test(); return
    if not a.root: raise SystemExit("root required unless --self-test")
    print("WROTE",a.out,"vertices",export(a.root,a.out))

if __name__=="__main__": main()

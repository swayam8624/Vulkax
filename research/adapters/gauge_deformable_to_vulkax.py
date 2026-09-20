#!/usr/bin/env python3
"""Convert a downloaded GAUGE deformable trial into Vulkax research observations.

No network access is performed. The caller supplies the GAUGE task metadata JSON
and one real trial JSON. Translation units are converted explicitly to metres.
"""
import argparse,csv,hashlib,json,math,pathlib

def sha256(path):
    h=hashlib.sha256()
    with open(path,"rb") as f:
        for chunk in iter(lambda:f.read(1<<20),b""):h.update(chunk)
    return h.hexdigest()

def scale_for(unit):
    u=unit.strip().lower()
    if u in ("mm","millimeter","millimetre","millimeters","millimetres"):return 1e-3
    if u in ("m","meter","metre","meters","metres"):return 1.0
    raise ValueError(f"unsupported translation unit: {unit}")

def convert(metadata_path,trial_path,material,outdir):
    metadata=json.load(open(metadata_path))
    trial=json.load(open(trial_path))
    fps=float(trial["FPS"]); scale=scale_for(trial["translation unit"])
    expected_fps=float(metadata["tasks"]["default"]["record"]["fps"])
    if abs(fps-expected_fps)>1e-12:raise ValueError(f"FPS mismatch: trial={fps} metadata={expected_fps}")
    mat=metadata["assets"]["foam"]["material"][material]
    markers=trial["foam"]
    if not markers:raise ValueError("trial has no foam markers")
    lengths=set()
    for mid,axes in markers.items():
        if not all(k in axes for k in ("x","y","z")):raise ValueError(f"{mid} missing xyz")
        lengths.update((len(axes["x"]),len(axes["y"]),len(axes["z"])))
    if len(lengths)!=1:raise ValueError("marker coordinate arrays have inconsistent lengths")
    frames=next(iter(lengths))
    outdir=pathlib.Path(outdir);outdir.mkdir(parents=True,exist_ok=True)
    with (outdir/"markers.csv").open("w",newline="") as f:
        w=csv.writer(f);w.writerow(["frame","time_s","marker_id","x_m","y_m","z_m"])
        for frame in range(frames):
            for mid in sorted(markers):
                xyz=[float(markers[mid][a][frame])*scale for a in ("x","y","z")]
                if not all(math.isfinite(v) for v in xyz):raise ValueError("non-finite marker coordinate")
                w.writerow([frame,frame/fps,mid,*xyz])
    base=trial.get("base")
    if base and all(k in base for k in ("x","y","z")):
        n=min(len(base["x"]),len(base["y"]),len(base["z"]))
        with (outdir/"driver.csv").open("w",newline="") as f:
            w=csv.writer(f);w.writerow(["frame","time_s","x_m","y_m","z_m"])
            for frame in range(n):w.writerow([frame,frame/fps,float(base["x"][frame])*scale,float(base["y"][frame])*scale,float(base["z"][frame])*scale])
    manifest={
      "schema":"vulkax.gauge_deformable_trial","version":1,"provenance":"measured",
      "source":{"metadata_sha256":sha256(metadata_path),"trial_sha256":sha256(trial_path)},
      "fps":fps,"source_translation_unit":trial["translation unit"],"output_translation_unit":"m",
      "material":{"label":material,"mass_kg":float(mat["mass"]),"density_kg_m3":float(mat["density"]),
                  "young_modulus_pa":float(mat["young"]),"poisson_ratio":float(mat["poisson"]),
                  "friction":float(mat.get("friction",0.0)),"restitution":float(mat.get("restitution",0.0))},
      "marker_count":len(markers),"frame_count":frames,
      "record_dimensions":metadata["tasks"]["default"]["record"]["dim"],
      "boundary_roles":{"fixed":metadata["tasks"]["default"]["fixed"],"kinematic":metadata["tasks"]["default"]["kinematic"]},
      "warning":"Measured GAUGE evidence. Do not relabel as Vulkax-generated or synthetic."
    }
    (outdir/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    return manifest

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--metadata",required=True);p.add_argument("--trial",required=True)
    p.add_argument("--material",choices=("soft","hard"),required=True);p.add_argument("--out",required=True)
    a=p.parse_args();m=convert(a.metadata,a.trial,a.material,a.out)
    print(json.dumps({"marker_count":m["marker_count"],"frame_count":m["frame_count"],"young_modulus_pa":m["material"]["young_modulus_pa"],"poisson_ratio":m["material"]["poisson_ratio"]}))
if __name__=="__main__":main()

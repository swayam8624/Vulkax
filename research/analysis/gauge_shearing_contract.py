#!/usr/bin/env python3
import argparse,csv,json,math,pathlib,statistics,hashlib

def sha256(p):
    h=hashlib.sha256()
    with open(p,"rb") as f:
        for b in iter(lambda:f.read(1<<20),b""): h.update(b)
    return h.hexdigest()

def v3(d,i,scale=1e-3):
    return [float(d["x"][i])*scale,float(d["y"][i])*scale,float(d["z"][i])*scale]

def sub(a,b): return [a[i]-b[i] for i in range(3)]
def norm(a): return math.sqrt(sum(x*x for x in a))
def mean(xs): return statistics.fmean(xs)

def unique_faces(md):
    seen=set();out=[]
    for f in md["markers"]["faces"]:
        k=tuple(int(x) for x in f)
        if k not in seen:
            seen.add(k);out.append(list(k))
    return out

def trial_record(path):
    d=json.loads(path.read_text())
    if d["translation unit"]!="mm": raise ValueError("expected mm")
    fps=float(d["FPS"]);base=d["base"]
    n=min(len(base[a]) for a in ("x","y","z"))
    p0=v3(base,0)
    rel=[sub(v3(base,i),p0) for i in range(n)]
    mags=[norm(x) for x in rel]
    peak=max(range(n),key=lambda i:mags[i])
    foam=d["foam"];ids=sorted(foam)
    pts=[v3(foam[m],0) for m in ids]
    lo=[min(p[k] for p in pts) for k in range(3)]
    hi=[max(p[k] for p in pts) for k in range(3)]
    return {"fps":fps,"frames":n,"driver_peak_m":mags[peak],"driver_peak_frame":peak,
            "driver_peak_vector_m":rel[peak],"driver_final_vector_m":rel[-1],
            "marker_ids":ids,"marker_initial_m":pts,"marker_bounds_min_m":lo,
            "marker_bounds_max_m":hi,"raw":d,"relative_driver":rel}

def write_driver(rec,path):
    with path.open("w",newline="") as f:
        w=csv.writer(f);w.writerow(["frame","time_s","dx_m","dy_m","dz_m","magnitude_m"])
        for i,p in enumerate(rec["relative_driver"]):
            w.writerow([i,i/rec["fps"],*p,norm(p)])

def write_markers(rec,path):
    with path.open("w",newline="") as f:
        w=csv.writer(f);w.writerow(["marker_id","x_m","y_m","z_m"])
        for mid,p in zip(rec["marker_ids"],rec["marker_initial_m"]): w.writerow([mid,*p])

def main():
    ap=argparse.ArgumentParser();ap.add_argument("--root",required=True);ap.add_argument("--out",required=True)
    a=ap.parse_args();root=pathlib.Path(a.root);out=pathlib.Path(a.out);out.mkdir(parents=True,exist_ok=True)
    mp=root/"metadata"/"foam shearing.json";md=json.loads(mp.read_text())
    records={}
    selected={}
    for material in ("soft","hard"):
        rows=[]
        for trial in range(1,11):
            p=root/"data"/"foam shearing"/material/f"{trial}.json"
            r=trial_record(p);r["trial"]=trial;r["sha256"]=sha256(p);rows.append(r)
        med=statistics.median(r["driver_peak_m"] for r in rows)
        pick=min(rows,key=lambda r:(abs(r["driver_peak_m"]-med),r["trial"]))
        records[material]=rows;selected[material]=pick
        write_driver(pick,out/f"driver_{material}.csv")
        write_markers(pick,out/f"markers_initial_{material}.csv")
    faces=unique_faces(md)
    summary={}
    for material in ("soft","hard"):
        rows=records[material]
        summary[material]={
            "selected_trial":selected[material]["trial"],
            "selection_rule":"closest driver-peak magnitude to within-material median; simulation error not consulted",
            "driver_peak_median_m":statistics.median(r["driver_peak_m"] for r in rows),
            "driver_peak_mean_m":mean([r["driver_peak_m"] for r in rows]),
            "driver_peak_sd_m":statistics.stdev(r["driver_peak_m"] for r in rows),
            "selected_driver_peak_m":selected[material]["driver_peak_m"],
            "selected_driver_peak_vector_m":selected[material]["driver_peak_vector_m"],
            "selected_driver_final_vector_m":selected[material]["driver_final_vector_m"],
            "selected_trial_sha256":selected[material]["sha256"],
            "marker_bounds_min_m":selected[material]["marker_bounds_min_m"],
            "marker_bounds_max_m":selected[material]["marker_bounds_max_m"],
            "material_metadata":md["assets"]["foam"]["material"][material],
        }
    contract={
      "schema":"vulkax.gauge_foam_shearing_contract","version":1,"provenance":"measured",
      "metadata_sha256":sha256(mp),
      "fps":selected["soft"]["fps"],
      "fixed_role":md["tasks"]["default"]["fixed"],
      "kinematic_role":md["tasks"]["default"]["kinematic"],
      "record_dimensions":md["tasks"]["default"]["record"]["dim"],
      "face_entries":len(md["markers"]["faces"]),"unique_faces":faces,
      "materials":summary,
      "notes":[
        "Representative repeat selection is based only on measured driver amplitude, before any Vulkax simulation.",
        "Coordinates are converted from GAUGE millimetres to metres.",
        "This artifact defines a sim-to-real contract; it does not claim parameter identification.",
        "Soft and hard foam differ in multiple material properties, so material attribution remains multivariate."
      ]
    }
    (out/"contract.json").write_text(json.dumps(contract,indent=2)+"\n")
    print("VALID GAUGE shearing contract")
    for m in ("soft","hard"):
        s=summary[m];print("SELECTED",m,s["selected_trial"],"driver_peak",s["selected_driver_peak_m"],
                          "median",s["driver_peak_median_m"],"bounds",s["marker_bounds_min_m"],s["marker_bounds_max_m"])

if __name__=="__main__": main()

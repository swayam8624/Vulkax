#!/usr/bin/env python3
"""Prospective GAUGE validation for Reality Probe.

Uses GAUGE development repeats only to estimate acquisition/repeat uncertainty.
Runs validation repeats with fixed material candidates and a held-out temporal
annihilating witness. Final-test repeats are refused unless an explicit frozen
lock is supplied.

This is a controlled real-data validation lane, not inverse material fitting.
"""
from __future__ import annotations
import argparse,csv,json,math,pathlib,statistics,subprocess,tempfile

TAU=2.0
FRACTIONS=(0.25,0.50,0.75)
WEIGHTS=(0.25,-0.50,0.25)

def decision(z):
    return "support" if z>=TAU else "veto" if z<=-TAU else "unresolved"

def read_csv(p):
    with open(p,newline="",encoding="utf-8") as f:return list(csv.DictReader(f))

def rms_delta(a,b):
    if len(a)!=len(b): raise ValueError("response width mismatch")
    return math.sqrt(sum((x-y)**2 for x,y in zip(a,b))/len(a))

def load_driver(p):
    rows=read_csv(p)
    xyz=[[float(r["x_m"]),float(r["y_m"]),float(r["z_m"])] for r in rows]
    t=[float(r["time_s"]) for r in rows]
    p0=xyz[0]
    rel=[[q[i]-p0[i] for i in range(3)] for q in xyz]
    mag=[math.sqrt(sum(v*v for v in q)) for q in rel]
    return t,rel,mag

def load_markers(p):
    rows=read_csv(p); frames={}; ids=set()
    for r in rows:
        k=int(r["frame"]); mid=r["marker_id"]; ids.add(mid)
        frames.setdefault(k,{})[mid]=[float(r["x_m"]),float(r["y_m"]),float(r["z_m"])]
    ids=sorted(ids)
    return frames,ids

def peak_progress_indices(mag):
    peak=max(range(len(mag)),key=mag.__getitem__)
    if peak<6: raise ValueError("driver peak occurs too early")
    m=max(mag[:peak+1])
    if not m>0: raise ValueError("zero driver motion")
    return [min(range(peak+1),key=lambda i:abs(mag[i]/m-f)) for f in FRACTIONS]

def response(frames,ids,indices):
    out=[]
    for i in indices:
        if i not in frames: raise ValueError(f"missing marker frame {i}")
        for mid in ids: out.extend(frames[i][mid])
    return out

def witness(frames,ids,indices):
    width=len(ids)*3
    chunks=[]
    for i in indices:
        q=[]
        for mid in ids:q.extend(frames[i][mid])
        chunks.append(q)
    return [sum(WEIGHTS[j]*chunks[j][k] for j in range(3)) for k in range(width)]

def write_probe_inputs(adapted,out):
    frames,ids=load_markers(adapted/"markers.csv")
    t,rel,mag=load_driver(adapted/"driver.csv")
    out.mkdir(parents=True,exist_ok=True)
    with (out/"initial.csv").open("w",newline="") as f:
        w=csv.writer(f);w.writerow(["marker_id","x_m","y_m","z_m"])
        for mid in ids:w.writerow([mid,*frames[0][mid]])
    with (out/"driver.csv").open("w",newline="") as f:
        w=csv.writer(f);w.writerow(["frame","time_s","dx_m","dy_m","dz_m","magnitude_m"])
        for i,(ti,d,m) in enumerate(zip(t,rel,mag)):w.writerow([i,ti,*d,m])
    return frames,ids,peak_progress_indices(mag)

def run_model(exe,inputs,out_csv,mat,E,dt):
    cmd=[str(exe),str(inputs/"initial.csv"),str(inputs/"driver.csv"),str(out_csv),
         str(E),str(mat["poisson_ratio"]),str(mat["density_kg_m3"]),str(mat["mass_kg"]),
         repr(dt),"prospective", "5","13","1","APIC","released_asset_aspect","zero","neo_hookean_log_j"]
    cp=subprocess.run(cmd,text=True,capture_output=True)
    if cp.returncode:
        raise RuntimeError(f"forward probe failed: {cp.stderr or cp.stdout}")

def manifest_map(adapted_root):
    out={}
    for p in adapted_root.glob("gauge/*/validation_manifest.json"):
        m=json.loads(p.read_text()); out[m["validation_scene"]]=(p.parent,m)
    return out

def repeat_variance(dev_items,kind):
    vecs=[]
    for adapted,_m in dev_items:
        frames,ids=load_markers(adapted/"markers.csv")
        _t,_rel,mag=load_driver(adapted/"driver.csv")
        idx=peak_progress_indices(mag)
        vecs.append(witness(frames,ids,idx) if kind=="witness" else response(frames,ids,idx))
    if len(vecs)<2: raise ValueError("need >=2 development repeats for repeat variance")
    n=len(vecs); width=len(vecs[0])
    mean=[sum(v[k] for v in vecs)/n for k in range(width)]
    mse=sum(sum((v[k]-mean[k])**2 for k in range(width))/width for v in vecs)/n
    return mse

def write_records(path,rows):
    fields=["record_version","trial_id","paired_key","dataset","scene","split","evidence_class",
      "confirmatory","trial_family","ground_truth","method","score","decision","decision_threshold",
      "confidence","physical_delta","target_error_delta","measurement_noise_sigma","pose_noise_sigma",
      "missing_fraction","channel_dependence","negative_control","seed","source_artifact","notes"]
    path.parent.mkdir(parents=True,exist_ok=True)
    with path.open("w",newline="") as f:
        w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(rows)

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--exe",type=pathlib.Path,required=True)
    ap.add_argument("--adapted-root",type=pathlib.Path,default=pathlib.Path("build/publication-validation/public-data/adapted"))
    ap.add_argument("--out",type=pathlib.Path,default=pathlib.Path("build/publication-validation/gauge-prospective"))
    ap.add_argument("--split",choices=("development","validation","final_test"),default="validation")
    ap.add_argument("--lock",type=pathlib.Path)
    ap.add_argument("--dt",type=float,default=8.333333333333333e-5)
    a=ap.parse_args()
    if a.split=="final_test":
        if not a.lock: raise SystemExit("final_test requires --lock")
        subprocess.run(["python3","research/analysis/freeze_publication_validation.py","--check",str(a.lock)],check=True)

    mm=manifest_map(a.adapted_root)
    groups={}
    for scene,(adapted,m) in mm.items():
        task,material,_trial=scene.split("/")
        if task=="foam shearing": continue
        groups.setdefault((task,material),[]).append((scene,adapted,m))
    rows=[]; summary=[]
    a.out.mkdir(parents=True,exist_ok=True)

    for (task,material),items in sorted(groups.items()):
        dev=[(ad,m) for scene,ad,m in items if m["validation_split"]=="development"]
        rv=repeat_variance(dev,"witness"); rr=repeat_variance(dev,"raw")
        for scene,adapted,m in sorted(items):
            if m["validation_split"]!=a.split: continue
            truthE=float(m["material"]["young_modulus_pa"])
            baseE=1.25*truthE
            candidates=[("support",truthE,"truth_control",False),
                        ("veto",1.60*truthE,"truth_control",False),
                        ("unresolved",baseE,"placebo",True)]
            case_dir=a.out/scene.replace("/","__")
            inputs=case_dir/"inputs"
            measured,ids,idx=write_probe_inputs(adapted,inputs)
            measured_w=witness(measured,ids,idx); measured_r=response(measured,ids,idx)

            configs={"baseline":baseE,**{lab:E for lab,E,_f,_n in candidates}}
            pred={}
            for name,E in configs.items():
                pred[name]={}
                for fidelity,dt in (("nominal",a.dt),("refined",a.dt*0.5)):
                    out_csv=case_dir/f"{name}_{fidelity}.csv"
                    run_model(a.exe,inputs,out_csv,m["material"],E,dt)
                    fr,ids2=load_markers(out_csv)
                    if ids2!=ids: raise RuntimeError("predicted marker identity mismatch")
                    pred[name][fidelity]=(witness(fr,ids,idx),response(fr,ids,idx))

            for truth,candE,family,neg in candidates:
                base_nom_w,base_nom_r=pred["baseline"]["nominal"]
                base_ref_w,base_ref_r=pred["baseline"]["refined"]
                cand_nom_w,cand_nom_r=pred[truth]["nominal"]
                cand_ref_w,cand_ref_r=pred[truth]["refined"]
                ebw=rms_delta(base_nom_w,measured_w); ecw=rms_delta(cand_nom_w,measured_w)
                ebr=rms_delta(base_nom_r,measured_r); ecr=rms_delta(cand_nom_r,measured_r)
                nbw=rms_delta(base_nom_w,base_ref_w); ncw=rms_delta(cand_nom_w,cand_ref_w)
                nbr=rms_delta(base_nom_r,base_ref_r); ncr=rms_delta(cand_nom_r,cand_ref_r)
                zw=(ebw-ecw)/math.sqrt(max(rv+nbw*nbw+ncw*ncw,1e-30))
                zr=(ebr-ecr)/math.sqrt(max(rr+nbr*nbr+ncr*ncr,1e-30))
                target_delta=abs(math.log(candE/truthE))-abs(math.log(baseE/truthE))
                base={"record_version":1,"paired_key":f"gauge:{scene}:{truth}",
                      "dataset":"gauge_real","scene":scene,"split":a.split,
                      "evidence_class":"prospective_real_trajectory_temporal_holdout",
                      "confirmatory":str(a.split=="final_test").lower(),"trial_family":family,
                      "ground_truth":truth,"decision_threshold":TAU,"confidence":"",
                      "physical_delta":math.log(candE/baseE),"target_error_delta":target_delta,
                      "measurement_noise_sigma":math.sqrt(rv),"pose_noise_sigma":"",
                      "missing_fraction":"","channel_dependence":"",
                      "negative_control":str(neg).lower(),"seed":0,
                      "source_artifact":str(case_dir),"notes":"GAUGE measured truth; dev-repeat variance; nominal/refined numerical uncertainty"}
                for method,z in (("temporal_annihilating_witness",zw),("raw_bundle_holdout",zr)):
                    r=dict(base);r["method"]=method;r["score"]=z;r["decision"]=decision(z)
                    r["trial_id"]=f"{base['paired_key']}:{method}"
                    rows.append(r)
                summary.append({"scene":scene,"truth":truth,"baseline_E":baseE,"candidate_E":candE,
                                "witness_z":zw,"witness_decision":decision(zw),
                                "raw_z":zr,"raw_decision":decision(zr),
                                "repeat_sigma_witness":math.sqrt(rv),"repeat_sigma_raw":math.sqrt(rr)})

    write_records(a.out/"validation_records.csv",rows)
    with (a.out/"case_summary.csv").open("w",newline="") as f:
        if summary:
            w=csv.DictWriter(f,fieldnames=list(summary[0]));w.writeheader();w.writerows(summary)
    meta={"schema":"vulkax.gauge.prospective_validation","version":1,"split":a.split,
          "record_count":len(rows),"case_count":len(summary),"decision_threshold_abs_z":TAU,
          "fractions":FRACTIONS,"weights":WEIGHTS,
          "truth_rule":{"baseline_E":"1.25 * measured E","support":"measured E",
                        "veto":"1.60 * measured E","unresolved":"identical to baseline (placebo)"},
          "uncertainty":"repeat variance from development repeats only + nominal/refined numerical discrepancy",
          "claim_guard":"Validation split is not confirmatory final-test evidence." if a.split!="final_test" else "Final-test records generated only after lock verification."}
    (a.out/"summary.json").write_text(json.dumps(meta,indent=2)+"\n")
    print("VALID GAUGE prospective validation")
    print("SPLIT",a.split,"CASES",len(summary),"RECORDS",len(rows))
    print("OUT",a.out/"validation_records.csv")
if __name__=="__main__": main()

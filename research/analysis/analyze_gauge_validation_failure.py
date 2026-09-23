#!/usr/bin/env python3
"""Forensic analysis of an already executed GAUGE validation campaign.

Reads saved nominal/refined predictions and measured adapted inputs. It does not
run the simulator, change thresholds, change candidates, or inspect final-test
repeats.
"""
from __future__ import annotations
import argparse,csv,json,math,pathlib,statistics,tempfile

FRACTIONS=(0.25,0.50,0.75)
WEIGHTS=(0.25,-0.50,0.25)

def read_csv(p):
    with open(p,newline="",encoding="utf-8") as f:return list(csv.DictReader(f))

def load_markers(p):
    frames={};ids=set()
    for r in read_csv(p):
        k=int(r["frame"]);mid=r["marker_id"];ids.add(mid)
        frames.setdefault(k,{})[mid]=[float(r["x_m"]),float(r["y_m"]),float(r["z_m"])]
    return frames,sorted(ids)

def load_driver(p):
    rows=read_csv(p)
    xyz=[[float(r["x_m"]),float(r["y_m"]),float(r["z_m"])] for r in rows]
    p0=xyz[0]
    rel=[[q[i]-p0[i] for i in range(3)] for q in xyz]
    return [math.sqrt(sum(v*v for v in q)) for q in rel]

def peak_progress_indices(mag):
    peak=max(range(len(mag)),key=mag.__getitem__)
    m=max(mag[:peak+1])
    return [min(range(peak+1),key=lambda i:abs(mag[i]/m-f)) for f in FRACTIONS]

def response(frames,ids,indices):
    out=[]
    for i in indices:
        for mid in ids: out.extend(frames[i][mid])
    return out

def witness(frames,ids,indices):
    chunks=[]
    for i in indices:
        q=[]
        for mid in ids:q.extend(frames[i][mid])
        chunks.append(q)
    return [sum(WEIGHTS[j]*chunks[j][k] for j in range(3)) for k in range(len(chunks[0]))]

def rms(a,b):
    return math.sqrt(sum((x-y)**2 for x,y in zip(a,b))/len(a))

def representations(path,indices,ids):
    fr,ids2=load_markers(path)
    if ids2!=ids: raise ValueError(f"marker mismatch: {path}")
    return {"witness":witness(fr,ids,indices),"raw":response(fr,ids,indices)}

def adapted_scene_map(adapted_root):
    out={}
    gauge=adapted_root/"gauge"
    for p in gauge.glob("*/validation_manifest.json"):
        m=json.loads(p.read_text(encoding="utf-8"))
        scene=m.get("validation_scene")
        if not scene:
            raise ValueError(f"missing validation_scene in {p}")
        if scene in out:
            raise ValueError(f"duplicate adapted GAUGE scene: {scene}")
        out[scene]=p.parent
    return out

def finite_div(a,b):
    return a/b if b>0 else None

def med(xs):
    q=[x for x in xs if x is not None and math.isfinite(x)]
    return statistics.median(q) if q else None

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--campaign",type=pathlib.Path,
                    default=pathlib.Path("build/publication-validation/gauge-prospective-validation"))
    ap.add_argument("--adapted-root",type=pathlib.Path,
                    default=pathlib.Path("build/publication-validation/public-data/adapted"))
    ap.add_argument("--out",type=pathlib.Path)
    ap.add_argument("--self-test",action="store_true")
    a=ap.parse_args()
    if a.self_test:
        with tempfile.TemporaryDirectory() as td:
            root=pathlib.Path(td)/"adapted"
            d=root/"gauge"/"foam_compression__hard__05"
            d.mkdir(parents=True)
            (d/"validation_manifest.json").write_text(
                json.dumps({"validation_scene":"foam compression/hard/05"})+"\n",
                encoding="utf-8")
            mm=adapted_scene_map(root)
            assert mm["foam compression/hard/05"]==d
        print("VALID GAUGE forensic adapted-scene lookup self-test")
        return
    out=a.out or (a.campaign/"forensics")
    out.mkdir(parents=True,exist_ok=True)

    cases=read_csv(a.campaign/"case_summary.csv")
    by_key={(r["scene"],r["truth"]):r for r in cases}
    adapted_map=adapted_scene_map(a.adapted_root)
    details=[]
    for (scene,truth),row in sorted(by_key.items()):
        if truth=="unresolved":
            continue
        case_dir=a.campaign/scene.replace("/","__")
        if scene not in adapted_map:
            raise FileNotFoundError(
                f"no adapted GAUGE package for scene {scene!r}; "
                f"available={sorted(adapted_map)[:8]}..."
            )
        adapted=adapted_map[scene]
        measured,ids=load_markers(adapted/"markers.csv")
        idx=peak_progress_indices(load_driver(adapted/"driver.csv"))
        meas={"witness":witness(measured,ids,idx),"raw":response(measured,ids,idx)}
        for kind,label in (("witness","temporal_annihilating_witness"),("raw","raw_bundle_holdout")):
            base_n=representations(case_dir/"baseline_nominal.csv",idx,ids)[kind]
            base_r=representations(case_dir/"baseline_refined.csv",idx,ids)[kind]
            cand_n=representations(case_dir/f"{truth}_nominal.csv",idx,ids)[kind]
            cand_r=representations(case_dir/f"{truth}_refined.csv",idx,ids)[kind]
            eb=rms(base_n,meas[kind]); ec=rms(cand_n,meas[kind])
            nb=rms(base_n,base_r); nc=rms(cand_n,cand_r)
            rep=float(row["repeat_sigma_witness" if kind=="witness" else "repeat_sigma_raw"])
            signal=eb-ec
            denom=math.sqrt(rep*rep+nb*nb+nc*nc)
            expected_sign=1.0 if truth=="support" else -1.0
            detail={
              "scene":scene,"truth":truth,"method":label,
              "baseline_error":eb,"candidate_error":ec,
              "signed_improvement":signal,
              "expected_sign":int(expected_sign),
              "ordering_correct":signal*expected_sign>0,
              "repeat_sigma":rep,
              "baseline_numerical_sigma":nb,
              "candidate_numerical_sigma":nc,
              "total_sigma":denom,
              "z_recomputed":signal/denom if denom else 0.0,
              "z_without_repeat_variance":finite_div(signal,math.sqrt(nb*nb+nc*nc)),
              "z_without_numerical_variance":finite_div(signal,rep),
              "baseline_candidate_separation":rms(base_n,cand_n),
              "signal_to_repeat_ratio":finite_div(abs(signal),rep),
              "signal_to_numerical_ratio":finite_div(abs(signal),math.sqrt(nb*nb+nc*nc)),
            }
            details.append(detail)

    fields=list(details[0]) if details else []
    with (out/"per_case.csv").open("w",newline="",encoding="utf-8") as f:
        if details:
            w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(details)

    groups={}
    for d in details: groups.setdefault((d["method"],d["truth"]),[]).append(d)
    summaries=[]
    for (method,truth),g in sorted(groups.items()):
        summaries.append({
          "method":method,"truth":truth,"n":len(g),
          "ordering_correct_n":sum(bool(x["ordering_correct"]) for x in g),
          "ordering_correct_rate":sum(bool(x["ordering_correct"]) for x in g)/len(g),
          "median_signed_improvement":med([x["signed_improvement"] for x in g]),
          "median_abs_z":med([abs(x["z_recomputed"]) for x in g]),
          "max_abs_z":max(abs(x["z_recomputed"]) for x in g),
          "median_repeat_sigma":med([x["repeat_sigma"] for x in g]),
          "median_numerical_sigma":med([math.sqrt(x["baseline_numerical_sigma"]**2+x["candidate_numerical_sigma"]**2) for x in g]),
          "median_model_separation":med([x["baseline_candidate_separation"] for x in g]),
          "median_signal_to_repeat_ratio":med([x["signal_to_repeat_ratio"] for x in g]),
          "median_signal_to_numerical_ratio":med([x["signal_to_numerical_ratio"] for x in g]),
          "would_cross_tau2_without_repeat_n":sum(abs(x["z_without_repeat_variance"] or 0.0)>=2 for x in g),
          "would_cross_tau2_without_numerical_n":sum(abs(x["z_without_numerical_variance"] or 0.0)>=2 for x in g),
        })
    with (out/"summary_by_method_truth.csv").open("w",newline="",encoding="utf-8") as f:
        if summaries:
            w=csv.DictWriter(f,fieldnames=list(summaries[0]));w.writeheader();w.writerows(summaries)

    directional=[x for x in details if x["method"]=="raw_bundle_holdout"]
    order_rate=sum(bool(x["ordering_correct"]) for x in directional)/len(directional) if directional else 0.0
    med_rep=med([x["signal_to_repeat_ratio"] for x in directional]) or 0.0
    med_num=med([x["signal_to_numerical_ratio"] for x in directional]) or 0.0
    if order_rate < 0.75:
        diagnosis="forward_model_truth_ordering_failure"
        action="do_not_open_final_test; GAUGE forward model does not reliably rank the measured material truth before thresholding"
    elif med_rep < 2.0 and med_num >= 2.0:
        diagnosis="repeat_variability_dominates"
        action="do_not_open_final_test; redesign the independent observable/probe using validation data only"
    elif med_num < 2.0 and med_rep >= 2.0:
        diagnosis="numerical_uncertainty_dominates"
        action="do_not_open_final_test; improve convergence/fidelity using validation data only"
    else:
        diagnosis="information_limited_under_frozen_probe"
        action="do_not_open_final_test; preserve abstention and move to a more identifiable external-validation lane"

    report={
      "schema":"vulkax.gauge.validation_failure_forensics","version":1,
      "campaign":str(a.campaign),"case_count":len(details),
      "raw_truth_ordering_correct_rate":order_rate,
      "median_raw_signal_to_repeat_ratio":med_rep,
      "median_raw_signal_to_numerical_ratio":med_num,
      "diagnosis":diagnosis,"recommended_action":action,
      "final_test_opened":False,
      "claim_guard":"Post-hoc diagnosis of validation split only. No threshold/candidate changes are authorized for final-test claims."
    }
    (out/"summary.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print("VALID GAUGE validation failure forensics")
    print("RAW_ORDERING_CORRECT_RATE",order_rate)
    print("RAW_MEDIAN_SIGNAL_TO_REPEAT",med_rep)
    print("RAW_MEDIAN_SIGNAL_TO_NUMERICAL",med_num)
    print("DIAGNOSIS",diagnosis)
    print("ACTION",action)
    print("OUT",out)
if __name__=="__main__": main()

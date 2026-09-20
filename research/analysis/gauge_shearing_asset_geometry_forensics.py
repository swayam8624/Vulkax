#!/usr/bin/env python3
"""No-fit GAUGE geometry x transfer falsification on the frozen shearing contract."""
import argparse,json,pathlib,subprocess,sys

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--exe",required=True)
    ap.add_argument("--contract-dir",required=True)
    ap.add_argument("--affine-dir",required=True)
    ap.add_argument("--asset-geometry-json",required=True)
    ap.add_argument("--out",required=True)
    a=ap.parse_args()
    out=pathlib.Path(a.out); out.mkdir(parents=True,exist_ok=True)
    here=pathlib.Path(__file__).resolve().parent
    variants=[]
    for geometry in ("measured_aspect","asset_bbox"):
        for transfer in ("APIC","PIC","FLIP"):
            variants.append((f"{geometry}__{transfer.lower()}",geometry,transfer))

    records={}
    for name,geometry,transfer in variants:
        fwd=out/f"forward_{name}"; ana=out/f"analysis_{name}"
        cmd=[sys.executable,str(here/"run_gauge_shearing_forward.py"),
             "--exe",a.exe,"--contract-dir",a.contract_dir,"--out",str(fwd),
             "--geometry-mode",geometry,"--transfer",transfer]
        if geometry=="asset_bbox":
            cmd.extend(["--asset-geometry-json",a.asset_geometry_json])
        cp=subprocess.run(cmd,text=True,capture_output=True)
        if cp.stdout: print(cp.stdout,end="")
        if cp.stderr: print(cp.stderr,end="",file=sys.stderr)
        if cp.returncode:
            records[name]={"status":"failed","geometry":geometry,"transfer":transfer,
                           "returncode":cp.returncode,"failure_tail":(cp.stderr+"\n"+cp.stdout)[-2000:]}
            print("ASSET_GEOMETRY_FAILED",name,cp.returncode)
            continue
        apcmd=[sys.executable,str(here/"gauge_shearing_forward_analysis.py"),
               "--contract-dir",a.contract_dir,"--forward-dir",str(fwd),
               "--affine-dir",a.affine_dir,"--out",str(ana)]
        subprocess.run(apcmd,check=True,capture_output=True,text=True)
        s=json.loads((ana/"summary.json").read_text())
        rec={
          "status":"success","geometry":geometry,"transfer":transfer,
          "soft_face_nrmse":s["materials"]["soft"]["forward_nrmse"],
          "hard_face_nrmse":s["materials"]["hard"]["forward_nrmse"],
          "mean_face_nrmse":0.5*(s["materials"]["soft"]["forward_nrmse"]+s["materials"]["hard"]["forward_nrmse"]),
          "soft_marker_rmse_m":s["materials"]["soft"]["marker_position_rmse_m"],
          "hard_marker_rmse_m":s["materials"]["hard"]["marker_position_rmse_m"],
          "mean_marker_rmse_m":0.5*(s["materials"]["soft"]["marker_position_rmse_m"]+s["materials"]["hard"]["marker_position_rmse_m"]),
          "beats_affine_both_materials":s["beats_affine_both_materials"],
          "material_separation_sign_agrees":s["material_separation_sign_agrees"],
          "soft_minus_hard_peak":s["soft_minus_hard_peak"]
        }
        records[name]=rec
        print("GEOMETRY_X_TRANSFER",name,"face",rec["mean_face_nrmse"],
              "marker_m",rec["mean_marker_rmse_m"],
              "beats_affine",rec["beats_affine_both_materials"],
              "sep_sign",rec["material_separation_sign_agrees"])

    paired={}
    for transfer in ("APIC","PIC","FLIP"):
        old=records.get(f"measured_aspect__{transfer.lower()}",{})
        new=records.get(f"asset_bbox__{transfer.lower()}",{})
        if old.get("status")=="success" and new.get("status")=="success":
            paired[transfer]={
              "face_delta_asset_minus_proxy":new["mean_face_nrmse"]-old["mean_face_nrmse"],
              "marker_delta_asset_minus_proxy_m":new["mean_marker_rmse_m"]-old["mean_marker_rmse_m"],
              "asset_improves_both_metrics":(
                  new["mean_face_nrmse"]<old["mean_face_nrmse"] and
                  new["mean_marker_rmse_m"]<old["mean_marker_rmse_m"])
            }

    successful=[(n,r) for n,r in records.items() if r.get("status")=="success"]
    successful.sort(key=lambda x:(x[1]["mean_face_nrmse"],x[1]["mean_marker_rmse_m"]))
    passers=[n for n,r in successful
             if r["geometry"]=="asset_bbox" and r["beats_affine_both_materials"] and r["material_separation_sign_agrees"]]
    result={
      "schema":"vulkax.gauge_asset_geometry_transfer_forensics","version":1,
      "provenance":"published-asset+measured-input+no-fit-model-prediction",
      "fit_performed":False,
      "factorial_design":{"geometry":["measured_aspect","asset_bbox"],"transfer":["APIC","PIC","FLIP"],"worlds":6},
      "frozen_controls":{"material_parameters":"GAUGE measured metadata","gravity":"zero",
                         "boundary_layers":1,"constitutive":"neo_hookean_log_j"},
      "records":records,
      "paired_asset_effect":paired,
      "diagnostic_ranking":[n for n,_ in successful],
      "asset_affine_gate_passers":passers,
      "inverse_fitting_unlocked":False,
      "decision":"replicate_asset_geometry_before_any_inverse_fit" if passers else "geometry_asset_bbox_insufficient_no_inverse_fit",
      "warning":"No trajectory parameter was fit. Even an asset-geometry gate passer is diagnostic only and must replicate across all measured repeats before any inverse material fitting is permitted."
    }
    (out/"summary.json").write_text(json.dumps(result,indent=2)+"\n")
    print("VALID GAUGE published-asset geometry x transfer forensic")
    print("PAIRED_ASSET_EFFECT",paired)
    print("ASSET_AFFINE_GATE_PASSERS",passers)
    print("DECISION",result["decision"])

if __name__=="__main__":
    main()

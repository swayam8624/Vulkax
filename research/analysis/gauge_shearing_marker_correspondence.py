#!/usr/bin/env python3
"""Benchmark-aligned GAUGE marker correspondence x resolution falsification."""
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

    # Fine resolution is not selected by trajectory error. It is the existing
    # CFL-safe 7x7x19 grid and is tested because its particle spacing is small
    # enough to satisfy GAUGE's published <1 cm marker matching contract.
    variants=[
      ("coarse_mls",5,13,1.0/12000.0,"MLS24"),
      ("coarse_hungarian",5,13,1.0/12000.0,"HUNGARIAN"),
      ("fine_mls",7,19,1.0/24000.0,"MLS24"),
      ("fine_hungarian",7,19,1.0/24000.0,"HUNGARIAN"),
    ]
    records={}
    for name,nc,nl,dt,tracking in variants:
        fwd=out/f"forward_{name}"; ana=out/f"analysis_{name}"
        cmd=[sys.executable,str(here/"run_gauge_shearing_forward.py"),
             "--exe",a.exe,"--contract-dir",a.contract_dir,"--out",str(fwd),
             "--geometry-mode","asset_bbox","--asset-geometry-json",a.asset_geometry_json,
             "--transfer","APIC","--n-cross",str(nc),"--n-long",str(nl),
             "--dt",repr(dt),"--marker-tracking",tracking]
        cp=subprocess.run(cmd,text=True,capture_output=True)
        if cp.stdout: print(cp.stdout,end="")
        if cp.stderr: print(cp.stderr,end="",file=sys.stderr)
        if cp.returncode:
            records[name]={"status":"failed","returncode":cp.returncode,
                           "failure_tail":(cp.stderr+"\n"+cp.stdout)[-2000:]}
            continue
        subprocess.run([sys.executable,str(here/"gauge_shearing_forward_analysis.py"),
                        "--contract-dir",a.contract_dir,"--forward-dir",str(fwd),
                        "--affine-dir",a.affine_dir,"--out",str(ana)],
                       check=True,capture_output=True,text=True)
        s=json.loads((ana/"summary.json").read_text())
        ss=json.loads((fwd/"predicted_soft_summary.json").read_text())
        hs=json.loads((fwd/"predicted_hard_summary.json").read_text())
        max_match=max(float(ss["marker_match_max_m"]),float(hs["marker_match_max_m"]))
        rec={
          "status":"success","n_cross":nc,"n_long":nl,"dt_s":dt,"marker_tracking":tracking,
          "marker_match_max_m":max_match,
          "gaussian_hungarian_protocol_valid":tracking!="HUNGARIAN" or max_match<0.01,
          "mean_face_nrmse":0.5*(s["materials"]["soft"]["forward_nrmse"]+s["materials"]["hard"]["forward_nrmse"]),
          "mean_marker_rmse_m":0.5*(s["materials"]["soft"]["marker_position_rmse_m"]+s["materials"]["hard"]["marker_position_rmse_m"]),
          "soft_face_nrmse":s["materials"]["soft"]["forward_nrmse"],
          "hard_face_nrmse":s["materials"]["hard"]["forward_nrmse"],
          "beats_affine_both_materials":s["beats_affine_both_materials"],
          "material_separation_sign_agrees":s["material_separation_sign_agrees"],
        }
        records[name]=rec
        print("MARKER_CORRESPONDENCE",name,
              "match_max_m",max_match,
              "protocol_valid",rec["gaussian_hungarian_protocol_valid"],
              "face",rec["mean_face_nrmse"],
              "marker_m",rec["mean_marker_rmse_m"],
              "beats_affine",rec["beats_affine_both_materials"])

    eligible=[]
    for name,r in records.items():
        if r.get("status")!="success" or r["marker_tracking"]!="HUNGARIAN": continue
        if r["gaussian_hungarian_protocol_valid"] and r["beats_affine_both_materials"] and r["material_separation_sign_agrees"]:
            eligible.append(name)

    pairs={}
    for level in ("coarse","fine"):
        a0=records.get(level+"_mls",{}); b0=records.get(level+"_hungarian",{})
        if a0.get("status")=="success" and b0.get("status")=="success":
            pairs[level]={
              "face_delta_hungarian_minus_mls":b0["mean_face_nrmse"]-a0["mean_face_nrmse"],
              "marker_delta_hungarian_minus_mls_m":b0["mean_marker_rmse_m"]-a0["mean_marker_rmse_m"],
              "hungarian_improves_both_metrics":(
                b0["mean_face_nrmse"]<a0["mean_face_nrmse"] and
                b0["mean_marker_rmse_m"]<a0["mean_marker_rmse_m"])
            }

    result={
      "schema":"vulkax.gauge_marker_correspondence_forensics","version":1,
      "provenance":"published-protocol+measured-input+no-fit-model-prediction",
      "fit_performed":False,
      "protocol_basis":"GAUGE assigns each initial marker to one unique nearby simulated vertex/particle and requires matching error below 1 cm.",
      "frozen_controls":{"geometry":"published foam.obj bounding box","transfer":"APIC",
                         "constitutive":"neo_hookean_log_j","gravity":"zero","boundary_layers":1,
                         "material_parameters":"GAUGE measured metadata"},
      "records":records,
      "paired_tracking_effect":pairs,
      "affine_gate_passers":eligible,
      "inverse_fitting_unlocked":False,
      "decision":"replicate_benchmark_aligned_correspondence_all_repeats" if eligible else "marker_correspondence_insufficient_no_inverse_fit",
      "warning":"Resolution and assignment are protocol/numerics controls, not fitted physical parameters. A representative-trial pass only earns all-repeat replication."
    }
    (out/"summary.json").write_text(json.dumps(result,indent=2)+"\n")
    print("VALID GAUGE marker correspondence x resolution forensic")
    print("PAIRED_TRACKING_EFFECT",pairs)
    print("AFFINE_GATE_PASSERS",eligible)
    print("DECISION",result["decision"])

if __name__=="__main__":
    main()

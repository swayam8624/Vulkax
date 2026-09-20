#!/usr/bin/env python3
import argparse,json,pathlib,subprocess,sys

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--exe",required=True)
    p.add_argument("--contract-dir",required=True)
    p.add_argument("--affine-dir",required=True)
    p.add_argument("--out",required=True)
    a=p.parse_args()
    out=pathlib.Path(a.out);out.mkdir(parents=True,exist_ok=True)
    here=pathlib.Path(__file__).resolve().parent
    variants=[
      ("baseline",{}),
      ("resolution_fine",{"n_cross":7,"n_long":19}),
      ("boundary_two_layers",{"boundary_layers":2}),
      ("transfer_pic",{"transfer":"PIC"}),
      ("transfer_flip",{"transfer":"FLIP"}),
      ("square_cross_section",{"geometry_mode":"square_cross"}),
      ("gravity_pos_x",{"gravity":"+x"}),
      ("gravity_neg_x",{"gravity":"-x"}),
      ("gravity_pos_y",{"gravity":"+y"}),
      ("gravity_neg_y",{"gravity":"-y"}),
      ("gravity_pos_z",{"gravity":"+z"}),
      ("gravity_neg_z",{"gravity":"-z"}),
    ]
    records={}
    for name,opts in variants:
        fd=out/f"forward_{name}";ad=out/f"analysis_{name}"
        cmd=[sys.executable,str(here/"run_gauge_shearing_forward.py"),
             "--exe",a.exe,"--contract-dir",a.contract_dir,"--out",str(fd)]
        keymap={"n_cross":"--n-cross","n_long":"--n-long","boundary_layers":"--boundary-layers",
                "transfer":"--transfer","geometry_mode":"--geometry-mode","gravity":"--gravity"}
        for k,v in opts.items():cmd.extend([keymap[k],str(v)])
        cp=subprocess.run(cmd,text=True,capture_output=True)
        if cp.returncode:
            records[name]={"status":"failed","options":opts,"returncode":cp.returncode,
                           "failure_tail":(cp.stderr+"\n"+cp.stdout)[-2000:]}
            print("STRUCTURAL_FAILED",name,cp.returncode)
            continue
        ap=[sys.executable,str(here/"gauge_shearing_forward_analysis.py"),
            "--contract-dir",a.contract_dir,"--forward-dir",str(fd),
            "--affine-dir",a.affine_dir,"--out",str(ad)]
        subprocess.run(ap,check=True,capture_output=True,text=True)
        summary=json.loads((ad/"summary.json").read_text())
        records[name]={"status":"success","options":opts,
          "soft_nrmse":summary["materials"]["soft"]["forward_nrmse"],
          "hard_nrmse":summary["materials"]["hard"]["forward_nrmse"],
          "soft_marker_rmse_m":summary["materials"]["soft"]["marker_position_rmse_m"],
          "hard_marker_rmse_m":summary["materials"]["hard"]["marker_position_rmse_m"],
          "separation":summary["soft_minus_hard_peak"]["vulkax_forward"],
          "beats_affine_both":summary["beats_affine_both_materials"]}
        print("STRUCTURAL",name,
              "soft",records[name]["soft_nrmse"],"hard",records[name]["hard_nrmse"],
              "sep",records[name]["separation"])
    base=records["baseline"]
    if base["status"]!="success":raise SystemExit("baseline structural forensic failed")
    for name,r in records.items():
        if r["status"]!="success":continue
        r["soft_delta_vs_baseline"]=r["soft_nrmse"]-base["soft_nrmse"]
        r["hard_delta_vs_baseline"]=r["hard_nrmse"]-base["hard_nrmse"]
        r["mean_nrmse"]=0.5*(r["soft_nrmse"]+r["hard_nrmse"])
        r["mean_delta_vs_baseline"]=r["mean_nrmse"]-0.5*(base["soft_nrmse"]+base["hard_nrmse"])
    successful=[(n,r) for n,r in records.items() if r["status"]=="success"]
    successful.sort(key=lambda x:x[1]["mean_nrmse"])
    result={
      "schema":"vulkax.gauge_shearing_structural_forensics","version":1,
      "provenance":"measured+model-prediction","fit_performed":False,
      "baseline_material_parameters_modified":False,
      "variants":records,
      "diagnostic_ranking":[{"name":n,"mean_nrmse":r["mean_nrmse"],
                             "mean_delta_vs_baseline":r["mean_delta_vs_baseline"]} for n,r in successful],
      "warning":"Ranking is forensic only. It may not be used to select/fix a model or unlock inverse fitting without an independently justified validation gate."
    }
    (out/"summary.json").write_text(json.dumps(result,indent=2)+"\n")
    print("VALID GAUGE structural forensics")
    print("RANKING",[(x["name"],x["mean_nrmse"]) for x in result["diagnostic_ranking"]])

if __name__=="__main__":main()

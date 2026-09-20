#!/usr/bin/env python3
import argparse,json,pathlib,subprocess

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--exe",required=True)
    p.add_argument("--contract-dir",required=True)
    p.add_argument("--out",required=True)
    p.add_argument("--dt",type=float,default=8.333333333333333e-5)
    a=p.parse_args()
    cdir=pathlib.Path(a.contract_dir); out=pathlib.Path(a.out); out.mkdir(parents=True,exist_ok=True)
    contract=json.loads((cdir/"contract.json").read_text())
    if contract.get("provenance")!="measured": raise SystemExit("GAUGE forward contract must be measured")
    commands=[]
    for material in ("soft","hard"):
        md=contract["materials"][material]["material_metadata"]
        cmd=[
            a.exe,
            str(cdir/f"markers_initial_{material}.csv"),
            str(cdir/f"driver_{material}.csv"),
            str(out/f"predicted_{material}.csv"),
            str(float(md["young"])),
            str(float(md["poisson"])),
            str(float(md["density"])),
            str(float(md["mass"])),
            repr(a.dt),
            material,
        ]
        cp=subprocess.run(cmd,check=True,text=True,capture_output=True)
        print(cp.stdout,end="")
        commands.append({
            "material":material,
            "young_pa":float(md["young"]),
            "poisson":float(md["poisson"]),
            "density_kg_m3":float(md["density"]),
            "mass_kg":float(md["mass"]),
            "selected_trial":int(contract["materials"][material]["selected_trial"]),
            "stdout":cp.stdout.strip(),
        })
    manifest={
      "schema":"vulkax.gauge_forward_invocation","version":1,
      "provenance":"measured-input+model-prediction",
      "fit_performed":False,
      "requested_dt_s":a.dt,
      "runs":commands,
      "warning":"Material values come directly from frozen GAUGE metadata. No simulation error is used to choose parameters or representative trials."
    }
    (out/"invocation.json").write_text(json.dumps(manifest,indent=2)+"\n")

if __name__=="__main__": main()

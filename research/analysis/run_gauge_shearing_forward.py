#!/usr/bin/env python3
import argparse,json,pathlib,subprocess

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--exe",required=True)
    p.add_argument("--contract-dir",required=True)
    p.add_argument("--out",required=True)
    p.add_argument("--dt",type=float,default=8.333333333333333e-5)
    p.add_argument("--n-cross",type=int,default=5)
    p.add_argument("--n-long",type=int,default=13)
    p.add_argument("--boundary-layers",type=int,default=1)
    p.add_argument("--transfer",choices=("APIC","PIC","FLIP"),default="APIC")
    p.add_argument("--geometry-mode",choices=("measured_aspect","square_cross","released_asset_aspect"),default="measured_aspect")
    p.add_argument("--gravity",choices=("zero","+x","-x","+y","-y","+z","-z"),default="zero")
    p.add_argument("--constitutive",choices=("neo_hookean_log_j","neo_hookean_quadratic_j","st_venant_kirchhoff"),default="neo_hookean_log_j")
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
            str(a.n_cross),
            str(a.n_long),
            str(a.boundary_layers),
            a.transfer,
            a.geometry_mode,
            a.gravity,
            a.constitutive,
        ]
        cp=subprocess.run(cmd,text=True,capture_output=True)
        if cp.stdout:
            print(cp.stdout,end="")
        if cp.stderr:
            print(cp.stderr,end="",file=__import__("sys").stderr)
        if cp.returncode != 0:
            raise SystemExit(f"GAUGE forward executable failed for {material} with exit code {cp.returncode}")
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
      "structural_controls":{
        "n_cross":a.n_cross,"n_long":a.n_long,"boundary_layers":a.boundary_layers,
        "transfer":a.transfer,"geometry_mode":a.geometry_mode,"gravity":a.gravity,
        "constitutive":a.constitutive
      },
      "runs":commands,
      "warning":"Material values come directly from frozen GAUGE metadata. No simulation error is used to choose parameters or representative trials."
    }
    (out/"invocation.json").write_text(json.dumps(manifest,indent=2)+"\n")

if __name__=="__main__": main()

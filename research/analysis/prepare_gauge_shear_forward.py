#!/usr/bin/env python3
import argparse,csv,json,math,pathlib,statistics

def v3(d,i): return [float(d[a][i])*1e-3 for a in ("x","y","z")]
def sub(a,b): return [x-y for x,y in zip(a,b)]
def norm(a): return math.sqrt(sum(x*x for x in a))

def selected_trial(root,material):
    rows=[]
    for trial in range(1,11):
        d=json.loads((root/"data"/"foam shearing"/material/(str(trial)+".json")).read_text())
        b=d["base"]; n=min(len(b[a]) for a in ("x","y","z")); p0=v3(b,0)
        mags=[norm(sub(v3(b,i),p0)) for i in range(n)]
        rows.append((max(mags),trial,d))
    med=statistics.median(x[0] for x in rows)
    return min(rows,key=lambda x:(abs(x[0]-med),x[1]))

def main():
    ap=argparse.ArgumentParser(); ap.add_argument("--root",required=True); ap.add_argument("--nonaffine",required=True); ap.add_argument("--out",required=True); a=ap.parse_args()
    root=pathlib.Path(a.root); out=pathlib.Path(a.out); out.mkdir(parents=True,exist_ok=True)
    md=json.loads((root/"metadata"/"foam shearing.json").read_text())
    non=json.loads(pathlib.Path(a.nonaffine).read_text())
    rows=[]
    for material in ("soft","hard"):
        peak,trial,d=selected_trial(root,material)
        foam=d["foam"]; ids=sorted(foam); pts=[v3(foam[m],0) for m in ids]
        lo=[min(p[k] for p in pts) for k in range(3)]; hi=[max(p[k] for p in pts) for k in range(3)]
        base=d["base"]; n=min(len(base[a]) for a in ("x","y","z")); b0=v3(base,0)
        disp=[sub(v3(base,i),b0) for i in range(n)]; pidx=max(range(n),key=lambda i:norm(disp[i]))
        m=md["assets"]["foam"]["material"][material]
        rows.append({
          "material":material,"trial":trial,"young_pa":float(m["young"]),"poisson":float(m["poisson"]),
          "density_kg_m3":float(m["density"]),"mass_kg":float(m["mass"]),
          "span_x_m":hi[0]-lo[0],"span_y_m":hi[1]-lo[1],"span_z_m":hi[2]-lo[2],
          "driver_dx_m":disp[pidx][0],"driver_dy_m":disp[pidx][1],"driver_dz_m":disp[pidx][2],
          "duration_s":(n-1)/float(d["FPS"]),
          "measured_marker_nonaffine_rms_m":float(non["groups"][material]["marker_nonaffine_rms_m_mean"]),
          "measured_face_nonaffine_rms":float(non["groups"][material]["face_nonaffine_rms_mean"])
        })
    fields=list(rows[0])
    with (out/"forward_config.csv").open("w",newline="") as f:
        w=csv.DictWriter(f,fieldnames=fields); w.writeheader(); w.writerows(rows)
    (out/"manifest.json").write_text(json.dumps({
      "schema":"vulkax.gauge_forward_config","version":1,"provenance":"measured-config",
      "selection":"same frozen representative-trial rule used before simulation",
      "simulation_policy":"no parameter fitting; material metadata copied from GAUGE; gravity disabled for first kinematic-constitutive sanity test",
      "rows":rows,
      "warning":"Bounding-box slab and bonded end layers are an approximation to GAUGE fixtures/contact, not a faithful reconstruction."
    },indent=2)+"\n")
    print("VALID GAUGE no-fit forward config")
    for r in rows: print("FORWARD_CONFIG",r)
if __name__=="__main__": main()

#!/usr/bin/env python3
"""Mechanism-level diagnostic for the frozen GAUGE foam-shearing forward test.

This script does not fit parameters and does not select a "best" model. It decomposes
the measured and Vulkax-predicted marker strip into directly observable deformation
modes supported by GAUGE's 2x7 surface-marker topology:

- longitudinal edge strain,
- transverse edge strain,
- shear-angle change,
- signed mean face-area change,
- face-area relative RMS.

The goal is to explain *how* the no-fit forward model misses the measured motion before
changing geometry, fixtures, constitutive parameters, or solver policy.
"""
import argparse,csv,json,math,pathlib,statistics

def sub(a,b): return [x-y for x,y in zip(a,b)]
def dot(a,b): return sum(x*y for x,y in zip(a,b))
def norm(a): return math.sqrt(dot(a,a))
def cross(a,b): return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def area(a,b,c): return 0.5*norm(cross(sub(b,a),sub(c,a)))
def mean(xs): return statistics.fmean(xs) if xs else 0.0
def rms(xs): return math.sqrt(mean([x*x for x in xs])) if xs else 0.0

def read_markers(path):
    frames={}
    with open(path,newline="") as f:
        for row in csv.DictReader(f):
            fr=int(row["frame"])
            frames.setdefault(fr,{})[row["marker_id"]]=[
                float(row["x_m"]),float(row["y_m"]),float(row["z_m"])]
    return frames

def edge_length(p,q):
    return norm(sub(q,p))

def cosine_between(a,b):
    na,nb=norm(a),norm(b)
    if na<=1e-15 or nb<=1e-15:
        raise ValueError("degenerate GAUGE marker edge")
    return max(-1.0,min(1.0,dot(a,b)/(na*nb)))

def infer_strip(ids,faces):
    # GAUGE foam face topology is a connected 2xN strip. Infer the two marker rows
    # from the canonical triangle pattern rather than hard-coding marker names.
    n=len(ids)
    if n<6 or n%2:
        raise ValueError(f"expected an even strip marker count, got {n}")
    half=n//2
    expected=set()
    for i in range(half-1):
        expected.add(tuple(sorted((i,half+i,half+i+1))))
        expected.add(tuple(sorted((i,half+i+1,i+1))))
    actual={tuple(sorted(map(int,f))) for f in faces}
    if expected != actual:
        raise ValueError("GAUGE face topology is not the expected 2xN strip; refusing to invent a decomposition")
    return list(range(half)),list(range(half,n))

def mode_frame(points,rest,row0,row1,faces):
    long_edges=[]
    cross_edges=[]
    shear=[]
    for row in (row0,row1):
        for a,b in zip(row[:-1],row[1:]):
            l0=edge_length(rest[a],rest[b]); l=edge_length(points[a],points[b])
            long_edges.append(l/l0-1.0)
    for a,b in zip(row0,row1):
        l0=edge_length(rest[a],rest[b]); l=edge_length(points[a],points[b])
        cross_edges.append(l/l0-1.0)
    for i in range(len(row0)-1):
        # Two local right-angle tests per quad, one from each strip row.
        a,b=row0[i],row0[i+1]
        c,d=row1[i],row1[i+1]
        refs=((a,b,c),(c,d,a))
        for o,u,v in refs:
            c0=cosine_between(sub(rest[u],rest[o]),sub(rest[v],rest[o]))
            ct=cosine_between(sub(points[u],points[o]),sub(points[v],points[o]))
            # Change in angle proxy as cosine delta. Signed and scale-free.
            shear.append(ct-c0)
    a0=[area(rest[i],rest[j],rest[k]) for i,j,k in faces]
    at=[area(points[i],points[j],points[k]) for i,j,k in faces]
    area_rel=[x/max(y,1e-15)-1.0 for x,y in zip(at,a0)]
    return {
        "longitudinal_strain_mean":mean(long_edges),
        "longitudinal_strain_rms":rms(long_edges),
        "transverse_strain_mean":mean(cross_edges),
        "transverse_strain_rms":rms(cross_edges),
        "shear_cos_delta_mean":mean(shear),
        "shear_cos_delta_rms":rms(shear),
        "face_area_rel_mean":mean(area_rel),
        "face_area_rel_rms":rms(area_rel),
    }

def curves(frames,ids,faces):
    keys=sorted(frames)
    if not keys or keys[0]!=0:
        raise ValueError("marker series must begin at frame 0")
    rest=[frames[0][m] for m in ids]
    row0,row1=infer_strip(ids,faces)
    out=[]
    for fr in keys:
        pts=[frames[fr][m] for m in ids]
        out.append(mode_frame(pts,rest,row0,row1,faces))
    return out

def discrepancy(measured,predicted,key):
    n=min(len(measured),len(predicted))
    a=[measured[i][key] for i in range(n)]
    b=[predicted[i][key] for i in range(n)]
    e=rms([x-y for x,y in zip(a,b)])
    scale=max(max((abs(x) for x in a),default=0.0),1e-12)
    return {
        "rmse":e,
        "nrmse_to_measured_peak_abs":e/scale,
        "measured_peak_abs":scale,
        "predicted_peak_abs":max((abs(x) for x in b),default=0.0),
    }

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--contract-dir",required=True)
    ap.add_argument("--forward-dir",required=True)
    ap.add_argument("--out",required=True)
    a=ap.parse_args()
    cdir=pathlib.Path(a.contract_dir)
    fdir=pathlib.Path(a.forward_dir)
    out=pathlib.Path(a.out); out.mkdir(parents=True,exist_ok=True)
    contract=json.loads((cdir/"contract.json").read_text())
    faces=[tuple(map(int,x)) for x in contract["unique_faces"]]
    metrics=[
        "longitudinal_strain_mean","longitudinal_strain_rms",
        "transverse_strain_mean","transverse_strain_rms",
        "shear_cos_delta_mean","shear_cos_delta_rms",
        "face_area_rel_mean","face_area_rel_rms",
    ]
    result={
        "schema":"vulkax.gauge_shearing_mode_diagnostic",
        "version":1,
        "provenance":"measured+no-fit-model-prediction",
        "fit_performed":False,
        "topology_assumption":"GAUGE 2xN surface-marker strip verified exactly from unique face connectivity",
        "materials":{},
        "guardrails":[
            "No material or structural parameter is selected from these errors.",
            "This diagnostic cannot unlock inverse fitting.",
            "A mode with lower error is descriptive only; any repair needs independent physical justification and a fresh gate.",
        ],
    }
    for material in ("soft","hard"):
        measured=read_markers(cdir/f"markers_measured_{material}.csv")
        predicted=read_markers(fdir/f"predicted_{material}.csv")
        ids=sorted(measured[0])
        common=min(len(measured),len(predicted))
        for fr in range(common):
            if set(measured[fr])!=set(ids) or set(predicted[fr])!=set(ids):
                raise SystemExit(f"marker identity mismatch: {material} frame {fr}")
        measured={fr:measured[fr] for fr in range(common)}
        predicted={fr:predicted[fr] for fr in range(common)}
        mc=curves(measured,ids,faces)
        pc=curves(predicted,ids,faces)
        disc={k:discrepancy(mc,pc,k) for k in metrics}
        # Ranking is diagnostic only: it points to the largest observable mode mismatch.
        ranking=sorted(metrics,key=lambda k:disc[k]["nrmse_to_measured_peak_abs"],reverse=True)
        result["materials"][material]={
            "frames":common,
            "mode_discrepancy":disc,
            "diagnostic_mismatch_ranking":ranking,
        }
        with (out/f"modes_{material}.csv").open("w",newline="") as f:
            w=csv.writer(f)
            header=["frame"]+[f"measured_{k}" for k in metrics]+[f"predicted_{k}" for k in metrics]
            w.writerow(header)
            for fr in range(common):
                w.writerow([fr]+[mc[fr][k] for k in metrics]+[pc[fr][k] for k in metrics])
        print("MODE_DIAGNOSTIC",material,
              [(k,disc[k]["nrmse_to_measured_peak_abs"]) for k in ranking])
    # Cross-material agreement in dominant failure mode is stronger evidence of a structural issue
    # than a one-material anomaly, but is still not a causal proof.
    s=result["materials"]["soft"]["diagnostic_mismatch_ranking"][0]
    h=result["materials"]["hard"]["diagnostic_mismatch_ranking"][0]
    result["dominant_mode_agrees_across_materials"]=(s==h)
    result["dominant_modes"]={"soft":s,"hard":h}
    result["decision"]="diagnose_structure_before_any_inverse_fitting"
    (out/"summary.json").write_text(json.dumps(result,indent=2)+"\n")
    print("VALID GAUGE deformation-mode diagnostic")
    print("DOMINANT_MODES",result["dominant_modes"],
          "agree",result["dominant_mode_agrees_across_materials"])

if __name__=="__main__":
    main()

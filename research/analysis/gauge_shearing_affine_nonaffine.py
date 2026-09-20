#!/usr/bin/env python3
"""Decompose GAUGE shearing into global affine and local non-affine response.

For each frame, fit the best 3D affine map from the frozen initial marker cloud to
the measured trajectory and separately to the Vulkax prediction. Compare:

1. global deformation gradient F and its determinant;
2. translation;
3. marker residual after removing each trajectory's own best affine map;
4. triangular-face area residual after removing each trajectory's own affine part.

This is descriptive mechanism forensics. It does not fit physical parameters.
"""
import argparse
import csv
import json
import math
import pathlib
import statistics

def sub(a,b): return [a[i]-b[i] for i in range(3)]
def add(a,b): return [a[i]+b[i] for i in range(3)]
def dot(a,b): return sum(x*y for x,y in zip(a,b))
def norm(a): return math.sqrt(dot(a,a))
def rms(xs): return math.sqrt(statistics.fmean(x*x for x in xs)) if xs else 0.0
def cross(a,b):
    return [a[1]*b[2]-a[2]*b[1],
            a[2]*b[0]-a[0]*b[2],
            a[0]*b[1]-a[1]*b[0]]
def area(a,b,c): return 0.5*norm(cross(sub(b,a),sub(c,a)))
def det3(A):
    return (
        A[0][0]*(A[1][1]*A[2][2]-A[1][2]*A[2][1])
       -A[0][1]*(A[1][0]*A[2][2]-A[1][2]*A[2][0])
       +A[0][2]*(A[1][0]*A[2][1]-A[1][1]*A[2][0])
    )
def frob(A):
    return math.sqrt(sum(x*x for row in A for x in row))
def mat_sub(A,B):
    return [[A[i][j]-B[i][j] for j in range(3)] for i in range(3)]
def apply(A,t,p):
    return [
        t[0]+A[0][0]*p[0]+A[0][1]*p[1]+A[0][2]*p[2],
        t[1]+A[1][0]*p[0]+A[1][1]*p[1]+A[1][2]*p[2],
        t[2]+A[2][0]*p[0]+A[2][1]*p[1]+A[2][2]*p[2],
    ]

def solve4(matrix,rhs):
    M=[list(row)+[float(rhs[i])] for i,row in enumerate(matrix)]
    for col in range(4):
        pivot=max(range(col,4),key=lambda r:abs(M[r][col]))
        if abs(M[pivot][col])<1.0e-14:
            raise RuntimeError("rank-deficient affine marker fit")
        if pivot!=col:
            M[col],M[pivot]=M[pivot],M[col]
        inv=1.0/M[col][col]
        for j in range(col,5):
            M[col][j]*=inv
        for r in range(4):
            if r==col: continue
            q=M[r][col]
            if abs(q)<=1.0e-18: continue
            for j in range(col,5):
                M[r][j]-=q*M[col][j]
    return [M[i][4] for i in range(4)]

def fit_affine(rest,current):
    if len(rest)!=len(current) or len(rest)<4:
        raise RuntimeError("affine fit requires matching marker sets")
    moment=[[0.0]*4 for _ in range(4)]
    rhs=[[0.0]*4 for _ in range(3)]
    for p,q in zip(rest,current):
        b=[1.0,p[0],p[1],p[2]]
        for i in range(4):
            for j in range(4):
                moment[i][j]+=b[i]*b[j]
            rhs[0][i]+=b[i]*q[0]
            rhs[1][i]+=b[i]*q[1]
            rhs[2][i]+=b[i]*q[2]
    cx=solve4(moment,rhs[0]); cy=solve4(moment,rhs[1]); cz=solve4(moment,rhs[2])
    t=[cx[0],cy[0],cz[0]]
    A=[
        [cx[1],cx[2],cx[3]],
        [cy[1],cy[2],cy[3]],
        [cz[1],cz[2],cz[3]],
    ]
    fitted=[apply(A,t,p) for p in rest]
    residual=[sub(q,qp) for q,qp in zip(current,fitted)]
    return A,t,fitted,residual

def read_markers(path):
    frames={}
    with open(path,newline="") as f:
        for row in csv.DictReader(f):
            fr=int(row["frame"])
            frames.setdefault(fr,{})[row["marker_id"]]=[
                float(row["x_m"]),float(row["y_m"]),float(row["z_m"])]
    return frames

def face_relative(points,rest_area,faces):
    return [
        area(points[i],points[j],points[k])/max(rest_area[n],1.0e-15)-1.0
        for n,(i,j,k) in enumerate(faces)
    ]

def vector_rms(vectors):
    return math.sqrt(statistics.fmean(dot(v,v) for v in vectors)) if vectors else 0.0

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
    faces=[tuple(map(int,f)) for f in contract["unique_faces"]]
    I=[[1.0,0.0,0.0],[0.0,1.0,0.0],[0.0,0.0,1.0]]
    result={
        "schema":"vulkax.gauge_affine_nonaffine_decomposition",
        "version":1,
        "provenance":"measured+no-fit-model-prediction",
        "fit_performed":False,
        "materials":{},
        "guardrails":[
            "Affine maps are descriptive kinematic fits only.",
            "No material, geometry, boundary or solver parameter is selected from this decomposition.",
            "Residual dominance is not a novelty claim or causal proof.",
        ],
    }

    for material in ("soft","hard"):
        measured=read_markers(cdir/f"markers_measured_{material}.csv")
        predicted=read_markers(fdir/f"predicted_{material}.csv")
        ids=sorted(measured[0])
        common=min(len(measured),len(predicted))
        rest=[measured[0][m] for m in ids]
        rest_area=[area(rest[i],rest[j],rest[k]) for i,j,k in faces]

        rows=[]
        for fr in range(common):
            qm=[measured[fr][m] for m in ids]
            qp=[predicted[fr][m] for m in ids]
            Am,tm,qma,rm=fit_affine(rest,qm)
            Ap,tp,qpa,rp=fit_affine(rest,qp)

            measured_rel=face_relative(qm,rest_area,faces)
            predicted_rel=face_relative(qp,rest_area,faces)
            measured_affine_rel=face_relative(qma,rest_area,faces)
            predicted_affine_rel=face_relative(qpa,rest_area,faces)
            measured_nonaff=[
                x-y for x,y in zip(measured_rel,measured_affine_rel)]
            predicted_nonaff=[
                x-y for x,y in zip(predicted_rel,predicted_affine_rel)]

            F_signal=frob(mat_sub(Am,I))
            F_error=frob(mat_sub(Ap,Am))
            row={
                "frame":fr,
                "F_signal_frobenius":F_signal,
                "F_error_frobenius":F_error,
                "F_error_normalized":F_error/max(F_signal,1.0e-12),
                "translation_error_m":norm(sub(tp,tm)),
                "measured_det_F":det3(Am),
                "predicted_det_F":det3(Ap),
                "measured_nonaffine_marker_rms_m":vector_rms(rm),
                "predicted_nonaffine_marker_rms_m":vector_rms(rp),
                "nonaffine_marker_vector_rmse_m":vector_rms(
                    [sub(a,b) for a,b in zip(rp,rm)]),
                "measured_nonaffine_face_rms":rms(measured_nonaff),
                "predicted_nonaffine_face_rms":rms(predicted_nonaff),
                "nonaffine_face_vector_rmse":rms(
                    x-y for x,y in zip(predicted_nonaff,measured_nonaff)),
            }
            rows.append(row)

        with (out/f"frames_{material}.csv").open("w",newline="") as f:
            w=csv.DictWriter(f,fieldnames=list(rows[0].keys()))
            w.writeheader(); w.writerows(rows)

        active=rows[1:] if len(rows)>1 else rows
        measured_face=statistics.fmean(r["measured_nonaffine_face_rms"] for r in active)
        predicted_face=statistics.fmean(r["predicted_nonaffine_face_rms"] for r in active)
        measured_marker=statistics.fmean(r["measured_nonaffine_marker_rms_m"] for r in active)
        predicted_marker=statistics.fmean(r["predicted_nonaffine_marker_rms_m"] for r in active)
        result["materials"][material]={
            "frames":common,
            "median_global_F_error_normalized":statistics.median(
                r["F_error_normalized"] for r in active),
            "mean_global_F_error_normalized":statistics.fmean(
                r["F_error_normalized"] for r in active),
            "final_measured_det_F":rows[-1]["measured_det_F"],
            "final_predicted_det_F":rows[-1]["predicted_det_F"],
            "mean_measured_nonaffine_marker_rms_m":measured_marker,
            "mean_predicted_nonaffine_marker_rms_m":predicted_marker,
            "predicted_to_measured_nonaffine_marker_ratio":(
                predicted_marker/max(measured_marker,1.0e-15)),
            "mean_measured_nonaffine_face_rms":measured_face,
            "mean_predicted_nonaffine_face_rms":predicted_face,
            "predicted_to_measured_nonaffine_face_ratio":(
                predicted_face/max(measured_face,1.0e-15)),
            "mean_nonaffine_face_vector_rmse":statistics.fmean(
                r["nonaffine_face_vector_rmse"] for r in active),
            "mean_nonaffine_marker_vector_rmse_m":statistics.fmean(
                r["nonaffine_marker_vector_rmse_m"] for r in active),
        }

        s=result["materials"][material]
        print("AFFINE_NONAFFINE",material,
              "F_norm_error",s["median_global_F_error_normalized"],
              "marker_ratio",s["predicted_to_measured_nonaffine_marker_ratio"],
              "face_ratio",s["predicted_to_measured_nonaffine_face_ratio"],
              "det_final",s["final_measured_det_F"],s["final_predicted_det_F"])

    (out/"summary.json").write_text(json.dumps(result,indent=2)+"\n")
    print("VALID GAUGE affine/non-affine decomposition")

if __name__=="__main__":
    main()

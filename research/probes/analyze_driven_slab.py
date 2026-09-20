#!/usr/bin/env python3
import csv, json, math, pathlib, sys
root=pathlib.Path(sys.argv[1] if len(sys.argv)>1 else "build/driven-slab")
rows=list(csv.DictReader((root/"driven_slab.csv").open()))
meta=json.loads((root/"summary.json").read_text())
if meta.get("provenance")!="synthetic": raise SystemExit("driven slab must remain synthetic")
if {r["case"] for r in rows}!={"soft","hard"}: raise SystemExit("expected soft/hard cases")
for r in rows:
    for key in ("young_pa","poisson","peak_shear","accumulated_constraint_impulse",
                "max_momentum_accounting_error","max_position_correction","min_J",
                "final_interior_mean_x","final_interior_rms_x","final_top_position_error"):
        v=float(r[key])
        if not math.isfinite(v): raise SystemExit(f"non-finite {key}: {r}")
    if float(r["min_J"])<=0: raise SystemExit(f"inverted state: {r}")
    if float(r["max_momentum_accounting_error"])>1e-9:
        raise SystemExit(f"constraint momentum accounting failed: {r}")
    if float(r["final_top_position_error"])>1e-12:
        raise SystemExit(f"top prescribed boundary not met: {r}")
    if float(r["final_interior_rms_x"])<=0:
        raise SystemExit(f"interior did not respond: {r}")
by={r["case"]:r for r in rows}
soft=float(by["soft"]["accumulated_constraint_impulse"])
hard=float(by["hard"]["accumulated_constraint_impulse"])
print("VALID Vulkax driven-slab boundary probe")
print("soft_accumulated_constraint_impulse",soft)
print("hard_accumulated_constraint_impulse",hard)
print("hard_to_soft_impulse_ratio",hard/max(soft,1e-30))
print("soft_interior_rms_x",by["soft"]["final_interior_rms_x"])
print("hard_interior_rms_x",by["hard"]["final_interior_rms_x"])
